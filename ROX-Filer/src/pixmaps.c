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
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "collection.h"

#include "global.h"

#include "fscache.h"
#include "support.h"
#include "gui_support.h"
#include "pixmaps.h"
#include "main.h"

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
static MaskedPixmap *image_from_pixbuf(GdkPixbuf *pixbuf);
static GdkPixbuf *scale_pixbuf(GdkPixbuf *src, int max_w, int max_h);


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

void pixmap_make_huge(MaskedPixmap *mp)
{
	if (mp->huge_pixmap)
		return;

	if (mp->huge_pixbuf)
	{
		gdk_pixbuf_render_pixmap_and_mask(mp->huge_pixbuf,
				&mp->huge_pixmap,
				&mp->huge_mask,
				128);

		if (mp->huge_pixmap)
			return;
	}

	gdk_pixmap_ref(mp->pixmap);
	if (mp->mask)
		gdk_bitmap_ref(mp->mask);
	mp->huge_pixmap = mp->pixmap;
	mp->huge_mask = mp->mask;
}

void pixmap_make_small(MaskedPixmap *mp)
{
	if (mp->sm_pixmap)
		return;

	if (mp->huge_pixbuf)
	{
		GdkPixbuf	*sm;
			
		sm = scale_pixbuf(mp->huge_pixbuf, SMALL_WIDTH, SMALL_HEIGHT);

		if (sm)
		{
			gdk_pixbuf_render_pixmap_and_mask(sm,
					&mp->sm_pixmap,
					&mp->sm_mask,
					128);
			gdk_pixbuf_unref(sm);
		}

		if (mp->sm_pixmap)
			return;
	}

	gdk_pixmap_ref(mp->pixmap);
	if (mp->mask)
		gdk_bitmap_ref(mp->mask);
	mp->sm_pixmap = mp->pixmap;
	mp->sm_mask = mp->mask;
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
	MaskedPixmap	*image;
	
	pixbuf = gdk_pixbuf_new_from_file(path);
	if (!pixbuf)
		return NULL;

	image = image_from_pixbuf(pixbuf);
	gdk_pixbuf_unref(pixbuf);

	return image;
}

/* Scale src down to fit in max_w, max_h and return the new pixbuf.
 * If src is small enough, then ref it and return that.
 */
static GdkPixbuf *scale_pixbuf(GdkPixbuf *src, int max_w, int max_h)
{
	int	w, h;

	w = gdk_pixbuf_get_width(src);
	h = gdk_pixbuf_get_height(src);

	if (w <= max_w && h <= max_h)
	{
		gdk_pixbuf_ref(src);
		return src;
	}
	else
	{
		float scale_x = ((float) w) / max_w;
		float scale_y = ((float) h) / max_h;
		float scale = MAX(scale_x, scale_y);
		
		return gdk_pixbuf_scale_simple(src,
						w / scale,
						h / scale,
						GDK_INTERP_BILINEAR);
	}
}

/* Turn a full-size pixbuf into a MaskedPixmap */
static MaskedPixmap *image_from_pixbuf(GdkPixbuf *full_size)
{
	MaskedPixmap	*mp;
	GdkPixbuf	*huge_pixbuf, *normal_pixbuf;
	GdkPixmap	*pixmap;
	GdkBitmap	*mask;

	g_return_val_if_fail(full_size != NULL, NULL);

	huge_pixbuf = scale_pixbuf(full_size, HUGE_WIDTH, HUGE_HEIGHT);
	g_return_val_if_fail(huge_pixbuf != NULL, NULL);
	normal_pixbuf = scale_pixbuf(huge_pixbuf, ICON_WIDTH, ICON_HEIGHT);
	g_return_val_if_fail(normal_pixbuf != NULL, NULL);

	gdk_pixbuf_render_pixmap_and_mask(normal_pixbuf, &pixmap, &mask, 128);
	gdk_pixbuf_unref(normal_pixbuf);

	if (!pixmap)
	{
		gdk_pixbuf_unref(huge_pixbuf);
		return NULL;
	}

	mp = g_new(MaskedPixmap, 1);
	mp->ref = 1;
	mp->pixmap = pixmap;
	mp->mask = mask;
	mp->huge_pixbuf = huge_pixbuf;

	mp->huge_pixmap = NULL;
	mp->huge_mask = NULL;

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
		image->huge_pixbuf = NULL;

		image->huge_pixmap = NULL;
		image->huge_mask = NULL;
		image->sm_pixmap = NULL;
		image->sm_mask = NULL;
	}

	image->ref++;

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
		if (mp->huge_pixbuf)
		{
			gdk_pixbuf_unref(mp->huge_pixbuf);
		}

		if (mp->huge_pixmap)
			gdk_pixmap_unref(mp->huge_pixmap);
		if (mp->huge_mask)
			gdk_bitmap_unref(mp->huge_mask);

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

	return TRUE;
}
