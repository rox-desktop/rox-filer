/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@users.sourceforge.net>.
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

#include <gtk/gtk.h>

#include "global.h"

#include "panel.h"
#include "choices.h"
#include "main.h"
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

#define MAX_ICON_HEIGHT 42

static Panel *current_panel[PANEL_NUMBER_OF_SIDES];

typedef struct _PanelIcon PanelIcon;

struct _Panel {
	GtkWidget	*window;
	PanelSide	side;
	guchar		*name;		/* Leaf name */

	GtkWidget	*before;	/* Icons at the left/top end */
	GtkWidget	*after;		/* Icons at the right/bottom end */
};

struct _PanelIcon {
	GtkWidget	*widget;	/* The drawing area for the icon */
	Panel		*panel;		/* Panel containing this icon */
	gboolean	selected;
	guchar		*path;
	DirItem		item;
};

/* NULL => Not loading a panel */
static Panel *loading_panel = NULL;

/* Static prototypes */
static int panel_delete(GtkWidget *widget, GdkEvent *event, Panel *panel);
static void panel_destroyed(GtkWidget *widget, Panel *panel);
static char *pan_from_file(guchar *line);
static void icon_destroyed(PanelIcon *icon);
static gint icon_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      PanelIcon *icon);
static void reposition_panel(Panel *panel);
static void icon_set_selected(PanelIcon *icon, gboolean selected);
static gint expose_icon(GtkWidget *widget,
			GdkEventExpose *event,
			PanelIcon *icon);
static gint draw_icon(GtkWidget *widget,
			GdkRectangle *badarea,
			PanelIcon *icon);
static void popup_panel_menu(GdkEventButton *event,
				Panel *panel,
				PanelIcon *icon);
static void xterm_here(gpointer data, guint action, GtkWidget *widget);
static gint panel_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      Panel *panel);
static void icon_may_update(PanelIcon *icon);
static void size_icon(PanelIcon *icon);
static void drag_set_panel_dest(PanelIcon *icon);
static void add_uri_list(GtkWidget          *widget,
                         GdkDragContext     *context,
                         gint               x,
                         gint               y,
                         GtkSelectionData   *selection_data,
                         guint              info,
                         guint32            time,
			 Panel		    *panel);
static void panel_add_item(Panel *panel, guchar *path, gboolean after);
static void menu_closed(GtkWidget *widget);
static void remove_items(gpointer data, guint action, GtkWidget *widget);
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    PanelIcon		*icon);
static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       PanelIcon	*icon);
static void panel_save(Panel *panel);
static GList *get_widget_list(Panel *panel);


static GtkItemFactoryEntry menu_def[] = {
{N_("ROX-Filer Help"),		NULL,   menu_rox_help, 0, NULL},
{N_("ROX-Filer Options..."),	NULL,   menu_show_options, 0, NULL},
{N_("Xterm in ~"),		NULL,	xterm_here, 0, NULL},
{"",				NULL,	NULL, 0, "<Separator>"},
{N_("Show Help"),    		NULL,  	NULL, 0, NULL},
{N_("Remove Item(s)"),		NULL,	remove_items, 0, NULL},
};

static GtkWidget *panel_menu = NULL;
static gboolean tmp_icon_selected = FALSE;

/* A list of selected PanelIcons */
static GList *panel_selection = NULL;

/* Each entry is a GList of PanelIcons which have the given pathname.
 * This allows us to update all necessary icons when something changes.
 */
static GHashTable *icons_hash = NULL;

static GtkWidget *dnd_highlight = NULL; /* (stops flickering) */


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void panel_init(void)
{
	panel_menu = menu_create(menu_def,
				 sizeof(menu_def) / sizeof(*menu_def),
				 "<panel>");
	gtk_signal_connect(GTK_OBJECT(panel_menu), "unmap_event",
			GTK_SIGNAL_FUNC(menu_closed), NULL);
	icons_hash = g_hash_table_new(g_str_hash, g_str_equal);
}

/* 'name' may be NULL or "" to remove the panel */
Panel *panel_new(guchar *name, PanelSide side)
{
	guchar	*load_path;
	Panel	*panel;
	GtkWidget	*frame, *box;
	GtkTargetEntry 	target_table[] = {
		{"text/uri-list", 0, TARGET_URI_LIST},
	};

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
	panel->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_wmclass(GTK_WINDOW(panel->window), "ROX-Panel", PROJECT);
	gtk_widget_set_events(panel->window, GDK_BUTTON_PRESS_MASK);

	gtk_signal_connect(GTK_OBJECT(panel->window), "delete-event",
			GTK_SIGNAL_FUNC(panel_delete), panel);
	gtk_signal_connect(GTK_OBJECT(panel->window), "destroy",
			GTK_SIGNAL_FUNC(panel_destroyed), panel);
	gtk_signal_connect(GTK_OBJECT(panel->window), "button_press_event",
			GTK_SIGNAL_FUNC(panel_button_press), panel);

	if (strchr(name, '/'))
	{
		if (access(name, F_OK))
			load_path = NULL;	/* File does not (yet) exist */
		else
			load_path = g_strdup(name);
	}
	else
	{
		guchar	*leaf;

		leaf = g_strconcat("pan_", name, NULL);
		load_path = choices_find_path_load(leaf, "ROX-Filer");
		g_free(leaf);
	}

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
	gtk_container_add(GTK_CONTAINER(panel->window), frame);

	if (side == PANEL_TOP || side == PANEL_BOTTOM)
	{
		box = gtk_hbox_new(FALSE, 0);
		panel->before = gtk_hbox_new(FALSE, 4);
		panel->after = gtk_hbox_new(FALSE, 4);
	}
	else
	{
		box = gtk_vbox_new(FALSE, 0);
		panel->before = gtk_vbox_new(FALSE, 4);
		panel->after = gtk_vbox_new(FALSE, 4);
	}

	gtk_container_add(GTK_CONTAINER(frame), box);
	gtk_box_pack_start(GTK_BOX(box), panel->before, FALSE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(box), panel->after, FALSE, TRUE, 0);

	frame = gtk_frame_new(NULL);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 4);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_widget_set_usize(frame, 64, MAX_ICON_HEIGHT);
	gtk_box_pack_start(GTK_BOX(box), frame, TRUE, TRUE, 4);

	gtk_signal_connect(GTK_OBJECT(frame), "drag-data-received",
				GTK_SIGNAL_FUNC(add_uri_list), panel);
	gtk_drag_dest_set(frame,
			  GTK_DEST_DEFAULT_ALL,
			  target_table,
			  sizeof(target_table) / sizeof(*target_table),
			  GDK_ACTION_COPY);

	gtk_widget_realize(panel->window);
	make_panel_window(panel->window->window);
	
	if (load_path)
	{
		loading_panel = panel;
		parse_file(load_path, pan_from_file);
		g_free(load_path);
		loading_panel = NULL;
	}

	reposition_panel(panel);

	number_of_windows++;
	gtk_widget_show_all(panel->window);

	current_panel[side] = panel;
	
	return panel;
}

/* If path is on a panel then it may have changed... check! */
void panel_may_update(guchar *path)
{
	GList	*affected;

	affected = g_hash_table_lookup(icons_hash, path);

	while (affected)
	{
		PanelIcon *icon = (PanelIcon *) affected->data;

		icon_may_update(icon);

		affected = affected->next;
	}
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

	g_free(panel->name);
	g_free(panel);

	if (--number_of_windows < 1)
		gtk_main_quit();
}

/* Called for each line in the config file while loading a new panel */
static char *pan_from_file(guchar *line)
{
	g_return_val_if_fail(line != NULL, NULL);
	g_return_val_if_fail(loading_panel != NULL, NULL);
	
	if (*line == '\0')
		return NULL;

	if (*line == '<')
		panel_add_item(loading_panel, line + 1, FALSE);
	else if (*line == '>')
		panel_add_item(loading_panel, line + 1, TRUE);
	else if (!isspace(*line))
		return _("Lines in a panel file must start with < or >");

	return NULL;
}

/* Add an icon with this path to the panel. If after is TRUE then the
 * icon is added to the right/bottom end of the panel.
 */
static void panel_add_item(Panel *panel, guchar *path, gboolean after)
{
	GtkWidget	*widget;
	PanelIcon	*icon;
	GdkFont		*font;
	guchar		*slash;
	GList		*list;

	slash = strrchr(path, '/');

	widget = gtk_event_box_new();
	gtk_widget_set_events(widget,
			GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);
	
	gtk_box_pack_start(GTK_BOX(after ? panel->after : panel->before),
			widget, FALSE, TRUE, 4);
	
	gtk_widget_realize(widget);
	font = widget->style->font;

	icon = g_new(PanelIcon, 1);
	icon->panel = panel;
	icon->path = g_strdup(path);

	gtk_object_set_data(GTK_OBJECT(widget), "icon", icon);
	
	list = g_hash_table_lookup(icons_hash, icon->path);
	list = g_list_prepend(list, icon);
	g_hash_table_insert(icons_hash, icon->path, list);
	
	icon->widget = widget;
	icon->selected = FALSE;
	dir_stat(path, &icon->item);
	icon->item.leafname = g_strdup(slash ? slash + 1 : path);
	icon->item.name_width = gdk_string_width(font,
						 icon->item.leafname);

	gtk_signal_connect_after(GTK_OBJECT(widget), "draw",
                           GTK_SIGNAL_FUNC(draw_icon), icon);
	gtk_signal_connect_after(GTK_OBJECT(widget), "expose_event",
                           GTK_SIGNAL_FUNC(expose_icon), icon);
	gtk_signal_connect(GTK_OBJECT(widget), "button_press_event",
                           GTK_SIGNAL_FUNC(icon_button_press), icon);

	gtk_signal_connect_object(GTK_OBJECT(widget), "destroy",
			  GTK_SIGNAL_FUNC(icon_destroyed), (gpointer) icon);

	drag_set_panel_dest(icon);

	size_icon(icon);

	if (!loading_panel)
	{
		reposition_panel(panel);
		panel_save(panel);
	}
		
	gtk_widget_show(widget);
}

/* Set the size of the widget for this icon.
 * You should call reposition_panel() sometime after doing this...
 */
static void size_icon(PanelIcon *icon)
{
	GdkFont	*font = icon->widget->style->font;

	gtk_widget_set_usize(icon->widget,
			MAX(icon->item.image->width, icon->item.name_width) + 4,
			font->ascent + font->descent + 8 + MAX_ICON_HEIGHT);
}

static gint expose_icon(GtkWidget *widget,
			GdkEventExpose *event,
			PanelIcon *icon)
{
	return draw_icon(widget, &event->area, icon);
}

static gint draw_icon(GtkWidget *widget, GdkRectangle *badarea, PanelIcon *icon)
{
	GdkFont		*font = widget->style->font;
	GdkRectangle	area;
	int		width, height;
	int		text_x, text_y;

	gdk_window_get_size(widget->window, &width, &height);

	area.x = 0;
	area.y = 0;
	area.width = width;
	area.height = height;
	
	draw_large_icon(widget, &area, &icon->item, icon->selected);

	text_x = (area.width - icon->item.name_width) >> 1;
	text_y = area.height - font->descent - 2;

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

static void icon_destroyed(PanelIcon *icon)
{
	GList		*list;

	list = g_hash_table_lookup(icons_hash, icon->path);
	g_return_if_fail(list != NULL);

	list = g_list_remove(list, icon);

	/* Remove it first; the hash key may have changed address */
	g_hash_table_remove(icons_hash, icon->path);
	if (list)
		g_hash_table_insert(icons_hash,
				((PanelIcon *) list->data)->path, list);

	panel_selection = g_list_remove(panel_selection, icon);
	
	g_print("[ icon_destroyed(%s) ]\n", icon->item.leafname);
	dir_item_clear(&icon->item);
	g_free(icon->path);
	g_free(icon);
}

static void panel_clear_selection(void)
{
	while (panel_selection)
		icon_set_selected((PanelIcon *) panel_selection->data, FALSE);
}

static void icon_set_selected(PanelIcon *icon, gboolean selected)
{
	g_return_if_fail(icon != NULL);

	if (icon->selected == selected)
		return;

	if (selected && panel_selection)
	{
		PanelIcon *current = (PanelIcon *) panel_selection->data;

		if (icon->panel != current->panel)
			panel_clear_selection();
	}

	icon->selected = selected;
	gtk_widget_queue_clear(icon->widget);

	if (selected)
		panel_selection = g_list_prepend(panel_selection, icon);
	else
		panel_selection = g_list_remove(panel_selection, icon);
}

static gint panel_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      Panel *panel)
{
	BindAction	action;

	action = bind_lookup_bev(BIND_PANEL, event);

	switch (action)
	{
		case ACT_CLEAR_SELECTION:
			panel_clear_selection();
			break;
		case ACT_POPUP_MENU:
			popup_panel_menu(event, panel, NULL);
			break;
		case ACT_IGNORE:
			break;
		default:
			g_warning("Unsupported action : %d\n", action);
			break;
	}

	return TRUE;
}

static gint icon_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      PanelIcon *icon)
{
	BindAction	action;

	action = bind_lookup_bev(BIND_PANEL_ICON, event);

	switch (action)
	{
		case ACT_TOGGLE_SELECTED:
			icon_set_selected(icon, !icon->selected);
			break;
		case ACT_SELECT_EXCL:
			icon_set_selected(icon, TRUE);
			break;
		case ACT_OPEN_ITEM:
			wink_widget(icon->widget);
			run_diritem(icon->path, &icon->item, NULL, FALSE);
			break;
		case ACT_EDIT_ITEM:
			wink_widget(icon->widget);
			run_diritem(icon->path, &icon->item, NULL, TRUE);
			break;
		case ACT_POPUP_MENU:
			popup_panel_menu(event, icon->panel, icon);
			break;
		case ACT_IGNORE:
			break;
		default:
			g_warning("Unsupported action : %d\n", action);
			break;
	}

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
		h = screen_height;
	}

	if (side == PANEL_BOTTOM)
		y = screen_height - h;
	if (side == PANEL_RIGHT)
		x = screen_width - w;
	
	gtk_widget_set_uposition(panel->window, x, y);
	gtk_widget_set_usize(panel->window, w, h);
}

static void position_menu(GtkMenu *menu, gint *x, gint *y, gpointer data)
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
				PanelIcon *icon)
{
	int		pos[2];
	PanelSide	side = panel->side;

	if (icon != NULL)
	{
		PanelIcon *current = panel_selection
				? ((PanelIcon *) panel_selection->data)
				: NULL;
		
		if (current && current->panel != panel)
			panel_clear_selection();

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

	gtk_menu_popup(GTK_MENU(panel_menu), NULL, NULL, position_menu,
			(gpointer) pos, event->button, event->time);
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

	panel = ((PanelIcon *) next->data)->panel;

	while (next)
	{
		PanelIcon *icon = (PanelIcon *) next->data;

		next = next->next;

		gtk_widget_destroy(icon->widget);
	}

	panel_save(panel);
}

static void xterm_here(gpointer data, guint action, GtkWidget *widget)
{
	char	*argv[] = {NULL, NULL};

	argv[0] = xterm_here_value;

	if (!spawn_full(argv, home_dir))
		report_error(PROJECT, _("Failed to fork() child process"));
}

/* See if the file the icon points to has changed. Update the icon
 * if so.
 */
static void icon_may_update(PanelIcon *icon)
{
	MaskedPixmap	*image = icon->item.image;
	int		flags = icon->item.flags;

	pixmap_ref(image);
	mount_update(FALSE);
	dir_restat(icon->path, &icon->item);

	if (icon->item.image != image || icon->item.flags != flags)
	{
		size_icon(icon);
		gtk_widget_queue_clear(icon->widget);
		reposition_panel(icon->panel);
	}

	pixmap_unref(image);
}

/* Same as drag_set_dest(), but for panel icons */
static void drag_set_panel_dest(PanelIcon *icon)
{
	GtkObject	*obj = GTK_OBJECT(icon->widget);

	make_drop_target(icon->widget, 0);

	gtk_signal_connect(obj, "drag_motion",
			GTK_SIGNAL_FUNC(drag_motion), icon);
	gtk_signal_connect(obj, "drag_leave",
			GTK_SIGNAL_FUNC(drag_leave), icon);
}

static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    PanelIcon		*icon)
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
	if (o_spring_open == FALSE &&
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
	GSList *uris, *next;
	
	if (!selection_data->data)
		return;

	g_return_if_fail(selection_data->data[selection_data->length] == '\0');

	uris = uri_list_to_gslist(selection_data->data);

	for (next = uris; next; next = next->next)
	{
		guchar	*path;

		path = get_local_path((guchar *) next->data);

		if (path)
			panel_add_item(panel, path, FALSE);
	}

	g_slist_free(uris);
}

static void menu_closed(GtkWidget *widget)
{
	if (tmp_icon_selected)
	{
		panel_clear_selection();
		tmp_icon_selected = FALSE;
	}
}

static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       PanelIcon	*icon)
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
		PanelIcon	*icon;

		icon = gtk_object_get_data(GTK_OBJECT(next->data), "icon");

		if (!icon)
		{
			g_warning("Can't find PanelIcon from widget\n");
			continue;
		}

		g_string_sprintf(tmp, "%c%s\n", side, icon->path);
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
	guchar	*save;
	FILE	*file = NULL;
	guchar	*save_new = NULL;

	g_return_if_fail(panel != NULL);
	
	if (strchr(panel->name, '/'))
		save = panel->name;
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
			gtk_container_children(GTK_CONTAINER(panel->after)),
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
