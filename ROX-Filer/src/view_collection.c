/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2002, the ROX-Filer team.
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

/* view_collection.c - a subclass of Collection, used for displaying files */

#include "config.h"

#include <gtk/gtk.h>
#include <time.h>
#include <math.h>

#include "global.h"

#include "collection.h"
#include "view_iface.h"
#include "view_collection.h"
#include "type.h"
#include "pixmaps.h"
#include "dir.h"
#include "diritem.h"
#include "gui_support.h"
#include "support.h"
#include "dnd.h"
#include "bind.h"
#include "options.h"

#include "display.h"	/* XXX */
#include "toolbar.h"	/* XXX */
#include "filer.h"	/* XXX */
#include "menu.h"	/* XXX */

#define MIN_ITEM_WIDTH 64

#if 0
/* The handler of the signal handler for scroll events.
 * This is used to cancel spring loading when autoscrolling is used.
 */
static gulong scrolled_signal = -1;
static GtkObject *scrolled_adj = NULL;	/* The object watched */
#endif

/* Item we are about to display a tooltip for */
static DirItem *tip_item = NULL;

static gpointer parent_class = NULL;

struct _ViewCollectionClass {
	GtkViewportClass parent;
};

struct _ViewCollection {
	GtkViewport viewport;

	Collection *collection;
	FilerWindow *filer_window;	/* Used for styles, etc */

	int	cursor_base;		/* Cursor when minibuffer opened */
};

typedef struct _Template Template;

struct _Template {
	GdkRectangle	icon;
	GdkRectangle	leafname;
	GdkRectangle	details;
};

/* GC for drawing colour filenames */
static GdkGC	*type_gc = NULL;

/* Static prototypes */
static void view_collection_finialize(GObject *object);
static void view_collection_class_init(gpointer gclass, gpointer data);
static void view_collection_init(GTypeInstance *object, gpointer gclass);

static void draw_item(GtkWidget *widget,
			CollectionItem *item,
			GdkRectangle *area,
			gpointer user_data);
static void fill_template(GdkRectangle *area, CollectionItem *item,
			ViewCollection *view_collection, Template *template);
static void huge_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void large_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void small_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void huge_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void large_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void small_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static gboolean test_point(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height,
				gpointer user_data);
static void draw_string(GtkWidget *widget,
		PangoLayout *layout,
		GdkRectangle *area,	/* Area available on screen */
		int 	width,		/* Width of the full string */
		GtkStateType selection_state,
		gboolean selected,
		gboolean box);
static void draw_small_icon(GtkWidget *widget,
			    GdkRectangle *area,
			    DirItem  *item,
			    MaskedPixmap *image,
			    gboolean selected);
static void draw_huge_icon(GtkWidget *widget,
			   GdkRectangle *area,
			   DirItem  *item,
			   MaskedPixmap *image,
			   gboolean selected);
static void view_collection_iface_init(gpointer giface, gpointer iface_data);
static gboolean name_is_truncated(ViewCollection *view_collection, int i);
static gint coll_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       ViewCollection *view_collection);
static gint coll_button_release(GtkWidget *widget,
			        GdkEventButton *event,
			        ViewCollection *view_collection);
static gint coll_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      ViewCollection *view_collection);
static void size_allocate(GtkWidget *w, GtkAllocation *a, gpointer data);
static void create_uri_list(ViewCollection *view_collection, GString *string);
static void perform_action(ViewCollection *view_collection,
			   GdkEventButton *event);
static void style_set(Collection 	*collection,
		      GtkStyle		*style,
		      ViewCollection	*view_collection);
static void display_free_colitem(Collection *collection,
				 CollectionItem *colitem);
static void lost_selection(Collection  *collection,
			   guint        time,
			   gpointer     user_data);
static void selection_changed(Collection *collection,
			      gint time,
			      gpointer user_data);
static void calc_size(FilerWindow *filer_window, CollectionItem *colitem,
		int *width, int *height);
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    ViewCollection	*view_collection);
static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       ViewCollection	*view_collection);
static void drag_end(GtkWidget *widget,
		     GdkDragContext *context,
		     ViewCollection *view_collection);
static void make_iter(ViewCollection *view_collection, ViewIter *iter,
		      IterFlags flags);
static void make_item_iter(ViewCollection *vc, ViewIter *iter, int i);

static void view_collection_sort(ViewIface *view);
static void view_collection_style_changed(ViewIface *view, int flags);
static gboolean view_collection_autoselect(ViewIface *view, const gchar *leaf);
static void view_collection_add_items(ViewIface *view, GPtrArray *items);
static void view_collection_update_items(ViewIface *view, GPtrArray *items);
static void view_collection_delete_if(ViewIface *view,
			  gboolean (*test)(gpointer item, gpointer data),
			  gpointer data);
static void view_collection_clear(ViewIface *view);
static void view_collection_select_all(ViewIface *view);
static void view_collection_clear_selection(ViewIface *view);
static int view_collection_count_items(ViewIface *view);
static int view_collection_count_selected(ViewIface *view);
static void view_collection_show_cursor(ViewIface *view);
static void view_collection_get_iter(ViewIface *view,
				     ViewIter *iter, IterFlags flags);
static void view_collection_cursor_to_iter(ViewIface *view, ViewIter *iter);
static void view_collection_set_selected(ViewIface *view,
					 ViewIter *iter,
					 gboolean selected);
static gboolean view_collection_get_selected(ViewIface *view, ViewIter *iter);
static void view_collection_select_only(ViewIface *view, ViewIter *iter);
static void view_collection_set_frozen(ViewIface *view, gboolean frozen);
static void view_collection_wink_item(ViewIface *view, ViewIter *iter);
static void view_collection_autosize(ViewIface *view);
static gboolean view_collection_cursor_visible(ViewIface *view);
static void view_collection_set_base(ViewIface *view, ViewIter *iter);

static DirItem *iter_next(ViewIter *iter);
static DirItem *iter_prev(ViewIter *iter);
static DirItem *iter_peek(ViewIter *iter);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

GtkWidget *view_collection_new(FilerWindow *filer_window)
{
	ViewCollection *view_collection;

	view_collection = g_object_new(view_collection_get_type(), NULL);
	view_collection->filer_window = filer_window;

	gtk_range_set_adjustment(GTK_RANGE(filer_window->scrollbar),
				 view_collection->collection->vadj);

	return GTK_WIDGET(view_collection);
}

GType view_collection_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (ViewCollectionClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			view_collection_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(ViewCollection),
			0,			/* n_preallocs */
			view_collection_init
		};
		static const GInterfaceInfo iface_info =
		{
			view_collection_iface_init, NULL, NULL
		};

		type = g_type_register_static(gtk_viewport_get_type(),
						"ViewCollection", &info, 0);
		g_type_add_interface_static(type, VIEW_TYPE_IFACE, &iface_info);
	}

	return type;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void view_collection_destroy(GtkObject *view_collection)
{
	VIEW_COLLECTION(view_collection)->filer_window = NULL;
}

static void view_collection_finialize(GObject *object)
{
	/* ViewCollection *view_collection = (ViewCollection *) object; */

	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void view_collection_class_init(gpointer gclass, gpointer data)
{
	GObjectClass *object = (GObjectClass *) gclass;

	parent_class = g_type_class_peek_parent(gclass);

	object->finalize = view_collection_finialize;
	GTK_OBJECT_CLASS(object)->destroy = view_collection_destroy;
}

static void view_collection_init(GTypeInstance *object, gpointer gclass)
{
	ViewCollection *view_collection = (ViewCollection *) object;
	GtkViewport *viewport = (GtkViewport *) object;
	GtkWidget *collection;
	GtkAdjustment *adj;

	collection = collection_new();
	view_collection->collection = COLLECTION(collection);

	adj = view_collection->collection->vadj;
	gtk_viewport_set_vadjustment(viewport, adj);
	gtk_viewport_set_shadow_type(viewport, GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(object), collection);
	gtk_widget_show(collection);
	gtk_widget_set_size_request(GTK_WIDGET(view_collection), 4, 4);

	gtk_container_set_resize_mode(GTK_CONTAINER(viewport),
			GTK_RESIZE_IMMEDIATE);

	view_collection->collection->free_item = display_free_colitem;
	view_collection->collection->draw_item = draw_item;
	view_collection->collection->test_point = test_point;
	view_collection->collection->cb_user_data = view_collection;

	g_signal_connect(collection, "style_set",
			G_CALLBACK(style_set),
			view_collection);

	g_signal_connect(collection, "lose_selection",
			G_CALLBACK(lost_selection), view_collection);
	g_signal_connect(collection, "selection_changed",
			G_CALLBACK(selection_changed), view_collection);

	g_signal_connect(collection, "button-release-event",
			G_CALLBACK(coll_button_release), view_collection);
	g_signal_connect(collection, "button-press-event",
			G_CALLBACK(coll_button_press), view_collection);
	g_signal_connect(collection, "motion-notify-event",
			G_CALLBACK(coll_motion_notify), view_collection);
	g_signal_connect(viewport, "size-allocate",
			G_CALLBACK(size_allocate), view_collection);

	gtk_widget_set_events(collection,
			GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
			GDK_BUTTON3_MOTION_MASK | GDK_POINTER_MOTION_MASK |
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

	/* Drag and drop events */
	g_signal_connect(collection, "drag_data_get",
			GTK_SIGNAL_FUNC(drag_data_get), NULL);

	make_drop_target(collection, 0);

	g_signal_connect(collection, "drag_motion",
			G_CALLBACK(drag_motion), view_collection);
	g_signal_connect(collection, "drag_leave",
			G_CALLBACK(drag_leave), view_collection);
	g_signal_connect(collection, "drag_end",
			G_CALLBACK(drag_end), view_collection);
}


static void draw_item(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area,
			gpointer user_data)
{
	DirItem		*item = (DirItem *) colitem->data;
	gboolean	selected = colitem->selected;
	Template	template;
	ViewData *view = (ViewData *) colitem->view_data;
	ViewCollection	*view_collection = (ViewCollection *) user_data;
	FilerWindow	*filer_window = view_collection->filer_window;

	g_return_if_fail(view != NULL);
	
	fill_template(area, colitem, view_collection, &template);
		
	/* Set up GC for coloured file types */
	if (!type_gc)
		type_gc = gdk_gc_new(widget->window);

	gdk_gc_set_foreground(type_gc, type_get_colour(item,
					&widget->style->fg[GTK_STATE_NORMAL]));

	if (template.icon.width <= SMALL_WIDTH &&
			template.icon.height <= SMALL_HEIGHT)
	{
		draw_small_icon(widget, &template.icon,
				item, view->image, selected);
	}
	else if (template.icon.width <= ICON_WIDTH &&
			template.icon.height <= ICON_HEIGHT)
	{
		draw_large_icon(widget, &template.icon,
				item, view->image, selected);
	}
	else
	{
		draw_huge_icon(widget, &template.icon,
				item, view->image, selected);
	}
	
	draw_string(widget, view->layout,
			&template.leafname,
			view->name_width,
			filer_window->selection_state,
			selected, TRUE);
	if (view->details)
		draw_string(widget, view->details,
				&template.details,
				template.details.width,
				filer_window->selection_state,
				selected, TRUE);
}

/* A template contains the locations of the three rectangles (for the icon,
 * name and extra details).
 * Fill in the empty 'template' with the rectanges for this item.
 */
static void fill_template(GdkRectangle *area, CollectionItem *colitem,
			ViewCollection *view_collection, Template *template)
{
	DisplayStyle	style = view_collection->filer_window->display_style;
	ViewData 	*view = (ViewData *) colitem->view_data;

	if (view->details)
	{
		template->details.width = view->details_width;
		template->details.height = view->details_height;

		if (style == SMALL_ICONS)
			small_full_template(area, colitem,
						view_collection, template);
		else if (style == LARGE_ICONS)
			large_full_template(area, colitem,
						view_collection, template);
		else
			huge_full_template(area, colitem,
						view_collection, template);
	}
	else
	{
		if (style == HUGE_ICONS)
			huge_template(area, colitem,
					view_collection, template);
		else if (style == LARGE_ICONS)
			large_template(area, colitem,
					view_collection, template);
		else
			small_template(area, colitem,
					view_collection, template);
	}
}

static void huge_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	col_width = view_collection->collection->item_width;
	int		text_x, text_y;
	ViewData	*view = (ViewData *) colitem->view_data;
	MaskedPixmap	*image = view->image;

	if (image)
	{
		if (!image->huge_pixbuf)
			pixmap_make_huge(image);
		template->icon.width = image->huge_width;
		template->icon.height = image->huge_height;
	}
	else
	{
		template->icon.width = HUGE_WIDTH * 3 / 2;
		template->icon.height = HUGE_HEIGHT;
	}

	template->leafname.width = view->name_width;
	template->leafname.height = view->name_height;

	text_x = area->x + ((col_width - template->leafname.width) >> 1);
	text_y = area->y + area->height - template->leafname.height;

	template->leafname.x = text_x;
	template->leafname.y = text_y;

	template->icon.x = area->x + ((col_width - template->icon.width) >> 1);
	template->icon.y = template->leafname.y - template->icon.height - 2;
}

static void large_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	col_width = view_collection->collection->item_width;
	int		iwidth, iheight;
	int		image_x;
	int		image_y;
	ViewData	*view = (ViewData *) colitem->view_data;
	MaskedPixmap	*image = view->image;
	
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

	template->leafname.width = view->name_width;
	template->leafname.height = view->name_height;

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
			   ViewCollection *view_collection, Template *template)
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
			   ViewCollection *view_collection, Template *template)
{
	int	max_text_width = area->width - HUGE_WIDTH - 4;
	ViewData *view = (ViewData *) colitem->view_data;
	MaskedPixmap	*image = view->image;

	if (image)
	{
		if (!image->huge_pixbuf)
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
			- (view->name_height + 2 + view->details_height) / 2;
	template->leafname.width = MIN(max_text_width, view->name_width);
	template->leafname.height = view->name_height;

	if (!image)
		return;		/* Not scanned yet */

	template->details.x = template->leafname.x;
	template->details.y = template->leafname.y + view->name_height + 2;
}

static void large_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	max_text_width = area->width - ICON_WIDTH - 4;
	ViewData *view = (ViewData *) colitem->view_data;
	MaskedPixmap *image = view->image;

	if (image)
	{
		template->icon.width = image->width;
		template->icon.height = image->height;
	}
	else
	{
		template->icon.width = ICON_WIDTH;
		template->icon.height = ICON_HEIGHT;
	}

	template->icon.x = area->x + (ICON_WIDTH - template->icon.width) / 2;
	template->icon.y = area->y + (area->height - template->icon.height) / 2;


	template->leafname.x = area->x + ICON_WIDTH + 4;
	template->leafname.y = area->y + area->height / 2
			- (view->name_height + 2 + view->details_height) / 2;
	template->leafname.width = MIN(max_text_width, view->name_width);
	template->leafname.height = view->name_height;

	if (!image)
		return;		/* Not scanned yet */

	template->details.x = template->leafname.x;
	template->details.y = template->leafname.y + view->name_height + 2;
}

static void small_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	col_width = view_collection->collection->item_width;
	ViewData *view = (ViewData *) colitem->view_data;

	small_template(area, colitem, view_collection, template);

	if (!view->image)
		return;		/* Not scanned yet */

	template->details.x = area->x + col_width - template->details.width;
	template->details.y = area->y + area->height / 2 - \
				view->details_height / 2;
}

#define INSIDE(px, py, area)	\
	(px >= area.x && py >= area.y && \
	 px <= area.x + area.width && py <= area.y + area.height)

static gboolean test_point(Collection *collection,
				int point_x, int point_y,
				CollectionItem *colitem,
				int width, int height,
				gpointer user_data)
{
	Template	template;
	GdkRectangle	area;
	ViewData	*view = (ViewData *) colitem->view_data;
	ViewCollection	*view_collection = (ViewCollection *) user_data;

	area.x = 0;
	area.y = 0;
	area.width = width;
	area.height = height;

	fill_template(&area, colitem, view_collection, &template);

	return INSIDE(point_x, point_y, template.leafname) ||
	       INSIDE(point_x, point_y, template.icon) ||
	       (view->details && INSIDE(point_x, point_y, template.details));
}

/* 'box' renders a background box if the string is also selected */
static void draw_string(GtkWidget *widget,
		PangoLayout *layout,
		GdkRectangle *area,	/* Area available on screen */
		int 	width,		/* Width of the full string */
		GtkStateType selection_state,
		gboolean selected,
		gboolean box)
{
	GdkGC		*gc = selected
			? widget->style->fg_gc[selection_state]
			: type_gc;
	
	if (selected && box)
		gtk_paint_flat_box(widget->style, widget->window, 
				selection_state, GTK_SHADOW_NONE,
				NULL, widget, "text",
				area->x, area->y,
				MIN(width, area->width),
				area->height);

	if (width > area->width)
	{
		gdk_gc_set_clip_origin(gc, 0, 0);
		gdk_gc_set_clip_rectangle(gc, area);
	}

	gdk_draw_layout(widget->window, gc, area->x, area->y, layout);

	if (width > area->width)
	{
		static GdkGC *red_gc = NULL;

		if (!red_gc)
		{
			gboolean success;
			GdkColor red = {0, 0xffff, 0, 0};

			red_gc = gdk_gc_new(widget->window);
			gdk_colormap_alloc_colors(
					gtk_widget_get_colormap(widget),
					&red, 1, FALSE, TRUE, &success);
			gdk_gc_set_foreground(red_gc, &red);
		}
		gdk_draw_rectangle(widget->window, red_gc, TRUE,
				area->x + area->width - 1, area->y,
				1, area->height);
		gdk_gc_set_clip_rectangle(gc, NULL);
	}
}

static void draw_small_icon(GtkWidget *widget,
			    GdkRectangle *area,
			    DirItem  *item,
			    MaskedPixmap *image,
			    gboolean selected)
{
	int		width, height, image_x, image_y;
	
	if (!image)
		return;

	if (!image->sm_pixbuf)
		pixmap_make_small(image);

	width = MIN(image->sm_width, SMALL_WIDTH);
	height = MIN(image->sm_height, SMALL_HEIGHT);
	image_x = area->x + ((area->width - width) >> 1);
	image_y = MAX(0, SMALL_HEIGHT - image->sm_height);
		
	gdk_pixbuf_render_to_drawable_alpha(
			selected ? image->sm_pixbuf_lit : image->sm_pixbuf,
			widget->window,
			0, 0, 				/* src */
			image_x, area->y + image_y,	/* dest */
			width, height,
			GDK_PIXBUF_ALPHA_FULL, 128,	/* (unused) */
			GDK_RGB_DITHER_NORMAL, 0, 0);

	if (item->flags & ITEM_FLAG_SYMLINK)
	{
		gdk_pixbuf_render_to_drawable_alpha(im_symlink->pixbuf,
				widget->window,
				0, 0, 				/* src */
				image_x, area->y + 8,	/* dest */
				-1, -1,
				GDK_PIXBUF_ALPHA_FULL, 128,	/* (unused) */
				GDK_RGB_DITHER_NORMAL, 0, 0);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		MaskedPixmap	*mp = item->flags & ITEM_FLAG_MOUNTED
					? im_mounted
					: im_unmounted;

		gdk_pixbuf_render_to_drawable_alpha(mp->pixbuf,
				widget->window,
				0, 0, 				/* src */
				image_x + 2, area->y + 2,	/* dest */
				-1, -1,
				GDK_PIXBUF_ALPHA_FULL, 128,	/* (unused) */
				GDK_RGB_DITHER_NORMAL, 0, 0);
	}
}

/* Draw this icon (including any symlink or mount symbol) inside the
 * given rectangle.
 */
static void draw_huge_icon(GtkWidget *widget,
			   GdkRectangle *area,
			   DirItem  *item,
			   MaskedPixmap *image,
			   gboolean selected)
{
	int		width, height;
	int		image_x;
	int		image_y;

	if (!image)
		return;

	width = image->huge_width;
	height = image->huge_height;
	image_x = area->x + ((area->width - width) >> 1);
	image_y = MAX(0, area->height - height - 6);

	gdk_pixbuf_render_to_drawable_alpha(
			selected ? image->huge_pixbuf_lit
				 : image->huge_pixbuf,
			widget->window,
			0, 0, 				/* src */
			image_x, area->y + image_y,	/* dest */
			width, height,
			GDK_PIXBUF_ALPHA_FULL, 128,	/* (unused) */
			GDK_RGB_DITHER_NORMAL, 0, 0);

	if (item->flags & ITEM_FLAG_SYMLINK)
	{
		gdk_pixbuf_render_to_drawable_alpha(im_symlink->pixbuf,
				widget->window,
				0, 0, 				/* src */
				image_x, area->y + 2,	/* dest */
				-1, -1,
				GDK_PIXBUF_ALPHA_FULL, 128,	/* (unused) */
				GDK_RGB_DITHER_NORMAL, 0, 0);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		MaskedPixmap	*mp = item->flags & ITEM_FLAG_MOUNTED
					? im_mounted
					: im_unmounted;

		gdk_pixbuf_render_to_drawable_alpha(mp->pixbuf,
				widget->window,
				0, 0, 				/* src */
				image_x, area->y + 2,		/* dest */
				-1, -1,
				GDK_PIXBUF_ALPHA_FULL, 128,	/* (unused) */
				GDK_RGB_DITHER_NORMAL, 0, 0);
	}
}

/* Create the handers for the View interface */
static void view_collection_iface_init(gpointer giface, gpointer iface_data)
{
	ViewIfaceClass *iface = giface;

	g_assert(G_TYPE_FROM_INTERFACE(iface) == VIEW_TYPE_IFACE);

	/* override stuff */
	iface->sort = view_collection_sort;
	iface->style_changed = view_collection_style_changed;
	iface->autoselect = view_collection_autoselect;
	iface->add_items = view_collection_add_items;
	iface->update_items = view_collection_update_items;
	iface->delete_if = view_collection_delete_if;
	iface->clear = view_collection_clear;
	iface->select_all = view_collection_select_all;
	iface->clear_selection = view_collection_clear_selection;
	iface->count_items = view_collection_count_items;
	iface->count_selected = view_collection_count_selected;
	iface->show_cursor = view_collection_show_cursor;
	iface->get_iter = view_collection_get_iter;
	iface->cursor_to_iter = view_collection_cursor_to_iter;
	iface->set_selected = view_collection_set_selected;
	iface->get_selected = view_collection_get_selected;
	iface->set_frozen = view_collection_set_frozen;
	iface->select_only = view_collection_select_only;
	iface->wink_item = view_collection_wink_item;
	iface->autosize = view_collection_autosize;
	iface->cursor_visible = view_collection_cursor_visible;
	iface->set_base = view_collection_set_base;
}

/* It's time to make the tooltip appear. If we're not over the item any
 * more, or the item doesn't need a tooltip, do nothing.
 */
static gboolean tooltip_activate(ViewCollection *view_collection)
{
	Collection *collection;
	gint 	x, y;
	int	i;
	GString	*tip = NULL;

	g_return_val_if_fail(tip_item != NULL, 0);

	if (!view_collection->filer_window)
		return FALSE;	/* Window has been destroyed */

	tooltip_show(NULL);

	collection = view_collection->collection;
	gdk_window_get_pointer(GTK_WIDGET(collection)->window, &x, &y, NULL);
	i = collection_get_item(collection, x, y);
	if (i == -1 || ((DirItem *) collection->items[i].data) != tip_item)
		return FALSE;	/* Not still under the pointer */

	/* OK, the filer window still exists and the pointer is still
	 * over the same item. Do we need to show a tip?
	 */

	tip = g_string_new(NULL);

	if (name_is_truncated(view_collection, i))
	{
		g_string_append(tip, tip_item->leafname);
		g_string_append_c(tip, '\n');
	}

	filer_add_tip_details(view_collection->filer_window, tip, tip_item);

	if (tip->len > 1)
	{
		g_string_truncate(tip, tip->len - 1);
		
		tooltip_show(tip->str);
	}

	g_string_free(tip, TRUE);

	return FALSE;
}

static gboolean name_is_truncated(ViewCollection *view_collection, int i)
{
	Template template;
	Collection *collection = view_collection->collection;
	FilerWindow	*filer_window = view_collection->filer_window;
	CollectionItem	*colitem = &collection->items[i];
	int	col = i % collection->columns;
	int	row = i / collection->columns;
	GdkRectangle area;
	ViewData	*view = (ViewData *) colitem->view_data;

	/* TODO: What if the window is narrower than 1 column? */
	if (filer_window->display_style == LARGE_ICONS ||
	    filer_window->display_style == HUGE_ICONS)
		return FALSE;	/* These wrap rather than truncate */

	area.x = col * collection->item_width;
	area.y = row * collection->item_height;
	area.height = collection->item_height;

	if (col == collection->columns - 1)
		area.width = GTK_WIDGET(collection)->allocation.width - area.x;
	else
		area.width = collection->item_width;

	fill_template(&area, colitem, view_collection, &template);

	return template.leafname.width < view->name_width;
}

static gint coll_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       ViewCollection *view_collection)
{
	Collection	*collection = view_collection->collection;
	FilerWindow	*filer_window = view_collection->filer_window;
	int		i;

	i = collection_get_item(collection, event->x, event->y);

	if (i == -1)
	{
		tooltip_show(NULL);
		tip_item = NULL;
	}
	else
	{
		DirItem *item = (DirItem *) collection->items[i].data;

		if (item != tip_item)
		{
			tooltip_show(NULL);

			tip_item = item;
			if (item)
				tooltip_prime((GtkFunction) tooltip_activate,
						G_OBJECT(view_collection));
		}
	}

	if (motion_state != MOTION_READY_FOR_DND)
		return FALSE;

	if (!dnd_motion_moved(event))
		return FALSE;

	i = collection_get_item(collection,
			event->x - (event->x_root - drag_start_x),
			event->y - (event->y_root - drag_start_y));
	if (i == -1)
		return FALSE;

	collection_wink_item(collection, -1);
	
	if (!collection->items[i].selected)
	{
		if (event->state & GDK_BUTTON1_MASK)
		{
			/* Select just this one */
			filer_window->temp_item_selected = TRUE;
			collection_clear_except(collection, i);
		}
		else
		{
			if (collection->number_selected == 0)
				filer_window->temp_item_selected = TRUE;
			collection_select_item(collection, i);
		}
	}

	g_return_val_if_fail(collection->number_selected > 0, TRUE);

	if (collection->number_selected == 1)
	{
		DirItem	 *item = (DirItem *) collection->items[i].data;
		ViewData *view = (ViewData *) collection->items[i].view_data;

		if (!item->image)
			item = dir_update_item(filer_window->directory,
						item->leafname);

		if (!item)
		{
			report_error(_("Item no longer exists!"));
			return FALSE;
		}

		drag_one_item(widget, event,
			make_path(filer_window->sym_path, item->leafname)->str,
			item, view ? view->image : NULL);
	}
	else
	{
		GString *uris;
	
		uris = g_string_new(NULL);
		create_uri_list(view_collection, uris);
		drag_selection(widget, event, uris->str);
		g_string_free(uris, TRUE);
	}

	return FALSE;
}

/* Viewport is to be resized, so calculate increments */
static void size_allocate(GtkWidget *w, GtkAllocation *a, gpointer data)
{
	Collection *col = ((ViewCollection *) data)->collection;

	col->vadj->step_increment = col->item_height;
	col->vadj->page_increment = col->vadj->page_size;
}

/* Append all the URIs in the selection to the string */
static void create_uri_list(ViewCollection *view_collection, GString *string)
{
	GString	*leader;
	ViewIter iter;
	DirItem	*item;

	leader = g_string_new("file://");
	g_string_append(leader, our_host_name_for_dnd());
	g_string_append(leader, view_collection->filer_window->sym_path);
	if (leader->str[leader->len - 1] != '/')
		g_string_append_c(leader, '/');

	make_iter(view_collection, &iter, VIEW_ITER_SELECTED);
	while ((item = iter.next(&iter)))
	{
		g_string_append(string, leader->str);
		g_string_append(string, item->leafname);
		g_string_append(string, "\r\n");
	}

	g_string_free(leader, TRUE);
}

static gint coll_button_release(GtkWidget *widget,
			        GdkEventButton *event,
				ViewCollection *view_collection)
{
	if (dnd_motion_release(event))
	{
		if (motion_buttons_pressed == 0 &&
					view_collection->collection->lasso_box)
		{
			collection_end_lasso(view_collection->collection,
				event->button == 1 ? GDK_SET : GDK_INVERT);
		}
		return FALSE;
	}

	perform_action(view_collection, event);

	return FALSE;
}

static gint coll_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      ViewCollection *view_collection)
{
	collection_set_cursor_item(view_collection->collection, -1);

	if (dnd_motion_press(widget, event))
		perform_action(view_collection, event);

	return FALSE;
}

static void perform_action(ViewCollection *view_collection,
			   GdkEventButton *event)
{
	Collection	*collection = view_collection->collection;
	DirItem		*dir_item;
	int		item;
	gboolean	press = event->type == GDK_BUTTON_PRESS;
	gboolean	selected = FALSE;
	OpenFlags	flags = 0;
	BindAction	action;
	FilerWindow	*filer_window = view_collection->filer_window;

	if (event->button > 3)
		return;

	item = collection_get_item(collection, event->x, event->y);

	if (item != -1 && event->button == 1 &&
		collection->items[item].selected &&
		filer_window->selection_state == GTK_STATE_INSENSITIVE)
	{
		/* Possibly a really slow DnD operation? */
		filer_window->temp_item_selected = FALSE;
		
		filer_selection_changed(filer_window, event->time);
		return;
	}

	if (filer_window->target_cb)
	{
		dnd_motion_ungrab();
		if (item != -1 && press && event->button == 1)
		{
			ViewIter iter;
			make_item_iter(view_collection, &iter, item);
			
			filer_window->target_cb(filer_window, &iter,
					filer_window->target_data);
		}
		filer_target_mode(filer_window, NULL, NULL, NULL);

		return;
	}

	action = bind_lookup_bev(
			item == -1 ? BIND_DIRECTORY : BIND_DIRECTORY_ICON,
			event);

	if (item != -1)
	{
		dir_item = (DirItem *) collection->items[item].data;
		selected = collection->items[item].selected;
	}
	else
		dir_item = NULL;

	switch (action)
	{
		case ACT_CLEAR_SELECTION:
			collection_clear_selection(collection);
			break;
		case ACT_TOGGLE_SELECTED:
			collection_toggle_item(collection, item);
			break;
		case ACT_SELECT_EXCL:
			collection_clear_except(collection, item);
			break;
		case ACT_EDIT_ITEM:
			flags |= OPEN_SHIFT;
			/* (no break) */
		case ACT_OPEN_ITEM:
		{
			ViewIter iter;

			make_item_iter(view_collection, &iter, item);
			
			if (event->button != 1 || event->state & GDK_MOD1_MASK)
				flags |= OPEN_CLOSE_WINDOW;
			else
				flags |= OPEN_SAME_WINDOW;
			if (o_new_button_1.int_value)
				flags ^= OPEN_SAME_WINDOW;
			if (event->type == GDK_2BUTTON_PRESS)
				collection_unselect_item(collection, item);
			dnd_motion_ungrab();

			filer_openitem(filer_window, &iter, flags);
			break;
		}
		case ACT_POPUP_MENU:
		{
			ViewIter iter;
			
			dnd_motion_ungrab();
			tooltip_show(NULL);

			make_item_iter(view_collection, &iter, item);
			show_filer_menu(filer_window,
					(GdkEvent *) event, &iter);
			break;
		}
		case ACT_PRIME_AND_SELECT:
			if (!selected)
				collection_clear_except(collection, item);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_AND_TOGGLE:
			collection_toggle_item(collection, item);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_FOR_DND:
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_IGNORE:
			if (press && event->button < 4)
			{
				if (item)
					collection_wink_item(collection, item);
				dnd_motion_start(MOTION_NONE);
			}
			break;
		case ACT_LASSO_CLEAR:
			collection_clear_selection(collection);
			/* (no break) */
		case ACT_LASSO_MODIFY:
			collection_lasso_box(collection, event->x, event->y);
			break;
		case ACT_RESIZE:
			filer_window_autosize(filer_window);
			break;
		default:
			g_warning("Unsupported action : %d\n", action);
			break;
	}
}

/* Nothing is selected anymore - give up primary */
static void lost_selection(Collection  *collection,
			   guint        time,
			   gpointer     user_data)
{
	ViewCollection *view_collection = VIEW_COLLECTION(user_data);

	filer_lost_selection(view_collection->filer_window, time);
}

static void selection_changed(Collection *collection,
			      gint time,
			      gpointer user_data)
{
	ViewCollection *view_collection = VIEW_COLLECTION(user_data);

	filer_selection_changed(view_collection->filer_window, time);
}

static void display_free_colitem(Collection *collection,
				 CollectionItem *colitem)
{
	ViewData	*view = (ViewData *) colitem->view_data;

	if (!view)
		return;

	if (view->layout)
	{
		g_object_unref(G_OBJECT(view->layout));
		view->layout = NULL;
	}
	if (view->details)
		g_object_unref(G_OBJECT(view->details));

	if (view->image)
		g_object_unref(view->image);
	
	g_free(view);
}

static void add_item(ViewCollection *view_collection, DirItem *item)
{
	Collection *collection = view_collection->collection;
	FilerWindow	*filer_window = view_collection->filer_window;
	int		old_w = collection->item_width;
	int		old_h = collection->item_height;
	int		w, h, i;
	
	i = collection_insert(collection, item,
				display_create_viewdata(filer_window, item));

	calc_size(filer_window, &collection->items[i], &w, &h); 

	if (w > old_w || h > old_h)
		collection_set_item_size(collection,
					 MAX(old_w, w),
					 MAX(old_h, h));
}

static void style_set(Collection 	*collection,
		      GtkStyle		*style,
		      ViewCollection	*view_collection)
{
	view_collection_style_changed(VIEW(view_collection),
			VIEW_UPDATE_VIEWDATA | VIEW_UPDATE_NAME);
}

/* Return the size needed for this item */
static void calc_size(FilerWindow *filer_window, CollectionItem *colitem,
		int *width, int *height)
{
	int		pix_width, pix_height;
	int		w;
	DisplayStyle	style = filer_window->display_style;
	ViewData	*view = (ViewData *) colitem->view_data;

	if (filer_window->details_type == DETAILS_NONE)
	{
                if (style == HUGE_ICONS)
		{
			if (view->image)
			{
				if (!view->image->huge_pixbuf)
					pixmap_make_huge(view->image);
				pix_width = view->image->huge_width;
				pix_height = view->image->huge_height;
			}
			else
			{
				pix_width = HUGE_WIDTH * 3 / 2;
				pix_height = HUGE_HEIGHT * 3 / 2;
			}
			*width = MAX(pix_width, view->name_width) + 4;
			*height = view->name_height + pix_height + 4;
		}
		else if (style == SMALL_ICONS)
		{
			w = MIN(view->name_width, o_small_width.int_value);
			*width = SMALL_WIDTH + 12 + w;
			*height = MAX(view->name_height, SMALL_HEIGHT) + 4;
		}
		else
		{
			if (view->image)
				pix_width = view->image->width;
			else
				pix_width = ICON_WIDTH;
			*width = MAX(pix_width, view->name_width) + 4;
			*height = view->name_height + ICON_HEIGHT + 2;
		}
	}
	else
	{
		w = view->details_width;
		if (style == HUGE_ICONS)
		{
			*width = HUGE_WIDTH + 12 + MAX(w, view->name_width);
			*height = HUGE_HEIGHT - 4;
		}
		else if (style == SMALL_ICONS)
		{
			int	text_height;

			*width = SMALL_WIDTH + view->name_width + 12 + w;
			text_height = MAX(view->name_height,
					  view->details_height);
			*height = MAX(text_height, SMALL_HEIGHT) + 4;
		}
		else
		{
                        *width = ICON_WIDTH + 12 + MAX(w, view->name_width);
			*height = ICON_HEIGHT;
		}
        }
}

static void update_item(ViewCollection *view_collection, int i)
{
	Collection *collection = view_collection->collection;
	int	old_w = collection->item_width;
	int	old_h = collection->item_height;
	int	w, h;
	CollectionItem *colitem;
	FilerWindow *filer_window = view_collection->filer_window;

	g_return_if_fail(i >= 0 && i < collection->number_of_items);
	
	colitem = &collection->items[i];

	display_update_view(filer_window,
			(DirItem *) colitem->data,
			(ViewData *) colitem->view_data,
			FALSE);
	
	calc_size(filer_window, colitem, &w, &h); 
	if (w > old_w || h > old_h)
		collection_set_item_size(collection,
					 MAX(old_w, w),
					 MAX(old_h, h));

	collection_draw_item(collection, i, TRUE);
}

/* Implementations of the View interface. See view_iface.c for comments. */

static void view_collection_style_changed(ViewIface *view, int flags)
{
	ViewCollection *view_collection = VIEW_COLLECTION(view);
	FilerWindow	*filer_window = view_collection->filer_window;
	int		i;
	Collection	*col = view_collection->collection;
	int		width = MIN_ITEM_WIDTH;
	int		height = SMALL_HEIGHT;
	int		n = col->number_of_items;

	if (n == 0 && filer_window->display_style != SMALL_ICONS)
		height = ICON_HEIGHT;

	/* Recalculate all the ViewData structs for this window
	 * (needed if the text or image has changed in any way) and
	 * get the size of each item.
	 */
	for (i = 0; i < n; i++)
	{
		CollectionItem *ci = &col->items[i];
		int	w, h;

		if (flags & (VIEW_UPDATE_VIEWDATA | VIEW_UPDATE_NAME))
			display_update_view(filer_window,
					(DirItem *) ci->data,
					(ViewData *) ci->view_data,
					(flags & VIEW_UPDATE_NAME) != 0);

		calc_size(filer_window, ci, &w, &h);
		if (w > width)
			width = w;
		if (h > height)
			height = h;
	}

	collection_set_item_size(col, width, height);
	
	gtk_widget_queue_draw(GTK_WIDGET(view_collection));
}

static void view_collection_sort(ViewIface *view)
{
	ViewCollection *view_collection = VIEW_COLLECTION(view);

	collection_qsort(view_collection->collection,
			view_collection->filer_window->sort_fn);
}

static gboolean view_collection_autoselect(ViewIface *view, const gchar *leaf)
{
	ViewCollection *view_collection = VIEW_COLLECTION(view);
	Collection	*col = view_collection->collection;
	int		i;

	for (i = 0; i < col->number_of_items; i++)
	{
		DirItem *item = (DirItem *) col->items[i].data;

		if (strcmp(item->leafname, leaf) == 0)
		{
			if (col->cursor_item != -1)
				collection_set_cursor_item(col, i);
			else
				collection_wink_item(col, i);
			return TRUE;
		}
	}

	return FALSE;
}

static void view_collection_add_items(ViewIface *view, GPtrArray *items)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	FilerWindow	*filer_window = view_collection->filer_window;
	int old_num, i;

	old_num = collection->number_of_items;
	for (i = 0; i < items->len; i++)
	{
		DirItem *item = (DirItem *) items->pdata[i];
		char		*leafname = item->leafname;

		if (leafname[0] == '.')
		{
			if (!filer_window->show_hidden)
				continue;

			if (leafname[1] == '\0')
				continue; /* Never show '.' */

			if (leafname[1] == '.' &&
					leafname[2] == '\0')
				continue; /* Never show '..' */
		}

		add_item(view_collection, item);
	}

	if (old_num != collection->number_of_items)
		view_collection_sort(view);
}

static void view_collection_update_items(ViewIface *view, GPtrArray *items)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	FilerWindow	*filer_window = view_collection->filer_window;
	int		i;

	g_return_if_fail(items->len > 0);
	
	/* The item data has already been modified, so this gives the
	 * final sort order...
	 */
	collection_qsort(collection, filer_window->sort_fn);

	for (i = 0; i < items->len; i++)
	{
		DirItem *item = (DirItem *) items->pdata[i];
		const gchar *leafname = item->leafname;
		int j;

		if (leafname[0] == '.' && filer_window->show_hidden == FALSE)
			continue;

		j = collection_find_item(collection, item,
						     filer_window->sort_fn);

		if (j < 0)
			g_warning("Failed to find '%s'\n", leafname);
		else
			update_item(view_collection, j);
	}
}

static void view_collection_delete_if(ViewIface *view,
			  gboolean (*test)(gpointer item, gpointer data),
			  gpointer data)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	collection_delete_if(collection, test, data);
}

static void view_collection_clear(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	
	collection_clear(collection);
}

static void view_collection_select_all(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	
	collection_select_all(collection);
}

static void view_collection_clear_selection(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	
	collection_clear_selection(collection);
}

static int view_collection_count_items(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	
	return collection->number_of_items;
}

static int view_collection_count_selected(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	
	return collection->number_selected;
}

static void view_collection_show_cursor(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	collection_move_cursor(collection, 0, 0);
}

/* The first time the next() method is used, this is called */
static DirItem *iter_init(ViewIter *iter)
{
	ViewCollection *view_collection = iter->view_collection;
	Collection *collection = view_collection->collection;
	int i = -1;
	int n = collection->number_of_items;
	int flags = iter->flags;

	g_return_val_if_fail(iter->n_remaining > 0, NULL);
	
	iter->peek = iter_peek;

	if (flags & VIEW_ITER_FROM_CURSOR)
	{
		i = collection->cursor_item;
		if (i == -1)
			return NULL;	/* No cursor */
	}
	else if (flags & VIEW_ITER_FROM_BASE)
		i = view_collection->cursor_base;
	
	if (i < 0 || i >= n)
	{
		/* Either a normal iteraction, or an iteration from an
		 * invalid starting point.
		 */
		if (flags & VIEW_ITER_BACKWARDS)
			i = n - 1;
		else
			i = 0;
	}

	if (i < 0 || i >= n)
		return NULL;	/* No items at all! */

	iter->next = flags & VIEW_ITER_BACKWARDS ? iter_prev : iter_next;
	iter->n_remaining--;
	iter->i = i;

	if (flags & VIEW_ITER_SELECTED && !collection->items[i].selected)
		return iter->next(iter);
	return iter->peek(iter);
}
/* Advance iter to point to the next item and return the new item
 * (this saves you calling peek after next each time).
 */
static DirItem *iter_next(ViewIter *iter)
{
	Collection *collection = iter->view_collection->collection;
	int n = collection->number_of_items;
	int i = iter->i;

	g_return_val_if_fail(iter->n_remaining >= 0, NULL);

	/* i is the last item returned (always valid) */

	g_return_val_if_fail(i >= 0 && i < n, NULL);
	
	while (iter->n_remaining)
	{
		i++;
		iter->n_remaining--;

		if (i == n)
			i = 0;

		g_return_val_if_fail(i >= 0 && i < n, NULL);

		if (iter->flags & VIEW_ITER_SELECTED &&
		    !collection->items[i].selected)
			continue;

		iter->i = i;
		return collection->items[i].data;
	}
	
	iter->i = -1;
	return NULL;
}

/* Like iter_next, but in the other direction */
static DirItem *iter_prev(ViewIter *iter)
{
	Collection *collection = iter->view_collection->collection;
	int n = collection->number_of_items;
	int i = iter->i;

	g_return_val_if_fail(iter->n_remaining >= 0, NULL);

	/* i is the last item returned (always valid) */

	g_return_val_if_fail(i >= 0 && i < n, NULL);

	while (iter->n_remaining)
	{
		i--;
		iter->n_remaining--;

		if (i == -1)
			i = collection->number_of_items - 1;

		g_return_val_if_fail(i >= 0 && i < n, NULL);

		if (iter->flags & VIEW_ITER_SELECTED &&
		    !collection->items[i].selected)
			continue;

		iter->i = i;
		return collection->items[i].data;
	}
	
	iter->i = -1;
	return NULL;
}

static DirItem *iter_peek(ViewIter *iter)
{
	Collection *collection = iter->view_collection->collection;
	int i = iter->i;

	if (i == -1)
		return NULL;
	
	g_return_val_if_fail(i >= 0 && i < collection->number_of_items, NULL);

	return collection->items[i].data;
}

static void make_iter(ViewCollection *view_collection, ViewIter *iter,
		      IterFlags flags)
{
	Collection *collection = view_collection->collection;

	iter->view_collection = view_collection;
	iter->next = iter_init;
	iter->peek = NULL;
	iter->i = -1;

	iter->flags = flags;

	if (flags & VIEW_ITER_ONE_ONLY)
	{
		iter->n_remaining = 1;
		iter->next(iter);
	}
	else
		iter->n_remaining = collection->number_of_items;
}

/* Set the iterator to return 'i' on the next peek() */
static void make_item_iter(ViewCollection *view_collection,
			   ViewIter *iter, int i)
{
	Collection *collection = view_collection->collection;

	g_return_if_fail(i >= -1 && i < collection->number_of_items);

	make_iter(view_collection, iter, 0);

	iter->i = i;
	iter->next = iter_next;
	iter->peek = iter_peek;
	iter->n_remaining = 0;
}

static void view_collection_get_iter(ViewIface *view,
				     ViewIter *iter, IterFlags flags)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);

	make_iter(view_collection, iter, flags);
}

static void view_collection_cursor_to_iter(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	
	g_return_if_fail(iter == NULL ||
			 iter->view_collection == view_collection);

	collection_set_cursor_item(collection, iter ? iter->i : -1);
}

static void view_collection_set_selected(ViewIface *view,
					 ViewIter *iter,
					 gboolean selected)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	
	g_return_if_fail(iter != NULL &&
			 iter->view_collection == view_collection);
	g_return_if_fail(iter->i >= 0 && iter->i < collection->number_of_items);

	if (selected)
		collection_select_item(collection, iter->i);
	else
		collection_unselect_item(collection, iter->i);
}

static gboolean view_collection_get_selected(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	g_return_val_if_fail(iter != NULL &&
			iter->view_collection == view_collection, FALSE);
	g_return_val_if_fail(iter->i >= 0 &&
				iter->i < collection->number_of_items, FALSE);
	
	return collection->items[iter->i].selected;
}

static void view_collection_select_only(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	g_return_if_fail(iter != NULL &&
			 iter->view_collection == view_collection);
	g_return_if_fail(iter->i >= 0 && iter->i < collection->number_of_items);

	collection_clear_except(collection, iter->i);
}

static void view_collection_set_frozen(ViewIface *view, gboolean frozen)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	if (frozen)
		collection->block_selection_changed++;
	else
		collection_unblock_selection_changed(collection,
				gtk_get_current_event_time(), TRUE);
}

static void view_collection_wink_item(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	g_return_if_fail(iter != NULL &&
			 iter->view_collection == view_collection);
	g_return_if_fail(iter->i >= 0 && iter->i < collection->number_of_items);

	collection_wink_item(collection, iter->i);
}

static void view_collection_autosize(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	FilerWindow	*filer_window = view_collection->filer_window;
	Collection	*collection = view_collection->collection;
	int 		n;
	int		w = collection->item_width;
	int		h = collection->item_height;
	int 		x;
	int		rows, cols;
	int 		max_x, max_rows;
	const float	r = 2.5;
	int		t = 0;
	int		space = 0;

	/* Get the extra height required for the toolbar and minibuffer,
	 * if visible.
	 */
	if (o_toolbar.int_value != TOOLBAR_NONE)
		t = filer_window->toolbar->allocation.height;
	if (filer_window->message)
		t += filer_window->message->allocation.height;
	if (GTK_WIDGET_VISIBLE(filer_window->minibuffer_area))
	{
		GtkRequisition req;

		gtk_widget_size_request(filer_window->minibuffer_area, &req);
		space = req.height + 2;
		t += space;
	}

	n = collection->number_of_items;
	n = MAX(n, 2);

	max_x = (o_filer_size_limit.int_value * screen_width) / 100;
	max_rows = (o_filer_size_limit.int_value * screen_height) / (h * 100);

	/* Aim for a size where
	 * 	   x = r(y + t + h),		(1)
	 * unless that's too wide.
	 *
	 * t = toolbar (and minibuffer) height
	 * r = desired (width / height) ratio
	 *
	 * Want to display all items:
	 * 	   (x/w)(y/h) = n
	 * 	=> xy = nwh
	 *	=> x(x/r - t - h) = nwh		(from 1)
	 *	=> xx - x.rt - hr(1 - nw) = 0
	 *	=> 2x = rt +/- sqrt(rt.rt + 4hr(nw - 1))
	 * Now,
	 * 	   4hr(nw - 1) > 0
	 * so
	 * 	   sqrt(rt.rt + ...) > rt
	 *
	 * So, the +/- must be +:
	 * 	
	 *	=> x = (rt + sqrt(rt.rt + 4hr(nw - 1))) / 2
	 *
	 * ( + w - 1 to round up)
	 */
	x = (r * t + sqrt(r*r*t*t + 4*h*r * (n*w - 1))) / 2 + w - 1;

	/* Limit x */
	if (x > max_x)
		x = max_x;

	cols = x / w;
	cols = MAX(cols, 1);

	/* Choose rows to display all items given our chosen x.
	 * Don't make the window *too* big!
	 */
	rows = (n + cols - 1) / cols;
	if (rows > max_rows)
		rows = max_rows;

	/* Leave some room for extra icons, but only in Small Icons mode
	 * otherwise it takes up too much space.
	 * Also, don't add space if the minibuffer is open.
	 */
	if (space == 0)
		space = filer_window->display_style == SMALL_ICONS ? h : 2;

	filer_window_set_size(filer_window,
			w * MAX(cols, 1),
			h * MAX(rows, 1) + space);
}

static gboolean view_collection_cursor_visible(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	return collection->cursor_item != -1;
}

static void view_collection_set_base(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);

	view_collection->cursor_base = iter->i;
}

static void drag_end(GtkWidget *widget,
		     GdkDragContext *context,
		     ViewCollection *view_collection)
{
	if (view_collection->filer_window->temp_item_selected)
	{
		view_collection_clear_selection(VIEW(view_collection));
		view_collection->filer_window->temp_item_selected = FALSE;
	}
}

/* Remove highlights */
static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       ViewCollection	*view_collection)
{
	dnd_spring_abort();
#if 0
	if (scrolled_adj)
	{
		g_signal_handler_disconnect(scrolled_adj, scrolled_signal);
		scrolled_adj = NULL;
	}
#endif
}

/* Called during the drag when the mouse is in a widget registered
 * as a drop target. Returns TRUE if we can accept the drop.
 */
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    ViewCollection	*view_collection)
{
	DirItem		*item;
	int		item_number;
	GdkDragAction	action = context->suggested_action;
	char	 	*new_path = NULL;
	const char	*type = NULL;
	gboolean	retval = FALSE;
	Collection	*collection = view_collection->collection;

	if (o_dnd_drag_to_icons.int_value)
		item_number = collection_get_item(collection, x, y);
	else
		item_number = -1;

	item = item_number >= 0
		? (DirItem *) collection->items[item_number].data
		: NULL;

	if (item && collection->items[item_number].selected)
		type = NULL;
	else
		type = dnd_motion_item(context, &item);

	if (!type)
		item = NULL;

	/* Don't allow drops to non-writeable directories. BUT, still
	 * allow drops on non-writeable SUBdirectories so that we can
	 * do the spring-open thing.
	 */
	if (item && type == drop_dest_dir &&
			!(item->flags & ITEM_FLAG_APPDIR))
	{
#if 0
		/* XXX: This is needed so that directories don't
		 * spring open while we scroll. Should go in
		 * view_collection.c, I think.
		 */

		/* XXX: Now it IS in view_collection, maybe we should
		 * fix it?
		 */
		
		GtkObject *vadj = GTK_OBJECT(collection->vadj);

		/* Subdir: prepare for spring-open */
		if (scrolled_adj != vadj)
		{
			if (scrolled_adj)
				gtk_signal_disconnect(scrolled_adj,
							scrolled_signal);
			scrolled_adj = vadj;
			scrolled_signal = gtk_signal_connect(
						scrolled_adj,
						"value_changed",
						GTK_SIGNAL_FUNC(scrolled),
						collection);
		}
#endif
		dnd_spring_load(context, view_collection->filer_window);
	}
	else
		dnd_spring_abort();

	if (item)
	{
		collection_set_cursor_item(collection,
				item_number);
	}
	else
	{
		collection_set_cursor_item(collection, -1);

		/* Disallow background drops within a single window */
		if (type && gtk_drag_get_source_widget(context) == widget)
			type = NULL;
	}

	if (type)
	{
		if (item)
			new_path = make_path(
					view_collection->filer_window->sym_path,
					item->leafname)->str;
		else
			new_path = view_collection->filer_window->sym_path;
	}

	g_dataset_set_data(context, "drop_dest_type", (gpointer) type);
	if (type)
	{
		gdk_drag_status(context, action, time);
		g_dataset_set_data_full(context, "drop_dest_path",
					g_strdup(new_path), g_free);
		retval = TRUE;
	}

	return retval;
}
