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

#include "string.h"
#include "filer.h"
#include "pixmaps.h"
#include "apps.h"
#include "gui_support.h"
#include "choices.h"
#include "type.h"
#include "support.h"

/* Maps extensions to MIME_types (eg 'png'-> MIME_type *) */
static GHashTable *extension_hash = NULL;

/* XXX: Just for testing... */
static MIME_type text_plain = {"text", "plain"};
static MIME_type image_xpm = {"image", "x-xpixmap"};
static MIME_type image_png = {"image", "png"};

void type_init()
{
	extension_hash = g_hash_table_new(g_str_hash, g_str_equal);

	g_hash_table_insert(extension_hash, "png", &image_png);
	g_hash_table_insert(extension_hash, "xpm", &image_xpm);
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
		return FALSE;

	argv[0] = g_strconcat(open, "/AppRun", NULL);

	if (!spawn_full(argv, getenv("HOME"), 0))
		report_error("ROX-Filer",
				"Failed to fork() child process");

	g_free(argv[0]);
	
	return TRUE;
}

/* Return the image for this type, loading it if needed.
 * Places to check are: (eg type="text_plain", base="text")
 * 1. Choices/MIME-types/<type>/MIME-icons/<type>
 * 2. Choices/MIME-types/<type>/MIME-icons/<base>
 * 3. Choices/MIME-types/<base>/MIME-icons/<base> [ TODO ]
 * 4. $APP_DIR/MIME-icons/<base>
 * 5. Unknown type icon.
 */
MaskedPixmap *type_to_icon(GtkWidget *window, MIME_type *type)
{
	char	*open, *path;
	char	*type_name;

	g_return_val_if_fail(type != NULL, default_pixmap + TYPE_UNKNOWN);

	/* Already got an image? TODO: Is it out-of-date? */
	if (type->image)
		return type->image;

	type_name = g_strconcat(type->media_type, "_", type->subtype, NULL);

	/* 1 and 2 : Check for icon provided by specific handler */
	open = choices_find_path_load_shared(type_name, "MIME-types");
	if (open)
	{
		/* 1 : Exact type provided */
		path = g_strconcat(open, "/MIME-icons/", type_name, ".xpm",
				   NULL);
		type->image = load_pixmap_from(window, path);
		g_free(path);

		if (!type->image)
		{
			/* 2 : Base type provided */
			path = g_strconcat(open, "/MIME-icons/",
					type->media_type, ".xpm", NULL);
			type->image = load_pixmap_from(window, path);
			g_free(path);
		}
	}

	g_free(type_name);

	if (type->image)
		return type->image;
	
	/* 4 : Load a default icon from our own application dir */
	
	path = g_strconcat(getenv("APP_DIR"), "/MIME-icons/", type->media_type,
			   ".xpm", NULL);
	type->image = load_pixmap_from(window, path);
	g_free(path);

	if (type->image)
		return type->image;
	
	/* 5 : Use the unknown type icon */

	type->image = default_pixmap + TYPE_UNKNOWN;
		
	return type->image;
}
