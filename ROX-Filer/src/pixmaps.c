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

MaskedPixmap *im_error;
MaskedPixmap *im_unknown;
MaskedPixmap *im_symlink;

MaskedPixmap *im_unmounted;
MaskedPixmap *im_mounted;
MaskedPixmap *im_multiple;
MaskedPixmap *im_exec_file;
MaskedPixmap *im_appdir;

MaskedPixmap *im_up_icon;
MaskedPixmap *im_home_icon;
MaskedPixmap *im_refresh_icon;
MaskedPixmap *im_help;

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
#ifdef HAVE_IMLIB
static GdkImlibImage *make_half_size(GdkImlibImage *big);
#endif


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
	im_error = load_pixmap("pixmaps/error.xpm");
	im_unknown = load_pixmap("pixmaps/unknown.xpm");
	im_symlink = load_pixmap("pixmaps/symlink.xpm");

	im_unmounted = load_pixmap("pixmaps/mount.xpm");
	im_mounted = load_pixmap("pixmaps/mounted.xpm");
	im_multiple = load_pixmap("pixmaps/multiple.xpm");
	im_exec_file = load_pixmap("pixmaps/exec.xpm");
	im_appdir = load_pixmap("pixmaps/application.xpm");

	im_up_icon = load_pixmap("pixmaps/up.xpm");
	im_home_icon = load_pixmap("pixmaps/home.xpm");
	im_refresh_icon = load_pixmap("pixmaps/refresh.xpm");
	im_help = load_pixmap("pixmaps/help.xpm");
}

void pixmap_ref(MaskedPixmap *mp)
{
	ref(mp, pixmap_cache->user_data);
}

void pixmap_unref(MaskedPixmap *mp)
{
	unref(mp, pixmap_cache->user_data);
}

void pixmap_make_small(MaskedPixmap *mp)
{
	if (mp->sm_pixmap)
		return;

#ifdef HAVE_IMLIB
	if (mp->image)
	{
		GdkImlibImage	*small;

		small = make_half_size(mp->image);

		if (small && gdk_imlib_render(small,
					small->rgb_width, small->rgb_height))
		{
			mp->sm_pixmap = gdk_imlib_move_image(small);
			mp->sm_mask = gdk_imlib_move_mask(small);
			mp->sm_width = small->width;
			mp->sm_height = small->height;
		}

		if (small)
			gdk_imlib_kill_image(small);
		if (mp->sm_pixmap)
			return;
	}
#endif
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

	/* Avoid ImLib cache - ours is better! */
	gdk_imlib_changed_image(image);

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
	width = ((GdkPixmapPrivate *) pixmap)->width;
	height = ((GdkPixmapPrivate *) pixmap)->height;
#endif

	mp = g_new(MaskedPixmap, 1);
	mp->ref = 1;
	mp->pixmap = pixmap;
	mp->mask = mask;
	mp->width = width;
	mp->height = height;
#ifdef HAVE_IMLIB
	mp->image = image;
#endif
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
		{
			gdk_imlib_kill_image(mp->image);
		}
		else
#endif
		{
			gdk_pixmap_unref(mp->pixmap);
			if (mp->mask)
				gdk_bitmap_unref(mp->mask);
		}
		if (mp->sm_pixmap)
			gdk_pixmap_unref(mp->sm_pixmap);
		if (mp->sm_mask)
			gdk_bitmap_unref(mp->sm_mask);
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

#ifdef HAVE_IMLIB

#define GREY_BG 0xd8

/* Returns data to make an 1/4 size image of 'big'. g_free() the result. */
static GdkImlibImage *make_half_size(GdkImlibImage *big)
{
	int		line_size = big->width * 3;
	int		sw = big->width >> 1;
	int		sh = big->height >> 1;
	GdkImlibColor	tr;		/* Mask colour */
	unsigned char	*small_data, *in, *out;
	GdkImlibImage	*small;
	int		x, y;

	gdk_imlib_get_image_shape(big, &tr);
	small_data = g_malloc(sw * sh * 3);

	out = small_data;

	for (y = 0; y < sh; y++)
	{
		in = big->rgb_data + y * line_size * 2;

		for (x = 0; x < sw; x++)
		{
			int	r1 = in[0], r2 = in[3];
			int	r3 = in[0 + line_size], r4 = in[3 + line_size];
			int	g1 = in[1], g2 = in[4];
			int	g3 = in[1 + line_size], g4 = in[4 + line_size];
			int	b1 = in[2], b2 = in[5];
			int	b3 = in[2 + line_size], b4 = in[5 + line_size];
			int	m = 0;		/* No. trans pixels */

			if (r1 == tr.r && g1 == tr.g && b1 == tr.b)
				r1 = g1 = b1 = GREY_BG, m++;
			if (r2 == tr.r && g2 == tr.g && b2 == tr.b)
				r2 = g2 = b2 = GREY_BG, m++;
			if (r3 == tr.r && g3 == tr.g && b3 == tr.b)
				r3 = g3 = b3 = GREY_BG, m++;
			if (r4 == tr.r && g4 == tr.g && b4 == tr.b)
				r4 = g4 = b4 = GREY_BG, m++;

			if (m < 3)
			{
				out[0] = (r1 + r2 + r3 + r4) >> 2;
				out[1] = (g1 + g2 + g3 + g4) >> 2;
				out[2] = (b1 + b2 + b3 + b4) >> 2;
			}
			else
			{
				out[0] = tr.r;
				out[1] = tr.g;
				out[2] = tr.b;
			}

			in += 6;
			out += 3;
		}
	}
	
	small = gdk_imlib_create_image_from_data(small_data, NULL, sw, sh);
	g_free(small_data);

	if (small)
		gdk_imlib_set_image_shape(small, &tr);

	return small;
}
#endif
