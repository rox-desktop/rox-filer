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

#include "global.h"

#include "main.h"
#include "display.h"
#include "collection.h"
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
#include "diritem.h"

#define ROW_HEIGHT_SMALL 20
#define ROW_HEIGHT_FULL_INFO 44
#define MIN_ITEM_WIDTH 64

#define MIN_TRUNCATE 0
#define MAX_TRUNCATE 250

#define HUGE_WRAP (1.5 * o_large_truncate)

/* Options bits */
static gboolean o_sort_nocase = TRUE;
static gboolean o_dirs_first = FALSE;
static gint	o_small_truncate = 250;
static gint	o_large_truncate = 89;
static gboolean	o_display_colour_types = TRUE;

/* Colours for file types (same order as base types) */
static gchar *opt_type_colours[][2] = {
	{"display_err_colour",  "#ff0000"},
	{"display_unkn_colour", "#000000"},
	{"display_dir_colour",  "#000080"},
	{"display_pipe_colour", "#444444"},
	{"display_sock_colour", "#ff00ff"},
	{"display_file_colour", "#000000"},
	{"display_cdev_colour", "#000000"},
	{"display_bdev_colour", "#000000"},
	{"display_exec_colour", "#006000"},
	{"display_adir_colour", "#006000"}
};
#define NUM_TYPE_COLOURS\
		(sizeof(opt_type_colours) / sizeof(opt_type_colours[0]))

/* Parsed colours for file types */
static GdkColor	type_colours[NUM_TYPE_COLOURS];

/* GC for drawing colour filenames */
static GdkGC	*type_gc = NULL;

typedef struct _Template Template;

struct _Template {
	GdkRectangle	icon;
	GdkRectangle	leafname;

	/* Note that details_string is either NULL or points to a
	 * static buffer - don't free it!
	 */
	guchar		*details_string;
	GdkRectangle	details;
};

typedef struct _ViewData ViewData;

struct _ViewData
{
#ifdef GTK2
	PangoLayout *layout;
#endif
	int	name_width;
	int	name_height;
#ifndef GTK2
	int	split_pos;		/* 0 => No split */
	int	split_width, split_height;
#endif
};

#define SHOW_RECT(ite, template)	\
	g_print("%s: %dx%d+%d+%d  %dx%d+%d+%d\n",	\
		item->leafname,				\
		(template)->leafname.width, (template)->leafname.height,\
		(template)->leafname.x, (template)->leafname.y,		\
		(template)->icon.width, (template)->icon.height,	\
		(template)->icon.x, (template)->icon.y)

/* Static prototypes */
static int alloc_type_colours(void);
static void fill_template(GdkRectangle *area, CollectionItem *item,
			FilerWindow *filer_window, Template *template);
static void huge_template(GdkRectangle *area, CollectionItem *colitem,
			   FilerWindow *filer_window, Template *template);
static void large_template(GdkRectangle *area, CollectionItem *colitem,
			   FilerWindow *filer_window, Template *template);
static void small_template(GdkRectangle *area, CollectionItem *colitem,
			   FilerWindow *filer_window, Template *template);
static void huge_full_template(GdkRectangle *area, CollectionItem *colitem,
			   FilerWindow *filer_window, Template *template);
static void large_full_template(GdkRectangle *area, CollectionItem *colitem,
			   FilerWindow *filer_window, Template *template);
static void small_full_template(GdkRectangle *area, CollectionItem *colitem,
			   FilerWindow *filer_window, Template *template);
static int details_width(FilerWindow *filer_window, DirItem *item);
static void draw_item(GtkWidget *widget,
			CollectionItem *item,
			GdkRectangle *area,
			FilerWindow *filer_window);
static gboolean test_point(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height,
				FilerWindow *filer_window);
static void display_details_set(FilerWindow *filer_window, DetailsType details);
static void display_style_set(FilerWindow *filer_window, DisplayStyle style);
static void options_changed(void);
static char *details(FilerWindow *filer_window, DirItem *item);
#ifndef GTK2
static void wrap_text(CollectionItem *colitem, int width);
#endif

enum {
	SORT_BY_NAME = 0,
	SORT_BY_TYPE = 1,
	SORT_BY_DATE = 2,
	SORT_BY_SIZE = 3,
};

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void display_init()
{
	int i;
	
	option_add_int("display_colour_types", o_display_colour_types, NULL);
	
	for (i = 0; i < NUM_TYPE_COLOURS; i++)
		option_add_string(
				opt_type_colours[i][0],
				opt_type_colours[i][1],
				NULL
		);
	alloc_type_colours();

	option_add_int("display_sort_nocase", o_sort_nocase, NULL);
	option_add_int("display_dirs_first", o_dirs_first, NULL);
	option_add_int("display_size", LARGE_ICONS, NULL);
	option_add_int("display_details", DETAILS_NONE, NULL);
	option_add_int("display_sort_by", SORT_BY_NAME, NULL);
	option_add_int("display_large_width", o_large_truncate, NULL);
	option_add_int("display_small_width", o_small_truncate, NULL);
	option_add_int("display_show_hidden", FALSE, NULL);
	option_add_int("display_inherit_options", FALSE, NULL);

	option_add_notify(options_changed);
}

/* A template contains the locations of the three rectangles (for the icon,
 * name and extra details).
 * Fill in the empty 'template' with the rectanges for this item.
 */
static void fill_template(GdkRectangle *area, CollectionItem *colitem,
			FilerWindow *filer_window, Template *template)
{
	DisplayStyle	style = filer_window->display_style;

	template->details_string = NULL;

	if (filer_window->details_type == DETAILS_NONE)
	{
		if (style == HUGE_ICONS)
			huge_template(area, colitem, filer_window, template);
		else if (style == LARGE_ICONS)
			large_template(area, colitem, filer_window, template);
		else
			small_template(area, colitem, filer_window, template);
	}
	else
	{
		if (style == SMALL_ICONS)
			small_full_template(area, colitem,
						filer_window, template);
		else if (style == LARGE_ICONS)
			large_full_template(area, colitem,
						filer_window, template);
		else
			huge_full_template(area, colitem,
						filer_window, template);
	}
}

/* Return the size needed for this item */
void calc_size(FilerWindow *filer_window, CollectionItem *colitem,
		int *width, int *height)
{
	int		pix_width;
	int		w;
	int		fixed_height;
	DisplayStyle	style = filer_window->display_style;
	DirItem		*item = (DirItem *) colitem->data;
	ViewData	*view = (ViewData *) colitem->view_data;

	if (!view)
	{
		int	w, h;

		view = g_new(ViewData, 1);
		colitem->view_data = view;

#ifdef GTK2
		view->layout = gtk_widget_create_pango_layout(
					filer_window->window, item->leafname);

		if (filer_window->details_type == DETAILS_NONE)
		{
			if (style == HUGE_ICONS)
				pango_layout_set_width(view->layout,
						HUGE_WRAP * PANGO_SCALE);
			else if (style == LARGE_ICONS)
				pango_layout_set_width(view->layout,
						o_large_truncate * PANGO_SCALE);
		}

		pango_layout_get_size(view->layout, &w, &h);
		view->name_width = w / PANGO_SCALE;
		view->name_height = h / PANGO_SCALE;
#else
		w = gdk_string_measure(item_font, item->leafname);
		h = item_font->ascent + item_font->descent;
				
		view->name_width = w;
		view->name_height = h;
		view->split_pos = 0;
		if (filer_window->details_type == DETAILS_NONE)
		{
			if (style == HUGE_ICONS)
				wrap_text(colitem, HUGE_WRAP);
			else if (style == LARGE_ICONS)
				wrap_text(colitem, o_large_truncate);
		}
#endif
	}

	if (filer_window->details_type == DETAILS_NONE)
	{
                if (style == HUGE_ICONS)
		{
			if (item->image)
			{
				if (!item->image->huge_pixmap)
					pixmap_make_huge(item->image);
				pix_width = item->image->huge_width;
			}
			else
				pix_width = HUGE_WIDTH * 3 / 2;
#ifdef GTK2
			*width = MAX(pix_width, view->name_width) + 4;
			*height = view->name_height + HUGE_HEIGHT + 4;
#else
			*width = MAX(pix_width, view->split_width) + 4;
			*height = view->split_height + HUGE_HEIGHT + 4;
#endif
		}
		else if (style == SMALL_ICONS)
		{
			w = MIN(view->name_width, o_small_truncate);
			*width = SMALL_WIDTH + 12 + w;
			*height = MAX(view->name_height, SMALL_HEIGHT) + 4;
		}
		else
		{
			if (item->image)
				pix_width = item->image->width;
			else
				pix_width = HUGE_WIDTH * 3 / 2;
#ifdef GTK2
			*width = MAX(pix_width, view->name_width) + 4;
			*height = view->name_height + ICON_HEIGHT + 2;
#else
                        *width = MAX(pix_width, view->split_width) + 4;
			*height = view->split_height + ICON_HEIGHT + 2;
#endif
		}
	}
	else
	{
		if (style == HUGE_ICONS)
		{
			w = details_width(filer_window, item);
			*width = HUGE_WIDTH + 12 + MAX(w, view->name_width);
			*height = HUGE_HEIGHT - 4;
		}
		else if (style == SMALL_ICONS)
		{
			int	text_height;

			w = details_width(filer_window, item);
			*width = SMALL_WIDTH + view->name_width + 12 + w;
			fixed_height = fixed_font->ascent + fixed_font->descent;
			text_height = MAX(view->name_height, fixed_height);
			*height = MAX(text_height, SMALL_HEIGHT) + 4;
		}
		else
		{
			w = details_width(filer_window, item);
                        *width = ICON_WIDTH + 12 + MAX(w, view->name_width);
			*height = ICON_HEIGHT;
		}
        }
}

/* Draw this icon (including any symlink or mount symbol) inside the
 * given rectangle.
 */
void draw_huge_icon(GtkWidget *widget,
		    GdkRectangle *area,
		    DirItem  *item,
		    gboolean selected)
{
	MaskedPixmap	*image = item->image;
	int		width, height;
	int		image_x;
	int		image_y;
	GdkGC	*gc = selected ? widget->style->white_gc
						: widget->style->black_gc;
	if (!image)
		return;

	width = image->huge_width;
	height = image->huge_height;
	image_x = area->x + ((area->width - width) >> 1);
		
	gdk_gc_set_clip_mask(gc, item->image->huge_mask);

	image_y = MAX(0, area->height - height - 6);
	gdk_gc_set_clip_origin(gc, image_x, area->y + image_y);
	gdk_draw_pixmap(widget->window, gc,
			item->image->huge_pixmap,
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
		gdk_gc_set_clip_origin(gc, image_x, area->y + 2);
		gdk_gc_set_clip_mask(gc, im_symlink->mask);
		gdk_draw_pixmap(widget->window, gc, im_symlink->pixmap,
				0, 0,		/* Source x,y */
				image_x, area->y + 2,	/* Dest x,y */
				-1, -1);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		MaskedPixmap	*mp = item->flags & ITEM_FLAG_MOUNTED
					? im_mounted
					: im_unmounted;

		gdk_gc_set_clip_origin(gc, image_x, area->y + 2);
		gdk_gc_set_clip_mask(gc, mp->mask);
		gdk_draw_pixmap(widget->window, gc, mp->pixmap,
				0, 0,		/* Source x,y */
				image_x, area->y + 2, /* Dest x,y */
				-1, -1);
	}
	
	gdk_gc_set_clip_mask(gc, NULL);
	gdk_gc_set_clip_origin(gc, 0, 0);
}

/* Draw this icon (including any symlink or mount symbol) inside the
 * given rectangle.
 */
void draw_large_icon(GtkWidget *widget,
		     GdkRectangle *area,
		     DirItem  *item,
		     gboolean selected)
{
	MaskedPixmap	*image = item->image;
	int	width;
	int	height;
	int	image_x;
	int	image_y;
	GdkGC	*gc = selected ? widget->style->white_gc
						: widget->style->black_gc;
	if (!image)
		return;

	width = MIN(image->width, ICON_WIDTH);
	height = MIN(image->height, ICON_HEIGHT);
	image_x = area->x + ((area->width - width) >> 1);

	gdk_gc_set_clip_mask(gc, item->image->mask);

	image_y = MAX(0, area->height - height - 6);
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
		gdk_gc_set_clip_origin(gc, image_x, area->y + 2);
		gdk_gc_set_clip_mask(gc, im_symlink->mask);
		gdk_draw_pixmap(widget->window, gc, im_symlink->pixmap,
				0, 0,		/* Source x,y */
				image_x, area->y + 2,	/* Dest x,y */
				-1, -1);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		MaskedPixmap	*mp = item->flags & ITEM_FLAG_MOUNTED
					? im_mounted
					: im_unmounted;

		gdk_gc_set_clip_origin(gc, image_x, area->y + 2);
		gdk_gc_set_clip_mask(gc, mp->mask);
		gdk_draw_pixmap(widget->window, gc, mp->pixmap,
				0, 0,		/* Source x,y */
				image_x, area->y + 2, /* Dest x,y */
				-1, -1);
	}
	
	gdk_gc_set_clip_mask(gc, NULL);
	gdk_gc_set_clip_origin(gc, 0, 0);
}

/* 'box' renders a background box if the string is also selected */
void draw_string(GtkWidget *widget,
		GdkFont	*font,
		char	*string,
		int	len,		/* -1 for whole string */
		int 	x,
		int 	y,
		int 	width,		/* Width of the full string */
		int	area_width,	/* Width available for drawing in */
		GtkStateType selection_state,
		gboolean selected,
		gboolean box)
{
	int		text_height = font->ascent + font->descent;
	GdkRectangle	clip;
	GdkGC		*gc = selected
			? widget->style->fg_gc[selection_state]
			: type_gc;
	
	if (selected && box)
		gtk_paint_flat_box(widget->style, widget->window, 
				selection_state, GTK_SHADOW_NONE,
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

	if (len == -1)
		len = strlen(string);
	gdk_draw_text(widget->window,
			font,
			gc,
			x, y,
			string, len);

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

/* The sort functions aren't called from outside, but they are
 * passed as arguments to display_set_sort_fn().
 */

#define IS_A_DIR(item) (item->base_type == TYPE_DIRECTORY && \
			!(item->flags & ITEM_FLAG_APPDIR))

#define SORT_DIRS	\
	if (o_dirs_first) {	\
		gboolean id1 = IS_A_DIR(i1);	\
		gboolean id2 = IS_A_DIR(i2);	\
		if (id1 && !id2) return -1;				\
		if (id2 && !id1) return 1;				\
	}

int sort_by_name(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) ((CollectionItem *) item1)->data;
	const DirItem *i2 = (DirItem *) ((CollectionItem *) item2)->data;

	SORT_DIRS;
		
	if (o_sort_nocase)
		return g_strcasecmp(i1->leafname, i2->leafname);
	return strcmp(i1->leafname, i2->leafname);
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

	SORT_DIRS;

	return i1->mtime > i2->mtime ? -1 :
		i1->mtime < i2->mtime ? 1 :
		sort_by_name(item1, item2);
}

int sort_by_size(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) ((CollectionItem *) item1)->data;
	const DirItem *i2 = (DirItem *) ((CollectionItem *) item2)->data;

	SORT_DIRS;

	return i1->size > i2->size ? -1 :
		i1->size < i2->size ? 1 :
		sort_by_name(item1, item2);
}

/* Make the items as small as possible */
void shrink_grid(FilerWindow *filer_window)
{
	int		i;
	Collection	*col = filer_window->collection;
	int		width = MIN_ITEM_WIDTH;
	int		height = SMALL_HEIGHT;

	for (i = 0; i < col->number_of_items; i++)
	{
		int	w, h;

		calc_size(filer_window, &col->items[i], &w, &h);
		if (w > width)
			width = w;
		if (h > height)
			height = h;
	}

	collection_set_item_size(filer_window->collection, width, height);
}

void display_set_sort_fn(FilerWindow *filer_window,
			int (*fn)(const void *a, const void *b))
{
	if (filer_window->sort_fn == fn)
		return;

	filer_window->sort_fn = fn;

	collection_qsort(filer_window->collection,
			filer_window->sort_fn);
}

void display_set_layout(FilerWindow  *filer_window,
			DisplayStyle style,
			DetailsType  details)
{
	g_return_if_fail(filer_window != NULL);

	display_style_set(filer_window, style);
	display_details_set(filer_window, details);

	shrink_grid(filer_window);

	if (option_get_int("filer_auto_resize") != RESIZE_NEVER)
		filer_window_autosize(filer_window, TRUE);
}

/* Set the 'Show Hidden' flag for this window */
void display_set_hidden(FilerWindow *filer_window, gboolean hidden)
{
	if (filer_window->show_hidden == hidden)
		return;

	filer_window->show_hidden = hidden;

	filer_detach_rescan(filer_window);
}

/* Highlight (wink or cursor) this item in the filer window. If the item
 * isn't already there but we're scanning then highlight it if it
 * appears later.
 */
void display_set_autoselect(FilerWindow *filer_window, guchar *leaf)
{
	Collection	*col;
	int		i;

	g_return_if_fail(filer_window != NULL);
	col = filer_window->collection;
	
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

gboolean display_is_truncated(FilerWindow *filer_window, int i)
{
	Template template;
	Collection *collection = filer_window->collection;
	CollectionItem	*colitem = &collection->items[i];
	int	col = i % collection->columns;
	int	row = i / collection->columns;
	int	scroll = 0;
	GdkRectangle area;
	ViewData	*view = (ViewData *) colitem->view_data;

#ifndef GTK2
	scroll = collection->vadj->value;	/* (round to int) */
#endif

	if (filer_window->display_style == LARGE_ICONS ||
	    filer_window->display_style == HUGE_ICONS)
		return FALSE;	/* These wrap rather than truncate */

	area.x = col * collection->item_width;
	area.y = row * collection->item_height - scroll;
	area.height = collection->item_height;

	if (col == collection->columns - 1)
		area.width = GTK_WIDGET(collection)->allocation.width - area.x;
	else
		area.width = collection->item_width;

	fill_template(&area, colitem, filer_window, &template);

	return template.leafname.width < view->name_width;
}

/* Change the icon size (wraps) */
void display_change_size(FilerWindow *filer_window, gboolean bigger)
{
	DisplayStyle	new;

	g_return_if_fail(filer_window != NULL);

	switch (filer_window->display_style)
	{
		case LARGE_ICONS:
			new = bigger ? HUGE_ICONS : SMALL_ICONS;
			break;
		case HUGE_ICONS:
			new = bigger ? SMALL_ICONS : LARGE_ICONS;
			break;
		default:
			new = bigger ? LARGE_ICONS : HUGE_ICONS;
			break;
	}

	display_set_layout(filer_window, new, filer_window->details_type);
}

void display_free_colitem(Collection *collection, CollectionItem *colitem)
{
	ViewData	*view = (ViewData *) colitem->view_data;

	if (!view)
		return;

#ifdef GTK2
	if (view->layout)
	{
		g_object_unref(G_OBJECT(view->layout));
		view->layout = NULL;
	}
#endif
	
	g_free(view);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Parse file type colours and allocate/free them as necessary.
 * Returns the number of colours changed (0 if 'allocated' is FALSE from last
 * call).
 */
static int alloc_type_colours(void)
{
	gboolean	success[NUM_TYPE_COLOURS];
	int		change_count = 0;	/* No. needing realloc */
	int		i;
	static gboolean	allocated = FALSE;

	/* Parse colours */
	for (i = 0; i < NUM_TYPE_COLOURS; i++) {
		GdkColor *c = &type_colours[i];
		gushort r = c->red;
		gushort g = c->green;
		gushort b = c->blue;

		gdk_color_parse(
			option_get_static_string(opt_type_colours[i][0]),
			&type_colours[i]);

		if (allocated
		    && (c->red != r || c->green != g || c->blue != b))
			change_count++;
	}
	
	/* Free colours if they were previously allocated and
	 * have changed or become unneeded.
	 */
	if (allocated && (change_count || !o_display_colour_types))
	{
		gdk_colormap_free_colors(gdk_rgb_get_cmap(),
					 type_colours, NUM_TYPE_COLOURS);
		allocated = FALSE;
	}

	/* Allocate colours, unless they are still allocated (=> they didn't
	 * change) or we don't want them anymore.
	 * XXX: what should be done if allocation fails?
	 */
	if (!allocated && o_display_colour_types)
	{
		gdk_colormap_alloc_colors(gdk_rgb_get_cmap(),
				type_colours, NUM_TYPE_COLOURS,
				FALSE, TRUE, success);
		allocated = TRUE;
	}

	return change_count;
}

static void options_changed(void)
{
	gboolean	old_case = o_sort_nocase;
	gboolean	old_dirs = o_dirs_first;
	gboolean	old_colours = o_display_colour_types;
	GList		*next = all_filer_windows;
	int		ch_colours;

	o_sort_nocase = option_get_int("display_sort_nocase");
	o_dirs_first = option_get_int("display_dirs_first");
	o_large_truncate = option_get_int("display_large_width");
	o_small_truncate = option_get_int("display_small_width");
	o_display_colour_types = option_get_int("display_colour_types");

	ch_colours = alloc_type_colours();
	
	while (next)
	{
		FilerWindow *filer_window = (FilerWindow *) next->data;

		if (o_sort_nocase != old_case || o_dirs_first != old_dirs)
		{
			collection_qsort(filer_window->collection,
					filer_window->sort_fn);
		}
		shrink_grid(filer_window);
		if (old_colours != o_display_colour_types
		    || (o_display_colour_types && ch_colours))
			gtk_widget_queue_draw(filer_window->window);

		next = next->next;
	}
}

static int details_width(FilerWindow *filer_window, DirItem *item)
{
	return fixed_width * strlen(details(filer_window, item));
}

#ifndef GTK2
/* Fill in the split_width, split_height and split_pos of this item's
 * ViewData, assuming a wrap width of 'width' pixels.
 * Note: result may still be wider than 'width'.
 */
static void wrap_text(CollectionItem *colitem, int width)
{
	int	top_len, bot_len, sp;
	int	font_height = item_font->ascent + item_font->descent;
	DirItem	*item = (DirItem *) colitem->data;
	ViewData *view = (ViewData *) colitem->view_data;

	if (view->name_width < width)
	{
		view->split_height = font_height;
		view->split_width = view->name_width;
		view->split_pos = 0;
		return;
	}
		
	sp = strlen(item->leafname) / 2;
	view->split_pos = sp;

	top_len = gdk_text_measure(item_font, item->leafname, sp);
	bot_len = gdk_string_measure(item_font, item->leafname + sp);

	view->split_width = MAX(top_len, bot_len);
	view->split_height = font_height * 2;
}
#endif

static void huge_template(GdkRectangle *area, CollectionItem *colitem,
			   FilerWindow *filer_window, Template *template)
{
	int	col_width = filer_window->collection->item_width;
	DirItem	*item = (DirItem *) colitem->data;
	MaskedPixmap	*image = item->image;
	int		image_x, image_y;
	int		text_x, text_y;
	ViewData	*view = (ViewData *) colitem->view_data;

	if (image)
	{
		if (!image->huge_pixmap)
			pixmap_make_huge(image);
		template->icon.width = image->huge_width;
		template->icon.height = image->huge_height;
	}
	else
	{
		template->icon.width = HUGE_WIDTH * 3 / 2;
		template->icon.height = HUGE_HEIGHT;
	}

	image_x = area->x + ((col_width - template->icon.width) >> 1);
	image_y = area->y + (HUGE_HEIGHT - template->icon.height);

#ifdef GTK2
	template->leafname.width = view->name_width;
	template->leafname.height = view->name_height;
#else
	template->leafname.width = view->split_width;
	template->leafname.height = view->split_height;
#endif
	text_x = area->x + ((col_width - template->leafname.width) >> 1);
	text_y = image_y + template->icon.height + 2;

	template->leafname.x = text_x;
	template->leafname.y = text_y;

	template->icon.x = image_x;
	template->icon.y = image_y;
	template->icon.width = template->icon.width;
	template->icon.height = template->icon.height;
}

static void large_template(GdkRectangle *area, CollectionItem *colitem,
			   FilerWindow *filer_window, Template *template)
{
	int	col_width = filer_window->collection->item_width;
	DirItem		*item = (DirItem *) colitem->data;
	MaskedPixmap	*image = item->image;
	int		iwidth, iheight;
	int		image_x;
	int		image_y;
	ViewData	*view = (ViewData *) colitem->view_data;
	
	int		text_x, text_y;
	
	if (image)
	{
		iwidth = MIN(image->width, ICON_WIDTH);
		iheight = MIN(image->height + 6, ICON_HEIGHT);
	}
	else
	{
		iwidth = ICON_WIDTH;
		iheight = ICON_HEIGHT;
	}
	image_x = area->x + ((col_width - iwidth) >> 1);

#ifdef GTK2
	template->leafname.width = view->name_width;
	template->leafname.height = view->name_height;
#else
	template->leafname.width = view->split_width;
	template->leafname.height = view->split_height;
#endif
	text_x = area->x + ((col_width - template->leafname.width) >> 1);
	text_y = area->y + ICON_HEIGHT + 2;

	template->leafname.x = text_x;
	template->leafname.y = text_y;

	image_y = text_y - iheight;
	image_y = MAX(area->y, image_y);
	
	template->icon.x = image_x;
	template->icon.y = image_y;
	template->icon.width = iwidth;
	template->icon.height = MIN(ICON_HEIGHT, iheight);
}

static void small_template(GdkRectangle *area, CollectionItem *colitem,
			   FilerWindow *filer_window, Template *template)
{
	int	text_x = area->x + SMALL_WIDTH + 4;
	int	low_text_y;
	int	max_text_width = area->width - SMALL_WIDTH - 4;
	ViewData *view = (ViewData *) colitem->view_data;

	low_text_y = area->y + area->height / 2 - view->name_height / 2;

	template->leafname.x = text_x;
	template->leafname.y = low_text_y;
	template->leafname.width = MIN(max_text_width, view->name_width);
	template->leafname.height = view->name_height;
	
	template->icon.x = area->x;
	template->icon.y = area->y + 1;
	template->icon.width = SMALL_WIDTH;
	template->icon.height = SMALL_HEIGHT;
}

static void huge_full_template(GdkRectangle *area, CollectionItem *colitem,
			   FilerWindow *filer_window, Template *template)
{
	DirItem		*item = (DirItem *) colitem->data;
	MaskedPixmap	*image = item->image;
	int	max_text_width = area->width - HUGE_WIDTH - 4;
	int	fixed_height = fixed_font->ascent + fixed_font->descent;
	ViewData *view = (ViewData *) colitem->view_data;

	if (image)
	{
		if (!image->huge_pixmap)
			pixmap_make_huge(image);
		template->icon.width = image->huge_width;
		template->icon.height = image->huge_height;
	}
	else
	{
		template->icon.width = HUGE_WIDTH * 3 / 2;
		template->icon.height = HUGE_HEIGHT;
	}

	template->icon.x = area->x + (HUGE_WIDTH - template->icon.width) / 2;
	template->icon.y = area->y + (area->height - template->icon.height) / 2;

	template->leafname.x = area->x + HUGE_WIDTH + 4;
	template->leafname.y = area->y + area->height / 2
			     - (view->name_height + 2 + fixed_height) / 2;
	template->leafname.width = MIN(max_text_width, view->name_width);
	template->leafname.height = view->name_height;

	template->details_string = details(filer_window, item);
	
	template->details.x = template->leafname.x;
	template->details.y = template->leafname.y + view->name_height + 2;
	template->details.width = fixed_width *
					strlen(template->details_string);
	template->details.height = fixed_height;
}

static void large_full_template(GdkRectangle *area, CollectionItem *colitem,
			   FilerWindow *filer_window, Template *template)
{
	int	fixed_height = fixed_font->ascent + fixed_font->descent;
	int	max_text_width = area->width - ICON_WIDTH - 4;
	DirItem	*item = (DirItem *) colitem->data;
	ViewData *view = (ViewData *) colitem->view_data;

	template->icon.x = area->x;
	template->icon.y = area->y + (area->height - ICON_HEIGHT) / 2;
	template->icon.width = ICON_WIDTH;
	template->icon.height = ICON_HEIGHT;

	template->leafname.x = area->x + ICON_WIDTH + 4;
	template->leafname.y = area->y + area->height / 2
				- (view->name_height + 2 + fixed_height) / 2;
	template->leafname.width = MIN(max_text_width, view->name_width);
	template->leafname.height = view->name_height;

	template->details_string = details(filer_window, item);
	
	template->details.x = template->leafname.x;
	template->details.y = template->leafname.y + view->name_height + 2;
	template->details.width = fixed_width *
					strlen(template->details_string);
	template->details.height = fixed_height;
}

static void small_full_template(GdkRectangle *area, CollectionItem *colitem,
			   FilerWindow *filer_window, Template *template)
{
	int	col_width = filer_window->collection->item_width;
	int	fixed_height = fixed_font->ascent + fixed_font->descent;
	DirItem	*item = (DirItem *) colitem->data;

	small_template(area, colitem, filer_window, template);

	template->details_string = details(filer_window, item);
	
	template->details.width = fixed_width *
					strlen(template->details_string);
	template->details.x = area->x + col_width - template->details.width;
	template->details.y = area->y + area->height / 2 - fixed_height / 2;
	template->details.height = fixed_height;
}

#define INSIDE(px, py, area)	\
	(px >= area.x && py >= area.y && \
	 px < area.x + area.width && py < area.y + area.height)

static gboolean test_point(Collection *collection,
				int point_x, int point_y,
				CollectionItem *colitem,
				int width, int height,
				FilerWindow *filer_window)
{
	Template	template;
	GdkRectangle	area;

	area.x = 0;
	area.y = 0;
	area.width = width;
	area.height = height;

	fill_template(&area, colitem, filer_window, &template);

	return INSIDE(point_x, point_y, template.leafname) ||
	       INSIDE(point_x, point_y, template.icon) ||
	       (template.details_string &&
			INSIDE(point_x, point_y, template.details));
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

	width = MIN(image->sm_width, SMALL_WIDTH);
	height = MIN(image->sm_height, SMALL_HEIGHT);
	image_x = area->x + ((area->width - width) >> 1);
		
	gdk_gc_set_clip_mask(gc, item->image->sm_mask);

	image_y = MAX(0, SMALL_HEIGHT - image->sm_height);
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
				0, 0,			/* Source x,y */
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
		gdk_gc_set_clip_origin(gc, image_x + 2, area->y + 2);
		gdk_gc_set_clip_mask(gc, mp->sm_mask);
		gdk_draw_pixmap(widget->window, gc,
				mp->sm_pixmap,
				0, 0,			/* Source x,y */
				image_x + 2, area->y + 2, /* Dest x,y */
				-1, -1);
	}
	
	gdk_gc_set_clip_mask(gc, NULL);
	gdk_gc_set_clip_origin(gc, 0, 0);
}

/* Render the details somewhere */
static void draw_details(FilerWindow *filer_window, DirItem *item, int x, int y,
			 int width, gboolean selected, guchar *string)
{
	GtkWidget	*widget = GTK_WIDGET(filer_window->collection);
	DetailsType	type = filer_window->details_type;
	int		w;
	
	w = fixed_width * strlen(string);

	draw_string(widget,
			fixed_font,
			string, -1,
			x, y,
			w,
			width,
			filer_window->selection_state,
			selected, TRUE);

	if (item->lstat_errno || item->base_type == TYPE_UNKNOWN)
		return;

	if (type == DETAILS_SUMMARY || type == DETAILS_PERMISSIONS)
	{
		int	perm_offset = type == DETAILS_SUMMARY ? 5 : 0;
		
		perm_offset += 4 * applicable(item->uid, item->gid);

		/* Underline the effective permissions */
		gdk_draw_rectangle(widget->window,
				selected
				? widget->style->fg_gc[
					filer_window->selection_state]
				: type_gc,
				TRUE,
				x - 1 + fixed_width * perm_offset,
				y + fixed_font->descent - 1,
				fixed_width * 3 + 1, 1);
	}
}

/* Return a string (valid until next call) giving details
 * of this item.
 */
static char *details(FilerWindow *filer_window, DirItem *item)
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
	else if (filer_window->details_type == DETAILS_TYPE)
	{
		MIME_type	*type = item->mime_type;

		buf = g_strdup_printf("(%s/%s)",
				type->media_type, type->subtype);
	}
	else if (filer_window->details_type == DETAILS_TIMES)
	{
		guchar	*ctime, *mtime;
		
		ctime = g_strdup(pretty_time(&item->ctime));
		mtime = g_strdup(pretty_time(&item->mtime));
		
		buf = g_strdup_printf("a[%s] c[%s] m[%s]",
				pretty_time(&item->atime), ctime, mtime);
		g_free(ctime);
		g_free(mtime);
	}
	else if (filer_window->details_type == DETAILS_PERMISSIONS)
	{
		buf = g_strdup_printf("%s %-8.8s %-8.8s",
				pretty_permissions(m),
				user_name(item->uid),
				group_name(item->gid));
	}
	else
	{
		if (item->base_type != TYPE_DIRECTORY)
		{
			if (filer_window->display_style == SMALL_ICONS)
				buf = g_strdup(format_size_aligned(item->size));
			else
				buf = g_strdup(format_size(item->size));
		}
		else
			buf = g_strdup("-");
	}
		
	return buf;
}

static void draw_item(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area,
			FilerWindow *filer_window)
{
	DirItem		*item = (DirItem *) colitem->data;
	gboolean	selected = colitem->selected;
	Template	template;
	ViewData *view = (ViewData *) colitem->view_data;

	fill_template(area, colitem, filer_window, &template);

	/* Set up GC for coloured file types */
	if (!type_gc)
		type_gc = gdk_gc_new(widget->window);
		
	if (o_display_colour_types) {
		if (item->flags & ITEM_FLAG_EXEC_FILE)
			gdk_gc_set_foreground(type_gc, &type_colours[8]);
		else if (item->flags & ITEM_FLAG_APPDIR)
			gdk_gc_set_foreground(type_gc, &type_colours[9]);
		else
			gdk_gc_set_foreground(type_gc,
					&type_colours[item->base_type]);
	} else
		gdk_gc_set_foreground(type_gc,
				&widget->style->fg[GTK_STATE_NORMAL]);

	if (template.icon.width <= SMALL_WIDTH &&
			template.icon.height <= SMALL_HEIGHT)
		draw_small_icon(widget, &template.icon, item, selected);
	else if (template.icon.width <= ICON_WIDTH &&
			template.icon.height <= ICON_HEIGHT)
		draw_large_icon(widget, &template.icon, item, selected);
	else
		draw_huge_icon(widget, &template.icon, item, selected);
	
#ifdef GTK2
	gdk_draw_layout(widget->window, type_gc,
			template.leafname.x,
			template.leafname.y,
			view->layout);
#else
	if (view->split_pos)
	{
		guchar	*bot = item->leafname + view->split_pos;
		int	w;

		w = gdk_string_measure(item_font, bot);

		draw_string(widget,
				item_font,
				item->leafname, view->split_pos,
				template.leafname.x,
				template.leafname.y + item_font->ascent,
				template.leafname.width,
				template.leafname.width,
				filer_window->selection_state,
				selected, TRUE);
		draw_string(widget,
				item_font,
				bot, -1,
				template.leafname.x,
				template.leafname.y + item_font->ascent * 2 +
					item_font->descent,
				MAX(w, template.leafname.width),
				template.leafname.width,
				filer_window->selection_state,
				selected, TRUE);
	}
	else
		draw_string(widget,
				item_font,
				item->leafname, -1,
				template.leafname.x,
				template.leafname.y + item_font->ascent,
				view->name_width,
				template.leafname.width,
				filer_window->selection_state,
				selected, TRUE);
#endif
	
	if (template.details_string)
		draw_details(filer_window, item,
				template.details.x,
				template.details.y + fixed_font->ascent,
				template.details.width,
				selected, template.details_string);
}

static void free_views(Collection *collection)
{
	int	i;
	
	for (i = 0; i < collection->number_of_items; i++)
	{
		CollectionItem *ci = &collection->items[i];

		display_free_colitem(collection, ci);

		ci->view_data = NULL;
	}
}

/* Note: Call shrink_grid after this */
static void display_details_set(FilerWindow *filer_window, DetailsType details)
{
	if (filer_window->details_type == details)
		return;
	filer_window->details_type = details;
	free_views(filer_window->collection);

#ifndef GTK2
	filer_window->collection->paint_level = PAINT_CLEAR;
#endif
	gtk_widget_queue_clear(GTK_WIDGET(filer_window->collection));
}

/* Note: Call shrink_grid after this */
static void display_style_set(FilerWindow *filer_window, DisplayStyle style)
{
	if (filer_window->display_style == style)
		return;

	filer_window->display_style = style;

	free_views(filer_window->collection);

	collection_set_functions(filer_window->collection,
			(CollectionDrawFunc) draw_item,
			(CollectionTestFunc) test_point,
			filer_window);
}
