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

#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/param.h>

#include <glib.h>

#include "string.h"
#include "main.h"
#include "filer.h"
#include "pixmaps.h"
#include "run.h"
#include "gui_support.h"
#include "choices.h"
#include "type.h"
#include "support.h"

/* Static prototypes */
static char *import_extensions(guchar *line);
static void import_for_dir(guchar *path);

/* Maps extensions to MIME_types (eg 'png'-> MIME_type *) */
static GHashTable *extension_hash = NULL;
static char *current_type = NULL;	/* (used while reading file) */

/* Most things on Unix are text files, so this is the default type */
MIME_type text_plain 		= {"text", "plain", NULL};

MIME_type special_directory 	= {"special", "directory", NULL};
MIME_type special_pipe 		= {"special", "pipe", NULL};
MIME_type special_socket 	= {"special", "socket", NULL};
MIME_type special_block_dev 	= {"special", "block-device", NULL};
MIME_type special_char_dev 	= {"special", "char-device", NULL};
MIME_type special_unknown 	= {"special", "unknown", NULL};

void type_init()
{
	int		i;
	GPtrArray	*list;
	
	extension_hash = g_hash_table_new(g_str_hash, g_str_equal);

	current_type = NULL;

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
			return _("Missing MIME-type");
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
			return _("Trailing chars after MIME-type");
		current_type = g_strdup(type);
	}
	return NULL;
}

char *basetype_name(DirItem *item)
{
	if (item->flags & ITEM_FLAG_SYMLINK)
		return _("Sym link");
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
		return _("Mount point");
	else if (item->flags & ITEM_FLAG_APPDIR)
		return _("App dir");

	switch (item->base_type)
	{
		case TYPE_FILE:
			return _("File");
		case TYPE_DIRECTORY:
			return _("Dir");
		case TYPE_CHAR_DEVICE:
			return _("Char dev");
		case TYPE_BLOCK_DEVICE:
			return _("Block dev");
		case TYPE_PIPE:
			return _("Pipe");
		case TYPE_SOCKET:
			return _("Socket");
	}
	
	return _("Unknown");
}

/*			MIME-type guessing 			*/

/* Returns a pointer to the MIME-type. Defaults to text/plain if we have
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
		report_error(PROJECT, g_strerror(errno));
		return FALSE;
	}

	if (S_ISDIR(info.st_mode))
		argv[0] = g_strconcat(open, "/AppRun", NULL);
	else
		argv[0] = open;

	if (!spawn_full(argv, home_dir))
	{
		report_error(PROJECT,
				_("Failed to fork() child process"));
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

	g_return_val_if_fail(type != NULL, im_unknown);

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
		type->image = im_unknown;
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

/* The user wants to set a new default action for files of this type.
 * Ask the user if they want to set eg 'text/plain' or just 'text'.
 * Removes the current binding if possible and returns the path to
 * save the new one to. NULL means cancel.
 */
char *type_ask_which_action(guchar *media_type, guchar *subtype)
{
	int		r;
	guchar		*tmp, *type_name, *path;
	struct stat 	info;

	g_return_val_if_fail(media_type != NULL, NULL);
	g_return_val_if_fail(subtype != NULL, NULL);

	if (!choices_find_path_save("", PROJECT, FALSE))
	{
		report_error(PROJECT,
		_("Choices saving is disabled by CHOICESPATH variable"));
		return NULL;
	}

	type_name = g_strconcat(media_type, "/", subtype, NULL);
	tmp = g_strdup_printf(
		_("You can choose to set the action for just '%s' files, or "
		  "the default action for all '%s' files which don't already "
		  "have a run action:"), type_name, media_type);
	r = get_choice(PROJECT, tmp, 3, type_name, media_type, "Cancel");
	g_free(tmp);
	g_free(type_name);

	if (r == 0)
	{
		type_name = g_strconcat(media_type, "_", subtype, NULL);
		path = choices_find_path_save(type_name, "MIME-types", TRUE);
		g_free(type_name);
	}
	else if (r == 1)
		path = choices_find_path_save(media_type, "MIME-types", TRUE);
	else
		return NULL;

	if (lstat(path, &info) == 0)
	{
		/* A binding already exists... */
		if (S_ISREG(info.st_mode) && info.st_size > 256)
		{
			if (get_choice(PROJECT,
				_("A run action already exists and is quite "
				"a big program - are you sure you want to "
				"delete it?"), 2, "Delete", "Cancel") != 0)
				return NULL;
		}
		
		if (unlink(path))
		{
			tmp = g_strdup_printf( _("Can't remove %s: %s"),
				path, g_strerror(errno));
			report_error(PROJECT, tmp);
			g_free(tmp);
			return NULL;
		}
	}

	return path;
}

MIME_type *mime_type_from_base_type(int base_type)
{
	switch (base_type)
	{
		case TYPE_DIRECTORY:
			return &special_directory;
		case TYPE_PIPE:
			return &special_pipe;
		case TYPE_SOCKET:
			return &special_socket;
		case TYPE_BLOCK_DEVICE:
			return &special_block_dev;
		case TYPE_CHAR_DEVICE:
			return &special_char_dev;
	}
	return &special_unknown;
}

/* Takes the st_mode field from stat() and returns the base type.
 * Should not be a symlink.
 */
int mode_to_base_type(int st_mode)
{
	if (S_ISREG(st_mode))
		return TYPE_FILE;
	else if (S_ISDIR(st_mode))
		return TYPE_DIRECTORY;
	else if (S_ISBLK(st_mode))
		return TYPE_BLOCK_DEVICE;
	else if (S_ISCHR(st_mode))
		return TYPE_CHAR_DEVICE;
	else if (S_ISFIFO(st_mode))
		return TYPE_PIPE;
	else if (S_ISSOCK(st_mode))
		return TYPE_SOCKET;

	return TYPE_UNKNOWN;
}
