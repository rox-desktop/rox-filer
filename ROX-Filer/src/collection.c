/*
 * $Id$
 *
 * Collection - a GTK+ widget
 * Copyright (C) 1999, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 *
 * The collection widget provides an area for displaying a collection of
 * objects (such as files). It allows the user to choose a selection of
 * them and provides signals to allow popping up menus, detecting
 * double-clicks etc.
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

#include <stdlib.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "collection.h"

#define MIN_WIDTH 80
#define MIN_HEIGHT 60
#define MINIMUM_ITEMS 16

int collection_menu_button = 3;
gboolean collection_single_click = FALSE;

enum
{
	ARG_0,
	ARG_VADJUSTMENT
};

/* Signals:
 *
 * void open_item(collection, item, item_number, user_data)
 * 	User has double clicked on this item.
 *
 * void drag_selection(collection, motion_event, number_selected, user_data)
 * 	User has tried to drag the selection.
 * 
 * void show_menu(collection, button_event, item, user_data)
 * 	User has menu-clicked on the collection. 'item' is the number
 * 	of the item clicked, or -1 if the click was over the background.
 *
 * void gain_selection(collection, time, user_data)
 * 	We've gone from no selected items to having a selection.
 * 	Time is the time of the event that caused the change, or
 * 	GDK_CURRENT_TIME if not known.
 *
 * void lose_selection(collection, time, user_data)
 * 	We've dropped to having no selected items.
 * 	Time is the time of the event that caused the change, or
 * 	GDK_CURRENT_TIME if not known.
 */
enum
{
	OPEN_ITEM,
	DRAG_SELECTION,
	SHOW_MENU,
	GAIN_SELECTION,
	LOSE_SELECTION,
	LAST_SIGNAL
};

static guint collection_signals[LAST_SIGNAL] = { 0 };

static guint32 current_event_time = GDK_CURRENT_TIME;

static GtkWidgetClass *parent_class = NULL;

static GdkCursor *crosshair = NULL;

/* Static prototypes */
static void draw_one_item(Collection 	*collection,
			  int 		item,
			  GdkRectangle 	*area);
static void collection_class_init(CollectionClass *class);
static void collection_init(Collection *object);
static void collection_destroy(GtkObject *object);
static void collection_finalize(GtkObject *object);
static void collection_realize(GtkWidget *widget);
static gint collection_paint(Collection 	*collection,
			     GdkRectangle 	*area);
static void collection_size_request(GtkWidget 		*widget,
				    GtkRequisition 	*requisition);
static void collection_size_allocate(GtkWidget 		*widget,
				     GtkAllocation 	*allocation);
static void collection_set_adjustment(Collection 	*collection,
				      GtkAdjustment 	*vadj);
static void collection_set_arg(	GtkObject *object,
				GtkArg    *arg,
				guint     arg_id);
static void collection_get_arg(	GtkObject *object,
				GtkArg    *arg,
				guint     arg_id);
static void collection_adjustment(GtkAdjustment *adjustment,
				  Collection    *collection);
static void collection_disconnect(GtkAdjustment *adjustment,
				  Collection    *collection);
static void set_vadjustment(Collection *collection);
static void collection_draw(GtkWidget *widget, GdkRectangle *area);
static gint collection_expose(GtkWidget *widget, GdkEventExpose *event);
static void scroll_by(Collection *collection, gint diff);
static gint collection_button_press(GtkWidget      *widget,
				    GdkEventButton *event);
static gint collection_button_release(GtkWidget      *widget,
				      GdkEventButton *event);
static void default_draw_item(GtkWidget *widget,
				CollectionItem *data,
				GdkRectangle *area);
static gboolean	default_test_point(Collection *collection,
				   int point_x, int point_y,
				   CollectionItem *data,
				   int width, int height);
static gint collection_motion_notify(GtkWidget *widget,
				     GdkEventMotion *event);
static void add_lasso_box(Collection *collection);
static void remove_lasso_box(Collection *collection);
static void draw_lasso_box(Collection *collection);
static int item_at_row_col(Collection *collection, int row, int col);
static void collection_clear_except(Collection *collection, gint exception);
static void cancel_wink(Collection *collection);
static gint collection_key_press(GtkWidget *widget, GdkEventKey *event);
static void get_visible_limits(Collection *collection, int *first, int *last);
static void scroll_to_show(Collection *collection, int item);

static void draw_one_item(Collection *collection, int item, GdkRectangle *area)
{
	if (item < collection->number_of_items)
	{
		collection->draw_item((GtkWidget *) collection,
				&collection->items[item],
				area);
		if (item == collection->wink_item)
			gdk_draw_rectangle(((GtkWidget *) collection)->window,
				   ((GtkWidget *) collection)->style->black_gc,
				   FALSE,
				   area->x, area->y,
				   area->width - 1, area->height - 1);
	}
	if (item == collection->cursor_item)
	{
		gdk_draw_rectangle(((GtkWidget *) collection)->window,
			collection->target_cb
				? ((GtkWidget *) collection)->style->white_gc
				: ((GtkWidget *) collection)->style->black_gc,
			FALSE,
			area->x + 1, area->y + 1,
			area->width - 3, area->height - 3);
	}
}
		
GtkType collection_get_type(void)
{
	static guint my_type = 0;

	if (!my_type)
	{
		static const GtkTypeInfo my_info =
		{
			"Collection",
			sizeof(Collection),
			sizeof(CollectionClass),
			(GtkClassInitFunc) collection_class_init,
			(GtkObjectInitFunc) collection_init,
			NULL,			/* Reserved 1 */
			NULL,			/* Reserved 2 */
			(GtkClassInitFunc) NULL	/* base_class_init_func */
		};

		my_type = gtk_type_unique(gtk_widget_get_type(),
				&my_info);
	}

	return my_type;
}

static void collection_class_init(CollectionClass *class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = (GtkObjectClass*) class;
	widget_class = (GtkWidgetClass*) class;

	parent_class = gtk_type_class(gtk_widget_get_type());

	gtk_object_add_arg_type("Collection::vadjustment",
			GTK_TYPE_ADJUSTMENT,
			GTK_ARG_READWRITE | GTK_ARG_CONSTRUCT,
			ARG_VADJUSTMENT);

	object_class->destroy = collection_destroy;
	object_class->finalize = collection_finalize;

	widget_class->realize = collection_realize;
	widget_class->draw = collection_draw;
	widget_class->expose_event = collection_expose;
	widget_class->size_request = collection_size_request;
	widget_class->size_allocate = collection_size_allocate;

	widget_class->key_press_event = collection_key_press;
	widget_class->button_press_event = collection_button_press;
	widget_class->button_release_event = collection_button_release;
	widget_class->motion_notify_event = collection_motion_notify;
	object_class->set_arg = collection_set_arg;
	object_class->get_arg = collection_get_arg;

	class->open_item = NULL;
	class->drag_selection = NULL;
	class->show_menu = NULL;
	class->gain_selection = NULL;
	class->lose_selection = NULL;

	collection_signals[OPEN_ITEM] = gtk_signal_new("open_item",
				     GTK_RUN_FIRST,
				     object_class->type,
				     GTK_SIGNAL_OFFSET(CollectionClass,
						     open_item),
				     gtk_marshal_NONE__POINTER_UINT,
				     GTK_TYPE_NONE, 2,
				     GTK_TYPE_POINTER,
				     GTK_TYPE_UINT);
	collection_signals[DRAG_SELECTION] = gtk_signal_new("drag_selection",
				     GTK_RUN_FIRST,
				     object_class->type,
				     GTK_SIGNAL_OFFSET(CollectionClass,
						     drag_selection),
				     gtk_marshal_NONE__POINTER_UINT,
				     GTK_TYPE_NONE, 2,
				     GTK_TYPE_POINTER,
				     GTK_TYPE_UINT);
	collection_signals[SHOW_MENU] = gtk_signal_new("show_menu",
				     GTK_RUN_FIRST,
				     object_class->type,
				     GTK_SIGNAL_OFFSET(CollectionClass,
						     show_menu),
				     gtk_marshal_NONE__POINTER_INT,
				     GTK_TYPE_NONE, 2,
				     GTK_TYPE_POINTER,
				     GTK_TYPE_INT);
	collection_signals[GAIN_SELECTION] = gtk_signal_new("gain_selection",
				     GTK_RUN_FIRST,
				     object_class->type,
				     GTK_SIGNAL_OFFSET(CollectionClass,
						     gain_selection),
				     gtk_marshal_NONE__UINT,
				     GTK_TYPE_NONE, 1,
				     GTK_TYPE_UINT);
	collection_signals[LOSE_SELECTION] = gtk_signal_new("lose_selection",
				     GTK_RUN_FIRST,
				     object_class->type,
				     GTK_SIGNAL_OFFSET(CollectionClass,
						     lose_selection),
				     gtk_marshal_NONE__UINT,
				     GTK_TYPE_NONE, 1,
				     GTK_TYPE_UINT);

	gtk_object_class_add_signals(object_class,
				collection_signals, LAST_SIGNAL);
}

static void collection_init(Collection *object)
{
	g_return_if_fail(object != NULL);
	g_return_if_fail(IS_COLLECTION(object));

	if (!crosshair)
		crosshair = gdk_cursor_new(GDK_CROSSHAIR);

	GTK_WIDGET_SET_FLAGS(GTK_WIDGET(object), GTK_CAN_FOCUS);

	object->panel = FALSE;
	object->number_of_items = 0;
	object->number_selected = 0;
	object->columns = 1;
	object->item_width = 64;
	object->item_height = 64;
	object->vadj = NULL;
	object->paint_level = PAINT_OVERWRITE;
	object->last_scroll = 0;

	object->items = g_malloc(sizeof(CollectionItem) * MINIMUM_ITEMS);
	object->cursor_item = -1;
	object->wink_item = -1;
	object->array_size = MINIMUM_ITEMS;
	object->draw_item = default_draw_item;
	object->test_point = default_test_point;

	object->buttons_pressed = 0;
	object->may_drag = FALSE;
	
	return;
}

GtkWidget* collection_new(GtkAdjustment *vadj)
{
	if (vadj)
		g_return_val_if_fail(GTK_IS_ADJUSTMENT(vadj), NULL);

	return GTK_WIDGET(gtk_widget_new(collection_get_type(),
				"vadjustment", vadj,
				NULL));
}

void collection_set_functions(Collection *collection,
				CollectionDrawFunc draw_item,
				CollectionTestFunc test_point)
{
	GtkWidget	*widget;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	widget = GTK_WIDGET(collection);
	
	if (!draw_item)
		draw_item = default_draw_item;
	if (!test_point)
		test_point = default_test_point;

	collection->draw_item = draw_item;
	collection->test_point = test_point;

	if (GTK_WIDGET_REALIZED(widget))
	{
		collection->paint_level = PAINT_CLEAR;
		gtk_widget_queue_clear(widget);
	}
}

/* After this we are unusable, but our data (if any) is still hanging around.
 * It will be freed later with finalize.
 */
static void collection_destroy(GtkObject *object)
{
	Collection *collection;

	g_return_if_fail(object != NULL);
	g_return_if_fail(IS_COLLECTION(object));

	collection = COLLECTION(object);

	if (collection->wink_item != -1)
	{
		collection->wink_item = -1;
		gtk_timeout_remove(collection->wink_timeout);
	}

	gtk_signal_disconnect_by_data(GTK_OBJECT(collection->vadj),
			collection);

	if (GTK_OBJECT_CLASS(parent_class)->destroy)
		(*GTK_OBJECT_CLASS(parent_class)->destroy)(object);
}

/* This is the last thing that happens to us. Free all data. */
static void collection_finalize(GtkObject *object)
{
	Collection *collection;

	collection = COLLECTION(object);

	if (collection->vadj)
	{
		gtk_object_unref(GTK_OBJECT(collection->vadj));
	}

	g_free(collection->items);
}

static void collection_realize(GtkWidget *widget)
{
	Collection 	*collection;
	GdkWindowAttr 	attributes;
	gint 		attributes_mask;
	GdkGCValues	xor_values;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_COLLECTION(widget));
	g_return_if_fail(widget->parent != NULL);

	GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);
	collection = COLLECTION(widget);

	attributes.x = widget->allocation.x;
	attributes.y = widget->allocation.y;
	attributes.width = widget->allocation.width;
	attributes.height = widget->allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.event_mask = gtk_widget_get_events(widget) | 
		GDK_EXPOSURE_MASK |
		GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
		GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
		GDK_BUTTON3_MOTION_MASK;
	attributes.visual = gtk_widget_get_visual(widget);
	attributes.colormap = gtk_widget_get_colormap(widget);

	attributes_mask = GDK_WA_X | GDK_WA_Y |
				GDK_WA_VISUAL | GDK_WA_COLORMAP;
	widget->window = gdk_window_new(widget->parent->window,
			&attributes, attributes_mask);

	widget->style = gtk_style_attach(widget->style, widget->window);

	gdk_window_set_user_data(widget->window, widget);

	gdk_window_set_background(widget->window,
			&widget->style->base[GTK_STATE_INSENSITIVE]);

	/* Try to stop everything flickering horribly */
	gdk_window_set_static_gravities(widget->window, TRUE);

	set_vadjustment(collection);

	xor_values.function = GDK_XOR;
	xor_values.foreground.red = 0xffff;
	xor_values.foreground.green = 0xffff;
	xor_values.foreground.blue = 0xffff;
	gdk_color_alloc(gtk_widget_get_colormap(widget),
			&xor_values.foreground);
	collection->xor_gc = gdk_gc_new_with_values(widget->window,
					&xor_values,
					GDK_GC_FOREGROUND
					| GDK_GC_FUNCTION);
}

static void collection_size_request(GtkWidget *widget,
				GtkRequisition *requisition)
{
	requisition->width = MIN_WIDTH;
	requisition->height = MIN_HEIGHT;
}

static void collection_size_allocate(GtkWidget *widget,
				GtkAllocation *allocation)
{
	Collection 	*collection;
	int		old_columns;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_COLLECTION(widget));
	g_return_if_fail(allocation != NULL);

	collection = COLLECTION(widget);

	old_columns = collection->columns;
	widget->allocation = *allocation;

	collection->columns = allocation->width / collection->item_width;
	if (collection->columns < 1)
		collection->columns = 1;
	
	if (GTK_WIDGET_REALIZED(widget))
	{
		gdk_window_move_resize(widget->window,
				allocation->x, allocation->y,
				allocation->width, allocation->height);

		if (old_columns != collection->columns)
		{
			collection->paint_level = PAINT_CLEAR;
			gtk_widget_queue_clear(widget);
		}

		set_vadjustment(collection);

		if (collection->cursor_item != -1)
			scroll_to_show(collection, collection->cursor_item);
	}
}

static gint collection_paint(Collection 	*collection,
			     GdkRectangle 	*area)
{
	GdkRectangle	whole, item_area;
	GtkWidget	*widget;
	int		row, col;
	int		item;
	int		scroll;
	int		start_row, last_row;
	int		start_col, last_col;
	int		phys_last_col;
	GdkRectangle 	clip;

	scroll = collection->vadj->value;

	widget = GTK_WIDGET(collection);

	if (collection->paint_level > PAINT_NORMAL || area == NULL)
	{
		guint	width, height;
		gdk_window_get_size(widget->window, &width, &height);
		
		whole.x = 0;
		whole.y = 0;
		whole.width = width;
		whole.height = height;
		
		area = &whole;

		if (collection->paint_level == PAINT_CLEAR
				&& !collection->lasso_box)
			gdk_window_clear(widget->window);

		collection->paint_level = PAINT_NORMAL;
	}

	/* Calculate the ranges to plot */
	start_row = (area->y + scroll) / collection->item_height;
	last_row = (area->y + area->height - 1 + scroll)
			/ collection->item_height;
	row = start_row;

	start_col = area->x / collection->item_width;
	phys_last_col = (area->x + area->width - 1) / collection->item_width;

	if (collection->lasso_box)
	{
		/* You can't be too careful with lasso boxes...
		 *
		 * clip gives the total area drawn over (this may be larger
		 * than the requested area). It's used to redraw the lasso
		 * box.
		 */
		clip.x = start_col * collection->item_width;
		clip.y = start_row * collection->item_height - scroll;
		clip.width = (phys_last_col - start_col + 1)
			* collection->item_width;
		clip.height = (last_row - start_row + 1)
			* collection->item_height;

		gdk_window_clear_area(widget->window,
				clip.x, clip.y, clip.width, clip.height);
	}

	if (start_col < collection->columns)
	{
		if (phys_last_col >= collection->columns)
			last_col = collection->columns - 1;
		else
			last_col = phys_last_col;

		col = start_col;

		item = row * collection->columns + col;
		item_area.width = collection->item_width;
		item_area.height = collection->item_height;

		while ((item == 0 || item < collection->number_of_items)
				&& row <= last_row)
		{
			item_area.x = col * collection->item_width;
			item_area.y = row * collection->item_height - scroll;

			draw_one_item(collection, item, &item_area);
			col++;

			if (col > last_col)
			{
				col = start_col;
				row++;
				item = row * collection->columns + col;
			}
			else
				item++;
		}
	}

	if (collection->lasso_box)
	{
		gdk_gc_set_clip_rectangle(collection->xor_gc, &clip);
		draw_lasso_box(collection);
		gdk_gc_set_clip_rectangle(collection->xor_gc, NULL);
	}

	return FALSE;
}

static void default_draw_item(  GtkWidget *widget,
				CollectionItem *item,
				GdkRectangle *area)
{
	gdk_draw_arc(widget->window,
			item->selected ? widget->style->white_gc
				       : widget->style->black_gc,
			TRUE,
			area->x, area->y,
		 	area->width, area->height,
			0, 360 * 64);
}


static gboolean	default_test_point(Collection *collection,
				   int point_x, int point_y,
				   CollectionItem *item,
				   int width, int height)
{
	float	f_x, f_y;

	/* Convert to point in unit circle */
	f_x = ((float) point_x / width) - 0.5;
	f_y = ((float) point_y / height) - 0.5;

	return (f_x * f_x) + (f_y * f_y) <= .25;
}

static void collection_set_arg(	GtkObject *object,
				GtkArg    *arg,
				guint     arg_id)
{
	Collection *collection;

	collection = COLLECTION(object);

	switch (arg_id)
	{
		case ARG_VADJUSTMENT:
			collection_set_adjustment(collection,
					GTK_VALUE_POINTER(*arg));
			break;
		default:
			break;
	}
}

static void collection_set_adjustment(  Collection    *collection,
					GtkAdjustment *vadj)
{
	if (vadj)
		g_return_if_fail (GTK_IS_ADJUSTMENT (vadj));
	else
		vadj = GTK_ADJUSTMENT(gtk_adjustment_new(0.0,
							 0.0, 0.0,
							 0.0, 0.0, 0.0));
	if (collection->vadj && (collection->vadj != vadj))
	{
		gtk_signal_disconnect_by_data(GTK_OBJECT(collection->vadj),
						collection);
		gtk_object_unref(GTK_OBJECT(collection->vadj));
	}

	if (collection->vadj != vadj)
	{
		collection->vadj = vadj;
		gtk_object_ref(GTK_OBJECT(collection->vadj));
		gtk_object_sink(GTK_OBJECT(collection->vadj));

		gtk_signal_connect(GTK_OBJECT(collection->vadj),
				"changed",
				(GtkSignalFunc) collection_adjustment,
				collection);
		gtk_signal_connect(GTK_OBJECT(collection->vadj),
				"value_changed",
				(GtkSignalFunc) collection_adjustment,
				collection);
		gtk_signal_connect(GTK_OBJECT(collection->vadj),
				"disconnect",
				(GtkSignalFunc) collection_disconnect,
				collection);
		collection_adjustment(vadj, collection);
	}
}

static void collection_get_arg(	GtkObject *object,
				GtkArg    *arg,
				guint     arg_id)
{
	Collection *collection;

	collection = COLLECTION(object);

	switch (arg_id)
	{
		case ARG_VADJUSTMENT:
			GTK_VALUE_POINTER(*arg) = collection->vadj;
			break;
		default:
			arg->type = GTK_TYPE_INVALID;
			break;
	}
}

/* Something about the adjustment has changed */
static void collection_adjustment(GtkAdjustment *adjustment,
				  Collection    *collection)
{
	gint diff;
	
	g_return_if_fail(adjustment != NULL);
	g_return_if_fail(GTK_IS_ADJUSTMENT(adjustment));
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	diff = ((gint) adjustment->value) - collection->last_scroll;

	if (diff)
	{
		collection->last_scroll = adjustment->value;

		scroll_by(collection, diff);
	}
}

static void collection_disconnect(GtkAdjustment *adjustment,
				  Collection    *collection)
{
	g_return_if_fail(adjustment != NULL);
	g_return_if_fail(GTK_IS_ADJUSTMENT(adjustment));
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	collection_set_adjustment(collection, NULL);
}

static void set_vadjustment(Collection *collection)
{	
	GtkWidget	*widget;
	guint		height;
	int		cols, rows;

	widget = GTK_WIDGET(collection);

	if (!GTK_WIDGET_REALIZED(widget))
		return;

	gdk_window_get_size(widget->window, NULL, &height);
	cols = collection->columns;
	rows = (collection->number_of_items + cols - 1) / cols;

	collection->vadj->lower = 0.0;
	collection->vadj->upper = collection->item_height * rows;

	collection->vadj->step_increment =
		MIN(collection->vadj->upper, collection->item_height / 4);
	
	collection->vadj->page_increment =
		MIN(collection->vadj->upper,
				height - 5.0);

	collection->vadj->page_size = MIN(collection->vadj->upper, height);

	collection->vadj->value = MIN(collection->vadj->value,
			collection->vadj->upper - collection->vadj->page_size);
	
	collection->vadj->value = MAX(collection->vadj->value, 0.0);

	gtk_signal_emit_by_name(GTK_OBJECT(collection->vadj), "changed");
}

static void collection_draw(GtkWidget *widget, GdkRectangle *area)
{
	Collection    *collection;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_COLLECTION(widget));
	g_return_if_fail(area != NULL);		/* Not actually used */

	collection = COLLECTION(widget);

	if (collection->paint_level > PAINT_NORMAL)
		collection_paint(collection, area);
}

static gint collection_expose(GtkWidget *widget, GdkEventExpose *event)
{
	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(IS_COLLECTION(widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	collection_paint(COLLECTION(widget), &event->area);

	return FALSE;
}

/* Positive makes the contents go move up the screen */
static void scroll_by(Collection *collection, gint diff)
{
	GtkWidget	*widget;
	guint		width, height;
	guint		from_y, to_y;
	guint		amount;
	GdkRectangle	new_area;

	if (diff == 0)
		return;

	widget = GTK_WIDGET(collection);
	
	if (collection->lasso_box)
		remove_lasso_box(collection);

	gdk_window_get_size(widget->window, &width, &height);
	new_area.x = 0;
	new_area.width = width;

	if (diff > 0)
	{
		amount = diff;
		from_y = amount;
		to_y = 0;
		new_area.y = height - amount;
	}
	else
	{
		amount = -diff;
		from_y = 0;
		to_y = amount;
		new_area.y = 0;
	}

	new_area.height = amount;
	
	if (amount < height)
	{
		gdk_draw_pixmap(widget->window,
				widget->style->white_gc,
				widget->window,
				0,
				from_y,
				0,
				to_y,
				width,
				height - amount);
		/* We have to redraw everything because any pending
		 * expose events now contain invalid areas.
		 * Don't need to clear the area first though...
		 */
		if (collection->paint_level < PAINT_OVERWRITE)
			collection->paint_level = PAINT_OVERWRITE;
	}
	else
		collection->paint_level = PAINT_CLEAR;

	gdk_window_clear_area(widget->window,
			0, new_area.y,
			width, new_area.height);
	collection_paint(collection, NULL);
}

static void resize_arrays(Collection *collection, guint new_size)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(new_size >= collection->number_of_items);

	collection->items = g_realloc(collection->items,
					sizeof(CollectionItem) * new_size);
	collection->array_size = new_size;
}

static void return_pressed(Collection *collection)
{
	int			item = collection->cursor_item;
	CollectionTargetFunc 	cb = collection->target_cb;
	gpointer		data = collection->target_data;

	collection_target(collection, NULL, NULL);
	if (item < 0 || item >= collection->number_of_items)
		return;

	if (cb)
	{
		cb(collection, item, data);
		return;
	}

	gtk_signal_emit(GTK_OBJECT(collection), 
			collection_signals[OPEN_ITEM],
			collection->items[item].data,
			item);
}

static gint collection_key_press(GtkWidget *widget, GdkEventKey *event)
{
	Collection *collection;
	int	   item;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(IS_COLLECTION(widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	collection = (Collection *) widget;
	item = collection->cursor_item;
	
	switch (event->keyval)
	{
		case GDK_Left:
			collection_move_cursor(collection, 0, -1);
			break;
		case GDK_Right:
			collection_move_cursor(collection, 0, 1);
			break;
		case GDK_Up:
			collection_move_cursor(collection, -1, 0);
			break;
		case GDK_Down:
			collection_move_cursor(collection, 1, 0);
			break;
		case GDK_Home:
			collection_set_cursor_item(collection, 0);
			break;
		case GDK_End:
			collection_set_cursor_item(collection,
				MAX((gint) collection->number_of_items - 1, 0));
			break;
		case GDK_Page_Up:
			collection_move_cursor(collection, -10, 0);
			break;
		case GDK_Page_Down:
			collection_move_cursor(collection, 10, 0);
			break;
		case GDK_Return:
			return_pressed(collection);
			break;
		case GDK_Escape:
			collection_target(collection, NULL, NULL);
			break;
		case ' ':
			if (item >=0 && item < collection->number_of_items)
				collection_toggle_item(collection, item);
			break;
		default:
			return FALSE;
	}

	return TRUE;
}

static gint collection_button_press(GtkWidget      *widget,
				    GdkEventButton *event)
{
	Collection    	*collection;
	int		row, col;
	int		item;
	int		action;
	int		scroll;
	guint		stacked_time;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(IS_COLLECTION(widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	collection = COLLECTION(widget);

	collection->item_clicked = -1;

	if (event->button > 3)
	{
		int	diff;
		
		/* Wheel mouse scrolling */
		if (event->button == 4)
			diff = -((signed int) collection->item_height) / 4;
		else if (event->button == 5)
			diff = collection->item_height / 4;
		else
			diff = 0;

		if (diff)
		{
			int	old_value = collection->vadj->value;
			int	new_value = 0;
			gboolean box = collection->lasso_box;

			new_value = CLAMP(old_value + diff, 0.0, 
					 collection->vadj->upper
						- collection->vadj->page_size);
			diff = new_value - old_value;
			if (diff)
			{
				if (box)
				{
					remove_lasso_box(collection);
					collection->drag_box_y[0] -= diff;
				}
				collection->vadj->value = new_value;
				gtk_signal_emit_by_name(
						GTK_OBJECT(collection->vadj),
						"changed");
				if (box)
					add_lasso_box(collection);
			}
		}
		return FALSE;
	}

	if (collection->cursor_item != -1)
		collection_set_cursor_item(collection, -1);

	scroll = collection->vadj->value;

	if (event->type == GDK_BUTTON_PRESS &&
			event->button != collection_menu_button)
	{
		if (collection->buttons_pressed++ == 0)
			gtk_grab_add(widget);
		else
			return FALSE;	/* Ignore extra presses */
	}

	if (event->state & GDK_CONTROL_MASK && !collection_single_click)
		action = 2;
	else
		action = event->button;

	/* Ignore all clicks while we are dragging a lasso box */
	if (collection->lasso_box)
		return TRUE;

	col = event->x / collection->item_width;
	row = (event->y + scroll) / collection->item_height;

	if (col < 0 || row < 0 || col >= collection->columns)
		item = -1;
	else
	{
		item = col + row * collection->columns;
		if (item >= collection->number_of_items
				|| 
			!collection->test_point(collection,
				event->x - col * collection->item_width,
				event->y - row * collection->item_height
					+ scroll,
				&collection->items[item],
				collection->item_width,
				collection->item_height))
		{
			item = -1;
		}
	}

	if (collection->target_cb)
	{
		CollectionTargetFunc cb = collection->target_cb;
		gpointer	data = collection->target_data;
		
		collection_target(collection, NULL, NULL);
		if (collection->buttons_pressed)
		{
			gtk_grab_remove(widget);
			collection->buttons_pressed = 0;
		}
		if (item > -1 && event->button != collection_menu_button)
			cb(collection, item, data);
		return TRUE;
	}
	
	collection->drag_box_x[0] = event->x;
	collection->drag_box_y[0] = event->y;
	collection->item_clicked = item;
	
	stacked_time = current_event_time;
	current_event_time = event->time;
	
	if (event->button == collection_menu_button)
	{
		gtk_signal_emit(GTK_OBJECT(collection),
				collection_signals[SHOW_MENU],
				event,
				item);
	}
	else if (event->type == GDK_2BUTTON_PRESS && collection->panel)
	{
		/* Do nothing */
	}
	else if ((event->type == GDK_2BUTTON_PRESS && !collection_single_click)
		|| collection->panel)
	{
		if (item >= 0)
		{
			if (collection->buttons_pressed)
			{
				gtk_grab_remove(widget);
				collection->buttons_pressed = 0;
			}
			collection_unselect_item(collection, item);
			gtk_signal_emit(GTK_OBJECT(collection), 
					collection_signals[OPEN_ITEM],
					collection->items[item].data,
					item);
		}
	}
	else if (event->type == GDK_BUTTON_PRESS)
	{
		collection->may_drag = event->button < collection_menu_button;

		if (item >= 0)
		{
			if (action == 1)
			{
				if (!collection->items[item].selected)
				{
					collection_select_item(collection,
							item);
					collection_clear_except(collection,
							item);
				}
			}
			else
				collection_toggle_item(collection, item);
		}
		else if (action == 1)
			collection_clear_selection(collection);
	}

	current_event_time = stacked_time;
	return FALSE;
}

static gint collection_button_release(GtkWidget      *widget,
				      GdkEventButton *event)
{
	Collection    	*collection;
	int		top, bottom;
	int		row, last_row;
	int		w, h;
	int		col, start_col, last_col;
	int		scroll;
	int		item;
	guint		stacked_time;
	int		button;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(IS_COLLECTION(widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	collection = COLLECTION(widget);
	button = event->button;
	
	scroll = collection->vadj->value;

	if (event->button > 3 || event->button == collection_menu_button)
		return FALSE;
	if (collection->buttons_pressed == 0)
		return FALSE;
	if (--collection->buttons_pressed == 0)
		gtk_grab_remove(widget);
	else
		return FALSE;		/* Wait until ALL buttons are up */
	
	if (!collection->lasso_box)
	{
		int	item = collection->item_clicked;
			
		if (collection_single_click && item > -1
			&& item < collection->number_of_items
			&& (event->state & GDK_CONTROL_MASK) == 0)
		{
			int	dx = event->x - collection->drag_box_x[0];
			int	dy = event->y - collection->drag_box_y[0];

			if (ABS(dx) + ABS(dy) > 9)
				return FALSE;

			collection_unselect_item(collection, item);
			gtk_signal_emit(GTK_OBJECT(collection), 
					collection_signals[OPEN_ITEM],
					collection->items[item].data,
					item);
		}

		return FALSE;
	}
			
	remove_lasso_box(collection);

	w = collection->item_width;
	h = collection->item_height;

	top = collection->drag_box_y[0] + scroll;
	bottom = collection->drag_box_y[1] + scroll;
	if (top > bottom)
	{
		int	tmp;
		tmp = top;
		top = bottom;
		bottom = tmp;
	}
	top += h / 4;
	bottom -= h / 4;
	
	row = MAX(top / h, 0);
	last_row = bottom / h;

	top = collection->drag_box_x[0];	/* (left) */
	bottom = collection->drag_box_x[1];
	if (top > bottom)
	{
		int	tmp;
		tmp = top;
		top = bottom;
		bottom = tmp;
	}
	top += w / 4;
	bottom -= w / 4;
	start_col = MAX(top / w, 0);
	last_col = bottom / w;
	if (last_col >= collection->columns)
		last_col = collection->columns - 1;

	stacked_time = current_event_time;
	current_event_time = event->time;

	while (row <= last_row)
	{
		col = start_col;
		item = row * collection->columns + col;
		while (col <= last_col)
		{
			if (item >= collection->number_of_items)
			{
				current_event_time = stacked_time;
				return FALSE;
			}

			if (button == 1)
				collection_select_item(collection, item);
			else
				collection_toggle_item(collection, item);
			col++;
			item++;
		}
		row++;
	}

	current_event_time = stacked_time;

	return FALSE;
}

static gint collection_motion_notify(GtkWidget *widget,
				     GdkEventMotion *event)
{
	Collection    	*collection;
	int		x, y;
	guint		stacked_time;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(IS_COLLECTION(widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	collection = COLLECTION(widget);

	if (collection->buttons_pressed == 0)
		return FALSE;

	stacked_time = current_event_time;
	current_event_time = event->time;

	if (event->window != widget->window)
		gdk_window_get_pointer(widget->window, &x, &y, NULL);
	else
	{
		x = event->x;
		y = event->y;
	}

	if (collection->lasso_box)
	{
		int	new_value = 0, diff;
		int	height;

		gdk_window_get_size(widget->window, NULL, &height);
		
		if (y < 0)
		{
			int	old_value = collection->vadj->value;

			new_value = MAX(old_value + y / 10, 0.0);
			diff = new_value - old_value;
		}
		else if (y > height)
		{
			int	old_value = collection->vadj->value;

			new_value = MIN(old_value + (y - height) / 10,
					collection->vadj->upper
						- collection->vadj->page_size);
			diff = new_value - old_value;
		}
		else
			diff = 0;
			
		remove_lasso_box(collection);
		collection->drag_box_x[1] = x;
		collection->drag_box_y[1] = y;

		if (diff)
		{
			collection->drag_box_y[0] -= diff;
			collection->vadj->value = new_value;
			gtk_signal_emit_by_name(GTK_OBJECT(collection->vadj),
					"changed");
		}
		add_lasso_box(collection);
	}
	else if (collection->may_drag)
	{
		int	dx = x - collection->drag_box_x[0];
		int	dy = y - collection->drag_box_y[0];

		if (abs(dx) > 9 || abs(dy) > 9)
		{
			int	row, col, item;
			int	scroll = collection->vadj->value;

			collection->may_drag = FALSE;

			col = collection->drag_box_x[0]
					/ collection->item_width;
			row = (collection->drag_box_y[0] + scroll)
					/ collection->item_height;
			item = item_at_row_col(collection, row, col);
			
			if (item != -1 && collection->test_point(collection,
				collection->drag_box_x[0] -
					col * collection->item_width,
				collection->drag_box_y[0]
					- row * collection->item_height
					+ scroll,
				&collection->items[item],
				collection->item_width,
				collection->item_height))
			{
				collection->buttons_pressed = 0;
				gtk_grab_remove(widget);
				collection_select_item(collection, item);
				gtk_signal_emit(GTK_OBJECT(collection), 
					collection_signals[DRAG_SELECTION],
					event,
					collection->number_selected);
			}
			else
			{
				collection->drag_box_x[1] = x;
				collection->drag_box_y[1] = y;
				add_lasso_box(collection);
			}
		}
	}

	current_event_time = stacked_time;
	return FALSE;
}

static void add_lasso_box(Collection *collection)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(collection->lasso_box == FALSE);

	collection->lasso_box = TRUE;
	draw_lasso_box(collection);
}

static void draw_lasso_box(Collection *collection)
{
	GtkWidget	*widget;
	int		x, y, width, height;
	
	widget = GTK_WIDGET(collection);

	x = MIN(collection->drag_box_x[0], collection->drag_box_x[1]);
	y = MIN(collection->drag_box_y[0], collection->drag_box_y[1]);
	width = abs(collection->drag_box_x[1] - collection->drag_box_x[0]);
	height = abs(collection->drag_box_y[1] - collection->drag_box_y[0]);

	gdk_draw_rectangle(widget->window, collection->xor_gc, FALSE,
			x, y, width, height);
}

static void remove_lasso_box(Collection *collection)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(collection->lasso_box == TRUE);

	draw_lasso_box(collection);

	collection->lasso_box = FALSE;

	return;
}

/* Convert a row,col address to an item number, or -1 if none */
static int item_at_row_col(Collection *collection, int row, int col)
{
	int	item;
	
	if (row < 0 || col < 0 || col >= collection->columns)
		return -1;

	item = col + row * collection->columns;

	if (item >= collection->number_of_items)
		return -1;
	return item;
}

/* Make sure that 'item' is fully visible (vertically), scrolling if not. */
static void scroll_to_show(Collection *collection, int item)
{
	int	first, last, row;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	row = item / collection->columns;
	get_visible_limits(collection, &first, &last);

	if (row <= first)
	{
		gtk_adjustment_set_value(collection->vadj,
				row * collection->item_height);
	}
	else if (row >= last)
	{
		GtkWidget	*widget = (GtkWidget *) collection;
		int 		height;

		if (GTK_WIDGET_REALIZED(widget))
		{
			gdk_window_get_size(widget->window, NULL, &height);
			gtk_adjustment_set_value(collection->vadj,
				(row + 1) * collection->item_height - height);
		}
	}
}

/* Return the first and last rows which are [partly] visible. Does not
 * ensure that the rows actually exist (contain items).
 */
static void get_visible_limits(Collection *collection, int *first, int *last)
{
	GtkWidget	*widget = (GtkWidget *) collection;
	int	scroll, height;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(first != NULL && last != NULL);

	if (!GTK_WIDGET_REALIZED(widget))
	{
		*first = 0;
		*last = 0;
	}
	else
	{
		scroll = collection->vadj->value;
		gdk_window_get_size(widget->window, NULL, &height);

		*first = MAX(scroll / collection->item_height, 0);
		*last = (scroll + height - 1) /collection->item_height;

		if (*last < *first)
			*last = *first;
	}
}

/* Unselect all items except number item (-1 to unselect everything) */
static void collection_clear_except(Collection *collection, gint exception)
{
	GtkWidget	*widget;
	GdkRectangle	area;
	int		item = 0;
	int		scroll;
	int		end;		/* Selected items to end up with */
	
	widget = GTK_WIDGET(collection);
	scroll = collection->vadj->value;

	end = exception >= 0 && exception < collection->number_of_items
		? collection->items[exception].selected != 0 : 0;

	area.width = collection->item_width;
	area.height = collection->item_height;
	
	if (collection->number_selected == 0)
		return;

	while (collection->number_selected > end)
	{
		while (item == exception || !collection->items[item].selected)
			item++;

		area.x = (item % collection->columns) * area.width;
		area.y = (item / collection->columns) * area.height
				- scroll;

		collection->items[item++].selected = FALSE;
		gdk_window_clear_area(widget->window,
				area.x, area.y, area.width, area.height);
		collection_paint(collection, &area);
		
		collection->number_selected--;
	}

	if (end == 0)
		gtk_signal_emit(GTK_OBJECT(collection),
				collection_signals[LOSE_SELECTION],
				current_event_time);
}

/* Cancel the current wink effect. */
static void cancel_wink(Collection *collection)
{
	gint	item;
	
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(collection->wink_item != -1);

	item = collection->wink_item;

	collection->wink_item = -1;
	gtk_timeout_remove(collection->wink_timeout);

	collection_draw_item(collection, item, TRUE);
}

static gboolean cancel_wink_timeout(Collection *collection)
{
	gint	item;
	
	g_return_val_if_fail(collection != NULL, FALSE);
	g_return_val_if_fail(IS_COLLECTION(collection), FALSE);
	g_return_val_if_fail(collection->wink_item != -1, FALSE);

	item = collection->wink_item;

	collection->wink_item = -1;

	collection_draw_item(collection, item, TRUE);

	return FALSE;
}

/* Functions for managing collections */

/* Remove all objects from the collection */
void collection_clear(Collection *collection)
{
	int	prev_selected;

	g_return_if_fail(IS_COLLECTION(collection));

	if (collection->number_of_items == 0)
		return;

	if (collection->wink_item != -1)
	{
		collection->wink_item = -1;
		gtk_timeout_remove(collection->wink_timeout);
	}

	collection_set_cursor_item(collection,
			collection->cursor_item == -1 ? -1: 0);
	prev_selected = collection->number_selected;
	collection->number_of_items = collection->number_selected = 0;

	resize_arrays(collection, MINIMUM_ITEMS);

	collection->paint_level = PAINT_CLEAR;

	gtk_widget_queue_clear(GTK_WIDGET(collection));

	if (prev_selected && collection->number_selected == 0)
		gtk_signal_emit(GTK_OBJECT(collection),
				collection_signals[LOSE_SELECTION],
				current_event_time);
}

/* Inserts a new item at the end. The new item is unselected, and its
 * number is returned.
 */
gint collection_insert(Collection *collection, gpointer data)
{
	int	item;
	
	g_return_val_if_fail(IS_COLLECTION(collection), -1);

	item = collection->number_of_items;

	if (item >= collection->array_size)
		resize_arrays(collection, item + (item >> 1));

	collection->items[item].data = data;
	collection->items[item].selected = FALSE;

	collection->number_of_items++;

	if (GTK_WIDGET_REALIZED(GTK_WIDGET(collection)))
	{
		set_vadjustment(collection);
		collection_draw_item(collection,
				collection->number_of_items - 1,
				FALSE);
	}

	return item;
}

/* Unselect an item by number */
void collection_unselect_item(Collection *collection, gint item)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(item >= 0 && item < collection->number_of_items);

	if (collection->items[item].selected)
	{
		collection->items[item].selected = FALSE;
		collection_draw_item(collection, item, TRUE);

		if (--collection->number_selected == 0)
			gtk_signal_emit(GTK_OBJECT(collection),
					collection_signals[LOSE_SELECTION],
					current_event_time);
	}
}

/* Select an item by number */
void collection_select_item(Collection *collection, gint item)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(item >= 0 && item < collection->number_of_items);

	if (collection->items[item].selected)
		return;		/* Already selected */
	
	collection->items[item].selected = TRUE;
	collection_draw_item(collection, item, TRUE);

	if (collection->number_selected++ == 0)
		gtk_signal_emit(GTK_OBJECT(collection),
				collection_signals[GAIN_SELECTION],
				current_event_time);
}

/* Toggle the selected state of an item (by number) */
void collection_toggle_item(Collection *collection, gint item)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(item >= 0 && item < collection->number_of_items);

	if (collection->items[item].selected)
	{
		collection->items[item].selected = FALSE;
		if (--collection->number_selected == 0)
			gtk_signal_emit(GTK_OBJECT(collection),
					collection_signals[LOSE_SELECTION],
					current_event_time);
	}
	else
	{
		collection->items[item].selected = TRUE;
		if (collection->number_selected++ == 0)
			gtk_signal_emit(GTK_OBJECT(collection),
					collection_signals[GAIN_SELECTION],
					current_event_time);
	}
	collection_draw_item(collection, item, TRUE);
}

/* Select all items in the collection */
void collection_select_all(Collection *collection)
{
	GtkWidget	*widget;
	GdkRectangle	area;
	int		item = 0;
	int		scroll;
	
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	widget = GTK_WIDGET(collection);
	scroll = collection->vadj->value;

	area.width = collection->item_width;
	area.height = collection->item_height;

	if (collection->number_selected == collection->number_of_items)
		return;		/* Nothing to do */

	while (collection->number_selected < collection->number_of_items)
	{
		while (collection->items[item].selected)
			item++;

		area.x = (item % collection->columns) * area.width;
		area.y = (item / collection->columns) * area.height
				- scroll;

		collection->items[item++].selected = TRUE;
		gdk_window_clear_area(widget->window,
				area.x, area.y, area.width, area.height);
		collection_paint(collection, &area);
		
		collection->number_selected++;
	}

	gtk_signal_emit(GTK_OBJECT(collection),
			collection_signals[GAIN_SELECTION],
			current_event_time);
}

/* Unselect all items in the collection */
void collection_clear_selection(Collection *collection)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	collection_clear_except(collection, -1);
}

/* Force a redraw of the specified item, if it is visible */
void collection_draw_item(Collection *collection, gint item, gboolean blank)
{
	int		height;
	GdkRectangle	area;
	GtkWidget	*widget;
	int		row, col;
	int		scroll;
	int		area_y, area_height;	/* NOT shorts! */

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(item >= 0 &&
			(item == 0 || item < collection->number_of_items));

	widget = GTK_WIDGET(collection);
	if (!GTK_WIDGET_REALIZED(widget))
		return;

	col = item % collection->columns;
	row = item / collection->columns;
	scroll = collection->vadj->value;	/* (round to int) */

	area.x = col * collection->item_width;
	area_y = row * collection->item_height - scroll;
	area.width = collection->item_width;
	area_height = collection->item_height;

	if (area_y + area_height < 0)
		return;

	gdk_window_get_size(widget->window, NULL, &height);

	if (area_y > height)
		return;

	area.y = area_y;
	area.height = area_height;
			
	if (blank || collection->lasso_box)
		gdk_window_clear_area(widget->window,
				area.x, area.y, area.width, area.height);

	draw_one_item(collection, item, &area);

	if (collection->lasso_box)
	{
		gdk_gc_set_clip_rectangle(collection->xor_gc, &area);
		draw_lasso_box(collection);
		gdk_gc_set_clip_rectangle(collection->xor_gc, NULL);
	}
}

void collection_set_item_size(Collection *collection, int width, int height)
{
	GtkWidget	*widget;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(width > 4 && height > 4);

	widget = GTK_WIDGET(collection);

	collection->item_width = width;
	collection->item_height = height;

	if (GTK_WIDGET_REALIZED(widget))
	{
		int		window_width;

		collection->paint_level = PAINT_CLEAR;
		gdk_window_get_size(widget->window, &window_width, NULL);
		collection->columns = MAX(window_width / collection->item_width,
					  1);

		set_vadjustment(collection);
		if (collection->cursor_item != -1)
			scroll_to_show(collection, collection->cursor_item);
		gtk_widget_queue_draw(widget);
	}
}

/* Cursor is positioned on item with the same data as before the sort.
 * Same for the wink item.
 */
void collection_qsort(Collection *collection,
			int (*compar)(const void *, const void *))
{
	int	cursor, wink, items;
	gpointer cursor_data = NULL;
	gpointer wink_data = NULL;
	
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(compar != NULL);

	items = collection->number_of_items;

	wink = collection->wink_item;
	if (wink >= 0 && wink < items)
		wink_data = collection->items[wink].data;
	else
		wink = -1;

	cursor = collection->cursor_item;
	if (cursor >= 0 && cursor < items)
		cursor_data = collection->items[cursor].data;
	else
		cursor = -1;

	if (collection->wink_item != -1)
	{
		collection->wink_item = -1;
		gtk_timeout_remove(collection->wink_timeout);
	}
	
	qsort(collection->items, items, sizeof(collection->items[0]), compar); 

	if (cursor > -1 || wink > -1)
	{
		int	item;

		for (item = 0; item < items; item++)
		{
			if (collection->items[item].data == cursor_data)
				collection_set_cursor_item(collection, item);
			if (collection->items[item].data == wink_data)
				collection_wink_item(collection, item);
		}
	}
	
	collection->paint_level = PAINT_CLEAR;

	gtk_widget_queue_draw(GTK_WIDGET(collection));
}

/* Find an item in an unsorted collection.
 * Returns the item number, or -1 if not found.
 */
int collection_find_item(Collection *collection, gpointer data,
		         int (*compar)(const void *, const void *))
{
	int	i;

	g_return_val_if_fail(collection != NULL, -1);
	g_return_val_if_fail(IS_COLLECTION(collection), -1);
	g_return_val_if_fail(compar != NULL, -1);

	for (i = 0; i < collection->number_of_items; i++)
		if (compar(&collection->items[i].data, &data) == 0)
			return i;

	return -1;
}

/* The collection may be in either normal mode or panel mode.
 * In panel mode:
 * - a single click calls open_item
 * - items are never selected
 * - lasso boxes are disabled
 */
void collection_set_panel(Collection *collection, gboolean panel)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	
	collection->panel = panel == TRUE;

	if (collection->panel)
	{
		collection_clear_selection(collection);
		if (collection->lasso_box)
			remove_lasso_box(collection);
	}
}

/* Return the number of the item under the point (x,y), or -1 for none.
 * This may call your test_point callback. The point is relative to the
 * collection's origin.
 */
int collection_get_item(Collection *collection, int x, int y)
{
	int		scroll;
	int		row, col;
	int		item;

	g_return_val_if_fail(collection != NULL, -1);

	scroll = collection->vadj->value;
	col = x / collection->item_width;
	row = (y + scroll) / collection->item_height;

	if (col < 0 || row < 0 || col >= collection->columns)
		return -1;

	item = col + row * collection->columns;
	if (item >= collection->number_of_items
			|| 
		!collection->test_point(collection,
			x - col * collection->item_width,
			y - row * collection->item_height
				+ scroll,
			&collection->items[item],
			collection->item_width,
			collection->item_height))
	{
		return -1;
	}

	return item;
}

/* Set the cursor/highlight over the given item. Passing -1
 * hides the cursor. As a special case, you may set the cursor item
 * to zero when there are no items.
 */
void collection_set_cursor_item(Collection *collection, gint item)
{
	int	old_item;
	
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(item >= -1 &&
		(item < (int) collection->number_of_items || item == 0));

	old_item = collection->cursor_item;

	if (old_item == item)
		return;
	
	collection->cursor_item = item;
	
	if (old_item != -1)
		collection_draw_item(collection, old_item, TRUE);

	if (item != -1)
	{
		collection_draw_item(collection, item, TRUE);
		scroll_to_show(collection, item);
	}
}

/* Briefly highlight an item to draw the user's attention to it.
 * -1 cancels the effect, as does deleting items, sorting the collection
 * or starting a new wink effect.
 * Otherwise, the effect will cancel itself after a short pause.
 * */
void collection_wink_item(Collection *collection, gint item)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(item >= -1 && item < collection->number_of_items);

	if (collection->wink_item != -1)
		cancel_wink(collection);
	if (item == -1)
		return;

	collection->wink_item = item;
	collection->wink_timeout = gtk_timeout_add(300,
					   (GtkFunction) cancel_wink_timeout,
					   collection);
	collection_draw_item(collection, item, TRUE);
	scroll_to_show(collection, item);

	gdk_flush();
}

/* Call test(item, data) on each item in the collection.
 * Remove all items for which it returns TRUE. test() should
 * free the data before returning TRUE. The collection is in an
 * inconsistant state during this call (ie, when test() is called).
 */
void collection_delete_if(Collection *collection,
			  gboolean (*test)(gpointer item, gpointer data),
			  gpointer data)
{
	int	in, out = 0;
	int	selected = 0;
	int	cursor;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));
	g_return_if_fail(test != NULL);

	cursor = collection->cursor_item;

	for (in = 0; in < collection->number_of_items; in++)
	{
		if (!test(collection->items[in].data, data))
		{
			if (collection->items[in].selected)
			{
				collection->items[out].selected = TRUE;
				selected++;
			}
			else
				collection->items[out].selected = FALSE;

			collection->items[out++].data =
				collection->items[in].data;
		}
		else if (cursor >= in)
			cursor--;
	}

	if (in != out)
	{
		collection->cursor_item = cursor;

		if (collection->wink_item != -1)
		{
			collection->wink_item = -1;
			gtk_timeout_remove(collection->wink_timeout);
		}
		
		collection->number_of_items = out;
		collection->number_selected = selected;
		resize_arrays(collection,
			MAX(collection->number_of_items, MINIMUM_ITEMS));

		collection->paint_level = PAINT_CLEAR;

		if (GTK_WIDGET_REALIZED(GTK_WIDGET(collection)))
		{
			set_vadjustment(collection);
			gtk_widget_queue_draw(GTK_WIDGET(collection));
		}
	}
}

/* Display a cross-hair pointer and the next time an item is clicked,
 * call the callback function. If the background is clicked or NULL
 * is passed as the callback then revert to normal operation.
 */
void collection_target(Collection *collection,
			CollectionTargetFunc callback,
			gpointer user_data)
{
	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	if (callback != collection->target_cb)
		gdk_window_set_cursor(GTK_WIDGET(collection)->window,
				callback ? crosshair : NULL);

	collection->target_cb = callback;
	collection->target_data = user_data;

	if (collection->cursor_item != -1)
		collection_draw_item(collection, collection->cursor_item,
				FALSE);
}

/* Move the cursor by the given row and column offsets. */
void collection_move_cursor(Collection *collection, int drow, int dcol)
{
	int	row, col, item;
	int	first, last, total_rows;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(IS_COLLECTION(collection));

	get_visible_limits(collection, &first, &last);

	item = collection->cursor_item;
	if (item == -1)
	{
		col = 0;
		row = first;
	}
	else
	{
		row = item / collection->columns;
		col = item % collection->columns;

		if (row < first)
			row = first;
		else if (row > last)
			row = last;
		else
			row = MAX(row + drow, 0);

		col = MAX(col + dcol, 0);
	}

	if (col >= collection->columns)
		col = collection->columns - 1;

	total_rows = (collection->number_of_items + collection->columns - 1)
				/ collection->columns;

	if (row >= total_rows - 1)
	{
		row = total_rows - 1;
		item = col + row * collection->columns;
		if (item >= collection->number_of_items)
			row--;
	}
	if (row < 0)
		row = 0;

	item = col + row * collection->columns;

	if (item >= 0 && item < collection->number_of_items)
		collection_set_cursor_item(collection, item);
}

