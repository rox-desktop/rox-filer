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

/* icon.c - shared code for the pinboard and panels */

#include "config.h"

#include <string.h>

#include <gtk/gtkinvisible.h>
#include <gtk/gtk.h>
#include <libxml/parser.h>

#include "global.h"

#include "main.h"
#include "gui_support.h"
#include "support.h"
#include "pinboard.h"
#include "panel.h"
#include "icon.h"
#include "diritem.h"
#include "menu.h"
#include "appmenu.h"
#include "dnd.h"
#include "run.h"
#include "infobox.h"
#include "pixmaps.h"
#include "mount.h"
#include "appinfo.h"
#include "type.h"
#include "usericons.h"

typedef void (*RenameFn)(Icon *icon);

static GtkWidget	*icon_menu;		/* The popup icon menu */
static GtkWidget	*icon_file_menu;	/* The file submenu */
static GtkWidget	*icon_file_item;	/* 'File' label */
static GtkWidget	*file_shift_item;	/* 'Shift Open' label */

/* Widget which holds the selection when we have it */
static GtkWidget *selection_invisible = NULL;
static guint losing_selection = 0;	/* > 0 => Don't send events */
gboolean tmp_icon_selected = FALSE;		/* When dragging */

/* A list of selected Icons */
GList *icon_selection = NULL;

/* Each entry is a GList of Icons which have the given pathname.
 * This allows us to update all necessary icons when something changes.
 */
static GHashTable *icons_hash = NULL;

static Icon *menu_icon = NULL;	/* Item clicked if there is no selection */

/* Static prototypes */
static void rename_activate(GtkWidget *dialog);
static void menu_closed(GtkWidget *widget);
static void panel_position_menu(GtkMenu *menu, gint *x, gint *y,
#ifdef GTK2
		gboolean  *push_in,
#endif
		gpointer data);
static gint lose_selection(GtkWidget *widget, GdkEventSelection *event);
static void selection_get(GtkWidget *widget, 
		       GtkSelectionData *selection_data,
		       guint      info,
		       guint      time,
		       gpointer   data);
static void remove_items(gpointer data, guint action, GtkWidget *widget);
static void file_op(gpointer data, guint action, GtkWidget *widget);
static void show_rename_box(GtkWidget *widget, Icon *icon, RenameFn callback);

enum {
	ACTION_SHIFT,
	ACTION_HELP,
	ACTION_INFO,
	ACTION_RUN_ACTION,
	ACTION_SET_ICON,
	ACTION_EDIT,
	ACTION_LOCATION,
};

#undef N_
#define N_(x) x
static GtkItemFactoryEntry menu_def[] = {
{N_("ROX-Filer"),		NULL, NULL, 0, "<Branch>"},
{">" N_("Help"),		NULL, menu_rox_help, 0, NULL},
{">" N_("Options..."),		NULL, menu_show_options, 0, NULL},
{">" N_("Home Directory"),	NULL, open_home, 0, NULL},
{N_("File"),			NULL, NULL, 0, "<Branch>"},
{">" N_("Shift Open"),   	NULL, file_op, ACTION_SHIFT, NULL},
{">" N_("Help"),    		NULL, file_op, ACTION_HELP, NULL},
{">" N_("Info"),    		NULL, file_op, ACTION_INFO, NULL},
{">" N_("Set Run Action..."),	NULL, file_op, ACTION_RUN_ACTION, NULL},
{">" N_("Set Icon..."),		NULL, file_op, ACTION_SET_ICON, NULL},
{N_("Edit Item"),  		NULL, file_op, ACTION_EDIT, NULL},
{N_("Show Location"),  		NULL, file_op, ACTION_LOCATION, NULL},
{N_("Remove Item(s)"),		NULL, remove_items, 0, NULL},
};

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void icon_init(void)
{
	guchar		*tmp;
	GList		*items;
	GtkItemFactory	*item_factory;
	GtkTargetEntry 	target_table[] =
	{
		{"text/uri-list", 0, TARGET_URI_LIST},
		{"STRING", 0, TARGET_STRING},
	};

	icons_hash = g_hash_table_new(g_str_hash, g_str_equal);

	item_factory = menu_create(menu_def,
				sizeof(menu_def) / sizeof(*menu_def),
				 "<icon>", NULL);

	tmp = g_strconcat("<icon>/", _("File"), NULL);
	icon_menu = gtk_item_factory_get_widget(item_factory, "<icon>");
	icon_file_menu = gtk_item_factory_get_widget(item_factory, tmp);
	g_free(tmp);

	/* File '' label... */
	items = gtk_container_children(GTK_CONTAINER(icon_menu));
	icon_file_item = GTK_BIN(g_list_nth(items, 1)->data)->child;
	g_list_free(items);

	/* Shift Open... label */
	items = gtk_container_children(GTK_CONTAINER(icon_file_menu));
	file_shift_item = GTK_BIN(items->data)->child;
	g_list_free(items);

	gtk_signal_connect(GTK_OBJECT(icon_menu), "unmap_event",
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

	tooltips = gtk_tooltips_new();
}

/* The icons_hash table allows us to convert from a path to a list
 * of icons that use that path.
 * Add this icon to the list for its path.
 */
void icon_hash_path(Icon *icon)
{
	GList	*list;

	g_return_if_fail(icon != NULL);

	/* g_print("[ hashing '%s' ]\n", icon->path); */

	list = g_hash_table_lookup(icons_hash, icon->path);
	list = g_list_prepend(list, icon);
	g_hash_table_insert(icons_hash, icon->path, list);
}

/* Remove this icon from the icons_hash table */
void icon_unhash_path(Icon *icon)
{
	GList	*list;

	g_return_if_fail(icon != NULL);

	/* g_print("[ unhashing '%s' ]\n", icon->path); */
	
	list = g_hash_table_lookup(icons_hash, icon->path);
	g_return_if_fail(list != NULL);

	list = g_list_remove(list, icon);

	/* Remove it first; the hash key may have changed address */
	g_hash_table_remove(icons_hash, icon->path);
	if (list)
		g_hash_table_insert(icons_hash,
				((Icon *) list->data)->path, list);
}

/* Called when the pointer moves over the icon */
void icon_may_update(Icon *icon)
{
	MaskedPixmap	*image;
	int		flags;

	g_return_if_fail(icon != NULL);

	image = icon->item->image;
	flags = icon->item->flags;

	pixmap_ref(image);
	mount_update(FALSE);
	diritem_restat(icon->path, icon->item);

	if (icon->item->image != image || icon->item->flags != flags)
	{
		/* Appearance changed; need to redraw */
		if (icon->panel)
			gtk_widget_queue_clear(icon->widget);
		else
			pinboard_reshape_icon(icon);
	}

	pixmap_unref(image);
}

/* If path is on an icon then it may have changed... check! */
void icons_may_update(const gchar *path)
{
	GList	*affected;

	affected = g_hash_table_lookup(icons_hash, path);

	for (; affected; affected = affected->next)
		icon_may_update((Icon *) affected->data);
}

typedef struct _CheckData CheckData;
struct _CheckData {
	const gchar *path;
	gboolean    found;
};

static void check_has(gpointer key, GList *icons, CheckData *check)
{
	Icon	*icon;
	
	g_return_if_fail(icons != NULL);
	
	icon = icons->data;

	if (is_sub_dir(icon->path, check->path))
		check->found = TRUE;
}

/* Set the tooltip AND hide/show the label for panel icons */
void icon_set_tip(Icon *icon)
{
	GtkWidget	*widget;
	XMLwrapper	*ai;
	xmlNode 	*node;

	g_return_if_fail(icon != NULL);

	widget = icon->panel ? icon->widget : icon->win;

	if (icon->panel && icon->label)
	{
		if (panel_want_show_text(icon))
			gtk_widget_show(icon->label);
		else
			gtk_widget_hide(icon->label);
	}

	if (icon->panel && icon->socket)
		ai = NULL;
	else
		ai = appinfo_get(icon->path, icon->item);

	if (ai && ((node = appinfo_get_section(ai, "Summary"))))
	{
		guchar *str;
		str = xmlNodeListGetString(node->doc,
				node->xmlChildrenNode, 1);
		if (str)
		{
			gtk_tooltips_set_tip(tooltips, widget, str, NULL);
			g_free(str);
		}
	}
	else if (icon->panel && (!panel_want_show_text(icon)) && !icon->socket)
	{
		gtk_tooltips_set_tip(tooltips, widget,
				icon->item->leafname, NULL);
	}
	else
		gtk_tooltips_set_tip(tooltips, widget, NULL, NULL);

	if (ai)
		xml_cache_unref(ai);
}

/* Returns TRUE if any icon links to this item (or any item inside
 * this item).
 */
gboolean icons_require(const gchar *path)
{
	CheckData	check;

	/* g_print("[ icons_require(%s)? ]\n", path); */

	check.path = path;
	check.found = FALSE;
	g_hash_table_foreach(icons_hash, (GHFunc) check_has, &check);

	return check.found;
}

/* Callback to update icons of a certain path */
static void update_icons(gpointer key, GList *icons, gpointer data)
{
	icons_may_update((guchar *) key);
}

/* Check all icons to see if they have been updated */
void update_all_icons(void)
{
	g_hash_table_foreach(icons_hash, (GHFunc) update_icons, NULL);
}

/* Show the menu for this panel (NULL for the pinboard).
 * icon is the icon that was clicked on, if any.
 */
void icon_show_menu(GdkEventButton *event, Icon *icon, Panel *panel)
{
	int		pos[3];

	pos[0] = event->x_root;
	pos[1] = event->y_root;
	pos[2] = 1;

	appmenu_remove();

	menu_icon = icon;

	/* Shade Remove Item(s) unless there is a selection */
	menu_set_items_shaded(icon_menu,
		(icon_selection || menu_icon) ? FALSE : TRUE,
		4, 1);

	menu_show_shift_action(file_shift_item, icon ? icon->item : NULL,
			FALSE);

	/* Shade the File/Edit/Show items unless an item was clicked */
	if (icon)
	{
		guchar *tmp;

		menu_set_items_shaded(icon_menu, FALSE, 1, 3);
		menu_set_items_shaded(icon_file_menu, FALSE, 0, 5);
		if (!can_set_run_action(icon->item))
			menu_set_items_shaded(icon_file_menu, TRUE, 3, 1);

		tmp = g_strdup_printf("%s '%s'",
				basetype_name(icon->item),
				icon->item->leafname);
		gtk_label_set_text(GTK_LABEL(icon_file_item), tmp);
		g_free(tmp);

		/* Check for app-specific menu */
		appmenu_add(icon->path, icon->item, icon_menu);
	}
	else
	{
		menu_set_items_shaded(icon_menu, TRUE, 1, 3);
		menu_set_items_shaded(icon_file_menu, TRUE, 0, 5);
		gtk_label_set_text(GTK_LABEL(icon_file_item), _("Nothing"));
	}

	if (panel)
	{
		PanelSide side = panel->side;

		if (side == PANEL_LEFT)
			pos[0] = -2;
		else if (side == PANEL_RIGHT)
			pos[0] = -1;

		if (side == PANEL_TOP)
			pos[1] = -2;
		else if (side == PANEL_BOTTOM)
			pos[1] = -1;
	}

	gtk_menu_popup(GTK_MENU(icon_menu), NULL, NULL,
			panel ? panel_position_menu : position_menu,
			(gpointer) pos, event->button, event->time);
}

void icon_set_selected(Icon *icon, gboolean selected)
{
	gboolean clear = FALSE;

	g_return_if_fail(icon != NULL);

	if (icon->selected == selected)
		return;
	
	/* When selecting an icon on another panel, we need to unselect
	 * everything else afterwards.
	 */
	if (selected && icon_selection)
	{
		Icon *current = (Icon *) icon_selection->data;

		if (icon->panel != current->panel)
			clear = TRUE;
	}

	icon->selected = selected;

	gtk_widget_queue_clear(icon->widget);

	if (selected)
	{
		icon_selection = g_list_prepend(icon_selection, icon);
		if (losing_selection == 0 && !icon_selection->next)
		{
			/* Grab selection */
			gtk_selection_owner_set(selection_invisible,
				GDK_SELECTION_PRIMARY,
				gdk_event_get_time(gtk_get_current_event()));
		}
	}
	else
	{
		icon_selection = g_list_remove(icon_selection, icon);
		if (losing_selection == 0 && !icon_selection)
		{
			/* Release selection */
			gtk_selection_owner_set(NULL,
				GDK_SELECTION_PRIMARY,
				gdk_event_get_time(gtk_get_current_event()));
		}
	}

	if (clear)
		icon_select_only(icon);
}

/* Clear everything, except 'select', which is selected.
 * If select is NULL, unselects everything.
 */
void icon_select_only(Icon *select)
{
	GList	*to_clear, *next;
	
	if (select)
		icon_set_selected(select, TRUE);

	to_clear = g_list_copy(icon_selection);

	if (select)
		to_clear = g_list_remove(to_clear, select);
		
	for (next = to_clear; next; next = next->next)
		icon_set_selected((Icon *) next->data, FALSE);

	g_list_free(to_clear);
}

/* XXX: Under Gtk+ 2.0, can this get called twice? */
void icon_destroyed(Icon *icon)
{
	g_return_if_fail(icon != NULL);

	if (icon == menu_icon)
		menu_icon = NULL;

#ifdef GTK2
	if (icon->layout)
	{
		g_object_unref(G_OBJECT(icon->layout));
		icon->layout = NULL;
	}
#endif

	icon_unhash_path(icon);

	pinboard_wink_item(NULL, FALSE);

	if (g_list_find(icon_selection, icon))
	{
		icon_selection = g_list_remove(icon_selection, icon);

		if (!icon_selection)
			gtk_selection_owner_set(NULL,
				GDK_SELECTION_PRIMARY,
				gdk_event_get_time(gtk_get_current_event()));
	}

	if (icon->panel == NULL)
	{
		gdk_pixmap_unref(icon->mask);
		if (current_pinboard)
			current_pinboard->icons =
				g_list_remove(current_pinboard->icons, icon);
	}
	
	diritem_free(icon->item);
	g_free(icon->path);
	g_free(icon->src_path);
	g_free(icon);
}

static void set_tip(gpointer key, GList *icons, gpointer data)
{
	for (; icons; icons = icons->next)
	{
		Icon	*icon = (Icon *) icons->data;
		
		icon_set_tip(icon);
	}
}

void icons_update_tip(void)
{
	g_hash_table_foreach(icons_hash, (GHFunc) set_tip, NULL);
}

/* Removes trailing / chars and converts a leading '~/' (if any) to
 * the user's home dir. g_free() the result.
 */
gchar *icon_convert_path(const gchar *path)
{
	guchar		*retval;
	int		path_len;

	g_return_val_if_fail(path != NULL, NULL);

	path_len = strlen(path);
	while (path_len > 1 && path[path_len - 1] == '/')
		path_len--;
	
	retval = g_strndup(path, path_len);

	if (path[0] == '~' && (path[1] == '\0' || path[1] == '/'))
	{
		guchar *tmp = retval;

		retval = g_strconcat(home_dir, retval + 1, NULL);
		g_free(tmp);
	}

	return retval;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void rename_activate(GtkWidget *dialog)
{
	GtkWidget *entry, *src;
	RenameFn callback;
	Icon	*icon;
	const guchar	*new_name, *new_src;
	
	entry = gtk_object_get_data(GTK_OBJECT(dialog), "new_name");
	icon = gtk_object_get_data(GTK_OBJECT(dialog), "callback_icon");
	callback = gtk_object_get_data(GTK_OBJECT(dialog), "callback_fn");
	src = gtk_object_get_data(GTK_OBJECT(dialog), "new_path");

	g_return_if_fail(callback != NULL &&
			 entry != NULL &&
			 src != NULL &&
			 icon != NULL);

	new_name = gtk_entry_get_text(GTK_ENTRY(entry));
	new_src = gtk_entry_get_text(GTK_ENTRY(src));
	
	if (*new_name == '\0')
		report_error(
			_("The label must contain at least one character!"));
	else if (*new_src == '\0')
		report_error(
			_("The location must contain at least one character!"));
	else
	{
#ifndef GTK2
		GdkFont	*font = icon->widget->style->font;
#endif
		
		g_free(icon->item->leafname);
		g_free(icon->src_path);

		icon->src_path = g_strdup(new_src);

		icon_unhash_path(icon);
		g_free(icon->path);
		icon->path = icon_convert_path(new_src);
		icon_hash_path(icon);
		
		icon->item->leafname = g_strdup(new_name);
#ifndef GTK2
		icon->name_width = gdk_string_measure(font, new_name);
#endif
		/* XXX: Set name_width in size_and_shape? */

		diritem_restat(icon->path, icon->item);

		callback(icon);
		gtk_widget_destroy(dialog);
	}
}

static void menu_closed(GtkWidget *widget)
{
	appmenu_remove();
	menu_icon = NULL;
}

static void panel_position_menu(GtkMenu *menu, gint *x, gint *y,
#ifdef GTK2
		gboolean  *push_in,
#endif
		gpointer data)
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

#ifdef GTK2
	*push_in = FALSE;
#endif
}

/* Called when another application takes the selection away from us */
static gint lose_selection(GtkWidget *widget, GdkEventSelection *event)
{
	/* Don't send any events */

	losing_selection++;
	icon_select_only(NULL);
	losing_selection--;

	return TRUE;
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
		leader = g_strdup_printf("file://%s", our_host_name_for_dnd());

	for (next = icon_selection; next; next = next->next)
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

static void rename_cb(Icon *icon)
{
	if (icon->panel)
	{
		gtk_widget_queue_clear(icon->widget);
		panel_icon_renamed(icon);
		panel_save(icon->panel);
	}
	else
	{
		pinboard_reshape_icon(icon);
		pinboard_save();
	}
}

static void remove_items(gpointer data, guint action, GtkWidget *widget)
{
	Panel	*panel;
	GList	*next;

	if (menu_icon)
		icon_set_selected(menu_icon, TRUE);
	
	next = icon_selection;
	
	if (!next)
	{
		delayed_error(
			_("You must first select some items to remove"));
		return;
	}

	panel = ((Icon *) next->data)->panel;

	if (panel)
	{
		while (next)
		{
			Icon *icon = (Icon *) next->data;

			next = next->next;

			gtk_widget_destroy(icon->widget);
		}

		panel_save(panel);
	}
	else
	{
		while (next)
		{
			Icon *icon = (Icon *) next->data;

			next = next->next;

			gtk_widget_destroy(icon->win);
		}
		
		pinboard_save();
	}
}

static void file_op(gpointer data, guint action, GtkWidget *widget)
{
	if (!menu_icon)
	{
		delayed_error(_("You must open the menu over an item"));
		return;
	}

	switch (action)
	{
		case ACTION_SHIFT:
			run_diritem(menu_icon->path, menu_icon->item,
					NULL, NULL, TRUE);
			break;
		case ACTION_EDIT:
			show_rename_box(menu_icon->widget,
					menu_icon, rename_cb);
			break;
		case ACTION_LOCATION:
			open_to_show(menu_icon->path);
			break;
		case ACTION_HELP:
			show_item_help(menu_icon->path, menu_icon->item);
			break;
		case ACTION_INFO:
			infobox_new(menu_icon->path);
			break;
		case ACTION_RUN_ACTION:
			if (can_set_run_action(menu_icon->item))
				type_set_handler_dialog(
						menu_icon->item->mime_type);
			else
				report_error(
				_("You can only set the run action for a "
				"regular file"));
			break;
		case ACTION_SET_ICON:
			icon_set_handler_dialog(menu_icon->item,
						menu_icon->path);
			break;
	}
}

/* Opens a box allowing the user to change the name of a pinned icon.
 * If 'widget' is destroyed then the box will close.
 * If the user chooses OK then the callback is called once the icon's
 * name, src_path and path fields have been updated and the item field
 * restatted.
 */
static void show_rename_box(GtkWidget *widget, Icon *icon, RenameFn callback)
{
	GtkWidget	*dialog, *hbox, *vbox, *label, *entry, *button;

	dialog = gtk_window_new(GTK_WINDOW_DIALOG);
#ifdef GTK2
	gtk_window_set_type_hint(GTK_WINDOW(dialog),
			GDK_WINDOW_TYPE_HINT_DIALOG);
#endif
	gtk_window_set_title(GTK_WINDOW(dialog), _("Edit Item"));
	gtk_container_set_border_width(GTK_CONTAINER(dialog), 10);

	vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(dialog), vbox);
	
	label = gtk_label_new(_("Clicking the icon opens:"));
	gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, TRUE, 0);

	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(vbox), entry, TRUE, FALSE, 2);
	gtk_entry_set_text(GTK_ENTRY(entry), icon->src_path);
	gtk_object_set_data(GTK_OBJECT(dialog), "new_path", entry);
	gtk_signal_connect_object(GTK_OBJECT(entry), "activate",
			GTK_SIGNAL_FUNC(rename_activate), GTK_OBJECT(dialog));

	gtk_box_pack_start(GTK_BOX(vbox), gtk_hseparator_new(), TRUE, TRUE, 2);

	label = gtk_label_new(_("The text displayed under the icon is:"));
	gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, TRUE, 0);
	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(vbox), entry, TRUE, FALSE, 2);
	gtk_entry_set_text(GTK_ENTRY(entry), icon->item->leafname);
	gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
	gtk_widget_grab_focus(entry);
	gtk_object_set_data(GTK_OBJECT(dialog), "new_name", entry);
	gtk_signal_connect_object(GTK_OBJECT(entry), "activate",
			GTK_SIGNAL_FUNC(rename_activate), GTK_OBJECT(dialog));
	
	gtk_signal_connect_object_while_alive(GTK_OBJECT(widget),
			"destroy",
			GTK_SIGNAL_FUNC(gtk_widget_destroy),
			GTK_OBJECT(dialog));

	gtk_object_set_data(GTK_OBJECT(dialog), "callback_icon", icon);
	gtk_object_set_data(GTK_OBJECT(dialog), "callback_fn", callback);

	gtk_box_pack_start(GTK_BOX(vbox), gtk_hseparator_new(), TRUE, TRUE, 2);

	hbox = gtk_hbox_new(TRUE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	button = gtk_button_new_with_label(_("OK"));
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_window_set_default(GTK_WINDOW(dialog), button);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(rename_activate), GTK_OBJECT(dialog));
	
	button = gtk_button_new_with_label(_("Cancel"));
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_destroy),
			GTK_OBJECT(dialog));

	gtk_widget_show_all(dialog);
}
