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

/* pinboard.c - icons on the background */

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gtk/gtkinvisible.h>

#include "main.h"
#include "dnd.h"
#include "pinboard.h"
#include "type.h"
#include "choices.h"
#include "support.h"
#include "gui_support.h"
#include "run.h"
#include "menu.h"

/* The number of pixels between the bottom of the image and the top
 * of the text.
 */
#define GAP 4

/* The size of the border around the icon which is used when winking */
#define WINK_FRAME 2

enum
{
	TARGET_STRING,
	TARGET_URI_LIST,
};

struct _PinIcon {
	GtkWidget	*win, *paper;
	GdkBitmap	*mask;
	guchar		*path;
	DirItem		item;
	int		x, y;
	gboolean	selected;
};

struct _Pinboard {
	guchar		*name;		/* Leaf name */
	GList		*icons;
};

extern int collection_menu_button;

static Pinboard	*current_pinboard = NULL;
static gint	loading_pinboard = 0;	/* Non-zero => loading */

static PinIcon	*current_wink_icon = NULL;
static gint	wink_timeout;

static gint	number_selected = 0;

static GdkColor		mask_solid = {1, 1, 1, 1};
static GdkColor		mask_transp = {0, 0, 0, 0};
static  GdkGC	*mask_gc = NULL;

/* Proxy window for DnD and clicks on the desktop */
static GtkWidget *proxy_invisible;

/* The window (owned by the wm) which root clicks are forwarded to.
 * NULL if wm does not support forwarding clicks.
 */
static GdkWindow *click_proxy_gdk_window = NULL;
static GdkAtom   win_button_proxy; /* _WIN_DESKTOP_BUTTON_PROXY */

static gint	drag_start_x, drag_start_y;
/* If the drag type is not DRAG_NONE then there is a grab in progress */
typedef enum {
	DRAG_NONE,
	DRAG_MOVE_ICON,	/* When you hold down Adjust on an icon */
	DRAG_MAY_COPY,	/* When you hold down Select on an icon, but */
			/* before the drag has actually started. */
} PinDragType;
static PinDragType pin_drag_type = DRAG_NONE;


/* Static prototypes */
static void set_size_and_shape(PinIcon *icon, int *rwidth, int *rheight);
static gint draw_icon(GtkWidget *widget, GdkEventExpose *event, PinIcon *icon);
static void mask_wink_border(PinIcon *icon, GdkColor *alpha);
static gint end_wink(gpointer data);
static gboolean button_release_event(GtkWidget *widget,
			    	     GdkEventButton *event,
                            	     PinIcon *icon);
static gboolean root_property_event(GtkWidget *widget,
			    	    GdkEventProperty *event,
                            	    gpointer data);
static gboolean root_button_press(GtkWidget *widget,
			    	  GdkEventButton *event,
                            	  gpointer data);
static gboolean enter_notify(GtkWidget *widget,
			     GdkEventCrossing *event,
			     PinIcon *icon);
static gboolean button_press_event(GtkWidget *widget,
			    GdkEventButton *event,
                            PinIcon *icon);
static gint icon_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       PinIcon *icon);
static char *pin_from_file(guchar *line);
static gboolean add_root_handlers(void);
static void pinboard_save(void);
static GdkFilterReturn proxy_filter(GdkXEvent *xevent,
				    GdkEvent *event,
				    gpointer data);
static void icon_destroyed(GtkWidget *widget, PinIcon *icon);
static void snap_to_grid(int *x, int *y);
static void offset_from_centre(PinIcon *icon,
			       int width, int height,
			       int *x, int *y);
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    PinIcon		*icon);
static void drag_set_pinicon_dest(PinIcon *icon);
static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       PinIcon		*icon);
static void icon_may_update(PinIcon *icon);
static void forward_root_clicks(void);
static void change_number_selected(int delta);
static gint lose_selection(GtkWidget *widget, GdkEventSelection *event);
static void selection_get(GtkWidget *widget, 
		       GtkSelectionData *selection_data,
		       guint      info,
		       guint      time,
		       gpointer   data);



/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Load 'pb_<pinboard>' config file from Choices (if it exists)
 * and make it the current pinboard.
 * Any existing pinned items are removed. You must call this
 * at least once before using the pinboard. NULL disables the
 * pinboard.
 */
void pinboard_activate(guchar *name)
{
	Pinboard	*old_board = current_pinboard;
	guchar		*path, *slash;

	/* Treat an empty name the same as NULL */
	if (!*name)
		name = NULL;

	if (old_board)
	{
		pinboard_clear();
		number_of_windows--;
	}

	if (!name)
	{
		if (number_of_windows < 1)
			gtk_main_quit();
		return;
	}

	if (!add_root_handlers())
	{
		delayed_error(PROJECT, _("Another application is already "
					"managing the pinboard!"));
		return;
	}

	number_of_windows++;
	
	slash = strchr(name, '/');
	if (slash)
		path = g_strdup(name);
	else
	{
		guchar	*leaf;

		leaf = g_strconcat("pb_", name, NULL);
		path = choices_find_path_load(leaf, "ROX-Filer");
		g_free(leaf);
	}

	current_pinboard = g_new(Pinboard, 1);
	current_pinboard->name = g_strdup(name);
	current_pinboard->icons = NULL;

	if (path)
	{
		loading_pinboard++;
		parse_file(path, pin_from_file);
		g_free(path);
		loading_pinboard--;
	}
}

/* Add a new icon to the background.
 * 'path' should be an absolute pathname.
 * 'x' and 'y' are the coordinates of the point in the middle of the text.
 * 'name' is the name to use. If NULL then the leafname of path is used.
 */
void pinboard_pin(guchar *path, guchar *name, int x, int y)
{
	PinIcon		*icon;
	int		path_len;
	int		width, height;

	g_return_if_fail(path != NULL);
	g_return_if_fail(current_pinboard != NULL);

	path_len = strlen(path);
	while (path_len > 0 && path[path_len - 1] == '/')
		path_len--;

	icon = g_new(PinIcon, 1);
	icon->selected = FALSE;
	icon->path = g_strndup(path, path_len);
	icon->mask = NULL;
	snap_to_grid(&x, &y);
	icon->x = x;
	icon->y = y;

	dir_stat(icon->path, &icon->item);

	if (!name)
	{
		name = strrchr(icon->path, '/');
		if (name)
			name++;
		else
			name = icon->path;	/* Shouldn't happen */
	}

	icon->item.leafname = g_strdup(name);

	icon->win = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_wmclass(GTK_WINDOW(icon->win), "ROX-Pinboard", PROJECT);

	icon->paper = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(icon->win), icon->paper);
	drag_set_pinicon_dest(icon);
	gtk_signal_connect(GTK_OBJECT(icon->paper), "drag_data_get",
				drag_data_get, NULL);

	gtk_widget_realize(icon->win);
	if (override_redirect)
	{
		gdk_window_lower(icon->win->window);
		gdk_window_set_override_redirect(icon->win->window, TRUE);
	}
	else
		make_panel_window(icon->win->window);

	set_size_and_shape(icon, &width, &height);
	offset_from_centre(icon, width, height, &x, &y);
	gtk_widget_set_uposition(icon->win, x, y);

	gtk_widget_add_events(icon->paper,
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			GDK_BUTTON1_MOTION_MASK | GDK_ENTER_NOTIFY_MASK |
			GDK_BUTTON2_MOTION_MASK | GDK_BUTTON3_MOTION_MASK);
	gtk_signal_connect(GTK_OBJECT(icon->paper), "enter-notify-event",
			GTK_SIGNAL_FUNC(enter_notify), icon);
	gtk_signal_connect(GTK_OBJECT(icon->paper), "button-press-event",
			GTK_SIGNAL_FUNC(button_press_event), icon);
	gtk_signal_connect(GTK_OBJECT(icon->paper), "button-release-event",
			GTK_SIGNAL_FUNC(button_release_event), icon);
	gtk_signal_connect(GTK_OBJECT(icon->paper), "motion-notify-event",
			GTK_SIGNAL_FUNC(icon_motion_notify), icon);
	gtk_signal_connect(GTK_OBJECT(icon->paper), "expose-event",
			GTK_SIGNAL_FUNC(draw_icon), icon);
	gtk_signal_connect(GTK_OBJECT(icon->win), "destroy",
			GTK_SIGNAL_FUNC(icon_destroyed), icon);

	current_pinboard->icons = g_list_prepend(current_pinboard->icons,
						 icon);
	gtk_widget_show_all(icon->win);

	if (!loading_pinboard)
		pinboard_save();
}

/* Remove an icon from the pinboard */
void pinboard_unpin(PinIcon *icon)
{
	g_return_if_fail(icon != NULL);

	gtk_widget_destroy(icon->win);
	pinboard_save();
}

/* Unpin all selected items */
void pinboard_unpin_selection(void)
{
	GList	*next;

	g_return_if_fail(current_pinboard != NULL);

	if (number_selected == 0)
	{
		delayed_error(PROJECT,
			_("You should first select some pinned icons to "
			"unpin. Hold down the Ctrl key to select some icons."));
		return;
	}

	next = current_pinboard->icons;
	while (next)
	{
		PinIcon	*icon = (PinIcon *) next->data;

		next = next->next;

		if (icon->selected)
			gtk_widget_destroy(icon->win);
	}

	pinboard_save();
}

/* Put a border around the icon, briefly.
 * If icon is NULL then cancel any existing wink.
 * The icon will automatically unhighlight unless timeout is FALSE,
 * in which case you must call this function again (with NULL or another
 * icon) to remove the highlight.
 */
void pinboard_wink_item(PinIcon *icon, gboolean timeout)
{
	if (current_wink_icon == icon)
		return;

	if (current_wink_icon)
	{
		mask_wink_border(current_wink_icon, &mask_transp);
		if (wink_timeout != -1)
			gtk_timeout_remove(wink_timeout);
	}

	current_wink_icon = icon;

	if (current_wink_icon)
	{
		mask_wink_border(current_wink_icon, &mask_solid);
		if (timeout)
			wink_timeout = gtk_timeout_add(300, end_wink, NULL);
		else
			wink_timeout = -1;
	}
}

/* Remove everything on the current pinboard and disables the pinboard.
 * Does not change any files. Does not change number_of_windows.
 */
void pinboard_clear(void)
{
	GList	*next;

	g_return_if_fail(current_pinboard != NULL);

	next = current_pinboard->icons;
	while (next)
	{
		PinIcon	*icon = (PinIcon *) next->data;

		next = next->next;

		gtk_widget_destroy(icon->win);
	}
	
	g_free(current_pinboard->name);
	g_free(current_pinboard);
	current_pinboard = NULL;

	release_xdnd_proxy(GDK_ROOT_WINDOW());
	gdk_window_remove_filter(GDK_ROOT_PARENT(), proxy_filter, NULL);
	gdk_window_set_user_data(GDK_ROOT_PARENT(), NULL);
}

/* If path is on the pinboard then it may have changed... check! */
void pinboard_may_update(guchar *path)
{
	GList	*next;

	if (!current_pinboard)
		return;

	for (next = current_pinboard->icons; next; next = next->next)
	{
		PinIcon	*icon = (PinIcon *) next->data;

		if (strcmp(icon->path, path) == 0)
			icon_may_update(icon);
	}
}

/* Return the single selected icon, or NULL */
PinIcon *pinboard_selected_icon(void)
{
	GList	*next;
	PinIcon	*found = NULL;
	
	g_return_val_if_fail(current_pinboard != NULL, NULL);

	for (next = current_pinboard->icons; next; next = next->next)
	{
		PinIcon	*icon = (PinIcon *) next->data;

		if (icon->selected)
		{
			if (found)
				return NULL;	/* >1 icon selected */
			else
				found = icon;
		}
	}

	return found;
}

/* Display the help for this application/item */
void pinboard_show_help(PinIcon *icon)
{
	g_return_if_fail(icon != NULL);

	show_item_help(icon->path, &icon->item);
}

void pinboard_clear_selection(void)
{
	pinboard_select_only(NULL);
}

gboolean pinboard_is_selected(PinIcon *icon)
{
	g_return_val_if_fail(icon != NULL, FALSE);

	return icon->selected;
}

/* Set whether an icon is selected or not */
void pinboard_set_selected(PinIcon *icon, gboolean selected)
{
	g_return_if_fail(icon != NULL);

	if (icon->selected == selected)
		return;

	if (selected)
		change_number_selected(+1);
	else
		change_number_selected(-1);
	
	icon->selected = selected;
	gtk_widget_queue_draw(icon->win);
}

/* Return a list of all the selected icons.
 * g_list_free() the result.
 */
GList *pinboard_get_selected(void)
{
	GList	*next;
	GList	*selected = NULL;

	for (next = current_pinboard->icons; next; next = next->next)
	{
		PinIcon	*i = (PinIcon *) next->data;

		if (i->selected)
			selected = g_list_append(selected, i);
	}

	return selected;
}

/* Clear the selection and then select this icon.
 * Doesn't release and claim the selection unnecessarily.
 * If icon is NULL, then just clears the selection.
 */
void pinboard_select_only(PinIcon *icon)
{
	GList	*next;
	
	g_return_if_fail(current_pinboard != NULL);

	if (icon)
		pinboard_set_selected(icon, TRUE);

	for (next = current_pinboard->icons; next; next = next->next)
	{
		PinIcon	*i = (PinIcon *) next->data;

		if (i->selected && i != icon)
			pinboard_set_selected(i, FALSE);
	}
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* See if the file the icon points to has changed. Update the icon
 * if so.
 */
static void icon_may_update(PinIcon *icon)
{
	MaskedPixmap *image = icon->item.image;

	pixmap_ref(image);
	dir_restat(icon->path, &icon->item);

	if (icon->item.image != image)
	{
		int	x = icon->x, y = icon->y;
		int	width, height;

		set_size_and_shape(icon, &width, &height);
		gdk_window_resize(icon->win->window, width, height);
		offset_from_centre(icon, width, height, &x, &y);
		gtk_widget_set_uposition(icon->win, x, y);
		gtk_widget_queue_draw(icon->win);
	}

	pixmap_unref(image);
}

static gint end_wink(gpointer data)
{
	pinboard_wink_item(NULL, FALSE);
	return FALSE;
}

/* Make the wink border solid or transparent */
static void mask_wink_border(PinIcon *icon, GdkColor *alpha)
{
	int	width, height;
	
	gdk_window_get_size(icon->paper->window, &width, &height);

	gdk_gc_set_foreground(mask_gc, alpha);
	gdk_draw_rectangle(icon->mask, mask_gc, FALSE,
			0, 0, width - 1, height - 1);
	gdk_draw_rectangle(icon->mask, mask_gc, FALSE,
			1, 1, width - 3, height - 3);

	gtk_widget_shape_combine_mask(icon->win, icon->mask, 0, 0);

	gtk_widget_draw(icon->paper, NULL);
}

/* Updates the name_width field and resizes and masks the window.
 * Returns the new width and height.
 */
static void set_size_and_shape(PinIcon *icon, int *rwidth, int *rheight)
{
	int		width, height;
	GdkFont		*font = icon->win->style->font;
	int		font_height;
	MaskedPixmap	*image = icon->item.image;
	DirItem		*item = &icon->item;
	
	font_height = font->ascent + font->descent;
	item->name_width = gdk_string_width(font, item->leafname);

	width = MAX(image->width, item->name_width + 2) +
				2 * WINK_FRAME;
	height = image->height + GAP + (font_height + 2) + 2 * WINK_FRAME;
	gtk_widget_set_usize(icon->win, width, height);

	if (icon->mask)
		gdk_pixmap_unref(icon->mask);
	icon->mask = gdk_pixmap_new(icon->win->window, width, height, 1);
	if (!mask_gc)
		mask_gc = gdk_gc_new(icon->mask);

	/* Clear the mask to transparent */
	gdk_gc_set_foreground(mask_gc, &mask_transp);
	gdk_draw_rectangle(icon->mask, mask_gc, TRUE, 0, 0, width, height);

	gdk_gc_set_foreground(mask_gc, &mask_solid);
	/* Make the icon area solid */
	if (image->mask)
	{
		gdk_draw_pixmap(icon->mask, mask_gc, image->mask,
				0, 0,
				(width - image->width) >> 1,
				WINK_FRAME,
				image->width,
				image->height);
	}
	else
	{
		gdk_draw_rectangle(icon->mask, mask_gc, TRUE,
				(width - image->width) >> 1,
				WINK_FRAME,
				image->width,
				image->height);
	}

	gdk_gc_set_function(mask_gc, GDK_OR);
	if (item->flags & ITEM_FLAG_SYMLINK)
	{
		gdk_draw_pixmap(icon->mask, mask_gc, im_symlink->mask,
				0, 0,		/* Source x,y */
				(width - image->width) >> 1,	/* Dest x */
				WINK_FRAME,			/* Dest y */
				-1, -1);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		/* Note: Both mount state pixmaps must have the same mask */
		gdk_draw_pixmap(icon->mask, mask_gc, im_mounted->mask,
				0, 0,		/* Source x,y */
				(width - image->width) >> 1,	/* Dest x */
				WINK_FRAME,			/* Dest y */
				-1, -1);
	}
	gdk_gc_set_function(mask_gc, GDK_COPY);

	/* Mask off an area for the text */
	gdk_draw_rectangle(icon->mask, mask_gc, TRUE,
			(width - (item->name_width + 2)) >> 1,
			WINK_FRAME + image->height + GAP,
			item->name_width + 2, font_height + 2);
	
	gtk_widget_shape_combine_mask(icon->win, icon->mask, 0, 0);

	*rwidth = width;
	*rheight = height;
}

static gint draw_icon(GtkWidget *widget, GdkEventExpose *event, PinIcon *icon)
{
	GdkFont		*font = icon->win->style->font;
	int		font_height;
	int		width, height;
	int		text_x, text_y;
	DirItem		*item = &icon->item;
	MaskedPixmap	*image = item->image;
	int		image_x;
	GdkGC		*gc = widget->style->black_gc;
	GtkStateType	state = icon->selected ? GTK_STATE_SELECTED
					       : GTK_STATE_NORMAL;

	font_height = font->ascent + font->descent;

	gdk_window_get_size(widget->window, &width, &height);
	image_x = (width - image->width) >> 1;

	/* TODO: If the shape extension is missing we might need to set
	 * the clip mask here...
	 */
	gdk_draw_pixmap(widget->window, gc,
			image->pixmap,
			0, 0,
			image_x,
			WINK_FRAME,
			image->width,
			image->height);

	if (item->flags & ITEM_FLAG_SYMLINK)
	{
		gdk_gc_set_clip_origin(gc, image_x, WINK_FRAME);
		gdk_gc_set_clip_mask(gc, im_symlink->mask);
		gdk_draw_pixmap(widget->window, gc,
				im_symlink->pixmap,
				0, 0,		/* Source x,y */
				image_x, WINK_FRAME,	/* Dest x,y */
				-1, -1);
		gdk_gc_set_clip_mask(gc, NULL);
		gdk_gc_set_clip_origin(gc, 0, 0);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		MaskedPixmap	*mp = item->flags & ITEM_FLAG_MOUNTED
					? im_mounted
					: im_unmounted;
					
		gdk_gc_set_clip_origin(gc, image_x, WINK_FRAME);
		gdk_gc_set_clip_mask(gc, mp->mask);
		gdk_draw_pixmap(widget->window, gc,
				mp->pixmap,
				0, 0,		/* Source x,y */
				image_x, WINK_FRAME,	/* Dest x,y */
				-1, -1);
		gdk_gc_set_clip_mask(gc, NULL);
		gdk_gc_set_clip_origin(gc, 0, 0);
	}

	text_x = (width - item->name_width) >> 1;
	text_y = WINK_FRAME + image->height + GAP + 1;

	gtk_paint_flat_box(widget->style, widget->window,
			state,
			GTK_SHADOW_NONE,
			NULL, widget, "text",
			text_x - 1,
			text_y - 1,
			item->name_width + 2,
			font_height + 2);

	gtk_paint_string(widget->style, widget->window,
			state,
			NULL, widget, "text",
			text_x,
			text_y + font->ascent,
			item->leafname);

	if (current_wink_icon == icon)
	{
		gdk_draw_rectangle(icon->paper->window,
				icon->paper->style->white_gc,
				FALSE,
				0, 0, width - 1, height - 1);
		gdk_draw_rectangle(icon->paper->window,
				icon->paper->style->black_gc,
				FALSE,
				1, 1, width - 3, height - 3);
	}

	return FALSE;
}

#define OTHER_BUTTONS (GDK_BUTTON2_MASK | GDK_BUTTON3_MASK | \
		       GDK_BUTTON4_MASK | GDK_BUTTON5_MASK)

static gboolean button_release_event(GtkWidget *widget,
			    	     GdkEventButton *event,
                            	     PinIcon *icon)
{
	int	b = event->button;
	DirItem	*item = &icon->item;

	if (pin_drag_type == DRAG_NONE)
		return TRUE;		/* Already done a drag? */

	gtk_grab_remove(widget);

	if (pin_drag_type == DRAG_MOVE_ICON)
		pinboard_save();
	else if (b == 1 && pin_drag_type == DRAG_MAY_COPY)
	{
		/* Could have been a copy, but the user didn't move
		 * far enough. Open the item instead.
		 */
		pinboard_wink_item(icon, TRUE);

		run_diritem(icon->path, item, NULL,
				(event->state & GDK_SHIFT_MASK) != 0);
	}

	pin_drag_type = DRAG_NONE;

	return TRUE;
}

static gboolean root_property_event(GtkWidget *widget,
			    	    GdkEventProperty *event,
                            	    gpointer data)
{
	if (event->atom == win_button_proxy &&
			event->state == GDK_PROPERTY_NEW_VALUE)
	{
		/* Setup forwarding on the new proxy window, if possible */
		forward_root_clicks();
	}

	return FALSE;
}

static gboolean root_button_press(GtkWidget *widget,
			    	  GdkEventButton *event,
                            	  gpointer data)
{
	if (event->button == collection_menu_button)
		show_pinboard_menu(event, NULL);
	else
		pinboard_clear_selection();

	return TRUE;
}

static gboolean enter_notify(GtkWidget *widget,
			     GdkEventCrossing *event,
			     PinIcon *icon)
{
	icon_may_update(icon);

	return FALSE;
}

static gboolean button_press_event(GtkWidget *widget,
			    	   GdkEventButton *event,
                            	   PinIcon *icon)
{
	int	b = event->button;

	if (pin_drag_type != DRAG_NONE)
		return TRUE;

	if (b == collection_menu_button)
		show_pinboard_menu(event, icon);
	else if (b < 4)
	{
		if (event->state & GDK_CONTROL_MASK)
		{
			pin_drag_type = DRAG_NONE;
			pinboard_set_selected(icon, !icon->selected);
		}
		else
		{
			drag_start_x = event->x_root;
			drag_start_y = event->y_root;
			gtk_grab_add(widget);

			if (b == 1)
				pin_drag_type = DRAG_MAY_COPY;
			else
				pin_drag_type = DRAG_MOVE_ICON;
		}
	}

	return TRUE;
}

/* Return a text/uri-list of all the icons in the list */
static guchar *create_uri_list(GList *list)
{
	GString	*tmp;
	guchar	*retval;
	guchar	*leader;

	tmp = g_string_new(NULL);
	leader = g_strdup_printf("file://%s", o_no_hostnames
						? ""
						: our_host_name());

	for (; list; list = list->next)
	{
		PinIcon *icon = (PinIcon *) list->data;

		g_string_append(tmp, leader);
		g_string_append(tmp, icon->path);
		g_string_append(tmp, "\r\n");
	}

	g_free(leader);
	retval = tmp->str;
	g_string_free(tmp, FALSE);
	
	return retval;
}

static void start_drag(PinIcon *icon, GdkEventMotion *event)
{
	GtkWidget *widget = icon->paper;
	GList	*selected;

	if (!icon->selected)
		pinboard_select_only(icon);
	
	selected = pinboard_get_selected();
	g_return_if_fail(selected != NULL);

	if (selected->next == NULL)
		drag_one_item(widget, event, icon->path, &icon->item, FALSE);
	else
	{
		guchar	*uri_list;

		uri_list = create_uri_list(selected);
		drag_selection(widget, event, uri_list);
		g_free(uri_list);
	}

	g_list_free(selected);
}

/* An icon is being dragged around... */
static gint icon_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       PinIcon *icon)
{
	int	x = event->x_root;
	int	y = event->y_root;

	if (pin_drag_type == DRAG_MOVE_ICON)
	{
		int	width, height;

		snap_to_grid(&x, &y);
		icon->x = x;
		icon->y = y;
		gdk_window_get_size(icon->win->window, &width, &height);
		offset_from_centre(icon, width, height, &x, &y);

		gdk_window_move(icon->win->window, x, y);
	}
	else if (pin_drag_type == DRAG_MAY_COPY)
	{
		int	dx = x - drag_start_x;
		int	dy = y - drag_start_y;

		if (ABS(dx) > 3 || ABS(dy) > 3)
		{
			pin_drag_type = DRAG_NONE;
			gtk_grab_remove(widget);
			start_drag(icon, event);
		}
	}

	return TRUE;
}

/* Called for each line in the pinboard file while loading a new board */
static char *pin_from_file(guchar *line)
{
	int	x, y, n;

	if (sscanf(line, " %d , %d , %n", &x, &y, &n) < 2)
		return NULL;		/* Ignore format errors */

	pinboard_pin(line + n, NULL, x, y);

	return NULL;
}

/* Make sure that clicks and drops on the root window come to us...
 * False if an error occurred (ie, someone else is using it).
 */
static gboolean add_root_handlers(void)
{
	GdkWindow	*root;

	if (!proxy_invisible)
	{
		GtkTargetEntry 	target_table[] =
		{
			{"text/uri-list", 0, TARGET_URI_LIST},
			{"STRING", 0, TARGET_STRING},
		};

		win_button_proxy = gdk_atom_intern("_WIN_DESKTOP_BUTTON_PROXY",
						   FALSE);
		proxy_invisible = gtk_invisible_new();
		gtk_widget_show(proxy_invisible);
		/*
		gdk_window_add_filter(proxy_invisible->window,
					proxy_filter, NULL);
					*/

		gtk_signal_connect(GTK_OBJECT(proxy_invisible),
				"property_notify_event",
				GTK_SIGNAL_FUNC(root_property_event), NULL);
		gtk_signal_connect(GTK_OBJECT(proxy_invisible),
				"button_press_event",
				GTK_SIGNAL_FUNC(root_button_press), NULL);
		drag_set_pinboard_dest(proxy_invisible);

		/* The proxy window is also used to hold the selection... */
		gtk_signal_connect(GTK_OBJECT(proxy_invisible),
				"selection_clear_event",
				GTK_SIGNAL_FUNC(lose_selection),
				NULL);
		
		gtk_signal_connect(GTK_OBJECT(proxy_invisible),
				"selection_get",
				GTK_SIGNAL_FUNC(selection_get), NULL);

		gtk_selection_add_targets(proxy_invisible,
				GDK_SELECTION_PRIMARY,
				target_table,
				sizeof(target_table) / sizeof(*target_table));
	}

	root = gdk_window_lookup(GDK_ROOT_WINDOW());
	if (!root)
		root = gdk_window_foreign_new(GDK_ROOT_WINDOW());
		
	if (!setup_xdnd_proxy(GDK_ROOT_WINDOW(), proxy_invisible->window))
		return FALSE;

	/* Forward events from the root window to our proxy window */
	gdk_window_add_filter(GDK_ROOT_PARENT(), proxy_filter, NULL);
	gdk_window_set_user_data(GDK_ROOT_PARENT(), proxy_invisible);
	gdk_window_set_events(GDK_ROOT_PARENT(),
			gdk_window_get_events(GDK_ROOT_PARENT()) |
				GDK_PROPERTY_CHANGE_MASK);

	forward_root_clicks();
	
	return TRUE;
}

/* See if the window manager is offering to forward root window clicks.
 * If so, grab them. Otherwise, do nothing.
 * Call this whenever the _WIN_DESKTOP_BUTTON_PROXY property changes.
 */
static void forward_root_clicks(void)
{
	click_proxy_gdk_window = find_click_proxy_window();
	if (!click_proxy_gdk_window)
		return;

	/* Events on the wm's proxy are dealt with by our proxy widget */
	gdk_window_set_user_data(click_proxy_gdk_window, proxy_invisible);
	gdk_window_add_filter(click_proxy_gdk_window, proxy_filter, NULL);

	/* The proxy window for clicks sends us button press events with
	 * SubstructureNotifyMask. We need StructureNotifyMask to receive
	 * DestroyNotify events, too.
	 */
	XSelectInput(GDK_DISPLAY(),
			GDK_WINDOW_XWINDOW(click_proxy_gdk_window),
			SubstructureNotifyMask | StructureNotifyMask);
}

/* Write the current state of the pinboard to the current pinboard file */
static void pinboard_save(void)
{
	guchar	*save;
	guchar	*leaf;
	GString	*tmp;
	FILE	*file = NULL;
	GList	*next;
	guchar	*save_new = NULL;

	g_return_if_fail(current_pinboard != NULL);
	
	leaf = g_strconcat("pb_", current_pinboard->name, NULL);
	save = choices_find_path_save(leaf, "ROX-Filer", TRUE);
	g_free(leaf);

	if (!save)
		return;

	save_new = g_strconcat(save, ".new", NULL);
	file = fopen(save_new, "wb");
	if (!file)
		goto err;

	tmp = g_string_new(NULL);
	for (next = current_pinboard->icons; next; next = next->next)
	{
		PinIcon *icon = (PinIcon *) next->data;

		g_string_sprintf(tmp, "%d, %d, %s\n",
				icon->x, icon->y, icon->path);
		if (fwrite(tmp->str, 1, tmp->len, file) < tmp->len)
			goto err;
	}

	g_string_free(tmp, TRUE);

	if (fclose(file))
	{
		file = NULL;
		goto err;
	}

	file = NULL;

	if (rename(save_new, save))
		goto err;

	goto out;
err:
	delayed_error(_("Error saving pinboard"), g_strerror(errno));
out:
	if (file)
		fclose(file);
	g_free(save_new);
}

/*
 * Filter that translates proxied events from virtual root windows into normal
 * Gdk events for the proxy_invisible widget. Stolen from gmc.
 *
 * Also gets events from the root window.
 */
static GdkFilterReturn proxy_filter(GdkXEvent *xevent,
				    GdkEvent *event,
				    gpointer data)
{
	XEvent 		*xev;
	GdkWindow	*proxy = proxy_invisible->window;

	xev = xevent;

	switch (xev->type) {
		case ButtonPress:
		case ButtonRelease:
			/* Translate button events into events that come from
			 * the proxy window, so that we can catch them as a
			 * signal from the invisible widget.
			 */
			if (xev->type == ButtonPress)
				event->button.type = GDK_BUTTON_PRESS;
			else
				event->button.type = GDK_BUTTON_RELEASE;

			gdk_window_ref(proxy);

			event->button.window = proxy;
			event->button.send_event = xev->xbutton.send_event;
			event->button.time = xev->xbutton.time;
			event->button.x_root = xev->xbutton.x_root;
			event->button.y_root = xev->xbutton.y_root;
			event->button.x = xev->xbutton.x;
			event->button.y = xev->xbutton.y;
			event->button.state = xev->xbutton.state;
			event->button.button = xev->xbutton.button;

			return GDK_FILTER_TRANSLATE;

		case DestroyNotify:
			/* XXX: I have no idea why this helps, but it does! */
			/* The proxy window was destroyed (i.e. the window
			 * manager died), so we have to cope with it
			 */
			if (((GdkEventAny *) event)->window == proxy)
				gdk_window_destroy_notify(proxy);

			return GDK_FILTER_REMOVE;

		default:
			break;
	}

	return GDK_FILTER_CONTINUE;
}

/* Does not save the new state */
static void icon_destroyed(GtkWidget *widget, PinIcon *icon)
{
	g_return_if_fail(icon != NULL);

	if (icon->selected)
		change_number_selected(-1);

	gdk_pixmap_unref(icon->mask);
	dir_item_clear(&icon->item);
	g_free(icon->path);
	g_free(icon);

	if (current_pinboard)
		current_pinboard->icons =
			g_list_remove(current_pinboard->icons, icon);
}

#define GRID_STEP 32

static void snap_to_grid(int *x, int *y)
{
	*x = ((*x + GRID_STEP / 2) / GRID_STEP) * GRID_STEP;
	*y = ((*y + GRID_STEP / 2) / GRID_STEP) * GRID_STEP;
}

/* Convert (x,y) from a centre point to a window position */
static void offset_from_centre(PinIcon *icon,
			       int width, int height,
			       int *x, int *y)
{
	*x -= width >> 1;
	*y -= height - (icon->paper->style->font->descent >> 1);
	*x = CLAMP(*x, 0, screen_width - width);
	*y = CLAMP(*y, 0, screen_height - height);
}

/* Same as drag_set_dest(), but for pinboard icons */
static void drag_set_pinicon_dest(PinIcon *icon)
{
	GtkObject	*obj = GTK_OBJECT(icon->paper);

	make_drop_target(icon->paper);

	gtk_signal_connect(obj, "drag_motion",
			GTK_SIGNAL_FUNC(drag_motion), icon);
	gtk_signal_connect(obj, "drag_leave",
			GTK_SIGNAL_FUNC(drag_leave), icon);

	/*
	gtk_signal_connect(obj, "drag_end",
			GTK_SIGNAL_FUNC(drag_end), filer_window);
			*/
}

/* Called during the drag when the mouse is in a widget registered
 * as a drop target. Returns TRUE if we can accept the drop.
 */
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    PinIcon		*icon)
{
	GdkDragAction	action = context->suggested_action;
	char		*type = NULL;
	DirItem		*item = &icon->item;

	if (provides(context, _rox_run_action))
	{
		/* This is a special internal type. The user is dragging
		 * to an executable item to set the run action.
		 */

		if (item->flags & (ITEM_FLAG_APPDIR | ITEM_FLAG_EXEC_FILE))
			type = drop_dest_prog;
		else
			goto out;

	}
	else
	{
		/* If we didn't drop onto a directory, application or
		 * executable file then give up.
		 */
		if (item->base_type != TYPE_DIRECTORY
				&& !(item->flags & ITEM_FLAG_EXEC_FILE))
			goto out;
	}
	
	if (gtk_drag_get_source_widget(context) == widget)
		goto out;	/* Can't drag something to itself! */

	if (item->base_type == TYPE_DIRECTORY &&
			!(item->flags & ITEM_FLAG_APPDIR))
	{
		if (provides(context, text_uri_list) ||
				provides(context, XdndDirectSave0))
			type = drop_dest_dir;
	}
	else
	{
		if (provides(context, text_uri_list) ||
				provides(context, application_octet_stream))
			type = drop_dest_prog;
	}

out:
	/* Don't allow drops to non-writeable directories */
	if (type == drop_dest_dir && access(icon->path, W_OK) != 0)
		type = NULL;

	if (type)
		pinboard_wink_item(icon, FALSE);

	g_dataset_set_data(context, "drop_dest_type", type);
	if (type)
	{
		gdk_drag_status(context, action, time);
		g_dataset_set_data_full(context, "drop_dest_path",
				g_strdup(icon->path), g_free);
	}

	return type != NULL;
}

static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       PinIcon		*icon)
{
	pinboard_wink_item(NULL, FALSE);
}

/* When changing the 'selected' attribute of an icon, call this
 * to update the global counter and claim or release the primary
 * selection as needed.
 */
static void change_number_selected(int delta)
{
	guint32	time;

	g_return_if_fail(delta != 0);
	g_return_if_fail(number_selected + delta >= 0);
	
	if (number_selected == 0)
	{
		time = gdk_event_get_time(gtk_get_current_event());

		gtk_selection_owner_set(proxy_invisible,
				GDK_SELECTION_PRIMARY,
				time);
	}
	
	number_selected += delta;

	if (number_selected == 0)
	{
		time = gdk_event_get_time(gtk_get_current_event());

		gtk_selection_owner_set(NULL,
				GDK_SELECTION_PRIMARY,
				time);
	}
}

/* Called when another application wants the contents of our selection */
static void selection_get(GtkWidget *widget, 
		       GtkSelectionData *selection_data,
		       guint      info,
		       guint      time,
		       gpointer   data)
{
	GString	*str;
	GList	*next;
	guchar	*leader = NULL;

	str = g_string_new(NULL);

	if (info == TARGET_URI_LIST)
		leader = g_strdup_printf("file://%s",
				o_no_hostnames ? "" : our_host_name());

	for (next = current_pinboard->icons; next; next = next->next)
	{
		PinIcon	*icon = (PinIcon *) next->data;

		if (!icon->selected)
			continue;

		if (leader)
			g_string_append(str, leader);
		g_string_append(str, icon->path);
		g_string_append_c(str, ' ');
	}

	g_free(leader);
	
	gtk_selection_data_set(selection_data,
				gdk_atom_intern("STRING", FALSE),
				8,
				str->str,
				str->len ? str->len - 1 : 0);

	g_string_free(str, TRUE);
}

/* Called when another application takes the selection away from us */
static gint lose_selection(GtkWidget *widget, GdkEventSelection *event)
{
	/* 'lock' number_selected so that we don't send any events */
	number_selected++;
	pinboard_clear_selection();
	number_selected--;

	return TRUE;
}

