/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* pixmaps.c - code for handling pixmaps */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <gtk/gtk.h>
#include <collection.h>

#include "support.h"
#include "gui_support.h"
#include "pixmaps.h"

MaskedPixmap default_pixmap[LAST_DEFAULT_PIXMAP];

void load_pixmap(GdkWindow *window, char *name, MaskedPixmap *image)
{
	image->pixmap = gdk_pixmap_create_from_xpm(window,
						   &image->mask,
						   0,
			   make_path(getenv("APP_DIR"), name)->str);
}

/* Load all the standard pixmaps */
void load_default_pixmaps(GdkWindow *window)
{
	static gboolean	loaded = FALSE;

	if (loaded)
		return;

	load_pixmap(window, "pixmaps/error.xpm",
			default_pixmap + TYPE_ERROR);
	load_pixmap(window, "pixmaps/unknown.xpm",
			default_pixmap + TYPE_UNKNOWN);
	load_pixmap(window, "pixmaps/symlink.xpm",
			default_pixmap + TYPE_SYMLINK);
	load_pixmap(window, "pixmaps/file.xpm",
			default_pixmap + TYPE_FILE);
	load_pixmap(window, "pixmaps/directory.xpm",
			default_pixmap + TYPE_DIRECTORY);
	load_pixmap(window, "pixmaps/char.xpm",
			default_pixmap + TYPE_CHAR_DEVICE);
	load_pixmap(window, "pixmaps/block.xpm",
			default_pixmap + TYPE_BLOCK_DEVICE);
	load_pixmap(window, "pixmaps/pipe.xpm",
			default_pixmap + TYPE_PIPE);
	load_pixmap(window, "pixmaps/socket.xpm",
			default_pixmap + TYPE_SOCKET);

	load_pixmap(window, "pixmaps/mount.xpm",
			default_pixmap + TYPE_UNMOUNTED);
	load_pixmap(window, "pixmaps/mounted.xpm",
			default_pixmap + TYPE_MOUNTED);
	load_pixmap(window, "pixmaps/multiple.xpm",
			default_pixmap + TYPE_MULTIPLE);
	load_pixmap(window, "pixmaps/application.xpm",
			default_pixmap + TYPE_APPDIR);

	loaded = TRUE;
}
