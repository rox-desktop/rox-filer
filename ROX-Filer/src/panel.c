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

/* panel.c - code for dealing with panel windows */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include "global.h"

#include "panel.h"
#include "options.h"
#include "choices.h"
#include "main.h"
#include "type.h"
#include "gui_support.h"
#include "dir.h"
#include "pixmaps.h"
#include "display.h"
#include "bind.h"
#include "dnd.h"
#include "support.h"
#include "filer.h"
#include "icon.h"
#include "run.h"

static Panel *current_panel[PANEL_NUMBER_OF_SIDES];

/* NULL => Not loading a panel */
static Panel *loading_panel = NULL;

/* Static prototypes */
static int panel_delete(GtkWidget *widget, GdkEvent *event, Panel *panel);
static void panel_destroyed(GtkWidget *widget, Panel *panel);
static char *pan_from_file(guchar *line);
static gint icon_button_release(GtkWidget *widget,
			        GdkEventButton *event,
			        Icon *icon);
static gint icon_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      Icon *icon);
static void reposition_panel(Panel *panel, gboolean force_resize);
static gint expose_icon(GtkWidget *widget,
			GdkEventExpose *event,
			Icon *icon);
static gint draw_icon(GtkWidget *widget,
			GdkRectangle *badarea,
			Icon *icon);
static gint panel_button_release(GtkWidget *widget,
			      GdkEventButton *event,
			      Panel *panel);
static gint panel_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      Panel *panel);
static void panel_post_resize(GtkWidget *box,
			GtkRequisition *req, Panel *panel);
static void box_resized(GtkWidget *box, GtkAllocation *alloc, Panel *panel);
static void drag_set_panel_dest(Icon *icon);
static void add_uri_list(GtkWidget          *widget,
                         GdkDragContext     *context,
                         gint               x,
                         gint               y,
                         GtkSelectionData   *selection_data,
                         guint              info,
                         guint32            time,
			 Panel		    *panel);
static void panel_add_item(Panel *panel,
			   guchar *path,
			   guchar *name,
			   gboolean after);
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    Icon		*icon);
static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       Icon	*icon);
static GList *get_widget_list(Panel *panel);
static GtkWidget *make_insert_frame(Panel *panel);
static gboolean enter_icon(GtkWidget *widget,
			   GdkEventCrossing *event,
			   Icon *icon);
static gint icon_motion_event(GtkWidget *widget,
			      GdkEventMotion *event,
			      Icon *icon);
static gint panel_motion_event(GtkWidget *widget,
			      GdkEventMotion *event,
			      Panel *panel);
static void reposition_icon(Icon *icon, int index);
static void start_drag(Icon *icon, GdkEventMotion *event);
static guchar *create_uri_list(GList *list);
static void drag_end(GtkWidget *widget,
		     GdkDragContext *context,
		     Icon *icon);
static void perform_action(Panel *panel,
			   Icon *icon,
			   GdkEventButton *event);
static void run_applet(Icon *icon);
static void panel_set_style(guchar *new);


static GtkWidget *dnd_highlight = NULL; /* (stops flickering) */

/* When sliding the panel, records where the panel was before */
static gint slide_from_value = 0;

#define SHOW_BOTH 0
#define SHOW_APPS_SMALL 1
#define SHOW_ICON 2
static int panel_style = SHOW_APPS_SMALL;

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void panel_init(void)
{
	option_add_int("panel_style", panel_style, panel_set_style);
}

/* 'name' may be NULL or "" to remove the panel */
Panel *panel_new(guchar *name, PanelSide side)
{
	guchar	*load_path;
	Panel	*panel;
	GtkWidget	*vp, *box, *frame;

	g_return_val_if_fail(side >= 0 && side < PANEL_NUMBER_OF_SIDES, NULL);
	g_return_val_if_fail(loading_panel == NULL, NULL);

	if (name && *name == '\0')
		name = NULL;

	if (current_panel[side])
	{
		if (name)
			number_of_windows++;
		gtk_widget_destroy(current_panel[side]->window);
		if (name)
			number_of_windows--;
	}

	if (name == NULL || *name == '\0')
		return NULL;

	panel = g_new(Panel, 1);
	panel->name = g_strdup(name);
	panel->side = side;
	panel->height = 0;
	panel->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_wmclass(GTK_WINDOW(panel->window), "ROX-Panel", PROJECT);
	gtk_widget_set_events(panel->window,
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
			GDK_BUTTON2_MOTION_MASK);

	gtk_signal_connect(GTK_OBJECT(panel->window), "delete-event",
			GTK_SIGNAL_FUNC(panel_delete), panel);
	gtk_signal_connect(GTK_OBJECT(panel->window), "destroy",
			GTK_SIGNAL_FUNC(panel_destroyed), panel);
	gtk_signal_connect(GTK_OBJECT(panel->window), "button_press_event",
			GTK_SIGNAL_FUNC(panel_button_press), panel);
	gtk_signal_connect(GTK_OBJECT(panel->window), "button_release_event",
			GTK_SIGNAL_FUNC(panel_button_release), panel);
	gtk_signal_connect(GTK_OBJECT(panel->window), "motion-notify-event",
			GTK_SIGNAL_FUNC(panel_motion_event), panel);

	if (strchr(name, '/'))
		load_path = g_strdup(name);
	else
	{
		guchar	*leaf;

		leaf = g_strconcat("pan_", name, NULL);
		load_path = choices_find_path_load(leaf, "ROX-Filer");
		g_free(leaf);
	}

	vp = gtk_viewport_new(NULL, NULL);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(vp), GTK_SHADOW_OUT);
	gtk_container_add(GTK_CONTAINER(panel->window), vp);

	if (side == PANEL_TOP || side == PANEL_BOTTOM)
	{
		panel->adj = gtk_viewport_get_hadjustment(GTK_VIEWPORT(vp));
		box = gtk_hbox_new(FALSE, 0);
		panel->before = gtk_hbox_new(FALSE, 0);
		panel->after = gtk_hbox_new(FALSE, 0);
	}
	else
	{
		panel->adj = gtk_viewport_get_vadjustment(GTK_VIEWPORT(vp));
		box = gtk_vbox_new(FALSE, 0);
		panel->before = gtk_vbox_new(FALSE, 0);
		panel->after = gtk_vbox_new(FALSE, 0);
	}

	gtk_container_add(GTK_CONTAINER(vp), box);
	gtk_box_pack_start(GTK_BOX(box), panel->before, FALSE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(box), panel->after, FALSE, TRUE, 0);

	frame = make_insert_frame(panel);
	gtk_box_pack_start(GTK_BOX(box), frame, TRUE, TRUE, 4);

	/* This is used so that we can find the middle easily! */
	panel->gap = gtk_event_box_new();
	gtk_box_pack_start(GTK_BOX(box), panel->gap, FALSE, FALSE, 0);
	
	frame = make_insert_frame(panel);
	gtk_object_set_data(GTK_OBJECT(frame), "after", "yes");
	gtk_box_pack_start(GTK_BOX(box), frame, TRUE, TRUE, 4);

	gtk_widget_realize(panel->window);
	make_panel_window(panel->window->window);
	
	loading_panel = panel;
	if (load_path && access(load_path, F_OK) == 0)
		parse_file(load_path, pan_from_file);
	else
	{
		/* Don't scare users with an empty panel... */
		guchar	*apps;
		
		panel_add_item(panel, "~", "Home", FALSE);

		apps = pathdup(make_path(app_dir, "..")->str);
		if (apps)
		{
			panel_add_item(panel, apps, "Apps", FALSE);
			g_free(apps);
		}
	}
	loading_panel = NULL;
	g_free(load_path);

	current_panel[side] = panel;

	gtk_widget_queue_resize(box);
	gtk_signal_connect_after(GTK_OBJECT(panel->window), "size-request",
		GTK_SIGNAL_FUNC(panel_post_resize), (GtkObject *) panel);
	gtk_signal_connect_after(GTK_OBJECT(box), "size-allocate",
			GTK_SIGNAL_FUNC(box_resized), (GtkObject *) panel);

	number_of_windows++;
	gtk_widget_show_all(panel->window);
	
	return panel;
}

/* Set the size of the widget for this icon */
void panel_size_icon(Icon *icon)
{
	int	im_height;
	GdkFont	*font;
	int	width, height;

	g_return_if_fail(icon != NULL);
	font = icon->widget->style->font;

	if (icon->socket)
		return;

	im_height = MIN(PIXMAP_HEIGHT(icon->item.image->pixmap),
					ICON_HEIGHT);

	width = PIXMAP_WIDTH(icon->item.image->pixmap);

	if (panel_want_show_text(icon))
	{
		height = font->ascent + font->descent + 6 + im_height;
		width = MAX(width, icon->item.name_width);
	}
	else
		height = im_height;

	gtk_widget_set_usize(icon->widget, width + 4, height);
}

gboolean panel_want_show_text(Icon *icon)
{
	if (panel_style == SHOW_BOTH)
		return TRUE;
	if (panel_style == SHOW_ICON)
		return FALSE;

	if (icon->item.flags & ITEM_FLAG_APPDIR)
		return FALSE;

	return TRUE;
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* User has tried to close the panel via the window manager - confirm */
static int panel_delete(GtkWidget *widget, GdkEvent *event, Panel *panel)
{
	return get_choice(_("Close panel?"),
		      _("You have tried to close a panel via the window "
			"manager - I usually find that this is accidental... "
			"really close?"),
			2, _("Remove"), _("Cancel")) != 0;
}

static void panel_destroyed(GtkWidget *widget, Panel *panel)
{
	if (current_panel[panel->side] == panel)
		current_panel[panel->side] = NULL;

	if (panel->side == PANEL_TOP || panel->side == PANEL_BOTTOM)
	{
		if (current_panel[PANEL_RIGHT])
			reposition_panel(current_panel[PANEL_RIGHT], FALSE);
		if (current_panel[PANEL_LEFT])
			reposition_panel(current_panel[PANEL_LEFT], FALSE);
	}

	g_free(panel->name);
	g_free(panel);

	if (--number_of_windows < 1)
		gtk_main_quit();
}

/* Called for each line in the config file while loading a new panel */
static char *pan_from_file(guchar *line)
{
	guchar	*sep, *leaf;
	
	g_return_val_if_fail(line != NULL, NULL);
	g_return_val_if_fail(loading_panel != NULL, NULL);

	if (*line == '\0')
		return NULL;

	sep = strpbrk(line, "<>");
	if (!sep)
		return _("Missing < or > in panel config file");

	if (sep != line)
		leaf = g_strndup(line, sep - line);
	else
		leaf = NULL;
	
	panel_add_item(loading_panel,
			sep + 1,
			leaf,
			sep[0] == '>');

	g_free(leaf);

	return NULL;
}

/* Add an icon with this path to the panel. If after is TRUE then the
 * icon is added to the right/bottom end of the panel.
 *
 * If name is NULL a suitable name is taken from path.
 */
static void panel_add_item(Panel *panel,
			   guchar *path,
			   guchar *name,
			   gboolean after)
{
	GtkWidget	*widget;
	Icon	*icon;
	GdkFont		*font;

	widget = gtk_event_box_new();
	gtk_widget_set_events(widget,
			GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
			GDK_BUTTON3_MOTION_MASK |
			GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK |
			GDK_BUTTON_RELEASE_MASK);
	
	gtk_box_pack_start(GTK_BOX(after ? panel->after : panel->before),
			widget, FALSE, TRUE, 4);
	if (after)
		gtk_box_reorder_child(GTK_BOX(panel->after), widget, 0);
	
	gtk_widget_realize(widget);
	font = widget->style->font;

	icon = g_new(Icon, 1);
	icon->panel = panel;
	icon->src_path = g_strdup(path);
	icon->path = icon_convert_path(path);
	icon->socket = NULL;

	gtk_object_set_data(GTK_OBJECT(widget), "icon", icon);
	
	icon_hash_path(icon);
	
	icon->widget = widget;
	icon->selected = FALSE;
	dir_stat(icon->path, &icon->item, FALSE);

	if (name)
		icon->item.leafname = g_strdup(name);
	else
	{
		guchar	*slash;

		slash = strrchr(icon->path, '/');
		icon->item.leafname = g_strdup(slash && slash[1] ? slash + 1
								 : path);
	}
	
	icon->item.name_width = gdk_string_width(font, icon->item.leafname);

	gtk_signal_connect_object(GTK_OBJECT(widget), "destroy",
			  GTK_SIGNAL_FUNC(icon_destroyed), (gpointer) icon);

	if (icon->item.base_type == TYPE_DIRECTORY)
		run_applet(icon);

	gtk_signal_connect(GTK_OBJECT(widget), "button_release_event",
			GTK_SIGNAL_FUNC(icon_button_release), icon);
	gtk_signal_connect(GTK_OBJECT(widget), "button_press_event",
			GTK_SIGNAL_FUNC(icon_button_press), icon);
	gtk_signal_connect(GTK_OBJECT(icon->widget), "motion-notify-event",
			GTK_SIGNAL_FUNC(icon_motion_event), icon);

	if (!icon->socket)
	{
		gtk_signal_connect_after(GTK_OBJECT(widget),
				"enter-notify-event",
				GTK_SIGNAL_FUNC(enter_icon), icon);
		gtk_signal_connect_after(GTK_OBJECT(widget), "draw",
				GTK_SIGNAL_FUNC(draw_icon), icon);
		gtk_signal_connect_after(GTK_OBJECT(widget), "expose_event",
				GTK_SIGNAL_FUNC(expose_icon), icon);
		gtk_signal_connect(GTK_OBJECT(widget), "drag_data_get",
				drag_data_get, NULL);

		drag_set_panel_dest(icon);
	}

	panel_size_icon(icon);

	if (!loading_panel)
		panel_save(panel);
		
	icon_set_tip(icon);
	gtk_widget_show(widget);
}

static gint expose_icon(GtkWidget *widget,
			GdkEventExpose *event,
			Icon *icon)
{
	return draw_icon(widget, &event->area, icon);
}

static gint draw_icon(GtkWidget *widget, GdkRectangle *badarea, Icon *icon)
{
	GdkFont		*font = widget->style->font;
	GdkRectangle	area;
	int		width, height;
	int		text_x, text_y;
	int		text_height = font->ascent + font->descent;

	gdk_window_get_size(widget->window, &width, &height);

	area.x = 0;
	area.width = width;
	area.height = PIXMAP_HEIGHT(icon->item.image->pixmap);

	if (panel_want_show_text(icon))
	{
		area.y = height - text_height - 6 - area.height;

		draw_large_icon(widget, &area, &icon->item, icon->selected);

		text_x = (area.width - icon->item.name_width) >> 1;
		text_y = height - font->descent - 4;

		draw_string(widget,
				font,
				icon->item.leafname, -1,
				MAX(0, text_x),
				text_y,
				icon->item.name_width,
				area.width,
				icon->selected, TRUE);
	}
	else
	{
		area.y = (height - area.height) >> 1;

		draw_large_icon(widget, &area, &icon->item, icon->selected);
	}

	return TRUE;
}

/* icon may be NULL if the event is on the background */
static void perform_action(Panel *panel, Icon *icon, GdkEventButton *event)
{
	BindAction	action;
	
	action = bind_lookup_bev(icon ? BIND_PANEL_ICON : BIND_PANEL, event);

	if (icon && icon->socket)
		if (action != ACT_POPUP_MENU && action != ACT_MOVE_ICON)
			return;

	switch (action)
	{
		case ACT_OPEN_ITEM:
			dnd_motion_ungrab();
			wink_widget(icon->widget);
			run_diritem(icon->path, &icon->item, NULL, FALSE);
			break;
		case ACT_EDIT_ITEM:
			dnd_motion_ungrab();
			wink_widget(icon->widget);
			run_diritem(icon->path, &icon->item, NULL, TRUE);
			break;
		case ACT_POPUP_MENU:
			dnd_motion_ungrab();
			icon_show_menu(event, icon, panel);
			break;
		case ACT_MOVE_ICON:
			dnd_motion_start(MOTION_REPOSITION);
			break;
		case ACT_PRIME_AND_SELECT:
			if (!icon->selected)
				icon_select_only(icon);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_AND_TOGGLE:
			icon_set_selected(icon, !icon->selected);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_FOR_DND:
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_TOGGLE_SELECTED:
			icon_set_selected(icon, !icon->selected);
			break;
		case ACT_SELECT_EXCL:
			icon_set_selected(icon, TRUE);
			break;
		case ACT_SLIDE_CLEAR_PANEL:
			icon_select_only(NULL);
			/* (no break) */
		case ACT_SLIDE_PANEL:
			dnd_motion_grab_pointer();
			slide_from_value = panel->adj->value;
			dnd_motion_start(MOTION_REPOSITION);
			break;
		case ACT_IGNORE:
			break;
		case ACT_CLEAR_SELECTION:
			icon_select_only(NULL);
			break;
		default:
			g_warning("Unsupported action : %d\n", action);
			break;
	}
}

static gint panel_button_release(GtkWidget *widget,
			      GdkEventButton *event,
			      Panel *panel)
{
	if (dnd_motion_release(event))
		return TRUE;

	perform_action(panel, NULL, event);
	
	return TRUE;
}

static gint panel_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      Panel *panel)
{
	if (dnd_motion_press(panel->window, event))
		perform_action(panel, NULL, event);

	return TRUE;
}

static gint icon_button_release(GtkWidget *widget,
			        GdkEventButton *event,
			        Icon *icon)
{
	if (dnd_motion_release(event))
		return TRUE;

	perform_action(icon->panel, icon, event);
	
	return TRUE;
}

static gint icon_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      Icon *icon)
{
	if (dnd_motion_press(widget, event))
		perform_action(icon->panel, icon, event);

	return TRUE;
}

/* Difference between height of an icon and height of panel (or width) */
#define MARGIN 8

static void reposition_panel(Panel *panel, gboolean force_resize)
{
	int	x = 0, y = 0;
	int	w = 32, h = 32;
	GList	*next, *children;
	PanelSide	side = panel->side;

	children = get_widget_list(panel);

	for (next = children; next; next = next->next)
	{
		GtkWidget	*widget = (GtkWidget *) next->data;
		GtkRequisition	req;

		if (force_resize)
			panel_size_icon(gtk_object_get_data(
						GTK_OBJECT(widget), "icon"));
		gtk_widget_get_child_requisition(widget, &req);

		if (req.width > w)
			w = req.width;
		if (req.height > h)
			h = req.height;
	}

	g_list_free(children);

	if (side == PANEL_TOP || side == PANEL_BOTTOM)
	{
		w = screen_width;
		h += MARGIN;
	}
	else
	{
		w += MARGIN;
		if (side == PANEL_RIGHT)
			x = screen_width - w;

		h = screen_height;

		if (current_panel[PANEL_TOP])
		{
			int	ph;

			ph = current_panel[PANEL_TOP]->height;
			y += ph;
			h -= ph;
		}

		if (current_panel[PANEL_BOTTOM])
			h -= current_panel[PANEL_BOTTOM]->height;
	}

	if (side == PANEL_BOTTOM)
		y = screen_height - h;
	
	gtk_widget_set_uposition(panel->window, x, y);
	panel->height = h;
	gdk_window_move_resize(panel->window->window, x, y, w, h);

	if (side == PANEL_BOTTOM || side == PANEL_TOP)
	{
		if (current_panel[PANEL_RIGHT])
			reposition_panel(current_panel[PANEL_RIGHT], FALSE);
		if (current_panel[PANEL_LEFT])
			reposition_panel(current_panel[PANEL_LEFT], FALSE);
	}
}

/* Same as drag_set_dest(), but for panel icons */
static void drag_set_panel_dest(Icon *icon)
{
	GtkObject	*obj = GTK_OBJECT(icon->widget);

	make_drop_target(icon->widget, 0);

	gtk_signal_connect(obj, "drag_motion",
			GTK_SIGNAL_FUNC(drag_motion), icon);
	gtk_signal_connect(obj, "drag_leave",
			GTK_SIGNAL_FUNC(drag_leave), icon);
	gtk_signal_connect(obj, "drag_end",
			GTK_SIGNAL_FUNC(drag_end), icon);
}

static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    Icon		*icon)
{
	GdkDragAction	action = context->suggested_action;
	char		*type = NULL;
	DirItem		*item = &icon->item;

	if (icon->selected)
		goto out;	/* Can't drag a selection to itself */

	type = dnd_motion_item(context, &item);

	if (!item)
		type = NULL;
out:
	/* We actually must pretend to accept the drop, even if the
	 * directory isn't writeable, so that the spring-opening
	 * thing works.
	 */

	/* Don't allow drops to non-writeable directories */
	if (option_get_int("dnd_spring_open") == FALSE &&
			type == drop_dest_dir &&
			access(icon->path, W_OK) != 0)
	{
		type = NULL;
	}

	g_dataset_set_data(context, "drop_dest_type", type);
	if (type)
	{
		gdk_drag_status(context, action, time);
		g_dataset_set_data_full(context, "drop_dest_path",
				g_strdup(icon->path), g_free);
		if (type == drop_dest_dir)
			dnd_spring_load(context);

		if (dnd_highlight && dnd_highlight != icon->widget)
		{
			gtk_drag_unhighlight(dnd_highlight);
			dnd_highlight = NULL;
		}

		if (dnd_highlight == NULL)
		{
			gtk_drag_highlight(icon->widget);
			dnd_highlight = icon->widget;
		}
	}

	return type != NULL;
}


static void add_uri_list(GtkWidget          *widget,
                         GdkDragContext     *context,
                         gint               x,
                         gint               y,
                         GtkSelectionData   *selection_data,
                         guint              info,
                         guint32            time,
			 Panel		    *panel)
{
	gboolean after = FALSE;
	GSList *uris, *next;
	
	if (!selection_data->data)
		return;

	g_return_if_fail(selection_data->data[selection_data->length] == '\0');

	if (gtk_object_get_data(GTK_OBJECT(widget), "after"))
		after = TRUE;

	uris = uri_list_to_gslist(selection_data->data);

	for (next = uris; next; next = next->next)
	{
		guchar	*path;

		path = get_local_path((guchar *) next->data);

		if (path)
			panel_add_item(panel, path, NULL, after);
	}

	g_slist_free(uris);
}

static void drag_end(GtkWidget *widget,
		     GdkDragContext *context,
		     Icon *icon)
{
	if (tmp_icon_selected)
	{
		icon_select_only(NULL);
		tmp_icon_selected = FALSE;
	}
}

static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       Icon	*icon)
{
	if (dnd_highlight && dnd_highlight == widget)
	{
		gtk_drag_unhighlight(dnd_highlight);
		dnd_highlight = NULL;
	}

	dnd_spring_abort();
}

/* Writes lines to the file, one for each widget, prefixed by 'side'.
 * Returns TRUE on success, or FALSE on error (and sets errno).
 * Always frees the widgets list.
 */
static gboolean write_widgets(FILE *file, GList *widgets, guchar side)
{
	GList	*next;
	GString	*tmp;

	tmp = g_string_new(NULL);

	for (next = widgets; next; next = next->next)
	{
		Icon	*icon;

		icon = gtk_object_get_data(GTK_OBJECT(next->data), "icon");

		if (!icon)
		{
			g_warning("Can't find Icon from widget\n");
			continue;
		}

		g_string_sprintf(tmp, "%s%c%s\n",
				icon->item.leafname,
				side, icon->src_path);
		if (fwrite(tmp->str, 1, tmp->len, file) < tmp->len)
		{
			g_list_free(widgets);
			return FALSE;
		}
	}

	g_string_free(tmp, TRUE);
	
	if (widgets)
		g_list_free(widgets);

	return TRUE;
}

void panel_save(Panel *panel)
{
	guchar	*save = NULL;
	FILE	*file = NULL;
	guchar	*save_new = NULL;

	g_return_if_fail(panel != NULL);
	
	if (strchr(panel->name, '/'))
		save = g_strdup(panel->name);
	else
	{
		guchar	*leaf;

		leaf = g_strconcat("pan_", panel->name, NULL);
		save = choices_find_path_save(leaf, "ROX-Filer", TRUE);
		g_free(leaf);
	}

	if (!save)
		return;

	save_new = g_strconcat(save, ".new", NULL);
	file = fopen(save_new, "wb");
	if (!file)
		goto err;

	if (!write_widgets(file,
			gtk_container_children(GTK_CONTAINER(panel->before)),
			'<'))
		goto err;

	if (!write_widgets(file,
			g_list_reverse(gtk_container_children(
					GTK_CONTAINER(panel->after))),
			'>'))
		goto err;

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
	delayed_error(_("Error saving panel"), g_strerror(errno));
out:
	if (file)
		fclose(file);
	g_free(save);
	g_free(save_new);
}

static GList *get_widget_list(Panel *panel)
{
	GList	*list;

	list = gtk_container_children(GTK_CONTAINER(panel->before));
	list = g_list_concat(list,
			gtk_container_children(GTK_CONTAINER(panel->after)));

	return list;
}

/* Create a frame widget which can be used to add icons to the panel */
static GtkWidget *make_insert_frame(Panel *panel)
{
	GtkWidget *frame;
	GtkTargetEntry 	target_table[] = {
		{"text/uri-list", 0, TARGET_URI_LIST},
	};

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
	gtk_widget_set_usize(frame, 16, 16);

	gtk_signal_connect(GTK_OBJECT(frame), "drag-data-received",
			GTK_SIGNAL_FUNC(add_uri_list), panel);
	gtk_drag_dest_set(frame,
			GTK_DEST_DEFAULT_ALL,
			target_table,
			sizeof(target_table) / sizeof(*target_table),
			GDK_ACTION_COPY);

	return frame;
}

static gboolean enter_icon(GtkWidget *widget,
			   GdkEventCrossing *event,
			   Icon *icon)
{
	icon_may_update(icon);

	return FALSE;
}

static gint panel_motion_event(GtkWidget *widget,
			      GdkEventMotion *event,
			      Panel *panel)
{
	gint	delta, new;
	gboolean horz = panel->side == PANEL_TOP || panel->side == PANEL_BOTTOM;

	if (motion_state != MOTION_REPOSITION)
		return FALSE;

	if (horz)
		delta = event->x_root - drag_start_x;
	else
		delta = event->y_root - drag_start_y;

	new = slide_from_value - delta;
	new = CLAMP(new, 0, panel->adj->upper - panel->adj->page_size);

	gtk_adjustment_set_value(panel->adj, new);

	return TRUE;
}

static gint icon_motion_event(GtkWidget *widget,
			      GdkEventMotion *event,
			      Icon *icon)
{
	Panel	*panel = icon->panel;
	GList	*list, *me;
	gboolean horz = panel->side == PANEL_TOP || panel->side == PANEL_BOTTOM;
	int	val;
	int	dir = 0;

	if (motion_state == MOTION_READY_FOR_DND)
	{
		if (dnd_motion_moved(event))
			start_drag(icon, event);
		return TRUE;
	}
	else if (motion_state != MOTION_REPOSITION)
		return FALSE;

	list = gtk_container_children(GTK_CONTAINER(panel->before));
	list = g_list_append(list, NULL);	/* The gap in the middle */
	list = g_list_concat(list,
			gtk_container_children(GTK_CONTAINER(panel->after)));
	me = g_list_find(list, widget);

	g_return_val_if_fail(me != NULL, TRUE);

	val = horz ? event->x_root : event->y_root;

	if (me->prev)
	{
		GtkWidget *prev;
		int	  x, y;

		if (me->prev->data)
			prev = GTK_WIDGET(me->prev->data);
		else
			prev = panel->gap;

		gdk_window_get_deskrelative_origin(prev->window, &x, &y);

		if (val <= (horz ? x : y))
			dir = -1;
	}

	if (dir == 0 && me->next)
	{
		GtkWidget *next;
		int	  x, y, w, h;

		if (me->next->data)
			next = GTK_WIDGET(me->next->data);
		else
			next = panel->gap;

		gdk_window_get_deskrelative_origin(next->window, &x, &y);

		gdk_window_get_size(next->window, &w, &h);

		x += w;
		y += h;

		if (val >= (horz ? x : y))
		{
			if (next == panel->gap)
				dir = +2;
			else
				dir = +1;
		}
	}

	if (dir)
		reposition_icon(icon, g_list_index(list, widget) + dir);

	return TRUE;
}

/* Move icon to this index in the complete widget list.
 * 0 makes the icon the left-most icon. The gap in the middle has
 * an index number, which allows you to specify that the icon should
 * go on the left or right side.
 */
static void reposition_icon(Icon *icon, int index)
{
	Panel	  *panel = icon->panel;
	GtkWidget *widget = icon->widget;
	GList	  *list;
	int	  before_len;

	list = gtk_container_children(GTK_CONTAINER(panel->before));
	before_len = g_list_length(list);

	if (index <= before_len)
	{
		/* Want to move icon to the 'before' list. Is it there
		 * already?
		 */

		if (!g_list_find(list, widget))
		{
			/* No, reparent */
			gtk_grab_remove(widget);
			gtk_widget_reparent(widget, panel->before);
			dnd_motion_grab_pointer();
			gtk_grab_add(widget);
		}
		
		gtk_box_reorder_child(GTK_BOX(panel->before), widget, index);
	}
	else
	{
		/* Else, we need it in the 'after' list. */

		index -= before_len + 1;

		g_list_free(list);

		list = gtk_container_children(GTK_CONTAINER(panel->after));

		if (!g_list_find(list, widget))
		{
			/* Not already there, reparent */
			gtk_grab_remove(widget);
			gtk_widget_reparent(widget, panel->after);
			dnd_motion_grab_pointer();
			gtk_grab_add(widget);
		}

		gtk_box_reorder_child(GTK_BOX(panel->after), widget, index);
	}

	g_list_free(list);

	panel_save(panel);
}

static void start_drag(Icon *icon, GdkEventMotion *event)
{
	GtkWidget *widget = icon->widget;

	if (!icon->selected)
	{
		if (event->state & GDK_BUTTON1_MASK)
		{
			/* Select just this one */
			icon_select_only(icon);
			tmp_icon_selected = TRUE;
		}
		else
			icon_set_selected(icon, TRUE);
	}
	
	g_return_if_fail(icon_selection != NULL);

	if (icon_selection->next == NULL)
		drag_one_item(widget, event, icon->path, &icon->item);
	else
	{
		guchar	*uri_list;

		uri_list = create_uri_list(icon_selection);
		drag_selection(widget, event, uri_list);
		g_free(uri_list);
	}
}

/* Return a text/uri-list of all the icons in the list */
static guchar *create_uri_list(GList *list)
{
	GString	*tmp;
	guchar	*retval;
	guchar	*leader;

	tmp = g_string_new(NULL);
	leader = g_strdup_printf("file://%s", our_host_name());

	for (; list; list = list->next)
	{
		Icon *icon = (Icon *) list->data;

		g_string_append(tmp, leader);
		g_string_append(tmp, icon->path);
		g_string_append(tmp, "\r\n");
	}

	g_free(leader);
	retval = tmp->str;
	g_string_free(tmp, FALSE);
	
	return retval;
}

static void applet_died(GtkWidget *socket)
{
	if (!GTK_OBJECT_DESTROYED(socket))
		gtk_widget_destroy(socket);

	gtk_widget_unref(socket);
}

static void restart_applet(GtkWidget *button, Icon *icon)
{
	gtk_widget_destroy(button);
	run_applet(icon);
}

static void socket_destroyed(GtkWidget *socket, GtkWidget *widget)
{
	Icon	*icon;
	gboolean lost_widget;

	lost_widget = GTK_OBJECT_DESTROYED(widget);
	gtk_widget_unref(widget);

	if (lost_widget)
		return;		/* We're removing the icon... */
		
	icon = gtk_object_get_data(GTK_OBJECT(widget), "icon");
	icon->socket = gtk_button_new_with_label(_("Restart\nApplet"));
	gtk_container_add(GTK_CONTAINER(icon->widget), icon->socket);
	gtk_container_set_border_width(GTK_CONTAINER(icon->socket), 4);
	gtk_misc_set_padding(GTK_MISC(GTK_BIN(icon->socket)->child), 4, 4);
	gtk_widget_show(icon->socket);

	gtk_signal_connect(GTK_OBJECT(icon->socket), "clicked",
			GTK_SIGNAL_FUNC(restart_applet), icon);
}

/* Try to run this applet. Fills in icon->socket on success. */
static void run_applet(Icon *icon)
{
	char	*argv[3];
	pid_t	pid;

	argv[0] = make_path(icon->path, "AppletRun")->str;
	
	if (access(argv[0], X_OK) != 0)
		return;

	icon->socket = gtk_socket_new();
	gtk_container_add(GTK_CONTAINER(icon->widget), icon->socket);
	gtk_widget_show_all(icon->socket);
	gtk_widget_realize(icon->socket);

	gtk_widget_ref(icon->widget);
	gtk_object_set_data(GTK_OBJECT(icon->widget), "icon", icon);
	gtk_signal_connect(GTK_OBJECT(icon->socket), "destroy",
			GTK_SIGNAL_FUNC(socket_destroyed), icon->widget);
	
	argv[1] = g_strdup_printf("%ld",
			GDK_WINDOW_XWINDOW(icon->socket->window));
	argv[2] = NULL;

	pid = spawn(argv);
	gtk_widget_ref(icon->socket);
	on_child_death(pid, (CallbackFn) applet_died, icon->socket);
	
	g_free(argv[1]);
}

/* When one of the panel icons resizes it will cause it's container box
 * to resize. This will cause the packing box inside the viewport to resize -
 * we get here right after that.
 */
static void box_resized(GtkWidget *box, GtkAllocation *alloc, Panel *panel)
{
	reposition_panel(panel, FALSE);
}

static void panel_post_resize(GtkWidget *win, GtkRequisition *req, Panel *panel)
{
	if (panel->side == PANEL_TOP || panel->side == PANEL_BOTTOM)
	{
		if (req->width < screen_width)
			req->width = screen_width;
	}
	else
	{
		int h = screen_height;

		if (current_panel[PANEL_TOP])
			h -= current_panel[PANEL_TOP]->height;

		if (current_panel[PANEL_BOTTOM])
			h -= current_panel[PANEL_BOTTOM]->height;

		if (req->height < h)
			req->height = h;
	}
}

/* The style setting has been changed -- update all panels */
static void panel_set_style(guchar *new)
{
	int	os = panel_style;
	
	panel_style = option_get_int("panel_style");

	if (os != panel_style)
	{
		int	i;

		for (i = 0; i < PANEL_NUMBER_OF_SIDES; i++)
			if (current_panel[i])
				reposition_panel(current_panel[i], TRUE);

		icons_update_tip();
	}
}

