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

#include "fscache.h"
#include "support.h"
#include "gui_support.h"
#include "pixmaps.h"

static GFSCache *pixmap_cache = NULL;

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

/* Static prototypes */

static MaskedPixmap *load(char *pathname, gpointer data);
static void ref(MaskedPixmap *mp, gpointer data);
static void unref(MaskedPixmap *mp, gpointer data);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void pixmaps_init(void)
{
	pixmap_cache = g_fscache_new((GFSLoadFunc) load,
					(GFSFunc) ref,
					(GFSFunc) unref,
					NULL);
}

/* Try to load the pixmap from the given path, allocate a MaskedPixmap
 * structure for it and return a pointer to the structure. NULL on failure.
 * XXX: Unref it when you're done.
 */
MaskedPixmap *load_pixmap_from(GtkWidget *window, char *path)
{
	MaskedPixmap	*masked_pixmap;
	
	pixmap_cache->user_data = window;
	masked_pixmap = g_fscache_lookup(pixmap_cache, path);
	pixmap_cache->user_data = NULL;

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

void pixmap_unref(MaskedPixmap *mp)
{
	unref(mp, pixmap_cache->user_data);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Try to load the pixmap from the given path, allocate a MaskedPixmap
 * structure for it and return a pointer to the structure. NULL on failure.
 */
static MaskedPixmap *load(char *pathname, gpointer user_data)
{
	GtkWidget	*window = (GtkWidget *) user_data;
	GdkPixmap	*pixmap;
	GdkBitmap	*mask;
	GtkStyle	*style;
	MaskedPixmap	*masked_pixmap;

	g_print("Load '%s'\n", pathname);
	style = gtk_widget_get_style(window);

	pixmap = gdk_pixmap_create_from_xpm(window->window,
			&mask,
			&style->bg[GTK_STATE_NORMAL],
			pathname);
	if (!pixmap)
		return NULL;

	masked_pixmap = g_new(MaskedPixmap, 1);
	masked_pixmap->pixmap = pixmap;
	masked_pixmap->mask = mask;
	masked_pixmap->ref = 1;

	return masked_pixmap;
}

static void ref(MaskedPixmap *mp, gpointer data)
{
	g_print("[ ref ]\n");
	if (mp)
		mp->ref++;
}

static void unref(MaskedPixmap *mp, gpointer data)
{
	g_print("[ unref ]\n");
	if (mp && --mp->ref == 0)
	{
		gdk_pixmap_unref(mp->pixmap);
		gdk_bitmap_unref(mp->mask);
		g_free(mp);
	}	
}
