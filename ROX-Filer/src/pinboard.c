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
#include <gtk/gtk.h>

#include "main.h"
#include "pinboard.h"
#include "type.h"

/* The number of pixels between the bottom of the image and the top
 * of the text.
 */
#define GAP 2

/* The size of the border around the icon which is used when winking */
#define WINK_FRAME 2

struct _PinIcon {
	GtkWidget	*win, *paper;
	guchar		*path;
	guchar		*name;
	int		base_type;
	MIME_type	*mime_type;
	MaskedPixmap	*image;
	GdkBitmap	*mask;

	/* String details */
	int		text_width;	/* Total width in pixels */
};

static PinIcon	*current_wink_icon = NULL;
static gint	wink_timeout;

static GdkColor		mask_solid = {1, 1, 1, 1};
static GdkColor		mask_transp = {0, 0, 0, 0};
static  GdkGC	*mask_gc = NULL;

/* Static prototypes */
void set_size_and_shape(PinIcon *icon);
static gint draw_icon(GtkWidget *widget, GdkEventExpose *event, PinIcon *icon);
void mask_wink_border(PinIcon *icon, GdkColor *alpha);
gint end_wink(gpointer data);
gboolean button_press_event(GtkWidget *widget,
			    GdkEventButton *event,
                            PinIcon *icon);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Add a new icon to the background.
 * 'path' should be an absolute pathname.
 * 'x' and 'y' should be on the screen (top left).
 * 'name' is the name to use. If NULL then the leafname of path is used.
 */
void pinboard_pin(guchar *path, guchar *name, int x, int y)
{
	PinIcon		*icon;
	struct stat	info;

	g_return_if_fail(path != NULL);

	icon = g_new(PinIcon, 1);
	icon->path = g_strdup(path);
	icon->mask = NULL;

	if (stat(path, &info))
		icon->base_type = TYPE_ERROR;
	else
		icon->base_type = mode_to_base_type(info.st_mode);

	if (icon->base_type == TYPE_FILE)
		icon->mime_type = type_from_path(path);
	else
		icon->mime_type = mime_type_from_base_type(icon->base_type);

	icon->image = type_to_icon(icon->mime_type);
	
	if (!name)
	{
		name = strrchr(path, '/');
		if (name)
			name++;
		else
			name = path;	/* Shouldn't happen */
	}

	icon->name = g_strdup(name);

	icon->win = gtk_window_new(GTK_WINDOW_DIALOG);
	number_of_windows++;
	gtk_window_set_wmclass(GTK_WINDOW(icon->win), "ROX-Pinboard", PROJECT);
	gtk_widget_set_uposition(icon->win, x, y);
	gtk_widget_realize(icon->win);
	gdk_window_set_decorations(icon->win->window, 0);
	gdk_window_set_functions(icon->win->window, 0);

	icon->paper = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(icon->win), icon->paper);

	set_size_and_shape(icon);

	gtk_widget_add_events(icon->paper, GDK_BUTTON_PRESS_MASK);
	gtk_signal_connect(GTK_OBJECT(icon->paper), "button-press-event",
			GTK_SIGNAL_FUNC(button_press_event), icon);
	gtk_signal_connect(GTK_OBJECT(icon->paper), "expose-event",
			GTK_SIGNAL_FUNC(draw_icon), icon);

	gtk_widget_show_all(icon->win);
}

/* Put a border around the icon, briefly.
 * If icon is NULL then cancel any existing wink.
 */
void pinboard_wink_item(PinIcon *icon)
{
	if (current_wink_icon)
	{
		mask_wink_border(current_wink_icon, &mask_transp);
		gtk_timeout_remove(wink_timeout);
	}

	current_wink_icon = icon;

	if (current_wink_icon)
	{
		mask_wink_border(current_wink_icon, &mask_solid);
		wink_timeout = gtk_timeout_add(300, end_wink, NULL);
	}
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

gint end_wink(gpointer data)
{
	pinboard_wink_item(NULL);
	return FALSE;
}

/* Make the wink border solid or transparent */
void mask_wink_border(PinIcon *icon, GdkColor *alpha)
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

void cancel_wink(void)
{
	g_return_if_fail(current_wink_icon != NULL);
	
	mask_wink_border(current_wink_icon, &mask_transp);

	current_wink_icon = NULL;
}

/* Updates the text_width field and resizes and masks the window */
void set_size_and_shape(PinIcon *icon)
{
	int		width, height;
	GdkFont		*font = icon->win->style->font;
	int		font_height;
	GdkBitmap	*mask;
	
	font_height = font->ascent + font->descent;
	icon->text_width = gdk_string_width(font, icon->name);

	width = MAX(icon->image->width, icon->text_width + 2) + 2 * WINK_FRAME;
	height = icon->image->height + GAP + (font_height + 2) + 2 * WINK_FRAME;
	gtk_widget_set_usize(icon->win, width, height);

	mask = gdk_pixmap_new(icon->win->window, width, height, 1);
	if (!mask_gc)
		mask_gc = gdk_gc_new(mask);

	/* Clear the mask to transparent */
	gdk_gc_set_foreground(mask_gc, &mask_transp);
	gdk_draw_rectangle(mask, mask_gc, TRUE, 0, 0, width, height);

	gdk_gc_set_foreground(mask_gc, &mask_solid);
	gdk_draw_pixmap(mask, mask_gc, icon->image->mask,
				0, 0,
				(width - icon->image->width) >> 1,
				WINK_FRAME,
				icon->image->width,
				icon->image->height);

	/* Mask off an area for the text */
	gdk_draw_rectangle(mask, mask_gc, TRUE,
			(width - (icon->text_width + 2)) >> 1,
			WINK_FRAME + icon->image->height + GAP,
			icon->text_width + 2, font_height + 2);
	
	gtk_widget_shape_combine_mask(icon->win, mask, 0, 0);

	if (icon->mask)
		gdk_pixmap_unref(icon->mask);
	icon->mask = mask;
}

static gint draw_icon(GtkWidget *widget, GdkEventExpose *event, PinIcon *icon)
{
	GdkFont		*font = icon->win->style->font;
	int		font_height;
	int		width, height;
	int		text_x, text_y;

	font_height = font->ascent + font->descent;

	gdk_window_get_size(widget->window, &width, &height);

	/* TODO: If the shape extension is missing we might need to set
	 * the clip mask here...
	 */
	gdk_draw_pixmap(widget->window, widget->style->black_gc,
			icon->image->pixmap,
			0, 0,
			(width - icon->image->width) >> 1,
			WINK_FRAME,
			icon->image->width,
			icon->image->height);

	text_x = (width - icon->text_width) >> 1;
	text_y = WINK_FRAME + icon->image->height + GAP + 1;

	gtk_paint_flat_box(widget->style, widget->window,
			GTK_STATE_NORMAL, GTK_SHADOW_NONE,
			NULL, widget, "text",
			text_x - 1,
			text_y - 1,
			icon->text_width + 2,
			font_height + 2);

	gtk_paint_string(widget->style, widget->window,
			GTK_STATE_NORMAL,
			NULL, widget, "text",
			text_x,
			text_y + font->ascent,
			icon->name);

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

gboolean button_press_event(GtkWidget *widget,
			    GdkEventButton *event,
                            PinIcon *icon)
{
	pinboard_wink_item(icon);

	filer_opendir(icon->path, PANEL_NO);

	return TRUE;
}
