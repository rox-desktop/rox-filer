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

/* panel.c - code for dealing with panel windows */

#include "config.h"

#undef GTK_DISABLE_DEPRECATED

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <libxml/parser.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include "global.h"

#include "panel.h"
#include "options.h"
#include "choices.h"
#include "main.h"
#include "type.h"
#include "gui_support.h"
#include "diritem.h"
#include "pixmaps.h"
#include "display.h"
#include "bind.h"
#include "dnd.h"
#include "support.h"
#include "filer.h"
#include "icon.h"
#include "run.h"

/* The width of the separator at the inner edge of the panel */
#define EDGE_WIDTH 2

/* The gap between panel icons */
#define PANEL_ICON_SPACING 8

Panel *current_panel[PANEL_NUMBER_OF_SIDES];

/* NULL => Not loading a panel */
static Panel *loading_panel = NULL;

/* Static prototypes */
static int panel_delete(GtkWidget *widget, GdkEvent *event, Panel *panel);
static void panel_destroyed(GtkWidget *widget, Panel *panel);
static const char *pan_from_file(gchar *line);
static gint icon_button_release(GtkWidget *widget,
			        GdkEventButton *event,
			        Icon *icon);
static gint icon_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      Icon *icon);
static void reposition_panel(GtkWidget *window,
				GtkAllocation *alloc, Panel *panel);
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
			   const guchar *path,
			   const guchar *name,
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
static void panel_set_style(void);
static void size_request(GtkWidget *widget, GtkRequisition *req, Icon *icon);
static void panel_load_from_xml(Panel *panel, xmlDocPtr doc);
static gboolean draw_panel_edge(GtkWidget *widget, GdkEventExpose *event,
				Panel *panel);


static GtkWidget *dnd_highlight = NULL; /* (stops flickering) */

/* When sliding the panel, records where the panel was before */
static gint slide_from_value = 0;

#define SHOW_BOTH 0
#define SHOW_APPS_SMALL 1
#define SHOW_ICON 2
static Option o_panel_style;

static int closing_panel = 0;	/* Don't panel_save; destroying! */

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void panel_init(void)
{
	option_add_int(&o_panel_style, "panel_style", SHOW_APPS_SMALL);

	option_add_notify(panel_set_style);
}

/* 'name' may be NULL or "" to remove the panel */
Panel *panel_new(const gchar *name, PanelSide side)
{
	guchar	*load_path;
	Panel	*panel;
	GtkWidget	*vp, *box, *frame, *align;

	g_return_val_if_fail(side >= 0 && side < PANEL_NUMBER_OF_SIDES, NULL);
	g_return_val_if_fail(loading_panel == NULL, NULL);

	if (name && *name == '\0')
		name = NULL;

	if (current_panel[side])
	{
		if (name)
			number_of_windows++;
		closing_panel++;
		gtk_widget_destroy(current_panel[side]->window);
		closing_panel--;
		if (name)
			number_of_windows--;
	}

	if (name == NULL || *name == '\0')
		return NULL;

	panel = g_new(Panel, 1);
	panel->name = g_strdup(name);
	panel->side = side;
	panel->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_resizable(GTK_WINDOW(panel->window), FALSE);
	gtk_window_set_wmclass(GTK_WINDOW(panel->window), "ROX-Panel", PROJECT);
	gtk_widget_set_events(panel->window,
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
			GDK_BUTTON2_MOTION_MASK);

	g_signal_connect(panel->window, "delete-event",
			G_CALLBACK(panel_delete), panel);
	g_signal_connect(panel->window, "destroy",
			G_CALLBACK(panel_destroyed), panel);
	g_signal_connect(panel->window, "button_press_event",
			G_CALLBACK(panel_button_press), panel);
	g_signal_connect(panel->window, "button_release_event",
			G_CALLBACK(panel_button_release), panel);
	g_signal_connect(panel->window, "motion-notify-event",
			G_CALLBACK(panel_motion_event), panel);

	if (strchr(name, '/'))
		load_path = g_strdup(name);
	else
	{
		guchar	*leaf;

		leaf = g_strconcat("pan_", name, NULL);
		load_path = choices_find_path_load(leaf, PROJECT);
		g_free(leaf);
	}

	if (panel->side == PANEL_RIGHT)
		align = gtk_alignment_new(1.0, 0.0, 0.0, 1.0);
	else if (panel->side == PANEL_BOTTOM)
		align = gtk_alignment_new(0.0, 1.0, 1.0, 0.0);
	else if (panel->side == PANEL_TOP)
		align = gtk_alignment_new(0.0, 0.0, 1.0, 0.0);
	else
		align = gtk_alignment_new(0.0, 0.0, 0.0, 1.0);

	gtk_container_add(GTK_CONTAINER(panel->window), align);

	vp = gtk_viewport_new(NULL, NULL);
	gtk_container_set_resize_mode(GTK_CONTAINER(vp), GTK_RESIZE_PARENT);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(vp), GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(align), vp);

	gtk_signal_connect(GTK_OBJECT(align), "expose-event",
			GTK_SIGNAL_FUNC(draw_panel_edge), panel);

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
	g_object_set_data(G_OBJECT(frame), "after", "yes");
	gtk_box_pack_start(GTK_BOX(box), frame, TRUE, TRUE, 4);

	gtk_widget_realize(panel->window);
	make_panel_window(panel->window);

	gtk_widget_show_all(align);
	
	loading_panel = panel;
	if (load_path && access(load_path, F_OK) == 0)
	{
		xmlDocPtr doc;
		doc = xmlParseFile(load_path);
		if (doc)
		{
			panel_load_from_xml(panel, doc);
			xmlFreeDoc(doc);
		}
		else
		{
			parse_file(load_path, pan_from_file);
			delayed_error(_("Your old panel file has been "
					"converted to the new XML format."));
			panel_save(panel);
		}
	}
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
	g_signal_connect(panel->window, "size-request",
			G_CALLBACK(panel_post_resize), panel);
	g_signal_connect(panel->window, "size-allocate",
			G_CALLBACK(reposition_panel), panel);

	number_of_windows++;
	gtk_widget_show(panel->window);

	return panel;
}

gboolean panel_want_show_text(Icon *icon)
{
	if (o_panel_style.int_value == SHOW_BOTH)
		return TRUE;
	if (o_panel_style.int_value == SHOW_ICON)
		return FALSE;

	if (icon->item->flags & ITEM_FLAG_APPDIR)
		return FALSE;

	return TRUE;
}

void panel_icon_renamed(Icon *icon)
{
	GtkLabel *label = GTK_LABEL(icon->label);

	gtk_label_set_text(label, icon->item->leafname);
}

/* Externally visible function to add an item to a panel */
gboolean panel_add(PanelSide side,
		   const gchar *path, const gchar *label, gboolean after)
{
	g_return_val_if_fail(side >= 0 && side < PANEL_NUMBER_OF_SIDES, FALSE);
	
	g_return_val_if_fail(current_panel[side] != NULL, FALSE);

	panel_add_item(current_panel[side], path, label, after);

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
			2, _("Cancel"), _("Remove")) != 1;
}

static void panel_destroyed(GtkWidget *widget, Panel *panel)
{
	if (current_panel[panel->side] == panel)
		current_panel[panel->side] = NULL;

	if (panel->side == PANEL_TOP || panel->side == PANEL_BOTTOM)
	{
		if (current_panel[PANEL_RIGHT])
			gtk_widget_queue_resize(
					current_panel[PANEL_RIGHT]->window);
		if (current_panel[PANEL_LEFT])
			gtk_widget_queue_resize(
					current_panel[PANEL_LEFT]->window);
	}

	g_free(panel->name);
	g_free(panel);

	if (--number_of_windows < 1)
		gtk_main_quit();
}

static void panel_load_side(Panel *panel, xmlNodePtr side, gboolean after)
{
	xmlNodePtr node;
	char	   *label, *path;

	for (node = side->xmlChildrenNode; node; node = node->next)
	{
		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (strcmp(node->name, "icon") != 0)
			continue;

		label = xmlGetProp(node, "label");
		if (!label)
			label = g_strdup("<missing label>");
		path = xmlNodeGetContent(node);
		if (!path)
			path = g_strdup("<missing path>");

		panel_add_item(panel, path, label, after);

		g_free(path);
		g_free(label);
	}
}

/* Create one panel icon for each icon in the doc */
static void panel_load_from_xml(Panel *panel, xmlDocPtr doc)
{
	xmlNodePtr root;

	root = xmlDocGetRootElement(doc);
	panel_load_side(panel, get_subnode(root, NULL, "start"), FALSE);
	panel_load_side(panel, get_subnode(root, NULL, "end"), TRUE);
}

/* Called for each line in the config file while loading a new panel */
static const char *pan_from_file(gchar *line)
{
	gchar	*sep, *leaf;
	
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
	
	panel_add_item(loading_panel, sep + 1, leaf, sep[0] == '>');

	g_free(leaf);

	return NULL;
}

static gboolean icon_pointer_in(GtkWidget *widget,
				GdkEventCrossing *event,
				Icon *icon)
{
	gtk_widget_set_state(widget, GTK_STATE_PRELIGHT);

	return 0;
}

static gboolean icon_pointer_out(GtkWidget *widget,
				 GdkEventCrossing *event,
				 Icon *icon)
{
	gtk_widget_set_state(widget, GTK_STATE_NORMAL);

	return 0;
}

/* Add an icon with this path to the panel. If after is TRUE then the
 * icon is added to the right/bottom end of the panel.
 *
 * If name is NULL a suitable name is taken from path.
 */
static void panel_add_item(Panel *panel,
			   const guchar *path,
			   const guchar *name,
			   gboolean after)
{
	GtkWidget	*widget;
	Icon		*icon;

	g_return_if_fail(panel != NULL);
	g_return_if_fail(path != NULL);

	widget = gtk_event_box_new();
	gtk_widget_set_events(widget,
			GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
			GDK_BUTTON3_MOTION_MASK |
			GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK |
			GDK_BUTTON_RELEASE_MASK);
	
	gtk_box_pack_start(GTK_BOX(after ? panel->after : panel->before),
			widget, FALSE, TRUE, 0);
	if (after)
		gtk_box_reorder_child(GTK_BOX(panel->after), widget, 0);
	
	gtk_widget_realize(widget);

	icon = g_new(Icon, 1);
	icon->panel = panel;
	icon->src_path = g_strdup(path);
	icon->path = icon_convert_path(path);
	icon->socket = NULL;
	icon->label = NULL;
	icon->layout = NULL;

	gtk_object_set_data(GTK_OBJECT(widget), "icon", icon);
	
	icon_hash_path(icon);
	
	icon->widget = widget;
	gtk_widget_set_name(icon->widget, "panel-icon");
	icon->selected = FALSE;

	if (name)
		icon->item = diritem_new(name);
	else
	{
		guchar	*slash;

		slash = strrchr(icon->path, '/');
		icon->item = diritem_new(slash && slash[1] ? slash + 1
								 : path);
	}
	diritem_restat(icon->path, icon->item);
	
	g_signal_connect_swapped(widget, "destroy",
			  G_CALLBACK(icon_destroyed), icon);

	if (icon->item->base_type == TYPE_DIRECTORY)
		run_applet(icon);

	g_signal_connect(widget, "button_release_event",
			G_CALLBACK(icon_button_release), icon);
	g_signal_connect(widget, "button_press_event",
			G_CALLBACK(icon_button_press), icon);
	g_signal_connect(icon->widget, "motion-notify-event",
			G_CALLBACK(icon_motion_event), icon);
	g_signal_connect(icon->widget, "enter-notify-event",
			G_CALLBACK(icon_pointer_in), icon);
	g_signal_connect(icon->widget, "leave-notify-event",
			G_CALLBACK(icon_pointer_out), icon);

	if (!icon->socket)
	{
		gtk_signal_connect_after(GTK_OBJECT(widget),
				"enter-notify-event",
				GTK_SIGNAL_FUNC(enter_icon), icon);
		gtk_signal_connect_after(GTK_OBJECT(widget), "expose_event",
				GTK_SIGNAL_FUNC(expose_icon), icon);
		gtk_signal_connect(GTK_OBJECT(widget), "drag_data_get",
				GTK_SIGNAL_FUNC(drag_data_get), NULL);

		gtk_signal_connect(GTK_OBJECT(widget), "size_request",
				GTK_SIGNAL_FUNC(size_request), icon);

		drag_set_panel_dest(icon);

		icon->label = gtk_label_new(icon->item->leafname);
		gtk_container_add(GTK_CONTAINER(icon->widget), icon->label);
		gtk_misc_set_alignment(GTK_MISC(icon->label), 0.5, 1);
		gtk_misc_set_padding(GTK_MISC(icon->label), 1, 2);
	}

	if (!loading_panel)
		panel_save(panel);
		
	icon_set_tip(icon);
	gtk_widget_show(widget);
}

/* Called when Gtk+ wants to know how much space an icon needs.
 * 'req' is already big enough for the label, if shown.
 */
static void size_request(GtkWidget *widget, GtkRequisition *req, Icon *icon)
{
	int	im_width, im_height;

	im_width = icon->item->image->width;
	im_height = MIN(icon->item->image->height, ICON_HEIGHT);

	req->height += im_height;
	req->width = MAX(req->width, im_width);

	if (icon->panel->side == PANEL_LEFT || icon->panel->side == PANEL_RIGHT)
		req->height += PANEL_ICON_SPACING;
	else
		req->width += PANEL_ICON_SPACING;
}

static gint expose_icon(GtkWidget *widget,
			GdkEventExpose *event,
			Icon *icon)
{
	return draw_icon(widget, &event->area, icon);
}

static gint draw_icon(GtkWidget *widget, GdkRectangle *badarea, Icon *icon)
{
	GdkRectangle	area;
	int		width, height;

	gdk_window_get_size(widget->window, &width, &height);

	area.x = 0;
	area.width = width;
	area.height = icon->item->image->height;

	if (panel_want_show_text(icon))
	{
		int	text_height = icon->label->requisition.height;

		area.y = height - text_height - area.height;
		
		draw_large_icon(widget, &area, icon->item,
				icon->item->image, icon->selected);
	}
	else
	{
		area.y = (height - area.height) >> 1;
		draw_large_icon(widget, &area, icon->item,
				icon->item->image, icon->selected);
	}

	return FALSE;
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
			run_diritem(icon->path, icon->item, NULL, NULL, FALSE);
			break;
		case ACT_EDIT_ITEM:
			dnd_motion_ungrab();
			wink_widget(icon->widget);
			run_diritem(icon->path, icon->item, NULL, NULL, TRUE);
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
	if (icon->socket && event->button == 1)
		return FALSE;	/* Restart button */

	if (dnd_motion_release(event))
		return TRUE;

	perform_action(icon->panel, icon, event);
	
	return TRUE;
}

static gint icon_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      Icon *icon)
{
	if (icon->socket && event->button == 1)
		return FALSE;	/* Restart button */

	if (dnd_motion_press(widget, event))
		perform_action(icon->panel, icon, event);

	return TRUE;
}

static void reposition_panel(GtkWidget *window,
				GtkAllocation *alloc, Panel *panel)
{
	int		x = 0, y = 0;
	PanelSide	side = panel->side;

	if (side == PANEL_LEFT || side == PANEL_RIGHT)
	{
		if (side == PANEL_RIGHT)
			x = screen_width - alloc->width;

		if (current_panel[PANEL_TOP])
		{
			GtkWidget *win = current_panel[PANEL_TOP]->window;
			y += win->allocation.height;
		}
	}

	if (side == PANEL_BOTTOM)
		y = screen_height - alloc->height;
	
	gtk_window_move(GTK_WINDOW(panel->window), x, y);
	gdk_window_move(panel->window->window, x, y);

	if (side == PANEL_BOTTOM || side == PANEL_TOP)
	{
		if (current_panel[PANEL_RIGHT])
			gtk_widget_queue_resize(
					current_panel[PANEL_RIGHT]->window);
		if (current_panel[PANEL_LEFT])
			gtk_widget_queue_resize(
					current_panel[PANEL_LEFT]->window);
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
	DirItem		*item = icon->item;

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
	if (o_dnd_spring_open.int_value == FALSE &&
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
			dnd_spring_load(context, NULL);

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
	GList *uris, *next;
	
	if (!selection_data->data)
		return;

	g_return_if_fail(selection_data->data[selection_data->length] == '\0');

	if (g_object_get_data(G_OBJECT(widget), "after"))
		after = TRUE;

	uris = uri_list_to_glist(selection_data->data);

	for (next = uris; next; next = next->next)
	{
		const guchar	*path;

		path = get_local_path((guchar *) next->data);

		if (path)
			panel_add_item(panel, path, NULL, after);
	}

	g_list_free(uris);
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

/* Create XML icon nodes for these widgets.
 * Always frees the widgets list.
 */
static void make_widgets(xmlNodePtr side, GList *widgets)
{
	GList	*next;

	for (next = widgets; next; next = next->next)
	{
		Icon	*icon;
		xmlNodePtr tree;

		icon = g_object_get_data(G_OBJECT(next->data), "icon");

		if (!icon)
		{
			g_warning("Can't find Icon from widget\n");
			continue;
		}

		tree = xmlNewTextChild(side, NULL, "icon", icon->src_path);

		xmlSetProp(tree, "label", icon->item->leafname);
	}
	
	if (widgets)
		g_list_free(widgets);
}

void panel_save(Panel *panel)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	guchar	*save = NULL;
	guchar	*save_new = NULL;

	g_return_if_fail(panel != NULL);
	
	if (strchr(panel->name, '/'))
		save = g_strdup(panel->name);
	else
	{
		guchar	*leaf;

		leaf = g_strconcat("pan_", panel->name, NULL);
		save = choices_find_path_save(leaf, PROJECT, TRUE);
		g_free(leaf);
	}

	if (!save)
		return;

	doc = xmlNewDoc("1.0");
	xmlDocSetRootElement(doc, xmlNewDocNode(doc, NULL, "pinboard", NULL));

	root = xmlDocGetRootElement(doc);
	make_widgets(xmlNewChild(root, NULL, "start", NULL),
			gtk_container_children(GTK_CONTAINER(panel->before)));

	make_widgets(xmlNewChild(root, NULL, "end", NULL),
			g_list_reverse(gtk_container_children(
					GTK_CONTAINER(panel->after))));

	save_new = g_strconcat(save, ".new", NULL);
	if (save_xml_file(doc, save_new) || rename(save_new, save))
		delayed_error(_("Error saving panel %s: %s"),
				save, g_strerror(errno));
	g_free(save_new);

	g_free(save);
	if (doc)
		xmlFreeDoc(doc);
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
		drag_one_item(widget, event, icon->path, icon->item, NULL);
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
	leader = g_strdup_printf("file://%s", our_host_name_for_dnd());

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
	gboolean never_plugged;

	never_plugged = (!gtk_object_get_data(GTK_OBJECT(socket), "lost_plug"))
		      && !GTK_SOCKET(socket)->plug_window;

	if (never_plugged)
	{
		report_error(
			_("Applet quit without ever creating a widget!"));
		gtk_widget_destroy(socket);
	}

	gtk_widget_unref(socket);
}

static void socket_destroyed(GtkWidget *socket, GtkWidget *widget)
{
	gtk_object_set_data(GTK_OBJECT(socket), "lost_plug", "yes");

	gtk_widget_unref(socket);

	gtk_widget_destroy(widget);	/* Remove from panel */

	if (!closing_panel)
		panel_save(gtk_object_get_data(GTK_OBJECT(socket), "panel"));
}

/* Try to run this applet.
 * Cases:
 * 
 * - No executable AppletRun:
 * 	icon->socket == NULL (unchanged) on return.
 *
 * Otherwise, create socket (setting icon->socket) and ref it twice.
 * 
 * - AppletRun quits without connecting a plug:
 * 	On child death lost_plug is unset and socket is empty.
 * 	Unref socket.
 * 	Report error and destroy widget (to 'socket destroyed').
 *
 * - AppletRun quits while plug is in socket:
 * 	Unref socket once. Socket will be destroyed later.
 *
 * - Socket is destroyed.
 * 	Set lost_plug = "yes" and remove widget from panel.
 * 	Unref socket.
 */
static void run_applet(Icon *icon)
{
	char	*argv[3];
	pid_t	pid;

	argv[0] = make_path(icon->path, "AppletRun")->str;
	
	if (access(argv[0], X_OK) != 0)
		return;

	icon->socket = gtk_socket_new();
	/* Two refs held: one for child death, one for socket destroyed */
	gtk_widget_ref(icon->socket);
	gtk_widget_ref(icon->socket);
	
	gtk_container_add(GTK_CONTAINER(icon->widget), icon->socket);
	gtk_widget_show_all(icon->socket);
	gtk_widget_realize(icon->socket);

	{
		gchar		*pos;
		PanelSide	side = icon->panel->side;

		/* Set a hint to let applets position their menus correctly */
		pos = g_strdup_printf("%s,%d",
				side == PANEL_TOP ? "Top" :
				side == PANEL_BOTTOM ? "Bottom" :
				side == PANEL_LEFT ? "Left" :
				"Right", MENU_MARGIN);
		gdk_property_change(icon->socket->window,
				gdk_atom_intern("_ROX_PANEL_MENU_POS", FALSE),
				gdk_atom_intern("STRING", FALSE),
				8, GDK_PROP_MODE_REPLACE,
				pos, strlen(pos));
		g_free(pos);
	}

	gtk_object_set_data(GTK_OBJECT(icon->widget), "icon", icon);
	gtk_object_set_data(GTK_OBJECT(icon->socket), "panel", icon->panel);

	gtk_signal_connect(GTK_OBJECT(icon->socket), "destroy",
			GTK_SIGNAL_FUNC(socket_destroyed), icon->widget);
	
	argv[1] = g_strdup_printf("%ld",
			GDK_WINDOW_XWINDOW(icon->socket->window));
	argv[2] = NULL;

	pid = spawn_full((const char **) argv, NULL, NULL);
	
	on_child_death(pid, (CallbackFn) applet_died, icon->socket);
	
	g_free(argv[1]);
}

static void panel_post_resize(GtkWidget *win, GtkRequisition *req, Panel *panel)
{
	if (panel->side == PANEL_TOP || panel->side == PANEL_BOTTOM)
	{
		req->width = screen_width;
		req->height += EDGE_WIDTH;
	}
	else
	{
		int h = screen_height;

		if (current_panel[PANEL_TOP])
		{
			GtkWidget *win = current_panel[PANEL_TOP]->window;
			h -= win->allocation.height;
		}

		if (current_panel[PANEL_BOTTOM])
		{
			GtkWidget *win = current_panel[PANEL_BOTTOM]->window;
			h -= win->allocation.height;
		}

		req->height = h;
		req->width += EDGE_WIDTH;
	}
}

/* The style setting has been changed -- update all panels */
static void panel_set_style(void)
{
	if (o_panel_style.has_changed)
	{
		int	i;

		icons_update_tip();

		for (i = 0; i < PANEL_NUMBER_OF_SIDES; i++)
		{
			Panel	*panel = current_panel[i];
			
			if (!panel)
				continue;

			gtk_widget_queue_resize(panel->window);
		}
	}
}

static gboolean draw_panel_edge(GtkWidget *widget, GdkEventExpose *event,
				Panel *panel)
{
	int	x, y, width, height;

	if (panel->side == PANEL_TOP || panel->side == PANEL_BOTTOM)
	{
		width = screen_width;
		height = EDGE_WIDTH;

		x = 0;
		if (panel->side == PANEL_BOTTOM)
			y = 0;
		else
			y = widget->allocation.height - EDGE_WIDTH;
	}
	else
	{
		width = EDGE_WIDTH;
		height = screen_height;

		y = 0;
		if (panel->side == PANEL_RIGHT)
			x = 0;
		else
			x = widget->allocation.width - EDGE_WIDTH;
	}
	
	gdk_draw_rectangle(widget->window,
			widget->style->fg_gc[GTK_STATE_NORMAL], TRUE,
			x, y, width, height);

	return FALSE;
}
