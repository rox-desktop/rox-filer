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

/* icon.c - abstract base class for pinboard and panel icons.
 *
 * An Icon contains the full pathname of its file and the DirItem for that
 * file. Icons emit the following signals:
 *
 * redraw - the image, details or selection state have changed.
 * update - the name or path has changed.
 * destroy - someone wishes to remove the icon.
 *
 * Note that an icon may be removed without emitting 'destroy'.
 */

#include "config.h"

#include <string.h>
#include <gtk/gtk.h>

#include "global.h"

#include "main.h"
#include "gui_support.h"
#include "support.h"
#include "icon.h"
#include "diritem.h"
#include "menu.h"
#include "appmenu.h"
#include "dnd.h"
#include "run.h"
#include "infobox.h"
#include "pixmaps.h"
#include "mount.h"
#include "type.h"
#include "usericons.h"

static gboolean have_primary = FALSE;	/* We own the PRIMARY selection? */

GtkWidget		*icon_menu;		/* The popup icon menu */
static GtkWidget	*icon_file_menu;	/* The file submenu */
static GtkWidget	*icon_file_item;	/* 'File' label */
static GtkWidget	*file_shift_item;	/* 'Shift Open' label */

/* A list of selected Icons. Every icon in the list is from the same group
 * (eg, you can't have icons from two different panels selected at the
 * same time).
 */
GList *icon_selection = NULL;

/* Each entry is a GList of Icons which have the given pathname.
 * This allows us to update all necessary icons when something changes.
 */
static GHashTable *icons_hash = NULL;	/* path -> [Icon] */

static Icon *menu_icon = NULL;	/* Item clicked if there is no selection */

/* Static prototypes */
static void rename_activate(GtkWidget *dialog);
static void menu_closed(GtkWidget *widget);
static void lose_selection(GtkClipboard *primary, gpointer data);
static void selection_get(GtkClipboard *primary, 
		       GtkSelectionData *selection_data,
		       guint      info,
		       gpointer   data);
static void remove_items(gpointer data, guint action, GtkWidget *widget);
static void file_op(gpointer data, guint action, GtkWidget *widget);
static void show_rename_box(Icon *icon);
static void icon_set_selected_int(Icon *icon, gboolean selected);
static void icon_class_init(gpointer gclass, gpointer data);
static void icon_init(GTypeInstance *object, gpointer gclass);
static void icon_hash_path(Icon *icon);
static void icon_unhash_path(Icon *icon);

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

/* Called when the pointer moves over the icon */
void icon_may_update(Icon *icon)
{
	MaskedPixmap	*image;
	int		flags;

	g_return_if_fail(icon != NULL);

	image = icon->item->image;
	flags = icon->item->flags;

	if (image)
		g_object_ref(image);
	mount_update(FALSE);
	diritem_restat(icon->path, icon->item, NULL);

	if (icon->item->image != image || icon->item->flags != flags)
	{
		/* Appearance changed; need to redraw */
		g_signal_emit_by_name(icon, "redraw");
	}

	if (image)
		g_object_unref(image);
}

/* If path is on an icon then it may have changed... check! */
void icons_may_update(const gchar *path)
{
	GList	*affected;

	if (icons_hash)
	{
		affected = g_hash_table_lookup(icons_hash, path);

		for (; affected; affected = affected->next)
			icon_may_update((Icon *) affected->data);
	}
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

/* Returns TRUE if any icon links to this file (or any file inside
 * this directory). Used to check that it's OK to delete 'path'.
 */
gboolean icons_require(const gchar *path)
{
	CheckData	check;

	if (!icons_hash)
		return FALSE;

	check.path = path;
	check.found = FALSE;
	g_hash_table_foreach(icons_hash, (GHFunc) check_has, &check);

	return check.found;
}

/* Menu was clicked over this icon. Set things up correctly (shade items,
 * add app menu stuff, etc).
 * You should show icon_menu after calling this...
 */
void icon_prepare_menu(Icon *icon)
{
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
}

/* Set whether this icon is selected. Will automatically clear the selection
 * if it contains icons from a different group.
 */
void icon_set_selected(Icon *icon, gboolean selected)
{
	if (selected && icon_selection)
	{
		IconClass	*iclass;
		Icon		*other = (Icon *) icon_selection->data;

		iclass = (IconClass *) G_OBJECT_GET_CLASS(icon);
		if (!iclass->same_group(icon, other))
		{
			icon_select_only(icon);
			return;
		}
	}

	icon_set_selected_int(icon, TRUE);
}

/* Clear everything, except 'select', which is selected.
 * If select is NULL, unselects everything.
 */
void icon_select_only(Icon *select)
{
	GList	*to_clear, *next;
	
	if (select)
		icon_set_selected_int(select, TRUE);

	to_clear = g_list_copy(icon_selection);

	if (select)
		to_clear = g_list_remove(to_clear, select);
		
	for (next = to_clear; next; next = next->next)
		icon_set_selected_int((Icon *) next->data, FALSE);

	g_list_free(to_clear);
}

/* Destroys this icon's widget, causing it to be removed from the screen */
void icon_destroy(Icon *icon)
{
	g_return_if_fail(icon != NULL);

	icon_set_selected_int(icon, FALSE);

	g_signal_emit_by_name(icon, "destroy");
}

/* Return a text/uri-list of all the icons in the selection */
gchar *icon_create_uri_list(void)
{
	GString	*tmp;
	guchar	*retval;
	guchar	*leader;
	GList	*next;

	tmp = g_string_new(NULL);
	leader = g_strdup_printf("file://%s", our_host_name_for_dnd());

	for (next = icon_selection; next; next = next->next)
	{
		Icon *icon = (Icon *) next->data;

		g_string_append(tmp, leader);
		g_string_append(tmp, icon->path);
		g_string_append(tmp, "\r\n");
	}

	g_free(leader);
	retval = tmp->str;
	g_string_free(tmp, FALSE);
	
	return retval;
}

GType icon_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (IconClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			icon_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(Icon),
			0,			/* n_preallocs */
			icon_init
		};

		type = g_type_register_static(G_TYPE_OBJECT, "Icon",
					      &info, 0);
	}

	return type;
}

/* Sets, unsets or changes the pathname and name for an icon.
 * Updates icons_hash and (re)stats the item.
 * If name is NULL then gets the leafname from pathname.
 * If pathname is NULL, frees everything.
 */
void icon_set_path(Icon *icon, const char *pathname, const char *name)
{
	if (icon->path)
	{
		icon_unhash_path(icon);
		icon->src_path = NULL;
		icon->path = NULL;

		diritem_free(icon->item);
		icon->item = NULL;
	}

	if (pathname)
	{
		icon->src_path = g_strdup(pathname);
		icon->path = expand_path(pathname);

		icon_hash_path(icon);

		if (!name)
			name = g_basename(pathname);

		icon->item = diritem_new(name);
		diritem_restat(icon->path, icon->item, NULL);
	}
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* The icons_hash table allows us to convert from a path to a list
 * of icons that use that path.
 * Add this icon to the list for its path.
 */
static void icon_hash_path(Icon *icon)
{
	GList	*list;

	g_return_if_fail(icon != NULL);

	/* g_print("[ hashing '%s' ]\n", icon->path); */

	list = g_hash_table_lookup(icons_hash, icon->path);
	list = g_list_prepend(list, icon);
	g_hash_table_insert(icons_hash, icon->path, list);
}

/* Remove this icon from the icons_hash table */
static void icon_unhash_path(Icon *icon)
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

static void rename_activate(GtkWidget *dialog)
{
	GtkWidget *entry, *src;
	Icon	*icon;
	const guchar	*new_name, *new_src;
	
	entry = g_object_get_data(G_OBJECT(dialog), "new_name");
	icon = g_object_get_data(G_OBJECT(dialog), "callback_icon");
	src = g_object_get_data(G_OBJECT(dialog), "new_path");

	g_return_if_fail(entry != NULL &&
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
		icon_set_path(icon, new_src, new_name);
		g_signal_emit_by_name(icon, "update");
		gtk_widget_destroy(dialog);
	}
}

static void menu_closed(GtkWidget *widget)
{
	appmenu_remove();
	menu_icon = NULL;
}

/* Called when another application takes the selection away from us */
static void lose_selection(GtkClipboard *primary, gpointer data)
{
	have_primary = FALSE;
	icon_select_only(NULL);
}

/* Called when another application wants the contents of our selection */
static void selection_get(GtkClipboard	*primary,
		          GtkSelectionData *selection_data,
		          guint		info,
		          gpointer	data)
{
	gchar *text;

	if (info == TARGET_URI_LIST)
		text = icon_create_uri_list();
	else
	{
		GList	*next;
		GString	*str;

		str = g_string_new(NULL);

		for (next = icon_selection; next; next = next->next)
		{
			Icon	*icon = (Icon *) next->data;

			g_string_append(str, icon->path);
			g_string_append_c(str, ' ');
		}

		text = str->str;
		g_string_free(str, FALSE);
	}

	gtk_selection_data_set(selection_data,
				gdk_atom_intern("STRING", FALSE),
				8, text, strlen(text));
}

static void remove_items(gpointer data, guint action, GtkWidget *widget)
{
	IconClass	*iclass;

	if (menu_icon)
		icon_set_selected(menu_icon, TRUE);
	
	if (!icon_selection)
	{
		delayed_error(
			_("You must first select some items to remove"));
		return;
	}

	iclass = (IconClass *)
		  G_OBJECT_GET_CLASS(G_OBJECT(icon_selection->data));

	iclass->remove_items();
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
			show_rename_box(menu_icon);
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

static void edit_response(GtkWidget *dialog, gint response, gpointer data)
{
	if (response == GTK_RESPONSE_OK)
		rename_activate(dialog);
	else if (response == GTK_RESPONSE_CANCEL)
		gtk_widget_destroy(dialog);
}

/* Opens a box allowing the user to change the name of a pinned icon.
 * If the icon is destroyed then the box will close.
 * If the user chooses OK then the callback is called once the icon's
 * name, src_path and path fields have been updated and the item field
 * restatted.
 */
static void show_rename_box(Icon *icon)
{
	GtkDialog	*dialog;
	GtkWidget	*label, *entry;

	if (icon->dialog)
	{
		gtk_window_present(GTK_WINDOW(icon->dialog));
		return;
	}

	icon->dialog = gtk_dialog_new();
	g_signal_connect(icon->dialog, "destroy",
			G_CALLBACK(gtk_widget_destroyed), &icon->dialog);

	dialog = GTK_DIALOG(icon->dialog);

	gtk_window_set_title(GTK_WINDOW(dialog), _("Edit Item"));
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);

	label = gtk_label_new(_("Clicking the icon opens:"));
	gtk_box_pack_start(GTK_BOX(dialog->vbox), label, TRUE, TRUE, 0);

	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(dialog->vbox), entry, TRUE, FALSE, 2);
	gtk_entry_set_text(GTK_ENTRY(entry), icon->src_path);
	g_object_set_data(G_OBJECT(dialog), "new_path", entry);
	g_signal_connect_swapped(entry, "activate",
			G_CALLBACK(rename_activate), dialog);

	gtk_box_pack_start(GTK_BOX(dialog->vbox),
			gtk_hseparator_new(), TRUE, TRUE, 2);

	label = gtk_label_new(_("The text displayed under the icon is:"));
	gtk_box_pack_start(GTK_BOX(dialog->vbox), label, TRUE, TRUE, 0);
	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(dialog->vbox), entry, TRUE, FALSE, 2);
	gtk_entry_set_text(GTK_ENTRY(entry), icon->item->leafname);
	gtk_widget_grab_focus(entry);
	g_object_set_data(G_OBJECT(dialog), "new_name", entry);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	
	g_object_set_data(G_OBJECT(dialog), "callback_icon", icon);

	gtk_dialog_add_buttons(dialog,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OK, GTK_RESPONSE_OK,
			NULL);
	gtk_dialog_set_default_response(dialog, GTK_RESPONSE_OK);

	g_signal_connect(dialog, "response", G_CALLBACK(edit_response), NULL);
	
	gtk_widget_show_all(GTK_WIDGET(dialog));
}

static gpointer parent_class = NULL;

static void icon_finialize(GObject *object)
{
	Icon *icon = (Icon *) object;

	g_return_if_fail(!icon->selected);

	if (icon->dialog)
		gtk_widget_destroy(icon->dialog);

	if (icon == menu_icon)
		menu_icon = NULL;

	icon_set_path(icon, NULL, NULL);

	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void icon_class_init(gpointer gclass, gpointer data)
{
	guchar		*tmp;
	GList		*items;
	GtkItemFactory	*item_factory;
	GObjectClass *object = (GObjectClass *) gclass;
	IconClass *icon = (IconClass *) gclass;

	parent_class = g_type_class_peek_parent(gclass);

	object->finalize = icon_finialize;
	icon->destroy = NULL;
	icon->redraw = NULL;
	icon->update = NULL;
	icon->same_group = NULL;

	g_signal_new("update",
			G_TYPE_FROM_CLASS(gclass),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET(IconClass, update),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	g_signal_new("destroy",
			G_TYPE_FROM_CLASS(gclass),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET(IconClass, destroy),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	g_signal_new("redraw",
			G_TYPE_FROM_CLASS(gclass),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET(IconClass, redraw),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	icons_hash = g_hash_table_new(g_str_hash, g_str_equal);

	item_factory = menu_create(menu_def,
				sizeof(menu_def) / sizeof(*menu_def),
				 "<icon>", NULL);

	tmp = g_strconcat("<icon>/", _("File"), NULL);
	icon_menu = gtk_item_factory_get_widget(item_factory, "<icon>");
	icon_file_menu = gtk_item_factory_get_widget(item_factory, tmp);
	g_free(tmp);

	/* File '' label... */
	items = gtk_container_get_children(GTK_CONTAINER(icon_menu));
	icon_file_item = GTK_BIN(g_list_nth(items, 1)->data)->child;
	g_list_free(items);

	/* Shift Open... label */
	items = gtk_container_get_children(GTK_CONTAINER(icon_file_menu));
	file_shift_item = GTK_BIN(items->data)->child;
	g_list_free(items);

	g_signal_connect(icon_menu, "unmap_event",
			G_CALLBACK(menu_closed), NULL);
}

static void icon_init(GTypeInstance *object, gpointer gclass)
{
	Icon *icon = (Icon *) object;

	icon->selected = FALSE;
	icon->src_path = NULL;
	icon->path = NULL;
	icon->item = NULL;
	icon->dialog = NULL;
}

/* As icon_set_selected(), but doesn't automatically unselect incompatible
 * icons.
 */
static void icon_set_selected_int(Icon *icon, gboolean selected)
{
	static GtkClipboard *primary;

	g_return_if_fail(icon != NULL);

	if (icon->selected == selected)
		return;

	if (!primary)
		primary = gtk_clipboard_get(gdk_atom_intern("PRIMARY", FALSE));

	if (selected)
	{
		icon_selection = g_list_prepend(icon_selection, icon);
		if (!have_primary)
		{
			GtkTargetEntry target_table[] =
			{
				{"text/uri-list", 0, TARGET_URI_LIST},
				{"UTF8", 0, TARGET_STRING},
				{"COMPOUND_TEXT", 0, TARGET_STRING},
				{"STRING", 0, TARGET_STRING},
			};

			/* Grab selection */
			have_primary = gtk_clipboard_set_with_data(primary,
				target_table,
				sizeof(target_table) / sizeof(*target_table),
				selection_get, lose_selection, NULL);
		}
	}
	else
	{
		icon_selection = g_list_remove(icon_selection, icon);
		if (have_primary && !icon_selection)
		{
			have_primary = FALSE;
			gtk_clipboard_clear(primary);
		}
	}

	icon->selected = selected;
	g_signal_emit_by_name(icon, "redraw");
}
