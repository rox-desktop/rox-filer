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

static const char * bad_xpm[] = {
"12 12 3 1",
" 	c #000000000000",
".	c #FFFF00000000",
"x	c #FFFFFFFFFFFF",
"            ",
" ..xxxxxx.. ",
" ...xxxx... ",
" x...xx...x ",
" xx......xx ",
" xxx....xxx ",
" xxx....xxx ",
" xx......xx ",
" x...xx...x ",
" ...xxxx... ",
" ..xxxxxx.. ",
"            "};

MaskedPixmap *im_error;
MaskedPixmap *im_unknown;
MaskedPixmap *im_symlink;

MaskedPixmap *im_unmounted;
MaskedPixmap *im_mounted;
MaskedPixmap *im_multiple;
MaskedPixmap *im_appdir;

MaskedPixmap *im_help;
MaskedPixmap *im_dirs;

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
static GdkPixbuf *scale_pixbuf_up(GdkPixbuf *src, int max_w, int max_h);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void pixmaps_init(void)
{
	gdk_rgb_init();
#ifndef GTK2
	gtk_widget_push_visual(gdk_rgb_get_visual());
#endif
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
	im_appdir = load_pixmap("pixmaps/application.xpm");

	im_help = load_pixmap("pixmaps/help.xpm");
	im_dirs = load_pixmap("pixmaps/dirs.xpm");
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
	GdkPixbuf	*hg;

	if (mp->huge_pixmap)
		return;

	g_return_if_fail(mp->huge_pixbuf != NULL);

	hg = scale_pixbuf_up(mp->huge_pixbuf,
				HUGE_WIDTH * 0.7,
				HUGE_HEIGHT * 0.7);

	if (hg)
	{
		gdk_pixbuf_render_pixmap_and_mask(hg,
				&mp->huge_pixmap,
				&mp->huge_mask,
				128);
		mp->huge_width = gdk_pixbuf_get_width(hg);
		mp->huge_height = gdk_pixbuf_get_height(hg);
		gdk_pixbuf_unref(hg);
	}

	if (!mp->huge_pixmap)
	{
		gdk_pixmap_ref(mp->pixmap);
		if (mp->mask)
			gdk_bitmap_ref(mp->mask);
		mp->huge_pixmap = mp->pixmap;
		mp->huge_mask = mp->mask;
		mp->huge_width = mp->width;
		mp->huge_height = mp->height;
	}
}

void pixmap_make_small(MaskedPixmap *mp)
{
	GdkPixbuf	*sm;

	if (mp->sm_pixmap)
		return;

	g_return_if_fail(mp->huge_pixbuf != NULL);
			
	sm = scale_pixbuf(mp->huge_pixbuf, SMALL_WIDTH, SMALL_HEIGHT);

	if (sm)
	{
		gdk_pixbuf_render_pixmap_and_mask(sm,
				&mp->sm_pixmap,
				&mp->sm_mask,
				128);
		mp->sm_width = gdk_pixbuf_get_width(sm);
		mp->sm_height = gdk_pixbuf_get_height(sm);
		gdk_pixbuf_unref(sm);
	}

	if (mp->sm_pixmap)
		return;

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

#ifdef GTK2
/* Create a thumbnail file for this image.
 * Spec says to use different permissions, but as we only use the global
 * save dir anyway...
 * XXX: Thumbnails should be deleted somewhere!
 */
static void save_thumbnail(char *path, GdkPixbuf *full, MaskedPixmap *image)
{
	struct stat info;
	int original_width, original_height;
	GString *to;
	char *swidth, *sheight, *ssize, *smtime;
	char *src;
	char **bits, **bit;

	original_width = gdk_pixbuf_get_width(full);
	original_height = gdk_pixbuf_get_height(full);

	if (mc_stat(path, &info) != 0)
		return;

	swidth = g_strdup_printf("%d", original_width);
	sheight = g_strdup_printf("%d", original_height);
	ssize = g_strdup_printf("%ld", info.st_size);
	smtime = g_strdup_printf("%ld", info.st_mtime);

	to = g_string_new(home_dir);
	g_string_append(to, "/.thumbnails");
	mkdir(to->str, 0700);
	g_string_append(to, "/96x96");
	mkdir(to->str, 0700);

	src = pathdup(path);
	g_return_if_fail(src[0] == '/');
	bits = g_strsplit(src + 1, "/", 0);
	g_free(src);

	for (bit = bits; *bit && bit[1]; bit++)
	{
		g_string_append_c(to, '/');
		g_string_append(to, *bit);
		mkdir(to->str, 0700);
	}

	g_string_append_c(to, '/');
	g_string_append(to, *bit);
	g_string_append(to, ".png");

	g_strfreev(bits);

	gdk_pixbuf_save(image->huge_pixbuf,
			to->str,
			"png",
			NULL,
			"tEXt::Thumb::OriginalWidth", swidth,
			"tEXt::Thumb::OriginalHeight", sheight,
			"tEXt::Thumb::OriginalSize", ssize,
			"tEXt::Thumb::OriginalMTime", smtime,
			NULL);

	g_string_free(to, TRUE);
	g_free(swidth);
	g_free(sheight);
	g_free(ssize);
	g_free(smtime);
}

/* Check if we have an up-to-date thumbnail for this image.
 * If so, return it. Otherwise, returns NULL.
 */
static GdkPixbuf *get_thumbnail_for(char *path)
{
	GdkPixbuf *thumb;
	char *thumb_path;
	char *ssize, *smtime;
	struct stat info;

	path = pathdup(path);
	
	thumb_path = g_strdup_printf("%s/.thumbnails/96x96/%s.png",
					home_dir, path);

	thumb = gdk_pixbuf_new_from_file(thumb_path, NULL);
	if (!thumb)
		goto err;

	/* Note that these don't need freeing... */
	ssize = gdk_pixbuf_get_option(thumb, "tEXt::Thumb::OriginalSize");
	if (!ssize)
		goto err;

	smtime = gdk_pixbuf_get_option(thumb, "tEXt::Thumb::OriginalMTime");
	if (!smtime)
		goto err;
	
	if (mc_stat(path, &info) != 0)
		goto err;
	
	if (info.st_mtime != atol(smtime) || info.st_size != atol(ssize))
		goto err;

	goto out;
err:
	if (thumb)
		gdk_pixbuf_unref(thumb);
	thumb = NULL;
out:
	g_free(path);
	g_free(thumb_path);

	return thumb;
}

#endif

/* Load the image 'path' and return a pointer to the resulting
 * MaskedPixmap. NULL on failure.
 */
static MaskedPixmap *image_from_file(char *path)
{
	GdkPixbuf	*pixbuf;
	MaskedPixmap	*image;
#ifdef GTK2
	GError		*error = NULL;
	
	pixbuf = get_thumbnail_for(path);
	if (!pixbuf)
		pixbuf = gdk_pixbuf_new_from_file(path, &error);
	if (!pixbuf)
	{
		g_print("%s\n", error ? error->message : _("Unknown error"));
		g_error_free(error);
		return NULL;
	}
#else
	pixbuf = gdk_pixbuf_new_from_file(path);
	if (!pixbuf)
		return NULL;
#endif

	image = image_from_pixbuf(pixbuf);

#ifdef GTK2
	/* If the source image was very large, save a thumbnail */
	if (gdk_pixbuf_get_width(pixbuf) * gdk_pixbuf_get_height(pixbuf) >
	   (HUGE_WIDTH * HUGE_HEIGHT * 3))
		save_thumbnail(path, pixbuf, image);
#endif
	
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
		int dest_w = w / scale;
		int dest_h = h / scale;
		
		return gdk_pixbuf_scale_simple(src,
						MAX(dest_w, 1),
						MAX(dest_h, 1),
						GDK_INTERP_BILINEAR);
	}
}

/* Scale src up to fit in max_w, max_h and return the new pixbuf.
 * If src is that size or bigger, then ref it and return that.
 */
static GdkPixbuf *scale_pixbuf_up(GdkPixbuf *src, int max_w, int max_h)
{
	int	w, h;

	w = gdk_pixbuf_get_width(src);
	h = gdk_pixbuf_get_height(src);

	if (w == 0 || h == 0 || (w >= max_w && h >= max_h))
	{
		gdk_pixbuf_ref(src);
		return src;
	}
	else
	{
		float scale_x = max_w / ((float) w);
		float scale_y = max_h / ((float) h);
		float scale = MIN(scale_x, scale_y);
		
		return gdk_pixbuf_scale_simple(src,
						w * scale,
						h * scale,
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

	if (!pixmap)
	{
		gdk_pixbuf_unref(huge_pixbuf);
		gdk_pixbuf_unref(normal_pixbuf);
		return NULL;
	}

	mp = g_new(MaskedPixmap, 1);
	mp->huge_pixbuf = huge_pixbuf;
	mp->ref = 1;

	mp->pixmap = pixmap;
	mp->mask = mask;
	mp->width = gdk_pixbuf_get_width(normal_pixbuf);
	mp->height = gdk_pixbuf_get_height(normal_pixbuf);

	gdk_pixbuf_unref(normal_pixbuf);

	mp->huge_pixmap = NULL;
	mp->huge_mask = NULL;
	mp->huge_width = -1;
	mp->huge_height = -1;

	mp->sm_pixmap = NULL;
	mp->sm_mask = NULL;
	mp->sm_width = -1;
	mp->sm_height = -1;

	return mp;
}

/* Return a pointer to the (static) bad image. The ref counter will ensure
 * that the image is never freed.
 */
static MaskedPixmap *get_bad_image(void)
{
	GdkPixbuf *bad;
	MaskedPixmap *mp;
	
	bad = gdk_pixbuf_new_from_xpm_data(bad_xpm);
	mp = image_from_pixbuf(bad);
	gdk_pixbuf_unref(bad);

	return mp;
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
