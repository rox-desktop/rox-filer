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

/* pixmaps.c - code for handling pixmaps */

/*
 * 2000/04/11 Imlib support added
 * Christiansen Merel <c.merel@wanadoo.fr>
 */

#include "config.h"
#define PIXMAPS_C

/* Remove pixmaps from the cache when they haven't been accessed for
 * this period of time (seconds).
 */

#define PIXMAP_PURGE_TIME 1200

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <gtk/gtk.h>
#include <gdk/gdkprivate.h> /* XXX - find another way to do this */
#include "collection.h"
#ifdef HAVE_IMLIB
#  include <gdk_imlib.h>
#endif

#include "fscache.h"
#include "support.h"
#include "gui_support.h"
#include "pixmaps.h"

GFSCache *pixmap_cache = NULL;

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

MaskedPixmap 	*default_pixmap[LAST_DEFAULT_PIXMAP];

/* Static prototypes */

static MaskedPixmap *load_pixmap(char *name);
static void load_default_pixmaps(void);
static MaskedPixmap *load(char *pathname, gpointer data);
static void ref(MaskedPixmap *mp, gpointer data);
static void unref(MaskedPixmap *mp, gpointer data);
static int getref(MaskedPixmap *mp);
static gint purge(gpointer data);
static MaskedPixmap *image_from_file(char *path);
static MaskedPixmap *get_bad_image(void);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void pixmaps_init(void)
{
#ifdef HAVE_IMLIB
	gdk_imlib_init();
#endif
	
	pixmap_cache = g_fscache_new((GFSLoadFunc) load,
					(GFSRefFunc) ref,
					(GFSRefFunc) unref,
					(GFSGetRefFunc) getref,
					NULL,	/* Update func */
					NULL);

	gtk_timeout_add(10000, purge, NULL);

	load_default_pixmaps();
}

/* 'name' is relative to APP_DIR. Always returns with a valid image. */
MaskedPixmap *load_pixmap(char *name)
{
	MaskedPixmap *retval;

	retval = image_from_file(make_path(getenv("APP_DIR"), name)->str);
	if (!retval)
		retval = get_bad_image();
	return retval;
}

/* Load all the standard pixmaps */
static void load_default_pixmaps(void)
{
	default_pixmap[TYPE_ERROR] = load_pixmap("pixmaps/error.xpm");
	default_pixmap[TYPE_UNKNOWN] = load_pixmap("pixmaps/unknown.xpm");
	default_pixmap[TYPE_SYMLINK] = load_pixmap("pixmaps/symlink.xpm");
	default_pixmap[TYPE_FILE] = load_pixmap("pixmaps/file.xpm");
	default_pixmap[TYPE_DIRECTORY] = load_pixmap("pixmaps/directory.xpm");
	default_pixmap[TYPE_CHAR_DEVICE] = load_pixmap("pixmaps/char.xpm");
	default_pixmap[TYPE_BLOCK_DEVICE] = load_pixmap("pixmaps/block.xpm");
	default_pixmap[TYPE_PIPE] = load_pixmap("pixmaps/pipe.xpm");
	default_pixmap[TYPE_SOCKET] = load_pixmap("pixmaps/socket.xpm");

	default_pixmap[TYPE_UNMOUNTED] = load_pixmap("pixmaps/mount.xpm");
	default_pixmap[TYPE_MOUNTED] = load_pixmap("pixmaps/mounted.xpm");
	default_pixmap[TYPE_MULTIPLE] = load_pixmap("pixmaps/multiple.xpm");
	default_pixmap[TYPE_EXEC_FILE] = load_pixmap("pixmaps/exec.xpm");
	default_pixmap[TYPE_APPDIR] = load_pixmap("pixmaps/application.xpm");

	default_pixmap[TOOLBAR_UP_ICON] = load_pixmap("pixmaps/up.xpm");
	default_pixmap[TOOLBAR_HOME_ICON] = load_pixmap("pixmaps/home.xpm");
	default_pixmap[TOOLBAR_REFRESH_ICON] =
					load_pixmap("pixmaps/refresh.xpm");
}

void pixmap_ref(MaskedPixmap *mp)
{
	ref(mp, pixmap_cache->user_data);
}

void pixmap_unref(MaskedPixmap *mp)
{
	unref(mp, pixmap_cache->user_data);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Load the image 'path' and return a pointer to the resulting
 * MaskedPixmap. NULL on failure.
 */
static MaskedPixmap *image_from_file(char *path)
{
	MaskedPixmap	*mp;
	GdkPixmap	*pixmap;
	GdkBitmap	*mask;
	int		width;
	int		height;
#ifdef HAVE_IMLIB
	GdkImlibImage 	*image;
	
	image = gdk_imlib_load_image(path);
	if (!image)
		return NULL;

	if (!gdk_imlib_render(image, image->rgb_width, image->rgb_height))
	{
		gdk_imlib_kill_image(image);
		return NULL;
	}

	pixmap = image->pixmap;
	mask = image->shape_mask;
	width = image->width;
	height = image->height;
#else
	pixmap = gdk_pixmap_colormap_create_from_xpm(NULL,
				gtk_widget_get_default_colormap(),
				&mask,
				0,
				path);

	if (!pixmap)
		return NULL;
	width = ((GdkPixmapPrivate *) mp->pixmap)->width;
	height = ((GdkPixmapPrivate *) mp->pixmap)->height;
#endif

	mp = g_new(MaskedPixmap, 1);
	mp->ref = 1;
#ifdef HAVE_IMLIB
	mp->image = image;
#endif
	mp->pixmap = pixmap;
	mp->mask = mask;
	mp->width = width;
	mp->height = height;

	return mp;
}

/* Return a pointer to the (static) bad image. The ref counter will ensure
 * that the image is never freed.
 */
static MaskedPixmap *get_bad_image(void)
{
	static	MaskedPixmap	*image = NULL;

	if (!image)
	{
		image = g_new(MaskedPixmap, 1);
		image->ref = 1;

		image->pixmap= gdk_pixmap_colormap_create_from_xpm_d(NULL,
				gtk_widget_get_default_colormap(),
				&image->mask, NULL, bad_xpm);
#ifdef HAVE_IMLIB
		image->image = NULL;
#endif
	}

	image->ref++;
	image->width = ((GdkPixmapPrivate *) image->pixmap)->width;
	image->height = ((GdkPixmapPrivate *) image->pixmap)->height;

	return image;
}

static MaskedPixmap *load(char *pathname, gpointer user_data)
{
	return image_from_file(pathname);
}

static void ref(MaskedPixmap *mp, gpointer data)
{
	/* printf("[ ref %p %d->%d ]\n", mp, mp->ref, mp->ref + 1); */
	
	if (mp)
		mp->ref++;
}

static void unref(MaskedPixmap *mp, gpointer data)
{
	/* printf("[ unref %p %d->%d ]\n", mp, mp->ref, mp->ref - 1); */
	
	if (mp && --mp->ref == 0)
	{
#ifdef HAVE_IMLIB
		if (mp->image)
			gdk_imlib_destroy_image(mp->image);
		else
#endif
		{
			gdk_pixmap_unref(mp->pixmap);
			gdk_bitmap_unref(mp->mask);
		}
		g_free(mp);
	}	
}

static int getref(MaskedPixmap *mp)
{
	return mp->ref;
}

/* Called now and then to clear out old pixmaps */
static gint purge(gpointer data)
{
	g_fscache_purge(pixmap_cache, PIXMAP_PURGE_TIME);

	return TRUE;
}
