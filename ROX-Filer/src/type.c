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
#include <errno.h>

#include "filer.h"
#include "pixmaps.h"
#include "apps.h"
#include "gui_support.h"
#include "choices.h"
#include "type.h"
#include "support.h"

/* Static prototypes */

/* XXX: Just for testing... */
static MIME_type text_plain = {"text/plain"};
static MIME_type image_xpm = {"image/x-xpixmap"};

void type_init()
{
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
	struct stat 	info;
	gboolean	needs_free = FALSE;

	open = choices_find_path_load_shared(type->name, "MIME-open");
	if (!open)
		return FALSE;

	if (stat(open, &info))
	{
		report_error("ROX-Filer", g_strerror(errno));
		return FALSE;
	}
	
	if (S_ISDIR(info.st_mode))
	{
		argv[0] = g_strconcat(open, "/AppRun");
		needs_free = TRUE;
	}
	else
		argv[0] = open;

	if (!spawn(argv))
		report_error("ROX-Filer",
				"Failed to fork() child process");

	if (needs_free)
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

		path = g_strconcat(type->name, ".xpm", NULL);
		open = choices_find_path_load_shared(path, "MIME-icons");
		g_free(path);
		type->image = load_pixmap_from(window, open);
		if (!type->image)
			type->image = default_pixmap + TYPE_UNKNOWN;
	}
		
	return type->image;
}
