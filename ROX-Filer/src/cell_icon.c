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

/* cell_icon.c - a GtkCellRenderer used for the icons in details mode
 *
 * Based on gtkcellrendererpixbuf.c.
 */

#include "config.h"

#include <gtk/gtk.h>

#include "global.h"

#include "cell_icon.h"
#include "display.h"
#include "diritem.h"
#include "pixmaps.h"

typedef struct _CellIcon CellIcon;
typedef struct _CellIconClass CellIconClass;

struct _CellIcon {
	GtkCellRenderer parent;

	DirItem *item;
	GdkColor background;
	gboolean selected;
};

struct _CellIconClass {
	GtkCellRendererClass parent_class;
};


/* Static prototypes */
static void cell_icon_set_property(GObject *object, guint param_id,
				       const GValue *value, GParamSpec *pspec);
static void cell_icon_init       (CellIcon *cell);
static void cell_icon_class_init (CellIconClass *class);
static void cell_icon_get_size   (GtkCellRenderer	*cell,
				      GtkWidget		*widget,
				      GdkRectangle	*rectangle,
				      gint		*x_offset,
				      gint		*y_offset,
				      gint		*width,
				      gint		*height);
static void cell_icon_render     (GtkCellRenderer		*cell,
				      GdkWindow		*window,
				      GtkWidget		*widget,
				      GdkRectangle	*background_area,
				      GdkRectangle	*cell_area,
				      GdkRectangle	*expose_area,
				      guint		flags);
static GtkType cell_icon_get_type(void);

enum {
	PROP_ZERO,
	PROP_ITEM,
	PROP_BACKGROUND_GDK,
};

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

GtkCellRenderer *cell_icon_new(void)
{
	return GTK_CELL_RENDERER(g_object_new(cell_icon_get_type(), NULL));
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/


static GtkType cell_icon_get_type(void)
{
	static GtkType cell_icon_type = 0;

	if (!cell_icon_type)
	{
		static const GTypeInfo cell_icon_info =
		{
			sizeof (CellIconClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) cell_icon_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (CellIcon),
			0,              /* n_preallocs */
			(GInstanceInitFunc) cell_icon_init,
		};

		cell_icon_type = g_type_register_static(GTK_TYPE_CELL_RENDERER,
							"CellIcon",
							&cell_icon_info, 0);
	}

	return cell_icon_type;
}

static void cell_icon_init(CellIcon *icon)
{
}

static void cell_icon_class_init(CellIconClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);
	GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS(class);

	object_class->set_property = cell_icon_set_property;

	cell_class->get_size = cell_icon_get_size;
	cell_class->render = cell_icon_render;

	g_object_class_install_property(object_class,
			PROP_ITEM,
			g_param_spec_pointer("item",
				_("DirItem"),
				_("The item to render."),
				G_PARAM_WRITABLE));

	g_object_class_install_property(object_class,
			PROP_BACKGROUND_GDK,
			g_param_spec_boxed("background_gdk",
				_("Background color"),
				_("Background color as a GdkColor"),
				GDK_TYPE_COLOR,
				G_PARAM_WRITABLE));  
}

static void cell_icon_set_property(GObject *object, guint param_id,
				       const GValue *value, GParamSpec *pspec)
{
	CellIcon *icon = (CellIcon *) object;

	switch (param_id)
	{
		case PROP_ITEM:
			icon->item = (DirItem *) g_value_get_pointer(value);
			break;
		case PROP_BACKGROUND_GDK:
		{
			GdkColor *bg = g_value_get_boxed(value);
			icon->selected = bg != NULL;
			if (bg)
			{
				icon->background.red = bg->red;
				icon->background.green = bg->green;
				icon->background.blue = bg->blue;
			}
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object,
							  param_id, pspec);
			break;
	}
}

static void cell_icon_get_size(GtkCellRenderer *cell,
				   GtkWidget       *widget,
				   GdkRectangle    *cell_area,
				   gint            *x_offset,
				   gint            *y_offset,
				   gint            *width,
				   gint            *height)
{
	if (x_offset)
		*x_offset = 0;
	if (y_offset)
		*y_offset = 0;
	if (width)
		*width = SMALL_WIDTH;
	if (height)
		*height = SMALL_HEIGHT;
}

static void cell_icon_render(GtkCellRenderer    *cell,
			     GdkWindow		*window,
			     GtkWidget		*widget,
			     GdkRectangle	*background_area,
			     GdkRectangle	*cell_area,
			     GdkRectangle	*expose_area,
			     guint		flags)
{
	CellIcon *icon = (CellIcon *) cell;

	g_return_if_fail(icon->item != NULL);

	/* Drag the background */

	if (icon->selected)
	{
		GdkColor color;
		GdkGC *gc;

		color.red = icon->background.red;
		color.green = icon->background.green;
		color.blue = icon->background.blue;

		gc = gdk_gc_new(window);

		gdk_gc_set_rgb_fg_color(gc, &color);

		gdk_draw_rectangle(window, gc, TRUE,
				background_area->x,
				background_area->y,
				background_area->width,
				background_area->height);

		g_object_unref(G_OBJECT(gc));
	}

	/* Draw the icon */

	if (!icon->item->image)
		return;

	draw_small_icon(window, cell_area, icon->item, icon->item->image,
			FALSE);	/* XXX: selected */
}
