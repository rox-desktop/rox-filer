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

/* filer.c - code for handling filer windows */

#include <stdlib.h>
#include <stdio.h>
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

#define ROW_HEIGHT_LARGE 64
#define ROW_HEIGHT_SMALL 32
#define ROW_HEIGHT_FULL_INFO 44
#define SMALL_ICON_HEIGHT 20
#define SMALL_ICON_WIDTH 48
#define MAX_ICON_HEIGHT 42
#define MAX_ICON_WIDTH 48
#define PANEL_BORDER 2
#define MIN_ITEM_WIDTH 64

extern int collection_menu_button;
extern gboolean collection_single_click;

FilerWindow 	*window_with_focus = NULL;
GList		*all_filer_windows = NULL;

static DisplayStyle last_display_style = LARGE_ICONS;
static gboolean last_show_hidden = FALSE;
static int (*last_sort_fn)(const void *a, const void *b) = sort_by_type;

static FilerWindow *window_with_selection = NULL;

/* Options bits */
static guchar *style_to_name(void);
static guchar *sort_fn_to_name(void);
static void update_options_label(void);
	
static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();
static char *filer_single_click(char *data);
static char *filer_menu_on_2(char *data);
static char *filer_new_window_on_1(char *data);
static char *filer_toolbar(char *data);
static char *filer_display_style(char *data);
static char *filer_sort_by(char *data);

static OptionsSection options =
{
	"Filer window options",
	create_options,
	update_options,
	set_options,
	save_options
};

/* The values correspond to the menu indexes in the option widget */
typedef enum {
	TOOLBAR_NONE 	= 0,
	TOOLBAR_NORMAL 	= 1,
	TOOLBAR_GNOME 	= 2,
} ToolbarType;
static ToolbarType o_toolbar = TOOLBAR_NORMAL;
static GtkWidget *menu_toolbar;

static GtkWidget *display_label;

static gboolean o_single_click = FALSE;
static GtkWidget *toggle_single_click;
static gboolean o_new_window_on_1 = FALSE;	/* Button 1 => New window */
static GtkWidget *toggle_new_window_on_1;
static GtkWidget *toggle_menu_on_2;

/* Static prototypes */
static void attach(FilerWindow *filer_window);
static void detach(FilerWindow *filer_window);
static void filer_window_destroyed(GtkWidget    *widget,
				   FilerWindow	*filer_window);
static void show_menu(Collection *collection, GdkEventButton *event,
		int number_selected, gpointer user_data);
static gint focus_in(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window);
static gint focus_out(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window);
static void add_item(FilerWindow *filer_window, DirItem *item);
static void toolbar_up_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_home_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void add_button(GtkWidget *box, int pixmap,
			GtkSignalFunc cb, FilerWindow *filer_window,
			char *label, char *tip);
static GtkWidget *create_toolbar(FilerWindow *filer_window);
static int filer_confirm_close(GtkWidget *widget, GdkEvent *event,
				FilerWindow *window);
static int calc_width(FilerWindow *filer_window, DirItem *item);
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
		gboolean selected);
static void draw_item_large(GtkWidget *widget,
			CollectionItem *item,
			GdkRectangle *area);
static void draw_item_small(GtkWidget *widget,
			CollectionItem *item,
			GdkRectangle *area);
static void draw_item_full_info(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area);
static gboolean test_point_large(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height);
static gboolean test_point_small(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height);
static gboolean test_point_full_info(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height);
static void update_display(Directory *dir,
			DirAction	action,
			GPtrArray	*items,
			FilerWindow *filer_window);
static void shrink_width(FilerWindow *filer_window);
static gboolean may_rescan(FilerWindow *filer_window, gboolean warning);
static void open_item(Collection *collection,
		gpointer item_data, int item_number,
		gpointer user_data);
static gboolean minibuffer_show_cb(FilerWindow *filer_window);
static void set_autoselect(FilerWindow *filer_window, guchar *leaf);

static GdkAtom xa_string;
enum
{
	TARGET_STRING,
	TARGET_URI_LIST,
};

static GdkCursor *busy_cursor = NULL;
static GtkTooltips *tooltips = NULL;

void filer_init()
{
	xa_string = gdk_atom_intern("STRING", FALSE);

	options_sections = g_slist_prepend(options_sections, &options);
	option_register("filer_new_window_on_1", filer_new_window_on_1);
	option_register("filer_menu_on_2", filer_menu_on_2);
	option_register("filer_single_click", filer_single_click);
	option_register("filer_toolbar", filer_toolbar);
	option_register("filer_display_style", filer_display_style);
	option_register("filer_sort_by", filer_sort_by);

	busy_cursor = gdk_cursor_new(GDK_WATCH);

	tooltips = gtk_tooltips_new();
}

static gboolean if_deleted(gpointer item, gpointer removed)
{
	int	i = ((GPtrArray *) removed)->len;
	DirItem	**r = (DirItem **) ((GPtrArray *) removed)->pdata;
	char	*leafname = ((DirItem *) item)->leafname;

	while (i--)
	{
		if (strcmp(leafname, r[i]->leafname) == 0)
			return TRUE;
	}

	return FALSE;
}

static void update_item(FilerWindow *filer_window, DirItem *item)
{
	int	i;
	char	*leafname = item->leafname;

	if (leafname[0] == '.')
	{
		if (filer_window->show_hidden == FALSE || leafname[1] == '\0'
				|| (leafname[1] == '.' && leafname[2] == '\0'))
		return;
	}

	i = collection_find_item(filer_window->collection, item, dir_item_cmp);

	if (i >= 0)
		collection_draw_item(filer_window->collection, i, TRUE);
	else
		g_warning("Failed to find '%s'\n", item->leafname);
}

static void update_display(Directory *dir,
			DirAction	action,
			GPtrArray	*items,
			FilerWindow *filer_window)
{
	int	i;
	int	cursor = filer_window->collection->cursor_item;
	char	*as;
	Collection *collection = filer_window->collection;

	switch (action)
	{
		case DIR_ADD:
			as = filer_window->auto_select;

			for (i = 0; i < items->len; i++)
			{
				DirItem *item = (DirItem *) items->pdata[i];

				add_item(filer_window, item);

				if (cursor != -1 || !as)
					continue;

				if (strcmp(as, item->leafname) != 0)
					continue;

				cursor = collection->number_of_items - 1;
				if (filer_window->had_cursor)
				{
					collection_set_cursor_item(collection,
							cursor);
					filer_window->mini_cursor_base = cursor;
				}
				else
					collection_wink_item(collection,
							cursor);
			}

			collection_qsort(filer_window->collection,
					filer_window->sort_fn);
			break;
		case DIR_REMOVE:
			collection_delete_if(filer_window->collection,
					if_deleted,
					items);
			break;
		case DIR_END_SCAN:
			if (filer_window->window->window)
				gdk_window_set_cursor(
						filer_window->window->window,
						NULL);
			shrink_width(filer_window);
			if (filer_window->had_cursor &&
					collection->cursor_item == -1)
			{
				collection_set_cursor_item(collection, 0);
				filer_window->had_cursor = FALSE;
			}
			break;
		case DIR_UPDATE:
			for (i = 0; i < items->len; i++)
			{
				DirItem *item = (DirItem *) items->pdata[i];

				update_item(filer_window, item);
			}
			collection_qsort(filer_window->collection,
					filer_window->sort_fn);
			break;
	}
}

static void attach(FilerWindow *filer_window)
{
	gdk_window_set_cursor(filer_window->window->window, busy_cursor);
	collection_clear(filer_window->collection);
	dir_attach(filer_window->directory, (DirCallback) update_display,
			filer_window);
}

static void detach(FilerWindow *filer_window)
{
	g_return_if_fail(filer_window->directory != NULL);

	dir_detach(filer_window->directory,
			(DirCallback) update_display, filer_window);
	g_fscache_data_unref(dir_cache, filer_window->directory);
	filer_window->directory = NULL;
}

static void filer_window_destroyed(GtkWidget 	*widget,
				   FilerWindow 	*filer_window)
{
	all_filer_windows = g_list_remove(all_filer_windows, filer_window);

	if (window_with_selection == filer_window)
		window_with_selection = NULL;
	if (window_with_focus == filer_window)
		window_with_focus = NULL;

	if (filer_window->directory)
		detach(filer_window);

	g_free(filer_window->auto_select);
	g_free(filer_window->path);
	g_free(filer_window);

	if (--number_of_windows < 1)
		gtk_main_quit();
}

static int calc_width(FilerWindow *filer_window, DirItem *item)
{
	int		pix_width = item->image->width;

        switch (filer_window->display_style)
        {
                case FULL_INFO:
                        return MAX_ICON_WIDTH + 12 + 
				MAX(item->details_width, item->name_width);
		case SMALL_ICONS:
			return SMALL_ICON_WIDTH + 12 + item->name_width;
                default:
                        return MAX(pix_width, item->name_width) + 4;
        }
}
	
/* Add a single object to a directory display */
static void add_item(FilerWindow *filer_window, DirItem *item)
{
	char		*leafname = item->leafname;
	int		item_width;

	if (leafname[0] == '.')
	{
		if (filer_window->show_hidden == FALSE || leafname[1] == '\0'
				|| (leafname[1] == '.' && leafname[2] == '\0'))
		return;
	}

	item_width = calc_width(filer_window, item); 
	if (item_width > filer_window->collection->item_width)
		collection_set_item_size(filer_window->collection,
					 item_width,
					 filer_window->collection->item_height);
	collection_insert(filer_window->collection, item);
}

/* Is a point inside an item? */
static gboolean test_point_large(Collection *collection,
				int point_x, int point_y,
				CollectionItem *colitem,
				int width, int height)
{
	DirItem		*item = (DirItem *) colitem->data;
	GdkFont		*font = GTK_WIDGET(collection)->style->font;
	int		text_height = font->ascent + font->descent;
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

static gboolean test_point_full_info(Collection *collection,
				int point_x, int point_y,
				CollectionItem *colitem,
				int width, int height)
{
	DirItem		*item = (DirItem *) colitem->data;
	GdkFont		*font = GTK_WIDGET(collection)->style->font;
	MaskedPixmap	*image = item->image;
	int		image_y = MAX(0, MAX_ICON_HEIGHT - image->height);
	int		low_top = height
				- fixed_font->descent - 2 - fixed_font->ascent;

	if (point_x < image->width + 2)
		return point_x > 2 && point_y > image_y;
	
	point_x -= MAX_ICON_WIDTH + 8;

	if (point_y >= low_top)
		return point_x < item->details_width;
	if (point_y >= low_top - font->ascent - font->descent)
		return point_x < item->name_width;
	return FALSE;
}

static gboolean test_point_small(Collection *collection,
				int point_x, int point_y,
				CollectionItem *colitem,
				int width, int height)
{
	DirItem		*item = (DirItem *) colitem->data;
	MaskedPixmap	*image = item->image;
	int		image_y = MAX(0, SMALL_ICON_HEIGHT - image->height);
	GdkFont		*font = GTK_WIDGET(collection)->style->font;
	int		low_top = height
				- fixed_font->descent - 2 - font->ascent;
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
	MaskedPixmap	*image = item->image;
	int	width = MIN(image->width, SMALL_ICON_WIDTH);
	int	height = MIN(image->height, SMALL_ICON_HEIGHT);
	int	image_x = area->x + ((area->width - width) >> 1);
	int	image_y;
	GdkGC	*gc = selected ? widget->style->white_gc
						: widget->style->black_gc;
	if (!item->image)
		return;
		
	gdk_gc_set_clip_mask(gc, item->image->mask);

	image_y = MAX(0, SMALL_ICON_HEIGHT - image->height);
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
		gdk_gc_set_clip_mask(gc,
				default_pixmap[TYPE_SYMLINK].mask);
		gdk_draw_pixmap(widget->window, gc,
				default_pixmap[TYPE_SYMLINK].pixmap,
				0, 0,		/* Source x,y */
				image_x, area->y + 8,	/* Dest x,y */
				-1, -1);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		int	type = item->flags & ITEM_FLAG_MOUNTED
				? TYPE_MOUNTED
				: TYPE_UNMOUNTED;
		gdk_gc_set_clip_origin(gc, image_x, area->y + 8);
		gdk_gc_set_clip_mask(gc,
				default_pixmap[type].mask);
		gdk_draw_pixmap(widget->window, gc,
				default_pixmap[type].pixmap,
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
		gdk_gc_set_clip_mask(gc,
				default_pixmap[TYPE_SYMLINK].mask);
		gdk_draw_pixmap(widget->window, gc,
				default_pixmap[TYPE_SYMLINK].pixmap,
				0, 0,		/* Source x,y */
				image_x, area->y + 8,	/* Dest x,y */
				-1, -1);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		int	type = item->flags & ITEM_FLAG_MOUNTED
				? TYPE_MOUNTED
				: TYPE_UNMOUNTED;
		gdk_gc_set_clip_origin(gc, image_x, area->y + 8);
		gdk_gc_set_clip_mask(gc,
				default_pixmap[type].mask);
		gdk_draw_pixmap(widget->window, gc,
				default_pixmap[type].pixmap,
				0, 0,		/* Source x,y */
				image_x, area->y + 8, /* Dest x,y */
				-1, -1);
	}
	
	gdk_gc_set_clip_mask(gc, NULL);
	gdk_gc_set_clip_origin(gc, 0, 0);
}

static void draw_string(GtkWidget *widget,
		GdkFont	*font,
		char	*string,
		int 	x,
		int 	y,
		int 	width,
		gboolean selected)
{
	int	text_height = font->ascent + font->descent;

	if (selected)
		gtk_paint_flat_box(widget->style, widget->window, 
				GTK_STATE_SELECTED, GTK_SHADOW_NONE,
				NULL, widget, "text",
				x, y - font->ascent,
				width,
				text_height);

	gdk_draw_text(widget->window,
			font,
			selected ? widget->style->white_gc
				 : widget->style->black_gc,
			x, y,
			string, strlen(string));
}
	
/* Return a string (valid until next call) giving details
 * of this item.
 */
char *details(DirItem *item)
{
	mode_t	m = item->mode;
	static GString *buf = NULL;
        static char time_buf[32];
	if (!buf)
		buf = g_string_new(NULL);

        if (strftime(time_buf, sizeof(time_buf),
			"%d-%b-%Y %T", localtime(&item->mtime)) == 0)
		time_buf[0]= 0;

	g_string_sprintf(buf, "%s%s %s %s",
				item->flags & ITEM_FLAG_APPDIR? "App  " :
			        S_ISDIR(m) ? "Dir  " :
				S_ISCHR(m) ? "Char " :
				S_ISBLK(m) ? "Blck " :
				S_ISLNK(m) ? "Link " :
				S_ISSOCK(m) ? "Sock " :
				S_ISFIFO(m) ? "Pipe " : "File ",
			pretty_permissions(m),
			format_size_aligned(item->size),
			time_buf);
	return buf->str;
}

static void draw_item_full_info(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area)
{
	DirItem	*item = (DirItem *) colitem->data;
	MaskedPixmap	*image = item->image;
	GdkFont	*font = widget->style->font;
	int	text_x = area->x + MAX_ICON_WIDTH + 8;
	int	low_text_y = area->y + area->height - fixed_font->descent - 2;
	gboolean	selected = colitem->selected;
	GdkRectangle	pic_area;

	pic_area.x = area->x;
	pic_area.y = area->y;
	pic_area.width = image->width + 8;
	pic_area.height = area->height;

	draw_large_icon(widget, &pic_area, item, selected);
	
	draw_string(widget,
			widget->style->font,
			item->leafname, 
			text_x,
			low_text_y - font->descent - fixed_font->ascent,
			item->name_width,
			selected);
	draw_string(widget,
			fixed_font,
			details(item),
			text_x, low_text_y,
			item->details_width,
			selected);
	gdk_draw_rectangle(widget->window,
			selected ? widget->style->white_gc
				 : widget->style->black_gc,
			TRUE,
			text_x - 1 + fixed_width *
				(5 + 4 * applicable(item->uid, item->gid)),
			low_text_y + fixed_font->descent - 1,
			fixed_width * 3 + 1, 1);
}

static void draw_item_small(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area)
{
	DirItem	*item = (DirItem *) colitem->data;
	GdkFont	*font = widget->style->font;
	int	text_x = area->x + SMALL_ICON_WIDTH + 4;
	int	low_text_y = area->y + area->height - font->descent - 2;
	gboolean	selected = colitem->selected;
	GdkRectangle	pic_area;

	pic_area.x = area->x;
	pic_area.y = area->y;
	pic_area.width = SMALL_ICON_WIDTH;
	pic_area.height = SMALL_ICON_HEIGHT;

	draw_small_icon(widget, &pic_area, item, selected);
	
	draw_string(widget,
			widget->style->font,
			item->leafname, 
			text_x,
			low_text_y,
			item->name_width,
			selected);
}

static void draw_item_large(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area)
{
	DirItem		*item = (DirItem *) colitem->data;
	GdkFont	*font = widget->style->font;
	int	text_x = area->x + ((area->width - item->name_width) >> 1);
	int	text_y = area->y + area->height - font->descent - 2;
	gboolean	selected = colitem->selected;

	draw_large_icon(widget, area, item, selected);
	
	draw_string(widget,
			widget->style->font,
			item->leafname, 
			text_x, text_y, item->name_width,
			selected);
}

static void show_menu(Collection *collection, GdkEventButton *event,
		int item, gpointer user_data)
{
	show_filer_menu((FilerWindow *) user_data, event, item);
}

/* Returns TRUE iff the directory still exits. */
static gboolean may_rescan(FilerWindow *filer_window, gboolean warning)
{
	Directory *dir;
	
	g_return_val_if_fail(filer_window != NULL, FALSE);

	/* We do a fresh lookup (rather than update) because the inode may
	 * have changed.
	 */
	dir = g_fscache_lookup(dir_cache, filer_window->path);
	if (!dir)
	{
		if (warning)
			delayed_error("ROX-Filer", "Directory missing/deleted");
		gtk_widget_destroy(filer_window->window);
		return FALSE;
	}
	if (dir == filer_window->directory)
		g_fscache_data_unref(dir_cache, dir);
	else
	{
		detach(filer_window);
		filer_window->directory = dir;
		attach(filer_window);
	}

	return TRUE;
}

/* Another app has grabbed the selection */
static gint collection_lose_selection(GtkWidget *widget,
				      GdkEventSelection *event)
{
	if (window_with_selection &&
			window_with_selection->collection == COLLECTION(widget))
	{
		FilerWindow *filer_window = window_with_selection;
		window_with_selection = NULL;
		collection_clear_selection(filer_window->collection);
	}

	return TRUE;
}

/* Someone wants us to send them the selection */
static void selection_get(GtkWidget *widget, 
		       GtkSelectionData *selection_data,
		       guint      info,
		       guint      time,
		       gpointer   data)
{
	GString	*reply, *header;
	FilerWindow 	*filer_window;
	int		i;
	Collection	*collection;

	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");

	reply = g_string_new(NULL);
	header = g_string_new(NULL);

	switch (info)
	{
		case TARGET_STRING:
			g_string_sprintf(header, " %s",
					make_path(filer_window->path, "")->str);
			break;
		case TARGET_URI_LIST:
			g_string_sprintf(header, " file://%s%s",
					our_host_name(),
					make_path(filer_window->path, "")->str);
			break;
	}

	collection = filer_window->collection;
	for (i = 0; i < collection->number_of_items; i++)
	{
		if (collection->items[i].selected)
		{
			DirItem *item =
				(DirItem *) collection->items[i].data;
			
			g_string_append(reply, header->str);
			g_string_append(reply, item->leafname);
		}
	}
	/* This works, but I don't think I like it... */
	/* g_string_append_c(reply, ' '); */
	
	gtk_selection_data_set(selection_data, xa_string,
			8, reply->str + 1, reply->len - 1);
	g_string_free(reply, TRUE);
	g_string_free(header, TRUE);
}

/* No items are now selected. This might be because another app claimed
 * the selection or because the user unselected all the items.
 */
static void lose_selection(Collection 	*collection,
			   guint	time,
			   gpointer 	user_data)
{
	FilerWindow *filer_window = (FilerWindow *) user_data;

	if (window_with_selection == filer_window)
	{
		window_with_selection = NULL;
		gtk_selection_owner_set(NULL,
				GDK_SELECTION_PRIMARY,
				time);
	}
}

static void gain_selection(Collection 	*collection,
			   guint	time,
			   gpointer 	user_data)
{
	FilerWindow *filer_window = (FilerWindow *) user_data;

	if (gtk_selection_owner_set(GTK_WIDGET(collection),
				GDK_SELECTION_PRIMARY,
				time))
	{
		window_with_selection = filer_window;
	}
	else
		collection_clear_selection(filer_window->collection);
}

int sort_by_name(const void *item1, const void *item2)
{
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

static void open_item(Collection *collection,
		gpointer item_data, int item_number,
		gpointer user_data)
{
	FilerWindow	*filer_window = (FilerWindow *) user_data;
	GdkEvent 	*event;
	GdkEventButton 	*bevent;
	GdkEventKey 	*kevent;
	OpenFlags	flags = 0;

	event = (GdkEvent *) gtk_get_current_event();

	bevent = (GdkEventButton *) event;
	kevent = (GdkEventKey *) event;

	switch (event->type)
	{
		case GDK_2BUTTON_PRESS:
		case GDK_BUTTON_PRESS:
		case GDK_BUTTON_RELEASE:
			if (bevent->state & GDK_SHIFT_MASK)
				flags |= OPEN_SHIFT;

			if (o_new_window_on_1 ^ (bevent->button == 1))
				flags |= OPEN_SAME_WINDOW;
			
			if (bevent->button != 1)
				flags |= OPEN_CLOSE_WINDOW;
			
			if (o_single_click == FALSE &&
				(bevent->state & GDK_CONTROL_MASK) != 0)
				flags ^= OPEN_SAME_WINDOW | OPEN_CLOSE_WINDOW;
			break;
		case GDK_KEY_PRESS:
			flags |= OPEN_SAME_WINDOW;
			if (kevent->state & GDK_SHIFT_MASK)
				flags |= OPEN_SHIFT;
			break;
		default:
			break;
	}

	filer_openitem(filer_window, item_number, flags);
}

/* Return the full path to the directory containing object 'path'.
 * Relative paths are resolved from the filerwindow's path.
 */
static void follow_symlink(FilerWindow *filer_window, char *path,
				gboolean same_window)
{
	char	*real, *slash;
	char	*new_dir;

	if (path[0] != '/')
		path = make_path(filer_window->path, path)->str;

	real = pathdup(path);
	slash = strrchr(real, '/');
	if (!slash)
	{
		g_free(real);
		delayed_error("ROX-Filer",
				"Broken symlink (or you don't have permission "
				"to follow it).");
		return;
	}

	*slash = '\0';

	if (*real)
		new_dir = real;
	else
		new_dir = "/";

	if (filer_window->panel_type || !same_window)
	{
		FilerWindow *new;
		
		new = filer_opendir(new_dir, PANEL_NO);
		set_autoselect(new, slash + 1);
	}
	else
		filer_change_to(filer_window, new_dir, slash + 1);

	g_free(real);
}

void filer_openitem(FilerWindow *filer_window, int item_number, OpenFlags flags)
{
	gboolean	shift = (flags & OPEN_SHIFT) != 0;
	gboolean	close_mini = flags & OPEN_FROM_MINI;
	gboolean	same_window = (flags & OPEN_SAME_WINDOW) != 0
					&& !filer_window->panel_type;
	gboolean	close_window = (flags & OPEN_CLOSE_WINDOW) != 0
					&& !filer_window->panel_type;
	GtkWidget	*widget;
	char		*full_path;
	DirItem		*item = (DirItem *)
			filer_window->collection->items[item_number].data;
	gboolean	wink = TRUE, destroy = FALSE;

	widget = filer_window->window;
	full_path = make_path(filer_window->path,
			item->leafname)->str;

	if (item->flags & ITEM_FLAG_SYMLINK && shift)
	{
		char	path[MAXPATHLEN + 1];
		int	got;

		got = readlink(make_path(filer_window->path,
					item->leafname)->str,
				path, MAXPATHLEN);
		if (got < 0)
			delayed_error("ROX-Filer", g_strerror(errno));
		else
		{
			g_return_if_fail(got <= MAXPATHLEN);
			path[got] = '\0';

			follow_symlink(filer_window, path,
					flags & OPEN_SAME_WINDOW);
		}
		return;
	}

	switch (item->base_type)
	{
		case TYPE_DIRECTORY:
			if (item->flags & ITEM_FLAG_APPDIR && !shift)
			{
				run_app(make_path(filer_window->path,
						item->leafname)->str);
				if (close_window)
					destroy = TRUE;
				break;
			}

			if (item->flags & ITEM_FLAG_MOUNT_POINT && shift)
			{
				action_mount(filer_window, item);
				if (item->flags & ITEM_FLAG_MOUNTED)
					break;
			}

			if (same_window)
			{
				wink = FALSE;
				filer_change_to(filer_window, full_path, NULL);
				close_mini = FALSE;
			}
			else
				filer_opendir(full_path, PANEL_NO);
			break;
		case TYPE_FILE:
			if ((item->flags & ITEM_FLAG_EXEC_FILE) && !shift)
			{
				char	*argv[] = {NULL, NULL};

				argv[0] = full_path;

				if (spawn_full(argv, filer_window->path))
				{
					if (close_window)
						destroy = TRUE;
				}
				else
					report_error("ROX-Filer",
						"Failed to fork() child");
			}
			else
			{
				GString		*message;
				MIME_type	*type = shift ? &text_plain
							      : item->mime_type;

				g_return_if_fail(type != NULL);

				if (type_open(full_path, type))
				{
					if (close_window)
						destroy = TRUE;
				}
				else
				{
					message = g_string_new(NULL);
					g_string_sprintf(message, "No open "
						"action specified for files of "
						"this type (%s/%s)",
						type->media_type,
						type->subtype);
					report_error("ROX-Filer", message->str);
					g_string_free(message, TRUE);
				}
			}
			break;
		default:
			report_error("open_item",
					"I don't know how to open that");
			break;
	}

	if (destroy)
		gtk_widget_destroy(filer_window->window);
	else
	{
		if (wink)
			collection_wink_item(filer_window->collection,
						item_number);
		if (close_mini)
			minibuffer_hide(filer_window);
	}
}

static gint pointer_in(GtkWidget *widget,
			GdkEventCrossing *event,
			FilerWindow *filer_window)
{
	may_rescan(filer_window, TRUE);
	return FALSE;
}

static gint focus_in(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window)
{
	window_with_focus = filer_window;

	return FALSE;
}

static gint focus_out(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window)
{
	/* TODO: Shade the cursor */

	return FALSE;
}

/* Handle keys that can't be bound with the menu */
static gint key_press_event(GtkWidget	*widget,
			GdkEventKey	*event,
			FilerWindow	*filer_window)
{
	switch (event->keyval)
	{
		case GDK_BackSpace:
			change_to_parent(filer_window);
			break;
		default:
			return FALSE;
	}

	return TRUE;
}

static void toolbar_refresh_clicked(GtkWidget *widget,
				    FilerWindow *filer_window)
{
	full_refresh();
	update_dir(filer_window, TRUE);
}

static void toolbar_home_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	filer_change_to(filer_window, getenv("HOME"), NULL);
}

static void toolbar_up_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	change_to_parent(filer_window);
}

void change_to_parent(FilerWindow *filer_window)
{
	char	*copy;
	char	*slash;

	if (filer_window->path[0] == '/' && filer_window->path[1] == '\0')
		return;		/* Already in the root */
	
	copy = g_strdup(filer_window->path);
	slash = strrchr(copy, '/');

	if (slash)
	{
		*slash = '\0';
		filer_change_to(filer_window,
				*copy ? copy : "/",
				slash + 1);
	}
	else
		g_warning("No / in directory path!\n");

	g_free(copy);

}

/* Make filer_window display path. When finished, highlight item 'from', or
 * the first item if from is NULL. If there is currently no cursor then
 * simply wink 'from' (if not NULL).
 */
void filer_change_to(FilerWindow *filer_window, char *path, char *from)
{
	char	*from_dup;
	
	from_dup = from && *from ? g_strdup(from) : NULL;

	detach(filer_window);
	g_free(filer_window->path);
	filer_window->path = pathdup(path);

	filer_window->directory = g_fscache_lookup(dir_cache,
					filer_window->path);
	if (filer_window->directory)
	{
		g_free(filer_window->auto_select);
		filer_window->had_cursor =
			filer_window->collection->cursor_item != -1
			|| filer_window->had_cursor;
		filer_window->auto_select = from_dup;

		gtk_window_set_title(GTK_WINDOW(filer_window->window),
				filer_window->path);
		collection_set_cursor_item(filer_window->collection, -1);
		attach(filer_window);

		if (GTK_WIDGET_VISIBLE(filer_window->minibuffer))
			gtk_idle_add((GtkFunction) minibuffer_show_cb,
					filer_window);
	}
	else
	{
		char	*error;

		g_free(from_dup);
		error = g_strdup_printf("Directory '%s' is not accessible.",
				path);
		delayed_error("ROX-Filer", error);
		g_free(error);
		gtk_widget_destroy(filer_window->window);
	}
}

int selected_item_number(Collection *collection)
{
	int	i;
	
	g_return_val_if_fail(collection != NULL, -1);
	g_return_val_if_fail(IS_COLLECTION(collection), -1);
	g_return_val_if_fail(collection->number_selected == 1, -1);

	for (i = 0; i < collection->number_of_items; i++)
		if (collection->items[i].selected)
			return i;

	g_warning("selected_item: number_selected is wrong\n");

	return -1;
}

DirItem *selected_item(Collection *collection)
{
	int	item;

	item = selected_item_number(collection);

	if (item > -1)
		return (DirItem *) collection->items[item].data;
	return NULL;
}

static int filer_confirm_close(GtkWidget *widget, GdkEvent *event,
				FilerWindow *window)
{
	/* TODO: We can open lots of these - very irritating! */
	return get_choice("Close panel?",
			"You have tried to close a panel via the window "
			"manager - I usually find that this is accidental... "
			"really close?",
			2, "Remove", "Cancel") != 0;
}

/* Make the items as narrow as possible */
static void shrink_width(FilerWindow *filer_window)
{
	int		i;
	Collection	*col = filer_window->collection;
	int		width = MIN_ITEM_WIDTH;
	int		this_width;
	DisplayStyle	style = filer_window->display_style;
	GdkFont		*font;
	int		text_height;

	font = gtk_widget_get_default_style()->font;
	text_height = font->ascent + font->descent;
	
	for (i = 0; i < col->number_of_items; i++)
	{
		this_width = calc_width(filer_window,
				(DirItem *) col->items[i].data);
		if (this_width > width)
			width = this_width;
	}
	
	collection_set_item_size(filer_window->collection,
		width,
		style == FULL_INFO ? 	MAX_ICON_HEIGHT + 4 :
		style == SMALL_ICONS ? 	MAX(text_height, SMALL_ICON_HEIGHT) + 4
				     :	text_height + MAX_ICON_HEIGHT + 8);
}

void filer_set_sort_fn(FilerWindow *filer_window,
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

void filer_style_set(FilerWindow *filer_window, DisplayStyle style)
{
	if (filer_window->display_style == style)
		return;

	if (filer_window->panel_type)
		style = LARGE_ICONS;
	else
		last_display_style = style;

	filer_window->display_style = style;

	switch (style)
	{
		case SMALL_ICONS:
			collection_set_functions(filer_window->collection,
				draw_item_small, test_point_small);
			break;
		case FULL_INFO:
			collection_set_functions(filer_window->collection,
				draw_item_full_info, test_point_full_info);
			break;
		default:
			collection_set_functions(filer_window->collection,
				draw_item_large, test_point_large);
			break;
	}

	shrink_width(filer_window);

	update_options_label();
}

FilerWindow *filer_opendir(char *path, PanelType panel_type)
{
	GtkWidget	*hbox, *scrollbar, *collection;
	FilerWindow	*filer_window;
	GtkTargetEntry 	target_table[] =
	{
		{"text/uri-list", 0, TARGET_URI_LIST},
		{"STRING", 0, TARGET_STRING},
	};

	filer_window = g_new(FilerWindow, 1);
	filer_window->minibuffer = NULL;
	filer_window->path = pathdup(path);
	filer_window->had_cursor = FALSE;
	filer_window->auto_select = NULL;

	filer_window->directory = g_fscache_lookup(dir_cache,
						   filer_window->path);
	if (!filer_window->directory)
	{
		char	*error;

		error = g_strdup_printf("Directory '%s' not found.", path);
		delayed_error("ROX-Filer", error);
		g_free(error);
		g_free(filer_window->path);
		g_free(filer_window);
		return NULL;
	}

	filer_window->show_hidden = last_show_hidden;
	filer_window->panel_type = panel_type;
	filer_window->temp_item_selected = FALSE;
	filer_window->sort_fn = last_sort_fn;
	filer_window->flags = (FilerFlags) 0;
	filer_window->display_style = UNKNOWN_STYLE;

	filer_window->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(filer_window->window),
			filer_window->path);

	collection = collection_new(NULL);
	gtk_object_set_data(GTK_OBJECT(collection),
			"filer_window", filer_window);
	filer_window->collection = COLLECTION(collection);

	gtk_widget_add_events(filer_window->window, GDK_ENTER_NOTIFY);
	gtk_signal_connect(GTK_OBJECT(filer_window->window),
			"enter-notify-event",
			GTK_SIGNAL_FUNC(pointer_in), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "focus_in_event",
			GTK_SIGNAL_FUNC(focus_in), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "focus_out_event",
			GTK_SIGNAL_FUNC(focus_out), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "destroy",
			filer_window_destroyed, filer_window);

	gtk_signal_connect(GTK_OBJECT(filer_window->collection), "open_item",
			open_item, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "show_menu",
			show_menu, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "gain_selection",
			gain_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "lose_selection",
			lose_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "drag_selection",
			drag_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "drag_data_get",
			drag_data_get, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "selection_clear_event",
			GTK_SIGNAL_FUNC(collection_lose_selection), NULL);
	gtk_signal_connect (GTK_OBJECT(collection), "selection_get",
			GTK_SIGNAL_FUNC(selection_get), NULL);
	gtk_selection_add_targets(collection, GDK_SELECTION_PRIMARY,
			target_table,
			sizeof(target_table) / sizeof(*target_table));

	filer_style_set(filer_window, last_display_style);
	drag_set_dest(collection);

	if (panel_type)
	{
		int		swidth, sheight, iwidth, iheight;
		GtkWidget	*frame, *win = filer_window->window;

		gtk_window_set_wmclass(GTK_WINDOW(win), "ROX-Panel",
				"ROX-Filer");
		collection_set_panel(filer_window->collection, TRUE);
		gtk_signal_connect(GTK_OBJECT(filer_window->window),
				"delete_event",
				GTK_SIGNAL_FUNC(filer_confirm_close),
				filer_window);

		gdk_window_get_size(GDK_ROOT_PARENT(), &swidth, &sheight);
		iwidth = filer_window->collection->item_width;
		iheight = filer_window->collection->item_height;
		
		{
			int	height = iheight + PANEL_BORDER;
			int	y = panel_type == PANEL_TOP 
					? -PANEL_BORDER
					: sheight - height - PANEL_BORDER;

			gtk_widget_set_usize(collection, swidth, height);
			gtk_widget_set_uposition(win, 0, y);
		}

		frame = gtk_frame_new(NULL);
		gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
		gtk_container_add(GTK_CONTAINER(frame), collection);
		gtk_container_add(GTK_CONTAINER(win), frame);

		gtk_widget_show_all(frame);
		gtk_widget_realize(win);
		if (override_redirect)
			gdk_window_set_override_redirect(win->window, TRUE);
		make_panel_window(win->window);
	}
	else
	{
		GtkWidget	*vbox;
		int		col_height = ROW_HEIGHT_LARGE * 3;

		gtk_signal_connect(GTK_OBJECT(collection),
				"key_press_event",
				GTK_SIGNAL_FUNC(key_press_event), filer_window);
		gtk_window_set_default_size(GTK_WINDOW(filer_window->window),
			filer_window->display_style == LARGE_ICONS ? 400 : 512,
			o_toolbar == TOOLBAR_NONE ? col_height:
			o_toolbar == TOOLBAR_NORMAL ? col_height + 24 :
			col_height + 38);

		hbox = gtk_hbox_new(FALSE, 0);
		gtk_container_add(GTK_CONTAINER(filer_window->window),
					hbox);

		vbox = gtk_vbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
		
		if (o_toolbar != TOOLBAR_NONE)
		{
			GtkWidget *toolbar;
			
			toolbar = create_toolbar(filer_window);
			gtk_box_pack_start(GTK_BOX(vbox), toolbar,
					FALSE, TRUE, 0);
			gtk_widget_show_all(toolbar);
		}

		gtk_box_pack_start(GTK_BOX(vbox), collection, TRUE, TRUE, 0);

		filer_window->minibuffer = create_minibuffer(filer_window);
		gtk_box_pack_start(GTK_BOX(vbox), filer_window->minibuffer,
				FALSE, TRUE, 0);

		scrollbar = gtk_vscrollbar_new(COLLECTION(collection)->vadj);
		gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, TRUE, 0);
		gtk_accel_group_attach(filer_keys,
				GTK_OBJECT(filer_window->window));
		gtk_window_set_focus(GTK_WINDOW(filer_window->window),
				collection);

		gtk_widget_show(hbox);
		gtk_widget_show(vbox);
		gtk_widget_show(scrollbar);
		gtk_widget_show(collection);
	}

	number_of_windows++;
	gtk_widget_show(filer_window->window);
	attach(filer_window);

	all_filer_windows = g_list_prepend(all_filer_windows, filer_window);

	return filer_window;
}

static GtkWidget *create_toolbar(FilerWindow *filer_window)
{
	GtkWidget	*frame, *box;

	if (o_toolbar == TOOLBAR_GNOME)
	{
		frame = gtk_handle_box_new();
		box = gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL,
				GTK_TOOLBAR_BOTH);
		gtk_container_set_border_width(GTK_CONTAINER(box), 2);
		gtk_toolbar_set_space_style(GTK_TOOLBAR(box),
					GTK_TOOLBAR_SPACE_LINE);
		gtk_toolbar_set_button_relief(GTK_TOOLBAR(box),
					GTK_RELIEF_NONE);
	}
	else
	{
		frame = gtk_frame_new(NULL);
		gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);

		box = gtk_hbutton_box_new();
		gtk_button_box_set_child_size_default(16, 16);
		gtk_hbutton_box_set_spacing_default(2);
		gtk_button_box_set_layout(GTK_BUTTON_BOX(box),
				GTK_BUTTONBOX_START);
	}

	gtk_container_add(GTK_CONTAINER(frame), box);

	add_button(box, TOOLBAR_UP_ICON,
			GTK_SIGNAL_FUNC(toolbar_up_clicked),
			filer_window,
			"Up", "Change to parent directory");
	add_button(box, TOOLBAR_HOME_ICON,
			GTK_SIGNAL_FUNC(toolbar_home_clicked),
			filer_window,
			"Home", "Change to home directory");
	add_button(box, TOOLBAR_REFRESH_ICON,
			GTK_SIGNAL_FUNC(toolbar_refresh_clicked),
			filer_window,
			"Rescan", "Rescan directory contents");

	return frame;
}

static void add_button(GtkWidget *box, int pixmap,
			GtkSignalFunc cb, FilerWindow *filer_window,
			char *label, char *tip)
{
	GtkWidget 	*button, *icon;

	icon = gtk_pixmap_new(default_pixmap[pixmap].pixmap,
				default_pixmap[pixmap].mask);

	if (o_toolbar == TOOLBAR_GNOME)
	{
		gtk_toolbar_append_element(GTK_TOOLBAR(box),
				GTK_TOOLBAR_CHILD_BUTTON,
				NULL,
				label,
				tip, NULL,
				icon,
				cb, filer_window);
	}
	else
	{
		button = gtk_button_new();
		GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);

		gtk_container_add(GTK_CONTAINER(button), icon);
		gtk_signal_connect(GTK_OBJECT(button), "clicked",
				cb, filer_window);

		gtk_tooltips_set_tip(tooltips, button, tip, NULL);

		gtk_container_add(GTK_CONTAINER(box), button);
	}
}

/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options()
{
	GtkWidget	*vbox, *menu, *hbox;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	display_label = gtk_label_new("<>");
	gtk_label_set_line_wrap(GTK_LABEL(display_label), TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), display_label, FALSE, TRUE, 0);

	toggle_new_window_on_1 =
		gtk_check_button_new_with_label("New window on button 1 "
				"(RISC OS style)");
	gtk_box_pack_start(GTK_BOX(vbox), toggle_new_window_on_1,
			FALSE, TRUE, 0);

	toggle_menu_on_2 =
		gtk_check_button_new_with_label("Menu on button 2 "
				"(RISC OS style)");
	gtk_box_pack_start(GTK_BOX(vbox), toggle_menu_on_2, FALSE, TRUE, 0);

	toggle_single_click =
		gtk_check_button_new_with_label("Single-click nagivation");
	gtk_box_pack_start(GTK_BOX(vbox), toggle_single_click, FALSE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(hbox),
			gtk_label_new("Toolbar type for new windows"),
			FALSE, TRUE, 0);
	menu_toolbar = gtk_option_menu_new();
	menu = gtk_menu_new();
	gtk_menu_append(GTK_MENU(menu), gtk_menu_item_new_with_label("None"));
	gtk_menu_append(GTK_MENU(menu), gtk_menu_item_new_with_label("Normal"));
	gtk_menu_append(GTK_MENU(menu), gtk_menu_item_new_with_label("GNOME"));
	gtk_option_menu_set_menu(GTK_OPTION_MENU(menu_toolbar), menu);
	gtk_box_pack_start(GTK_BOX(hbox), menu_toolbar, TRUE, TRUE, 0);

	return vbox;
}

static void update_options_label(void)
{
	guchar	*str;
	
	str = g_strdup_printf("The last used display style (%s) and sort "
			"function (Sort By %s) will be saved if you click on "
			"Save.", style_to_name(), sort_fn_to_name());
	gtk_label_set_text(GTK_LABEL(display_label), str);
	g_free(str);
}

/* Reflect current state by changing the widgets in the options box */
static void update_options()
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_new_window_on_1),
			o_new_window_on_1);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_menu_on_2),
			collection_menu_button == 2 ? 1 : 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_single_click),
			o_single_click);
	gtk_option_menu_set_history(GTK_OPTION_MENU(menu_toolbar), o_toolbar);

	update_options_label();
}

/* Set current values by reading the states of the widgets in the options box */
static void set_options()
{
	GtkWidget 	*item, *menu;
	GList		*list;
	
	o_new_window_on_1 = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_new_window_on_1));

	collection_menu_button = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_menu_on_2)) ? 2 : 3;

	o_single_click = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_single_click));
	collection_single_click = o_single_click ? TRUE : FALSE;
	
	menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(menu_toolbar));
	item = gtk_menu_get_active(GTK_MENU(menu));
	list = gtk_container_children(GTK_CONTAINER(menu));
	o_toolbar = (ToolbarType) g_list_index(list, item);
	g_list_free(list);
	
}

static guchar *style_to_name(void)
{
	return last_display_style == LARGE_ICONS ? "Large Icons" :
		last_display_style == SMALL_ICONS ? "Small Icons" :
		"Full Info";
}

static guchar *sort_fn_to_name(void)
{
	return last_sort_fn == sort_by_name ? "Name" :
		last_sort_fn == sort_by_type ? "Type" :
		last_sort_fn == sort_by_date ? "Date" :
		"Size";
}

static void save_options()
{
	option_write("filer_new_window_on_1", o_new_window_on_1 ? "1" : "0");
	option_write("filer_menu_on_2",
			collection_menu_button == 2 ? "1" : "0");
	option_write("filer_single_click", o_single_click ? "1" : "0");
	option_write("filer_display_style", style_to_name());
	option_write("filer_sort_by", sort_fn_to_name());
	option_write("filer_toolbar", o_toolbar == TOOLBAR_NONE ? "None" :
				      o_toolbar == TOOLBAR_NORMAL ? "Normal" :
				      o_toolbar == TOOLBAR_GNOME ? "GNOME" :
				      "Unknown");
}

static char *filer_new_window_on_1(char *data)
{
	o_new_window_on_1 = atoi(data) != 0;
	return NULL;
}

static char *filer_menu_on_2(char *data)
{
	collection_menu_button = atoi(data) != 0 ? 2 : 3;
	return NULL;
}

static char *filer_single_click(char *data)
{
	o_single_click = atoi(data) != 0;
	collection_single_click = o_single_click ? TRUE : FALSE;
	return NULL;
}

static char *filer_display_style(char *data)
{
	if (g_strcasecmp(data, "Large Icons") == 0)
		last_display_style = LARGE_ICONS;
	else if (g_strcasecmp(data, "Small Icons") == 0)
		last_display_style = SMALL_ICONS;
	else if (g_strcasecmp(data, "Full Info") == 0)
		last_display_style = FULL_INFO;
	else
		return "Unknown display style";

	return NULL;
}

static char *filer_sort_by(char *data)
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
		return "Unknown sort type";

	return NULL;
}

static char *filer_toolbar(char *data)
{
	if (g_strcasecmp(data, "None") == 0)
		o_toolbar = TOOLBAR_NONE;
	else if (g_strcasecmp(data, "Normal") == 0)
		o_toolbar = TOOLBAR_NORMAL;
	else if (g_strcasecmp(data, "GNOME") == 0)
		o_toolbar = TOOLBAR_GNOME;
	else
		return "Unknown toolbar type";

	return NULL;
}

/* Note that filer_window may not exist after this call. */
void update_dir(FilerWindow *filer_window, gboolean warning)
{
	if (may_rescan(filer_window, warning))
		dir_update(filer_window->directory, filer_window->path);
}

void filer_set_hidden(FilerWindow *filer_window, gboolean hidden)
{
	Directory *dir = filer_window->directory;
	
	if (filer_window->show_hidden == hidden)
		return;

	filer_window->show_hidden = hidden;
	last_show_hidden = hidden;

	g_fscache_data_ref(dir_cache, dir);
	detach(filer_window);
	filer_window->directory = dir;
	attach(filer_window);
}

/* Refresh the various caches even if we don't think we need to */
void full_refresh(void)
{
	mount_update(TRUE);
}

/* This path has been mounted/umounted - update all dirs */
void filer_check_mounted(char *path)
{
	GList	*next = all_filer_windows;
	int	len;

	len = strlen(path);

	while (next)
	{
		FilerWindow *filer_window = (FilerWindow *) next->data;

		next = next->next;

		if (strncmp(path, filer_window->path, len) == 0)
		{
			char	s = filer_window->path[len];

			if (s == '/' || s == '\0')
				update_dir(filer_window, FALSE);
		}
	}
}

/* Like minibuffer_show(), except that:
 * - It returns FALSE (to be used from an idle callback)
 * - It checks that the filer window still exists.
 */
static gboolean minibuffer_show_cb(FilerWindow *filer_window)
{
	GList	*next = all_filer_windows;

	while (next)
	{
		FilerWindow *fw = (FilerWindow *) next->data;


		if (fw == filer_window)
		{
			minibuffer_show(filer_window);
			break;
		}
		
		next = next->next;
	}

	return FALSE;
}

/* Highlight (wink or cursor) this item in the filer window. If the item
 * isn't already there but we're scanning then highlight it if it
 * appears later.
 */
static void set_autoselect(FilerWindow *filer_window, guchar *leaf)
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
