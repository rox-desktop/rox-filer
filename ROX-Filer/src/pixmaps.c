/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 1999, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
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

/* pixmaps.c - code for handling pixmaps */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <gtk/gtk.h>
#include "collection.h"

#include "support.h"
#include "gui_support.h"
#include "pixmaps.h"

static char * bad_xpm[] = {
"12 12 3 1",
" 	c #000000000000",
".	c #FFFF00000000",
"X	c #FFFFFFFFFFFF",
"            ",
" ..XXXXXX.. ",
" ...XXXX... ",
" X...XX...X ",
" XX......XX ",
" XXX....XXX ",
" XXX....XXX ",
" XX......XX ",
" X...XX...X ",
" ...XXXX... ",
" ..XXXXXX.. ",
"            "};

MaskedPixmap 	default_pixmap[LAST_DEFAULT_PIXMAP];

/* Try to load the pixmap from the given path, allocate a MaskedPixmap
 * structure for it and return a pointer to the structure. NULL on failure.
 */
MaskedPixmap *load_pixmap_from(GtkWidget *window, char *path)
{
	GdkPixmap	*pixmap;
	GdkBitmap	*mask;
	GtkStyle	*style;
	MaskedPixmap	*masked_pixmap;

	style = gtk_widget_get_style(window);

	pixmap = gdk_pixmap_create_from_xpm(window->window,
			&mask,
			&style->bg[GTK_STATE_NORMAL],
			path);
	if (!pixmap)
		return NULL;

	masked_pixmap = g_malloc(sizeof(MaskedPixmap));
	masked_pixmap->pixmap = pixmap;
	masked_pixmap->mask = mask;

	return masked_pixmap;
}

void load_pixmap(GdkWindow *window, char *name, MaskedPixmap *image)
{
	image->pixmap = gdk_pixmap_create_from_xpm(window,
						   &image->mask,
						   0,
			   make_path(getenv("APP_DIR"), name)->str);

	if (!image->pixmap)
	{
		image->pixmap= gdk_pixmap_create_from_xpm_d(window,
				&image->mask, NULL, bad_xpm);
	}
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
	load_pixmap(window, "pixmaps/exec.xpm",
			default_pixmap + TYPE_EXEC_FILE);
	load_pixmap(window, "pixmaps/application.xpm",
			default_pixmap + TYPE_APPDIR);

	loaded = TRUE;
}
