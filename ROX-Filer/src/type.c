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

/* Static prototypes */
static GString *to_(char *mime_name);

/* XXX: Just for testing... */
static MIME_type text_plain = {"text/plain"};
static MIME_type image_xpm = {"image/x-xpixmap"};

void type_init()
{
}

/* Convert / to _, eg: text/plain to text_plain.
 * Returns a GString pointer (valid until next call).
 */
static GString *to_(char *mime_name)
{
	static GString	*converted;
	char		*slash;

	if (!converted)
		converted = g_string_new(mime_name);
	else
		g_string_assign(converted, mime_name);

	slash = strchr(converted->str, '/');
	if (slash)
		*slash = '_';
	else
		g_print("Missing / in MIME type name");
	return converted;
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
	if (strstr(path, ".xpm"))
		return &image_xpm;
	return &text_plain;
}

/*			Actions for types 			*/

gboolean type_open(char *path, MIME_type *type)
{
	char		*argv[] = {NULL, path, NULL};
	char		*open;

	open = choices_find_path_load_shared(to_(type->name)->str,
					     "MIME-types");
	if (!open)
		return FALSE;

	argv[0] = g_strconcat(open, "/AppRun", NULL);

	if (!spawn_full(argv, getenv("HOME"), 0))
		report_error("ROX-Filer",
				"Failed to fork() child process");

	g_free(argv[0]);
	
	return TRUE;
}

MaskedPixmap *type_to_icon(GtkWidget *window, MIME_type *type)
{
	if (!type)
		return NULL;

	if (!type->image)
	{
		char	*open, *path;

		open = choices_find_path_load_shared(to_(type->name)->str,
						     "MIME-types");
		if (open)
		{
			path = g_strconcat(open, "/MIME-types/",
					   to_(type->name)->str, ".xpm",
					   NULL);
			type->image = load_pixmap_from(window, path);
			if (!type->image)
				g_print("Unable to load pixmap file '%s'\n",
						path);
			g_free(path);
		}
		if (!type->image)
			type->image = default_pixmap + TYPE_UNKNOWN;
	}
		
	return type->image;
}
