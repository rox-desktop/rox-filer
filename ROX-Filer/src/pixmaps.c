/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2001, the ROX-Filer team.
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
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "collection.h"

#include "global.h"

#include "fscache.h"
#include "support.h"
#include "gui_support.h"
#include "pixmaps.h"
#include "main.h"

GFSCache *pixmap_cache = NULL;
GFSCache *thumb_cache = NULL;

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

MaskedPixmap *im_error;
MaskedPixmap *im_unknown;
MaskedPixmap *im_symlink;

MaskedPixmap *im_unmounted;
MaskedPixmap *im_mounted;
MaskedPixmap *im_multiple;
MaskedPixmap *im_exec_file;
MaskedPixmap *im_appdir;

MaskedPixmap *im_help;

/* Static prototypes */

static void load_default_pixmaps(void);
static MaskedPixmap *load(char *pathname, gpointer data);
static void ref(MaskedPixmap *mp, gpointer data);
static void unref(MaskedPixmap *mp, gpointer data);
static int getref(MaskedPixmap *mp);
static gint purge(gpointer data);
static MaskedPixmap *image_from_file(char *path);
static MaskedPixmap *get_bad_image(void);
static MaskedPixmap *load_thumb(char *pathname, gpointer user_data);
static MaskedPixmap *image_from_pixbuf(GdkPixbuf *pixbuf);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void pixmaps_init(void)
{
	gdk_rgb_init();
	gtk_widget_push_visual(gdk_rgb_get_visual());
	gtk_widget_push_colormap(gdk_rgb_get_cmap());

	pixmap_cache = g_fscache_new((GFSLoadFunc) load,
					(GFSRefFunc) ref,
					(GFSRefFunc) unref,
					(GFSGetRefFunc) getref,
					NULL,	/* Update func */
					NULL);

	thumb_cache = g_fscache_new((GFSLoadFunc) load_thumb,
					(GFSRefFunc) ref,
					(GFSRefFunc) unref,
					(GFSGetRefFunc) getref,
					NULL,	/* Update func */
					NULL);

	gtk_timeout_add(10000, purge, NULL);

	load_default_pixmaps();
}

/* 'name' is relative to app_dir. Always returns with a valid image. */
MaskedPixmap *load_pixmap(char *name)
{
	MaskedPixmap *retval;

	retval = image_from_file(make_path(app_dir, name)->str);
	if (!retval)
		retval = get_bad_image();
	return retval;
}

/* Load all the standard pixmaps */
static void load_default_pixmaps(void)
{
	im_error = load_pixmap("pixmaps/error.xpm");
	im_unknown = load_pixmap("pixmaps/unknown.xpm");
	im_symlink = load_pixmap("pixmaps/symlink.xpm");

	im_unmounted = load_pixmap("pixmaps/mount.xpm");
	im_mounted = load_pixmap("pixmaps/mounted.xpm");
	im_multiple = load_pixmap("pixmaps/multiple.xpm");
	im_exec_file = load_pixmap("pixmaps/exec.xpm");
	im_appdir = load_pixmap("pixmaps/application.xpm");

	im_help = load_pixmap("pixmaps/help.xpm");
}

void pixmap_ref(MaskedPixmap *mp)
{
	ref(mp, NULL);
}

void pixmap_unref(MaskedPixmap *mp)
{
	unref(mp, NULL);
}

void pixmap_make_small(MaskedPixmap *mp)
{
	if (mp->sm_pixmap)
		return;

	if (mp->pixbuf)
	{
		GdkPixbuf	*small;

		mp->sm_width = mp->width / 2;
		mp->sm_height = mp->height / 2;
			
		small = gdk_pixbuf_scale_simple(
				mp->pixbuf,
				mp->sm_width,
				mp->sm_height,
				GDK_INTERP_BILINEAR);

		if (small)
		{
			gdk_pixbuf_render_pixmap_and_mask(small,
					&mp->sm_pixmap,
					&mp->sm_mask,
					128);
			gdk_pixbuf_unref(small);
		}

		if (mp->sm_pixmap)
			return;
	}

	gdk_pixmap_ref(mp->pixmap);
	if (mp->mask)
		gdk_bitmap_ref(mp->mask);
	mp->sm_pixmap = mp->pixmap;
	mp->sm_mask = mp->mask;
	mp->sm_width = mp->width;
	mp->sm_height = mp->height;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Load the image 'path' and return a pointer to the resulting
 * MaskedPixmap. NULL on failure.
 */
static MaskedPixmap *image_from_file(char *path)
{
	GdkPixbuf	*pixbuf;
	
	pixbuf = gdk_pixbuf_new_from_file(path);
	if (!pixbuf)
		return NULL;

	return image_from_pixbuf(pixbuf);
}

static MaskedPixmap *image_from_pixbuf(GdkPixbuf *pixbuf)
{
	MaskedPixmap	*mp;
	GdkPixmap	*pixmap;
	GdkBitmap	*mask;
	int		width;
	int		height;

	gdk_pixbuf_render_pixmap_and_mask(pixbuf, &pixmap, &mask, 128);

	if (!pixmap)
	{
		gdk_pixbuf_unref(pixbuf);
		return NULL;
	}

	width = gdk_pixbuf_get_width(pixbuf);
	height = gdk_pixbuf_get_height(pixbuf);

	mp = g_new(MaskedPixmap, 1);
	mp->ref = 1;
	mp->pixmap = pixmap;
	mp->mask = mask;
	mp->width = width;
	mp->height = height;
	mp->pixbuf = pixbuf;
	mp->sm_pixmap = NULL;
	mp->sm_mask = NULL;

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
		image->pixbuf = NULL;

		image->sm_pixmap = NULL;
		image->sm_mask = NULL;
	}

	image->ref++;
	image->width = ((GdkPixmapPrivate *) image->pixmap)->width;
	image->height = ((GdkPixmapPrivate *) image->pixmap)->height;

	return image;
}

static MaskedPixmap *load(char *pathname, gpointer user_data)
{
	MaskedPixmap	*retval;

	retval = image_from_file(pathname);

	return retval ? retval : get_bad_image();
}

static MaskedPixmap *load_thumb(char *pathname, gpointer user_data)
{
	GdkPixbuf	*full_size, *thumb_pb;
	int		w, h;

	full_size = gdk_pixbuf_new_from_file(pathname);

	if (!full_size)
		return NULL;

	w = gdk_pixbuf_get_width(full_size);
	h = gdk_pixbuf_get_height(full_size);

	if (w <= THUMB_WIDTH &&	h <= THUMB_HEIGHT)
		thumb_pb = full_size;
	else
	{
		float scale_x = ((float) w) / THUMB_WIDTH;
		float scale_y = ((float) h) / THUMB_HEIGHT;
		float scale = MAX(scale_x, scale_y);
		
		thumb_pb = gdk_pixbuf_scale_simple(full_size,
						w / scale,
						h / scale,
						GDK_INTERP_BILINEAR);
		gdk_pixbuf_unref(full_size);
	}

	if (!thumb_pb)
		return get_bad_image();

	return image_from_pixbuf(thumb_pb);
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
		if (mp->pixbuf)
		{
			gdk_pixbuf_unref(mp->pixbuf);
		}

		if (mp->pixmap)
			gdk_pixmap_unref(mp->pixmap);
		if (mp->mask)
			gdk_bitmap_unref(mp->mask);

		if (mp->sm_pixmap)
			gdk_pixmap_unref(mp->sm_pixmap);
		if (mp->sm_mask)
			gdk_bitmap_unref(mp->sm_mask);

		g_free(mp);
	}	
}

static int getref(MaskedPixmap *mp)
{
	return mp ? mp->ref : 0;
}

/* Called now and then to clear out old pixmaps */
static gint purge(gpointer data)
{
	g_fscache_purge(pixmap_cache, PIXMAP_PURGE_TIME);
	g_fscache_purge(thumb_cache, PIXMAP_PURGE_TIME);

	return TRUE;
}

