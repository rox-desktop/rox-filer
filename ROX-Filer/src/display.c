/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@users.sourceforge.net>.
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

/* display.c - code for arranging and displaying file items */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include "collection.h"

#include "global.h"

#include "main.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "pixmaps.h"
#include "menu.h"
#include "dnd.h"
#include "run.h"
#include "mount.h"
#include "type.h"
#include "options.h"
#include "action.h"
#include "minibuffer.h"
#include "dir.h"

#define ROW_HEIGHT_SMALL 20
#define ROW_HEIGHT_FULL_INFO 44
#define SMALL_ICON_HEIGHT 20
#ifdef HAVE_IMLIB
#  define SMALL_ICON_WIDTH 24
#else
#  define SMALL_ICON_WIDTH 48
#endif
#define MAX_ICON_HEIGHT 42
#define MAX_ICON_WIDTH 48
#define MIN_ITEM_WIDTH 64

#define MIN_TRUNCATE 0
#define MAX_TRUNCATE 250

/* This is a string so the saving code doesn't get out of sync (again ;-) */
guchar *last_layout = NULL;

gboolean last_show_hidden = FALSE;
int (*last_sort_fn)(const void *a, const void *b) = sort_by_type;

/* Options bits */
static guchar *sort_fn_to_name(void);
static void update_options_label(void);

static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();
static char *display_sort_nocase(char *data);
static char *display_layout(char *data);
static char *display_sort_by(char *data);
static char *display_truncate(char *data);

static OptionsSection options =
{
	N_("Display options"),
	create_options,
	update_options,
	set_options,
	save_options
};

static GtkWidget *display_label;

static gboolean o_sort_nocase = TRUE;
static gint	o_small_truncate = 250;
static gint	o_large_truncate = 89;
static GtkAdjustment *adj_small_truncate;
static GtkAdjustment *adj_large_truncate;
static GtkWidget *toggle_sort_nocase;

/* Static prototypes */
static void draw_large_icon(GtkWidget *widget,
			    GdkRectangle *area,
			    DirItem  *item,
			    gboolean selected);
static void draw_string(GtkWidget *widget,
		GdkFont *font,
		char	*string,
		int 	x,
		int 	y,
		int 	width,
		int	area_width,
		gboolean selected,
		gboolean box);
static void draw_item_large(GtkWidget *widget,
			CollectionItem *item,
			GdkRectangle *area,
			FilerWindow *filer_window);
static void draw_item_small(GtkWidget *widget,
			CollectionItem *item,
			GdkRectangle *area,
			FilerWindow *filer_window);
static void draw_item_large_full(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area,
			FilerWindow *filer_window);
static void draw_item_small_full(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area,
			FilerWindow *filer_window);
static gboolean test_point_large(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height,
				FilerWindow *filer_window);
static gboolean test_point_small(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height,
				FilerWindow *filer_window);
static gboolean test_point_large_full(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height,
				FilerWindow *filer_window);
static gboolean test_point_small_full(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height,
				FilerWindow *filer_window);
static void display_details_set(FilerWindow *filer_window, DetailsType details);
static void display_style_set(FilerWindow *filer_window, DisplayStyle style);

void display_init()
{
	last_layout = g_strdup("Large");

	options_sections = g_slist_prepend(options_sections, &options);
	option_register("display_sort_nocase", display_sort_nocase);
	option_register("display_layout", display_layout);
	option_register("display_sort_by", display_sort_by);
	option_register("display_truncate", display_truncate);
}

#define BAR_SIZE(size) ((size) > 0 ? (log((size) + 1) * 16) : 0)
#define MAX_BAR_SIZE 460

static int details_width(FilerWindow *filer_window, DirItem *item)
{
	int	bar = 0;

	if (filer_window->details_type == DETAILS_SIZE_BARS &&
			item->base_type != TYPE_DIRECTORY)
		return 6 * fixed_width + MAX_BAR_SIZE + 16;

	return bar + fixed_width * strlen(details(filer_window, item));
}

int calc_width(FilerWindow *filer_window, DirItem *item)
{
	int		pix_width = item->image->width;
	int		w;

        switch (filer_window->display_style)
        {
                case LARGE_FULL_INFO:
			w = details_width(filer_window, item);
                        return MAX_ICON_WIDTH + 12 + 
				MAX(w, item->name_width);
                case SMALL_FULL_INFO:
			w = details_width(filer_window, item);
			return SMALL_ICON_WIDTH + item->name_width + 12 + w;
		case SMALL_ICONS:
			w = MIN(item->name_width, o_small_truncate);
			return SMALL_ICON_WIDTH + 12 + w;
                default:
			w = MIN(item->name_width, o_large_truncate);
                        return MAX(pix_width, w) + 4;
        }
}
	
/* Is a point inside an item? */
static gboolean test_point_large(Collection *collection,
				int point_x, int point_y,
				CollectionItem *colitem,
				int width, int height,
				FilerWindow *filer_window)
{
	DirItem		*item = (DirItem *) colitem->data;
	int		text_height = item_font->ascent + item_font->descent;
	MaskedPixmap	*image = item->image;
	int		image_y = MAX(0, MAX_ICON_HEIGHT - image->height);
	int		image_width = (image->width >> 1) + 2;
	int		text_width = (item->name_width >> 1) + 2;
	int		x_limit;

	if (point_y < image_y)
		return FALSE;	/* Too high up (don't worry about too low) */

	if (point_y <= image_y + image->height + 2)
		x_limit = image_width;
	else if (point_y > height - text_height - 2)
		x_limit = text_width;
	else
		x_limit = MIN(image_width, text_width);
	
	return ABS(point_x - (width >> 1)) < x_limit;
}

static gboolean test_point_large_full(Collection *collection,
				int point_x, int point_y,
				CollectionItem *colitem,
				int width, int height,
				FilerWindow *filer_window)
{
	DirItem		*item = (DirItem *) colitem->data;
	MaskedPixmap	*image = item->image;
	int		image_y = MAX(0, MAX_ICON_HEIGHT - image->height);
	int		low_top = height
				- fixed_font->descent - 2 - fixed_font->ascent;

	if (point_x < image->width + 2)
		return point_x > 2 && point_y > image_y;
	
	point_x -= MAX_ICON_WIDTH + 8;

	if (point_y >= low_top)
		return point_x < details_width(filer_window, item);
	if (point_y >= low_top - item_font->ascent - item_font->descent)
		return point_x < item->name_width;
	return FALSE;
}

static gboolean test_point_small_full(Collection *collection,
				int point_x, int point_y,
				CollectionItem *colitem,
				int width, int height,
				FilerWindow *filer_window)
{
	DirItem		*item = (DirItem *) colitem->data;
	MaskedPixmap	*image = item->image;
	int		image_y = MAX(0, SMALL_ICON_HEIGHT - image->height);
	int		low_top = height
				- fixed_font->descent - 2 - item_font->ascent;
	int		iwidth = MIN(SMALL_ICON_WIDTH, image->width);

	if (point_x < iwidth + 2)
		return point_x > 2 && point_y > image_y;

	if (point_y < low_top)
		return FALSE;

	if (filer_window->details_type == DETAILS_SIZE_BARS)
	{
		int	bar_x = width - MAX_BAR_SIZE - 4;
		int	bar_size;

		if (item->base_type == TYPE_DIRECTORY)
			bar_size = 0;
		else
			bar_size = BAR_SIZE(item->size);

		return point_x < bar_x + bar_size;
	}
	
	return TRUE;
}

static gboolean test_point_small(Collection *collection,
				int point_x, int point_y,
				CollectionItem *colitem,
				int width, int height,
				FilerWindow *filer_window)
{
	DirItem		*item = (DirItem *) colitem->data;
	MaskedPixmap	*image = item->image;
	int		image_y = MAX(0, SMALL_ICON_HEIGHT - image->height);
	int		low_top = height
				- fixed_font->descent - 2 - item_font->ascent;
	int		iwidth = MIN(SMALL_ICON_WIDTH, image->width);

	if (point_x < iwidth + 2)
		return point_x > 2 && point_y > image_y;
	
	point_x -= SMALL_ICON_WIDTH + 4;

	if (point_y >= low_top)
		return point_x < item->name_width;
	return FALSE;
}

static void draw_small_icon(GtkWidget *widget,
			    GdkRectangle *area,
			    DirItem  *item,
			    gboolean selected)
{
	GdkGC	*gc = selected ? widget->style->white_gc
			       : widget->style->black_gc;
	MaskedPixmap	*image = item->image;
	int		width, height, image_x, image_y;
	
	if (!image)
		return;

	if (!image->sm_pixmap)
		pixmap_make_small(image);

	width = MIN(image->sm_width, SMALL_ICON_WIDTH);
	height = MIN(image->sm_height, SMALL_ICON_HEIGHT);
	image_x = area->x + ((area->width - width) >> 1);
		
	gdk_gc_set_clip_mask(gc, item->image->sm_mask);

	image_y = MAX(0, SMALL_ICON_HEIGHT - image->sm_height);
	gdk_gc_set_clip_origin(gc, image_x, area->y + image_y);
	gdk_draw_pixmap(widget->window, gc,
			item->image->sm_pixmap,
			0, 0,			/* Source x,y */
			image_x, area->y + image_y, /* Dest x,y */
			width, height);

	if (selected)
	{
		gdk_gc_set_function(gc, GDK_INVERT);
		gdk_draw_rectangle(widget->window,
				gc,
				TRUE, image_x, area->y + image_y,
				width, height);
		gdk_gc_set_function(gc, GDK_COPY);
	}

	if (item->flags & ITEM_FLAG_SYMLINK)
	{
		gdk_gc_set_clip_origin(gc, image_x, area->y + 8);
		gdk_gc_set_clip_mask(gc, im_symlink->mask);
		gdk_draw_pixmap(widget->window, gc, im_symlink->pixmap,
				0, 0,		/* Source x,y */
				image_x, area->y + 8,	/* Dest x,y */
				-1, -1);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		MaskedPixmap	*mp = item->flags & ITEM_FLAG_MOUNTED
					? im_mounted
					: im_unmounted;

		if (!mp->sm_pixmap)
			pixmap_make_small(mp);
		gdk_gc_set_clip_origin(gc, image_x, area->y + 8);
		gdk_gc_set_clip_mask(gc, mp->sm_mask);
		gdk_draw_pixmap(widget->window, gc,
				mp->sm_pixmap,
				0, 0,		/* Source x,y */
				image_x, area->y + 8, /* Dest x,y */
				-1, -1);
	}
	
	gdk_gc_set_clip_mask(gc, NULL);
	gdk_gc_set_clip_origin(gc, 0, 0);
}

static void draw_large_icon(GtkWidget *widget,
			    GdkRectangle *area,
			    DirItem  *item,
			    gboolean selected)
{
	MaskedPixmap	*image = item->image;
	int	width = MIN(image->width, MAX_ICON_WIDTH);
	int	height = MIN(image->height, MAX_ICON_WIDTH);
	int	image_x = area->x + ((area->width - width) >> 1);
	int	image_y;
	GdkGC	*gc = selected ? widget->style->white_gc
						: widget->style->black_gc;
		
	gdk_gc_set_clip_mask(gc, item->image->mask);

	image_y = MAX(0, MAX_ICON_HEIGHT - image->height);
	gdk_gc_set_clip_origin(gc, image_x, area->y + image_y);
	gdk_draw_pixmap(widget->window, gc,
			item->image->pixmap,
			0, 0,			/* Source x,y */
			image_x, area->y + image_y, /* Dest x,y */
			width, height);

	if (selected)
	{
		gdk_gc_set_function(gc, GDK_INVERT);
		gdk_draw_rectangle(widget->window,
				gc,
				TRUE, image_x, area->y + image_y,
				width, height);
		gdk_gc_set_function(gc, GDK_COPY);
	}

	if (item->flags & ITEM_FLAG_SYMLINK)
	{
		gdk_gc_set_clip_origin(gc, image_x, area->y + 8);
		gdk_gc_set_clip_mask(gc, im_symlink->mask);
		gdk_draw_pixmap(widget->window, gc, im_symlink->pixmap,
				0, 0,		/* Source x,y */
				image_x, area->y + 8,	/* Dest x,y */
				-1, -1);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		MaskedPixmap	*mp = item->flags & ITEM_FLAG_MOUNTED
					? im_mounted
					: im_unmounted;

		gdk_gc_set_clip_origin(gc, image_x, area->y + 8);
		gdk_gc_set_clip_mask(gc, mp->mask);
		gdk_draw_pixmap(widget->window, gc, mp->pixmap,
				0, 0,		/* Source x,y */
				image_x, area->y + 8, /* Dest x,y */
				-1, -1);
	}
	
	gdk_gc_set_clip_mask(gc, NULL);
	gdk_gc_set_clip_origin(gc, 0, 0);
}

/* 'box' renders a background box if the string is also selected */
static void draw_string(GtkWidget *widget,
		GdkFont	*font,
		char	*string,
		int 	x,
		int 	y,
		int 	width,
		int	area_width,
		gboolean selected,
		gboolean box)
{
	int		text_height = font->ascent + font->descent;
	GdkRectangle	clip;
	GdkGC		*gc = selected
			? widget->style->fg_gc[GTK_STATE_SELECTED]
			: widget->style->fg_gc[GTK_STATE_NORMAL];

	if (selected && box)
		gtk_paint_flat_box(widget->style, widget->window, 
				GTK_STATE_SELECTED, GTK_SHADOW_NONE,
				NULL, widget, "text",
				x, y - font->ascent,
				MIN(width, area_width),
				text_height);

	if (width > area_width)
	{
		clip.x = x;
		clip.y = y - font->ascent;
		clip.width = area_width;
		clip.height = text_height;
		gdk_gc_set_clip_origin(gc, 0, 0);
		gdk_gc_set_clip_rectangle(gc, &clip);
	}

	gdk_draw_text(widget->window,
			font,
			gc,
			x, y,
			string, strlen(string));

	if (width > area_width)
	{
		if (!red_gc)
		{
			red_gc = gdk_gc_new(widget->window);
			gdk_gc_set_foreground(red_gc, &red);
		}
		gdk_draw_rectangle(widget->window, red_gc, TRUE,
				x + area_width - 1, clip.y, 1, text_height);
		gdk_gc_set_clip_rectangle(gc, NULL);
	}
}

/* Render the details somewhere */
static void draw_details(FilerWindow *filer_window, DirItem *item, int x, int y,
			 int width, gboolean selected, gboolean box)
{
	GtkWidget	*widget = GTK_WIDGET(filer_window->collection);
	int		w;
	
	w = details_width(filer_window, item);

	draw_string(widget,
			fixed_font,
			details(filer_window, item),
			x, y,
			w,
			width,
			selected, box);

	if (item->lstat_errno)
		return;

	if (filer_window->details_type == DETAILS_SIZE_BARS)
	{
		int	bar;
		int	bar_x = x + width - MAX_BAR_SIZE - 4;

		if (item->base_type == TYPE_DIRECTORY)
			return;
		
		bar = BAR_SIZE(item->size);

		gdk_draw_rectangle(widget->window,
				widget->style->black_gc,
				TRUE,
				bar_x, y - fixed_font->ascent,
				bar + 4,
				fixed_font->ascent + fixed_font->descent);
		gdk_draw_rectangle(widget->window,
				widget->style->white_gc,
				TRUE,
				bar_x + 2,
				y - fixed_font->ascent + 2,
				bar,
				fixed_font->ascent + fixed_font->descent - 4);
				
		return;
	}

	if (filer_window->details_type == DETAILS_SUMMARY)
	{
		/* Underline the effective permissions */
		gdk_draw_rectangle(widget->window,
				widget->style->fg_gc[selected
							? GTK_STATE_SELECTED
							: GTK_STATE_NORMAL],
				TRUE,
				x - 1 + fixed_width *
				(5 + 4 * applicable(item->uid, item->gid)),
				y + fixed_font->descent - 1,
				fixed_width * 3 + 1, 1);
	}
}

/* Return a string (valid until next call) giving details
 * of this item.
 */
char *details(FilerWindow *filer_window, DirItem *item)
{
	mode_t		m = item->mode;
	static guchar 	*buf = NULL;

	if (buf)
		g_free(buf);

	if (item->lstat_errno)
		buf = g_strdup_printf(_("lstat(2) failed: %s"),
				g_strerror(item->lstat_errno));
	else if (filer_window->details_type == DETAILS_SUMMARY)
	{
		buf = g_strdup_printf("%s %s %-8.8s %-8.8s %s %s",
				item->flags & ITEM_FLAG_APPDIR? "App " :
			        S_ISDIR(m) ? "Dir " :
				S_ISCHR(m) ? "Char" :
				S_ISBLK(m) ? "Blck" :
				S_ISLNK(m) ? "Link" :
				S_ISSOCK(m) ? "Sock" :
				S_ISFIFO(m) ? "Pipe" : "File",
			pretty_permissions(m),
			user_name(item->uid),
			group_name(item->gid),
			format_size_aligned(item->size),
			pretty_time(&item->mtime));
	}
	else
	{
		if (item->base_type != TYPE_DIRECTORY)
		{
			if (filer_window->display_style == SMALL_FULL_INFO)
				buf = g_strdup(format_size_aligned(item->size));
			else
				buf = g_strdup(format_size(item->size));
		}
		else
			buf = g_strdup("-");
	}
		
	return buf;
}

static void draw_item_large_full(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area,
			FilerWindow *filer_window)
{
	DirItem	*item = (DirItem *) colitem->data;
	MaskedPixmap	*image = item->image;
	int	text_x = area->x + MAX_ICON_WIDTH + 8;
	int	low_text_y = area->y + area->height - fixed_font->descent - 2;
	gboolean	selected = colitem->selected;
	GdkRectangle	pic_area;
	int		text_area_width = area->width - (text_x - area->x);

	pic_area.x = area->x;
	pic_area.y = area->y;
	pic_area.width = image->width + 8;
	pic_area.height = area->height;

	draw_large_icon(widget, &pic_area, item, selected);
	
	draw_string(widget,
			item_font,
			item->leafname, 
			text_x,
			low_text_y - item_font->descent - fixed_font->ascent,
			item->name_width,
			text_area_width,
			selected, TRUE);

	draw_details(filer_window, item, text_x, low_text_y,
			text_area_width, selected, TRUE);
}

static void draw_item_small_full(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area,
			FilerWindow *filer_window)
{
	DirItem	*item = (DirItem *) colitem->data;
	int	text_x = area->x + SMALL_ICON_WIDTH + 4;
	int	bottom = area->y + area->height;
	int	low_text_y = bottom - item_font->descent - 2;
	gboolean	selected = colitem->selected;
	GdkRectangle	pic_area;
	int		text_height = item_font->ascent + item_font->descent;
	int		fixed_height = fixed_font->ascent + fixed_font->descent;
	int		box_height = MAX(text_height, fixed_height) + 2;
	int		w;

	pic_area.x = area->x;
	pic_area.y = area->y;
	pic_area.width = SMALL_ICON_WIDTH;
	pic_area.height = SMALL_ICON_HEIGHT;

	draw_small_icon(widget, &pic_area, item, selected);
	
	if (selected)
		gtk_paint_flat_box(widget->style, widget->window, 
				GTK_STATE_SELECTED, GTK_SHADOW_NONE,
				NULL, widget, "text",
				text_x, bottom - 1 - box_height,
				area->width - (text_x - area->x) - 1,
				box_height);
	draw_string(widget,
			item_font,
			item->leafname, 
			text_x,
			low_text_y,
			item->name_width,
			area->width - (text_x - area->x),
			selected, FALSE);

	w = details_width(filer_window, item);
	text_x = area->width - w - 4 + area->x;
	draw_details(filer_window,
			item, text_x, bottom - fixed_font->descent - 2,
			w + 2, selected, FALSE);
}

static void draw_item_small(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area,
			FilerWindow *filer_window)
{
	DirItem	*item = (DirItem *) colitem->data;
	int	text_x = area->x + SMALL_ICON_WIDTH + 4;
	int	low_text_y = area->y + area->height - item_font->descent - 2;
	gboolean	selected = colitem->selected;
	GdkRectangle	pic_area;

	pic_area.x = area->x;
	pic_area.y = area->y;
	pic_area.width = SMALL_ICON_WIDTH;
	pic_area.height = SMALL_ICON_HEIGHT;

	draw_small_icon(widget, &pic_area, item, selected);
	
	draw_string(widget,
			item_font,
			item->leafname, 
			text_x,
			low_text_y,
			item->name_width,
			area->width - (text_x - area->x),
			selected, TRUE);
}

static void draw_item_large(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area,
			FilerWindow *filer_window)
{
	DirItem		*item = (DirItem *) colitem->data;
	int		text_width = item->name_width;
	int	text_x = area->x + ((area->width - text_width) >> 1);
	int	text_y = area->y + area->height - item_font->descent - 2;
	gboolean	selected = colitem->selected;

	draw_large_icon(widget, area, item, selected);
	
	if (text_x < area->x)
		text_x = area->x;

	draw_string(widget,
			item_font,
			item->leafname, 
			text_x, text_y,
			item->name_width,
			area->width,
			selected, TRUE);
}

int sort_by_name(const void *item1, const void *item2)
{
	if (o_sort_nocase)
		return g_strcasecmp((*((DirItem **)item1))->leafname,
			      	    (*((DirItem **)item2))->leafname);
	return strcmp((*((DirItem **)item1))->leafname,
		      (*((DirItem **)item2))->leafname);
}

int sort_by_type(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) ((CollectionItem *) item1)->data;
	const DirItem *i2 = (DirItem *) ((CollectionItem *) item2)->data;
	MIME_type *m1, *m2;

	int	 diff = i1->base_type - i2->base_type;

	if (!diff)
		diff = (i1->flags & ITEM_FLAG_APPDIR)
		     - (i2->flags & ITEM_FLAG_APPDIR);
	if (diff)
		return diff > 0 ? 1 : -1;

	m1 = i1->mime_type;
	m2 = i2->mime_type;
	
	if (m1 && m2)
	{
		diff = strcmp(m1->media_type, m2->media_type);
		if (!diff)
			diff = strcmp(m1->subtype, m2->subtype);
	}
	else if (m1 || m2)
		diff = m1 ? 1 : -1;
	else
		diff = 0;

	if (diff)
		return diff > 0 ? 1 : -1;
	
	return sort_by_name(item1, item2);
}

int sort_by_date(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) ((CollectionItem *) item1)->data;
	const DirItem *i2 = (DirItem *) ((CollectionItem *) item2)->data;

	return i1->mtime > i2->mtime ? -1 :
		i1->mtime < i2->mtime ? 1 :
		sort_by_name(item1, item2);
}

int sort_by_size(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) ((CollectionItem *) item1)->data;
	const DirItem *i2 = (DirItem *) ((CollectionItem *) item2)->data;

	return i1->size > i2->size ? -1 :
		i1->size < i2->size ? 1 :
		sort_by_name(item1, item2);
}

/* Make the items as narrow as possible */
void shrink_width(FilerWindow *filer_window)
{
	int		i;
	Collection	*col = filer_window->collection;
	int		width = MIN_ITEM_WIDTH;
	int		this_width;
	DisplayStyle	style = filer_window->display_style;
	int		text_height, fixed_height;
	int		height;

	text_height = item_font->ascent + item_font->descent;
	
	for (i = 0; i < col->number_of_items; i++)
	{
		this_width = calc_width(filer_window,
				(DirItem *) col->items[i].data);
		if (this_width > width)
			width = this_width;
	}
	
	switch (style)
	{
		case LARGE_FULL_INFO:
			height = MAX_ICON_HEIGHT + 4;
			break;
		case SMALL_FULL_INFO:
			fixed_height = fixed_font->ascent + fixed_font->descent;
			text_height = MAX(text_height, fixed_height);
			/* No break */
		case SMALL_ICONS:
			height = MAX(text_height, SMALL_ICON_HEIGHT) + 4;
			break;
		case LARGE_ICONS:
		default:
			height = text_height + MAX_ICON_HEIGHT + 8;
			break;
	}

	collection_set_item_size(filer_window->collection, width, height);
}

void display_set_sort_fn(FilerWindow *filer_window,
			int (*fn)(const void *a, const void *b))
{
	if (filer_window->sort_fn == fn)
		return;

	filer_window->sort_fn = fn;
	last_sort_fn = fn;

	collection_qsort(filer_window->collection,
			filer_window->sort_fn);

	update_options_label();
}

/* Make 'layout' the default display layout.
 * If filer_window is not NULL then set the style for that window too.
 * FALSE if layout is invalid.
 */
gboolean display_set_layout(FilerWindow *filer_window, guchar *layout)
{
	DisplayStyle	style = LARGE_ICONS;
	DetailsType	details = DETAILS_SUMMARY;

	g_return_val_if_fail(layout != NULL, FALSE);
			
	if (g_strncasecmp(layout, "Large", 5) == 0)
	{
		if (layout[5])
			style = LARGE_FULL_INFO;
		else
			style = LARGE_ICONS;
	}
	else if (g_strncasecmp(layout, "Small", 5) == 0)
	{
		if (layout[5])
			style = SMALL_FULL_INFO;
		else
			style = SMALL_ICONS;
	}
	else
		return FALSE;

	if (layout[5])
	{
		if (g_strcasecmp(layout + 5, "+Summary") == 0)
			details = DETAILS_SUMMARY;
		else if (g_strcasecmp(layout + 5, "+Sizes") == 0)
			details = DETAILS_SIZE;
		else if (g_strcasecmp(layout + 5, "+SizeBars") == 0)
			details = DETAILS_SIZE_BARS;
		else
			return FALSE;
	}

	if (last_layout != layout)
	{
		g_free(last_layout);
		last_layout = g_strdup(layout);
	}
	
	if (filer_window)
	{
		display_style_set(filer_window, style);
		if (layout[5])
			display_details_set(filer_window, details);
	}

	return TRUE;
}

/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options()
{
	GtkWidget	*vbox, *hbox, *slide;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	display_label = gtk_label_new("<>");
	gtk_label_set_line_wrap(GTK_LABEL(display_label), TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), display_label, FALSE, TRUE, 0);

	toggle_sort_nocase =
		gtk_check_button_new_with_label(_("Ignore case when sorting"));
	gtk_box_pack_start(GTK_BOX(vbox), toggle_sort_nocase, FALSE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(hbox),
			gtk_label_new(_("Max Large Icons width")),
			TRUE, TRUE, 0);
	adj_large_truncate = GTK_ADJUSTMENT(gtk_adjustment_new(0,
				MIN_TRUNCATE, MAX_TRUNCATE, 1, 10, 0));
	slide = gtk_hscale_new(adj_large_truncate);
	gtk_widget_set_usize(slide, MAX_TRUNCATE, 24);
	gtk_scale_set_draw_value(GTK_SCALE(slide), FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), slide, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(hbox),
			gtk_label_new(_("Max Small Icons width")),
			TRUE, TRUE, 0);
	adj_small_truncate = GTK_ADJUSTMENT(gtk_adjustment_new(0,
				MIN_TRUNCATE, MAX_TRUNCATE, 1, 10, 0));
	slide = gtk_hscale_new(adj_small_truncate);
	gtk_widget_set_usize(slide, MAX_TRUNCATE, 24);
	gtk_scale_set_draw_value(GTK_SCALE(slide), FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), slide, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	return vbox;
}

static void update_options_label(void)
{
	guchar	*str;
	
	str = g_strdup_printf(_("The last used display style (%s) and sort "
			"function (Sort By %s) will be saved if you click on "
			"Save."), last_layout, sort_fn_to_name());
	gtk_label_set_text(GTK_LABEL(display_label), str);
	g_free(str);
}

/* Reflect current state by changing the widgets in the options box */
static void update_options()
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_sort_nocase),
			o_sort_nocase);

	gtk_adjustment_set_value(adj_small_truncate, o_small_truncate);
	gtk_adjustment_set_value(adj_large_truncate, o_large_truncate);

	update_options_label();
}

/* Set current values by reading the states of the widgets in the options box */
static void set_options()
{
	gboolean	old_case = o_sort_nocase;
	GList		*next = all_filer_windows;
	
	o_sort_nocase = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_sort_nocase));

	o_small_truncate = adj_small_truncate->value;
	o_large_truncate = adj_large_truncate->value;

	while (next)
	{
		FilerWindow *filer_window = (FilerWindow *) next->data;

		if (o_sort_nocase != old_case)
		{
			collection_qsort(filer_window->collection,
					filer_window->sort_fn);
		}
		shrink_width(filer_window);

		next = next->next;
	}
}

static guchar *sort_fn_to_name(void)
{
	return last_sort_fn == sort_by_name ? _("Name") :
		last_sort_fn == sort_by_type ? _("Type") :
		last_sort_fn == sort_by_date ? _("Date") :
		_("Size");
}

static void save_options()
{
	guchar	*tmp;

	option_write("display_sort_nocase", o_sort_nocase ? "1" : "0");
	option_write("display_layout", last_layout);
	option_write("display_sort_by",
		last_sort_fn == sort_by_name ? "Name" :
		last_sort_fn == sort_by_type ? "Type" :
		last_sort_fn == sort_by_date ? "Date" :
			"Size");

	tmp = g_strdup_printf("%d, %d", o_large_truncate, o_small_truncate);
	option_write("display_truncate", tmp);
	g_free(tmp);
}

static char *display_sort_nocase(char *data)
{
	o_sort_nocase = atoi(data) != 0;
	return NULL;
}

static char *display_layout(char *data)
{
	if (display_set_layout(NULL, data))
		return NULL;

	return _("Unknown display style");
}

static char *display_sort_by(char *data)
{
	if (g_strcasecmp(data, "Name") == 0)
		last_sort_fn = sort_by_name;
	else if (g_strcasecmp(data, "Type") == 0)
		last_sort_fn = sort_by_type;
	else if (g_strcasecmp(data, "Date") == 0)
		last_sort_fn = sort_by_date;
	else if (g_strcasecmp(data, "Size") == 0)
		last_sort_fn = sort_by_size;
	else
		return _("Unknown sort type");

	return NULL;
}

static char *display_truncate(char *data)
{
	guchar	*comma;

	comma = strchr(data, ',');
	if (!comma)
		return "Missing , in display_truncate";

	o_large_truncate = CLAMP(atoi(data), MIN_TRUNCATE, MAX_TRUNCATE);
	o_small_truncate = CLAMP(atoi(comma + 1), MIN_TRUNCATE, MAX_TRUNCATE);

	return NULL;
}

/* Set the 'Show Hidden' flag for this window */
void display_set_hidden(FilerWindow *filer_window, gboolean hidden)
{
	if (filer_window->show_hidden == hidden)
		return;

	filer_window->show_hidden = hidden;
	last_show_hidden = hidden;

	filer_detach_rescan(filer_window);
}

/* Highlight (wink or cursor) this item in the filer window. If the item
 * isn't already there but we're scanning then highlight it if it
 * appears later.
 */
void display_set_autoselect(FilerWindow *filer_window, guchar *leaf)
{
	Collection	*col = filer_window->collection;
	int		i;
	
	g_free(filer_window->auto_select);
	filer_window->auto_select = NULL;

	for (i = 0; i < col->number_of_items; i++)
	{
		DirItem *item = (DirItem *) col->items[i].data;

		if (strcmp(item->leafname, leaf) == 0)
		{
			if (col->cursor_item != -1)
				collection_set_cursor_item(col, i);
			else
				collection_wink_item(col, i);
			return;
		}
	}
	
	filer_window->auto_select = g_strdup(leaf);
}

static void display_details_set(FilerWindow *filer_window, DetailsType details)
{
	if (filer_window->details_type == details)
		return;
	filer_window->details_type = details;
	shrink_width(filer_window);
	update_options_label();
}

static void display_style_set(FilerWindow *filer_window, DisplayStyle style)
{
	if (filer_window->display_style == style)
		return;

	if (filer_window->panel_type)
		style = LARGE_ICONS;

	filer_window->display_style = style;

	switch (style)
	{
		case SMALL_FULL_INFO:
			collection_set_functions(filer_window->collection,
				(CollectionDrawFunc) draw_item_small_full,
				(CollectionTestFunc) test_point_small_full,
				filer_window);
			break;
		case SMALL_ICONS:
			collection_set_functions(filer_window->collection,
				(CollectionDrawFunc) draw_item_small,
				(CollectionTestFunc) test_point_small,
				filer_window);
			break;
		case LARGE_FULL_INFO:
			collection_set_functions(filer_window->collection,
				(CollectionDrawFunc) draw_item_large_full,
				(CollectionTestFunc) test_point_large_full,
				filer_window);
			break;
		default:
			collection_set_functions(filer_window->collection,
				(CollectionDrawFunc) draw_item_large,
				(CollectionTestFunc) test_point_large,
				filer_window);
			break;
	}

	shrink_width(filer_window);

	update_options_label();
}
