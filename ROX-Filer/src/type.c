/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* type.c - code for dealing with filetypes */

#include <glib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

#include "string.h"
#include "filer.h"
#include "pixmaps.h"
#include "apps.h"
#include "gui_support.h"
#include "choices.h"
#include "type.h"
#include "support.h"
#include "options.h"

/* Static prototypes */
static char *import_extensions(char *line);

/* Maps extensions to MIME_types (eg 'png'-> MIME_type *) */
static GHashTable *extension_hash = NULL;
static char *current_type = NULL;	/* (used while reading file) */

/* Most things on Unix are text files, so this is the default type */
static MIME_type text_plain = {"text", "plain"};

void type_init()
{
	ChoicesList 	*paths, *next;
	
	extension_hash = g_hash_table_new(g_str_hash, g_str_equal);

	paths = choices_find_load_all("guess", "MIME-types");
	while (paths)
	{
		current_type = NULL;
		parse_file(paths->path, import_extensions);
		next = paths->next;
		g_free(paths->path);
		g_free(paths);
		paths = next;
	}
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

	new = g_malloc(sizeof(MIME_type));
	new->media_type = g_malloc(sizeof(char) * (len + 1));
	memcpy(new->media_type, type_name, len);
	new->media_type[len] = '\0';

	new->subtype = g_strdup(slash + 1);
	new->image = NULL;

	g_hash_table_insert(extension_hash, g_strdup(ext), new);
}

/* Parse one line from the file and add entries to extension_hash */
static char *import_extensions(char *line)
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
		while (*line && !isspace(*line))
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

char *basetype_name(FileItem *item)
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
	char	*argv[] = {NULL, path, NULL};
	char	*open;
	char	*type_name;

	type_name = g_strconcat(type->media_type, "_", type->subtype, NULL);
	open = choices_find_path_load_shared(type_name, "MIME-types");
	g_free(type_name);
	if (!open)
	{
		open = choices_find_path_load_shared(type->media_type,
				"MIME-types");
		if (!open)
			return FALSE;
	}

	argv[0] = g_strconcat(open, "/AppRun", NULL);

	if (!spawn_full(argv, getenv("HOME"), 0))
		report_error("ROX-Filer",
				"Failed to fork() child process");

	g_free(argv[0]);
	
	return TRUE;
}

/* Tries to load image from <handler>/MIME-icons/<icon>.xpm */
static MaskedPixmap *try_icon_path(GtkWidget *window, char *handler, char *icon)
{
	char		*path;
	MaskedPixmap 	*image;

	if (!handler)
		return NULL;

	path = g_strconcat(handler, "/MIME-icons/", icon, ".xpm", NULL);
	image = load_pixmap_from(window, path);
	g_free(path);

	return image;
}

/* Return the image for this type, loading it if needed.
 * Find handler application, <app>, then:
 * Places to check are: (eg type="text_plain", base="text")
 * 1. <app>/MIME-icons/<type>
 * 2. <app>/MIME-icons/<base>
 * 3. $APP_DIR/MIME-icons/<base>
 * 4. Unknown type icon.
 */
MaskedPixmap *type_to_icon(GtkWidget *window, MIME_type *type)
{
	char	*open;
	char	*type_name;
	MaskedPixmap *i;

	g_return_val_if_fail(type != NULL, default_pixmap + TYPE_UNKNOWN);

	/* Already got an image? TODO: Is it out-of-date? */
	if (type->image)
		return type->image;

	type_name = g_strconcat(type->media_type, "_", type->subtype, NULL);

	open = choices_find_path_load_shared(type_name, "MIME-types");
	if (!open)
		open = choices_find_path_load_shared(type->media_type,
							"MIME-types");
	if (((i = try_icon_path(window, open, type_name)))
	 || ((i = try_icon_path(window, open, type->media_type)))
	 || ((i = try_icon_path(window, getenv("APP_DIR"), type->media_type))))
		type->image = i;
	else
		type->image = default_pixmap + TYPE_UNKNOWN;
	
	g_free(type_name);

	return i;
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

