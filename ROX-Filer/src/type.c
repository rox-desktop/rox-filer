/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* type.c - code for dealing with filetypes */

#include "config.h"

#include <glib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

#include "string.h"
#include "main.h"
#include "filer.h"
#include "pixmaps.h"
#include "run.h"
#include "gui_support.h"
#include "choices.h"
#include "type.h"
#include "support.h"
#include "options.h"

/* Static prototypes */
static char *import_extensions(guchar *line);
static void import_for_dir(guchar *path);

/* Maps extensions to MIME_types (eg 'png'-> MIME_type *) */
static GHashTable *extension_hash = NULL;
static char *current_type = NULL;	/* (used while reading file) */

/* Most things on Unix are text files, so this is the default type */
MIME_type text_plain = {"text", "plain", NULL};

void type_init()
{
	int		i;
	GPtrArray	*list;
	
	extension_hash = g_hash_table_new(g_str_hash, g_str_equal);

	current_type = NULL;
	parse_file(make_path(getenv("APP_DIR"), "MIME-info")->str,
				import_extensions);

	list = choices_list_dirs("MIME-info");
	for (i = 0; i < list->len; i++)
		import_for_dir((gchar *) g_ptr_array_index(list, i));
	choices_free_list(list);
}

/* Parse every file in 'dir' */
static void import_for_dir(guchar *path)
{
	DIR		*dir;
	struct dirent	*item;

	dir = opendir(path);
	if (!dir)
		return;

	while ((item = readdir(dir)))
	{
		if (item->d_name[0] == '.')
			continue;

		current_type = NULL;
		parse_file(make_path(path, item->d_name)->str,
				import_extensions);
	}

	closedir(dir);
}

/* Add one entry to the extension_hash table */
static void add_ext(char *type_name, char *ext)
{
	MIME_type *new;
	char	  *slash;
	int	  len;

	slash = strchr(type_name, '/');
	g_return_if_fail(slash != NULL);	/* XXX: Report nicely */
	len = slash - type_name;

	new = g_new(MIME_type, 1);
	new->media_type = g_malloc(sizeof(char) * (len + 1));
	memcpy(new->media_type, type_name, len);
	new->media_type[len] = '\0';

	new->subtype = g_strdup(slash + 1);
	new->image = NULL;

	g_hash_table_insert(extension_hash, g_strdup(ext), new);
}

/* Parse one line from the file and add entries to extension_hash */
static char *import_extensions(guchar *line)
{

	if (*line == '\0' || *line == '#')
		return NULL;		/* Comment */

	if (isspace(*line))
	{
		if (!current_type)
			return "Missing MIME-type";
		while (*line && isspace(*line))
			line++;

		if (strncmp(line, "ext:", 4) == 0)
		{
			char *ext;
			line += 4;

			for (;;)
			{
				while (*line && isspace(*line))
					line++;
				if (*line == '\0')
					break;
				ext = line;
				while (*line && !isspace(*line))
					line++;
				if (*line)
					*line++ = '\0';
				add_ext(current_type, ext);
			}
		}
		/* else ignore */
	}
	else
	{
		char		*type = line;
		while (*line && *line != ':' && !isspace(*line))
			line++;
		if (*line)
			*line++ = '\0';
		while (*line && isspace(*line))
			line++;
		if (*line)
			return "Trailing chars after MIME-type";
		current_type = g_strdup(type);
	}
	return NULL;
}

char *basetype_name(DirItem *item)
{
	if (item->flags & ITEM_FLAG_SYMLINK)
		return "Sym link";
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
		return "Mount point";
	else if (item->flags & ITEM_FLAG_APPDIR)
		return "App dir";

	switch (item->base_type)
	{
		case TYPE_FILE:
			return "File";
		case TYPE_DIRECTORY:
			return "Dir";
		case TYPE_CHAR_DEVICE:
			return "Char dev";
		case TYPE_BLOCK_DEVICE:
			return "Block dev";
		case TYPE_PIPE:
			return "Pipe";
		case TYPE_SOCKET:
			return "Socket";
	}
	
	return "Unknown";
}

/*			MIME-type guessing 			*/

/* Returns a pointer to the MIME-type string, or NULL if we have
 * no opinion.
 */
MIME_type *type_from_path(char *path)
{
	char	*dot;

	dot = strrchr(path, '.');
	if (dot)
	{
		MIME_type *type;
		type = g_hash_table_lookup(extension_hash, dot + 1);
		if (type)
			return type;
	}

	return &text_plain;
}

/*			Actions for types 			*/

gboolean type_open(char *path, MIME_type *type)
{
	char	*argv[] = {NULL, NULL, NULL};
	char	*open;
	char	*type_name;
	gboolean	retval = TRUE;
	struct stat	info;

	argv[1] = path;

	type_name = g_strconcat(type->media_type, "_", type->subtype, NULL);
	open = choices_find_path_load(type_name, "MIME-types");
	g_free(type_name);
	if (!open)
	{
		open = choices_find_path_load(type->media_type,
				"MIME-types");
		if (!open)
			return FALSE;
	}

	if (stat(open, &info))
	{
		report_error("ROX-Filer", g_strerror(errno));
		return FALSE;
	}

	if (S_ISDIR(info.st_mode))
		argv[0] = g_strconcat(open, "/AppRun", NULL);
	else
		argv[0] = open;

	if (!spawn_full(argv, home_dir))
	{
		report_error("ROX-Filer",
				"Failed to fork() child process");
		retval = FALSE;
	}

	if (argv[0] != open)
		g_free(argv[0]);
	
	return retval;
}

/* Return the image for this type, loading it if needed.
 * Places to check are: (eg type="text_plain", base="text")
 * 1. Choices:MIME-icons/<type>
 * 2. Choices:MIME-icons/<base>
 * 3. Unknown type icon.
 *
 * Note: You must pixmap_unref() the image afterwards.
 */
MaskedPixmap *type_to_icon(MIME_type *type)
{
	char	*path;
	char	*type_name;
	time_t	now;

	g_return_val_if_fail(type != NULL, default_pixmap[TYPE_UNKNOWN]);

	now = time(NULL);
	/* Already got an image? */
	if (type->image)
	{
		/* Yes - don't recheck too often */
		if (abs(now - type->image_time) < 2)
		{
			pixmap_ref(type->image);
			return type->image;
		}
		pixmap_unref(type->image);
		type->image = NULL;
	}

	type_name = g_strconcat(type->media_type, "_",
				type->subtype, ".xpm", NULL);
	path = choices_find_path_load(type_name, "MIME-icons");
	if (!path)
	{
		strcpy(type_name + strlen(type->media_type), ".xpm");
		path = choices_find_path_load(type_name, "MIME-icons");
	}
	
	g_free(type_name);

	if (path)
		type->image = g_fscache_lookup(pixmap_cache, path);

	if (!type->image)
	{
		type->image = default_pixmap[TYPE_UNKNOWN];
		pixmap_ref(type->image);
	}

	type->image_time = now;
	
	pixmap_ref(type->image);
	return type->image;
}

GdkAtom type_to_atom(MIME_type *type)
{
	char	*str;
	GdkAtom	retval;
	
	g_return_val_if_fail(type != NULL, GDK_NONE);

	str = g_strconcat(type->media_type, "/", type->subtype, NULL);
	retval = gdk_atom_intern(str, FALSE);
	g_free(str);
	
	return retval;
}

