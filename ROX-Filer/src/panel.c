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

#include <gtk/gtkinvisible.h>
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
#include "run.h"
#include "dnd.h"
#include "menu.h"
#include "support.h"
#include "mount.h"
#include "filer.h"
#include "icon.h"
#include "appmenu.h"

static Panel *current_panel[PANEL_NUMBER_OF_SIDES];

/* Widget which holds the selection when we have it */
static GtkWidget *selection_invisible = NULL;
static guint losing_selection = 0;	/* > 0 => Don't send events */

struct _Panel {
	GtkWidget	*window;
	int		height;
	GtkAdjustment	*adj;		/* Scroll position of the bar */
	PanelSide	side;
	guchar		*name;		/* Leaf name */

	GtkWidget	*before;	/* Icons at the left/top end */
	GtkWidget	*after;		/* Icons at the right/bottom end */

	GtkWidget	*gap;		/* Event box between sides */
};

/* NULL => Not loading a panel */
static Panel *loading_panel = NULL;

/* Static prototypes */
static int panel_delete(GtkWidget *widget, GdkEvent *event, Panel *panel);
static void panel_destroyed(GtkWidget *widget, Panel *panel);
static char *pan_from_file(guchar *line);
static void icon_destroyed(Icon *icon);
static gint icon_button_release(GtkWidget *widget,
			        GdkEventButton *event,
			        Icon *icon);
static gint icon_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      Icon *icon);
static void reposition_panel(Panel *panel);
static void icon_set_selected(Icon *icon, gboolean selected);
static gint expose_icon(GtkWidget *widget,
			GdkEventExpose *event,
			Icon *icon);
static gint draw_icon(GtkWidget *widget,
			GdkRectangle *badarea,
			Icon *icon);
static void popup_panel_menu(GdkEventButton *event,
				Panel *panel,
				Icon *icon);
static gint panel_button_release(GtkWidget *widget,
			      GdkEventButton *event,
			      Panel *panel);
static gint panel_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      Panel *panel);
static void size_icon(Icon *icon);
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
static void menu_closed(GtkWidget *widget);
static void remove_items(gpointer data, guint action, GtkWidget *widget);
static void edit_icon(gpointer data, guint action, GtkWidget *widget);
static void show_location(gpointer data, guint action, GtkWidget *widget);
static void show_help(gpointer data, guint action, GtkWidget *widget);
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
static void panel_save(Panel *panel);
static GList *get_widget_list(Panel *panel);
static GtkWidget *make_insert_frame(Panel *panel);
static gboolean enter_icon(GtkWidget *widget,
			   GdkEventCrossing *event,
			   Icon *icon);
static gint lose_selection(GtkWidget *widget, GdkEventSelection *event);
static void selection_get(GtkWidget *widget, 
		       GtkSelectionData *selection_data,
		       guint      info,
		       guint      time,
		       gpointer   data);
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


static GtkItemFactoryEntry menu_def[] = {
{N_("ROX-Filer Help"),		NULL,   menu_rox_help, 0, NULL},
{N_("ROX-Filer Options..."),	NULL,   menu_show_options, 0, NULL},
{N_("Open Home Directory"),	NULL,	open_home, 0, NULL},
{"",				NULL,	NULL, 0, "<Separator>"},
{N_("Edit Icon"),  		NULL,  	edit_icon, 0, NULL},
{N_("Show Location"),  		NULL,  	show_location, 0, NULL},
{N_("Show Help"),    		NULL,  	show_help, 0, NULL},
{N_("Remove Item(s)"),		NULL,	remove_items, 0, NULL},
};

static GtkWidget *panel_menu = NULL;
static gboolean tmp_icon_selected = FALSE;

/* A list of selected Icons */
static GList *panel_selection = NULL;

static GtkWidget *dnd_highlight = NULL; /* (stops flickering) */

/* When sliding the panel, records where the panel was before */
static gint slide_from_value = 0;


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void panel_init(void)
{
	GtkTargetEntry 	target_table[] =
	{
		{"text/uri-list", 0, TARGET_URI_LIST},
		{"STRING", 0, TARGET_STRING},
	};

	panel_menu = menu_create(menu_def,
				 sizeof(menu_def) / sizeof(*menu_def),
				 "<panel>");
	gtk_signal_connect(GTK_OBJECT(panel_menu), "unmap_event",
			GTK_SIGNAL_FUNC(menu_closed), NULL);

	selection_invisible = gtk_invisible_new();

	gtk_signal_connect(GTK_OBJECT(selection_invisible),
			"selection_clear_event",
			GTK_SIGNAL_FUNC(lose_selection),
			NULL);

	gtk_signal_connect(GTK_OBJECT(selection_invisible),
			"selection_get",
			GTK_SIGNAL_FUNC(selection_get), NULL);

	gtk_selection_add_targets(selection_invisible,
			GDK_SELECTION_PRIMARY,
			target_table,
			sizeof(target_table) / sizeof(*target_table));
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

/* See if the file the icon points to has changed. Update the icon
 * if so.
 */
void panel_icon_may_update(Icon *icon)
{
	MaskedPixmap	*image = icon->item.image;
	int		flags = icon->item.flags;

	pixmap_ref(image);
	mount_update(FALSE);
	dir_restat(icon->path, &icon->item, FALSE);

	if (icon->item.image != image || icon->item.flags != flags)
	{
		size_icon(icon);
		gtk_widget_queue_clear(icon->widget);
	}

	pixmap_unref(image);
}



/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* User has tried to close the panel via the window manager - confirm */
static int panel_delete(GtkWidget *widget, GdkEvent *event, Panel *panel)
{
	/* TODO: We can open lots of these - very irritating! */
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
			reposition_panel(current_panel[PANEL_RIGHT]);
		if (current_panel[PANEL_LEFT])
			reposition_panel(current_panel[PANEL_LEFT]);
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
	icon->type = ICON_PANEL;
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

	size_icon(icon);

	if (!loading_panel)
		panel_save(panel);
		
	gtk_widget_show(widget);
}

/* Set the size of the widget for this icon */
static void size_icon(Icon *icon)
{
	int	im_height;
	GdkFont	*font = icon->widget->style->font;
	int	width, height;

	if (icon->socket)
		return;

	im_height = MIN(PIXMAP_HEIGHT(icon->item.image->pixmap),
					ICON_HEIGHT);

	width = PIXMAP_WIDTH(icon->item.image->pixmap);
	width = MAX(width, icon->item.name_width);
	height = font->ascent + font->descent + 6 + im_height;

	gtk_widget_set_usize(icon->widget, width + 4, height);
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

	gdk_window_get_size(widget->window, &width, &height);

	area.x = 0;
	area.width = width;
	area.height = PIXMAP_HEIGHT(icon->item.image->pixmap);
	area.y = height - font->ascent - font->descent - 6 - area.height;
	
	draw_large_icon(widget, &area, &icon->item, icon->selected);

	text_x = (area.width - icon->item.name_width) >> 1;

	text_y = height - font->descent - 4;

	draw_string(widget,
			font,
			icon->item.leafname, 
			MAX(0, text_x),
			text_y,
			icon->item.name_width,
			area.width,
			icon->selected, TRUE);

	return TRUE;
}

static void icon_destroyed(Icon *icon)
{
	icon_unhash_path(icon);

	if (g_list_find(panel_selection, icon))
	{
		panel_selection = g_list_remove(panel_selection, icon);

		if (!panel_selection)
			gtk_selection_owner_set(NULL,
				GDK_SELECTION_PRIMARY,
				gdk_event_get_time(gtk_get_current_event()));
	}
	
	dir_item_clear(&icon->item);
	g_free(icon->path);
	g_free(icon->src_path);
	g_free(icon);
}

/* Clear everything, except 'select', which is selected.
 * If select is NULL, unselects everything.
 */
static void panel_clear_selection(Icon *select)
{
	GList	*to_clear, *next;
	
	if (select)
		icon_set_selected(select, TRUE);

	to_clear = g_list_copy(panel_selection), select;

	if (select)
		to_clear = g_list_remove(g_list_copy(panel_selection), select);
		
	for (next = to_clear; next; next = next->next)
		icon_set_selected((Icon *) next->data, FALSE);

	g_list_free(to_clear);
}

static void icon_set_selected(Icon *icon, gboolean selected)
{
	gboolean clear = FALSE;

	g_return_if_fail(icon != NULL);

	if (icon->selected == selected)
		return;
	
	/* When selecting an icon on another panel, we need to unselect
	 * everything else afterwards.
	 */
	if (selected && panel_selection)
	{
		Icon *current = (Icon *) panel_selection->data;

		if (icon->panel != current->panel)
			clear = TRUE;
	}

	icon->selected = selected;
	gtk_widget_queue_clear(icon->widget);

	if (selected)
	{
		panel_selection = g_list_prepend(panel_selection, icon);
		if (losing_selection == 0 && !panel_selection->next)
		{
			/* Grab selection */
			gtk_selection_owner_set(selection_invisible,
				GDK_SELECTION_PRIMARY,
				gdk_event_get_time(gtk_get_current_event()));
		}
	}
	else
	{
		panel_selection = g_list_remove(panel_selection, icon);
		if (losing_selection == 0 && !panel_selection)
		{
			/* Release selection */
			gtk_selection_owner_set(NULL,
				GDK_SELECTION_PRIMARY,
				gdk_event_get_time(gtk_get_current_event()));
		}
	}

	if (clear)
		panel_clear_selection(icon);
}

/* icon may be NULL if the event is on the background */
static void perform_action(Panel *panel, Icon *icon, GdkEventButton *event)
{
	GtkWidget	*widget = icon ? icon->widget : NULL;
	BindAction	action;
	
	action = bind_lookup_bev(icon ? BIND_PANEL_ICON : BIND_PANEL, event);

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
			popup_panel_menu(event, panel, icon);
			break;
		case ACT_MOVE_ICON:
			dnd_motion_start(MOTION_REPOSITION);
			break;
		case ACT_PRIME_AND_SELECT:
			if (!icon->selected)
				panel_clear_selection(icon);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_AND_TOGGLE:
			icon_set_selected(icon, !icon->selected);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_FOR_DND:
			wink_widget(widget);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_TOGGLE_SELECTED:
			icon_set_selected(icon, !icon->selected);
			break;
		case ACT_SELECT_EXCL:
			icon_set_selected(icon, TRUE);
			break;
		case ACT_SLIDE_CLEAR_PANEL:
			panel_clear_selection(NULL);
			/* (no break) */
		case ACT_SLIDE_PANEL:
			dnd_motion_grab_pointer();
			slide_from_value = panel->adj->value;
			dnd_motion_start(MOTION_REPOSITION);
			break;
		case ACT_IGNORE:
			break;
		case ACT_CLEAR_SELECTION:
			panel_clear_selection(NULL);
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

static void reposition_panel(Panel *panel)
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
			reposition_panel(current_panel[PANEL_RIGHT]);
		if (current_panel[PANEL_LEFT])
			reposition_panel(current_panel[PANEL_LEFT]);
	}
}

static void panel_position_menu(GtkMenu *menu, gint *x, gint *y, gpointer data)
{
	int		*pos = (int *) data;
	GtkRequisition 	requisition;

	gtk_widget_size_request(GTK_WIDGET(menu), &requisition);

	if (pos[0] == -1)
		*x = screen_width - MENU_MARGIN - requisition.width;
	else if (pos[0] == -2)
		*x = MENU_MARGIN;
	else
		*x = pos[0] - (requisition.width >> 2);
		
	if (pos[1] == -1)
		*y = screen_height - MENU_MARGIN - requisition.height;
	else if (pos[1] == -2)
		*y = MENU_MARGIN;
	else
		*y = pos[1] - (requisition.height >> 2);

	*x = CLAMP(*x, 0, screen_width - requisition.width);
	*y = CLAMP(*y, 0, screen_height - requisition.height);
}


static void popup_panel_menu(GdkEventButton *event,
				Panel *panel,
				Icon *icon)
{
	int		pos[2];
	PanelSide	side = panel->side;

	appmenu_remove();

	if (icon != NULL)
	{
		Icon *current = panel_selection
				? ((Icon *) panel_selection->data)
				: NULL;
		
		if (current && current->panel != panel)
			panel_clear_selection(icon);

		if (panel_selection == NULL)
		{
			icon_set_selected(icon, TRUE);

			/* Unselect when panel closes */
			tmp_icon_selected = TRUE;
		}
	}

	if (side == PANEL_LEFT)
		pos[0] = -2;
	else if (side == PANEL_RIGHT)
		pos[0] = -1;
	else
		pos[0] = event->x_root;

	if (side == PANEL_TOP)
		pos[1] = -2;
	else if (side == PANEL_BOTTOM)
		pos[1] = -1;
	else
		pos[1] = event->y_root;

	/* Shade Remove Item(s) unless there is a selection */
	menu_set_items_shaded(panel_menu,
		panel_selection ? FALSE : TRUE,
		7, 1);

	/* Shade the Rename/Location/Help items unless exactly one item is
	 * selected.
	 */
	if (panel_selection == NULL || panel_selection->next)
		menu_set_items_shaded(panel_menu, TRUE, 4, 3);
	else
	{
		Icon	*icon = (Icon *) panel_selection->data;

		menu_set_items_shaded(panel_menu, FALSE, 4, 3);

		/* Check for app-specific menu */
		appmenu_add(icon->path, &icon->item, panel_menu);
	}

	gtk_menu_popup(GTK_MENU(panel_menu), NULL, NULL, panel_position_menu,
			(gpointer) pos, event->button, event->time);
}

static void rename_cb(Icon *icon)
{
	size_icon(icon);
	gtk_widget_queue_clear(icon->widget);

	panel_save(icon->panel);
}

static void edit_icon(gpointer data, guint action, GtkWidget *widget)
{
	Icon	*icon;

	if (panel_selection == NULL || panel_selection->next)
	{
		delayed_error(PROJECT,
			_("First, select a single item to edit"));
		return;
	}

	icon = (Icon *) panel_selection->data;
	show_rename_box(icon->widget, icon, rename_cb);
}

static void show_location(gpointer data, guint action, GtkWidget *widget)
{
	Icon	*icon;

	if (panel_selection == NULL || panel_selection->next)
	{
		delayed_error(PROJECT,
			_("Select a single item, then use this to find out "
			  "where it is in the filesystem."));
		return;
	}

	icon = (Icon *) panel_selection->data;

	open_to_show(icon->path);
}

static void show_help(gpointer data, guint action, GtkWidget *widget)
{
	Icon	*icon;

	if (panel_selection == NULL || panel_selection->next)
	{
		delayed_error(PROJECT,
			_("You must select a single item to get help on"));
		return;
	}

	icon = (Icon *) panel_selection->data;
	show_item_help(icon->path, &icon->item);
}
	
static void remove_items(gpointer data, guint action, GtkWidget *widget)
{
	Panel	*panel;
	GList	*next = panel_selection;
	
	if (!next)
	{
		delayed_error(PROJECT,
			_("You must first select some icons to remove"));
		return;
	}

	panel = ((Icon *) next->data)->panel;

	while (next)
	{
		Icon *icon = (Icon *) next->data;

		next = next->next;

		gtk_widget_destroy(icon->widget);
	}

	panel_save(panel);
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
		panel_clear_selection(NULL);
		tmp_icon_selected = FALSE;
	}
}

static void menu_closed(GtkWidget *widget)
{
	if (tmp_icon_selected)
	{
		panel_clear_selection(NULL);
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

static void panel_save(Panel *panel)
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
	panel_icon_may_update(icon);

	return FALSE;
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
		leader = g_strdup_printf("file://%s", our_host_name());

	for (next = panel_selection; next; next = next->next)
	{
		Icon	*icon = (Icon *) next->data;

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
	/* Don't send any events */

	losing_selection++;
	panel_clear_selection(NULL);
	losing_selection--;

	return TRUE;
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
			panel_clear_selection(icon);
			tmp_icon_selected = TRUE;
		}
		else
			icon_set_selected(icon, TRUE);
	}
	
	g_return_if_fail(panel_selection != NULL);

	if (panel_selection->next == NULL)
		drag_one_item(widget, event, icon->path, &icon->item);
	else
	{
		guchar	*uri_list;

		uri_list = create_uri_list(panel_selection);
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
	g_print("Applet died!\n");
	if (GTK_OBJECT_DESTROYED(socket))
		g_print("[ socket already gone ]\n");
	else
	{
		g_print("[ socket still here - killing it ]\n");
		gtk_widget_destroy(socket);
	}

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
	{
		g_print("[ removing socket ]\n");
		return;		/* We're removing the icon... */
	}
		
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
	reposition_panel(panel);
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
