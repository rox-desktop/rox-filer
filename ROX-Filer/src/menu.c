/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 1999, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
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

/* menu.c - code for handling the popup menu */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "run.h"
#include "action.h"
#include "filer.h"
#include "type.h"
#include "support.h"
#include "gui_support.h"
#include "options.h"
#include "choices.h"
#include "savebox.h"
#include "mount.h"
#include "minibuffer.h"

#define C_ "<control>"

#define MENU_MARGIN 32

GtkAccelGroup	*filer_keys;
GtkAccelGroup	*panel_keys;

static GtkWidget *popup_menu = NULL;	/* Currently open menu */

/* Options */
static GtkWidget *xterm_here_entry;
static char *xterm_here_value;

/* Static prototypes */

static void position_menu(GtkMenu *menu, gint *x, gint *y, gpointer data);
static void menu_closed(GtkWidget *widget);
static void items_sensitive(GtkWidget *menu, int from, int n, gboolean state);
static char *load_xterm_here(char *data);

/* Note that for these callbacks none of the arguments are used. */
static void not_yet(gpointer data, guint action, GtkWidget *widget);

static void large(gpointer data, guint action, GtkWidget *widget);
static void small(gpointer data, guint action, GtkWidget *widget);
static void full_info(gpointer data, guint action, GtkWidget *widget);

static void sort_name(gpointer data, guint action, GtkWidget *widget);
static void sort_type(gpointer data, guint action, GtkWidget *widget);
static void sort_size(gpointer data, guint action, GtkWidget *widget);
static void sort_date(gpointer data, guint action, GtkWidget *widget);

static void hidden(gpointer data, guint action, GtkWidget *widget);
static void refresh(gpointer data, guint action, GtkWidget *widget);

static void copy_item(gpointer data, guint action, GtkWidget *widget);
static void rename_item(gpointer data, guint action, GtkWidget *widget);
static void link_item(gpointer data, guint action, GtkWidget *widget);
static void open_file(gpointer data, guint action, GtkWidget *widget);
static void help(gpointer data, guint action, GtkWidget *widget);
static void show_file_info(gpointer data, guint action, GtkWidget *widget);
static void mount(gpointer data, guint action, GtkWidget *widget);
static void delete(gpointer data, guint action, GtkWidget *widget);
static void usage(gpointer data, guint action, GtkWidget *widget);

static void select_all(gpointer data, guint action, GtkWidget *widget);
static void clear_selection(gpointer data, guint action, GtkWidget *widget);
static void show_options(gpointer data, guint action, GtkWidget *widget);
static void new_directory(gpointer data, guint action, GtkWidget *widget);
static void xterm_here(gpointer data, guint action, GtkWidget *widget);

static void open_parent_same(gpointer data, guint action, GtkWidget *widget);
static void open_parent(gpointer data, guint action, GtkWidget *widget);
static void new_window(gpointer data, guint action, GtkWidget *widget);
static void close_window(gpointer data, guint action, GtkWidget *widget);
static void enter_path(gpointer data, guint action, GtkWidget *widget);
static void rox_help(gpointer data, guint action, GtkWidget *widget);

static void open_as_dir(gpointer data, guint action, GtkWidget *widget);
static void close_panel(gpointer data, guint action, GtkWidget *widget);

static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();

static OptionsSection options =
{
	"Menu options",
	create_options,
	update_options,
	set_options,
	save_options
};


static GtkWidget	*filer_menu;		/* The popup filer menu */
static GtkWidget	*filer_file_item;	/* The File '' label */
static GtkWidget	*filer_file_menu;	/* The File '' menu */
static GtkWidget	*filer_hidden_menu;	/* The Show Hidden item */
static GtkWidget	*panel_menu;		/* The popup panel menu */
static GtkWidget	*panel_file_item;	/* The File '' label */
static GtkWidget	*panel_file_menu;	/* The File '' menu */
static GtkWidget	*panel_hidden_menu;	/* The Show Hidden item */

static gint		screen_width, screen_height;

static GtkItemFactoryEntry filer_menu_def[] = {
{"/Display",			NULL,	NULL, 0, "<Branch>"},
{"/Display/Large Icons",   	NULL,  	large, 0, "<RadioItem>"},
{"/Display/Small Icons",   	NULL,  	small, 0, "/Display/Large Icons"},
{"/Display/Full Info",		NULL,  	full_info, 0, "/Display/Large Icons"},
{"/Display/Separator",		NULL,  	NULL, 0, "<Separator>"},
{"/Display/Sort by Name",	NULL,  	sort_name, 0, "<RadioItem>"},
{"/Display/Sort by Type",	NULL,  	sort_type, 0, "/Display/Sort by Name"},
{"/Display/Sort by Date",	NULL,  	sort_date, 0, "/Display/Sort by Name"},
{"/Display/Sort by Size",	NULL,  	sort_size, 0, "/Display/Sort by Name"},
{"/Display/Separator",		NULL,  	NULL, 0, "<Separator>"},
{"/Display/Show Hidden",   	C_"H", 	hidden, 0, "<ToggleItem>"},
{"/Display/Refresh",	   	C_"L", 	refresh, 0,	NULL},
{"/File",			NULL,  	NULL, 0, "<Branch>"},
{"/File/Copy...",		NULL,  	copy_item, 0, NULL},
{"/File/Rename...",		NULL,  	rename_item, 0, NULL},
{"/File/Link...",		NULL,  	link_item, 0, NULL},
{"/File/Shift Open",   		NULL,  	open_file, 0, NULL},
{"/File/Help",		    	"F1",  	help, 0, NULL},
{"/File/Info",			"I",  	show_file_info, 0, NULL},
{"/File/Separator",		NULL,   NULL, 0, "<Separator>"},
{"/File/Mount",	    		"M",  	mount, 0,	NULL},
{"/File/Delete",	    	C_"X", 	delete, 0,	NULL},
{"/File/Disk Usage",	    	"U", 	usage, 0, NULL},
{"/File/Permissions",		NULL,   not_yet, 0, NULL},
{"/File/Touch",    		NULL,   not_yet, 0, NULL},
{"/File/Find",			NULL,   not_yet, 0, NULL},
{"/Select All",	    		C_"A",  select_all, 0, NULL},
{"/Clear Selection",	    	C_"Z",  clear_selection, 0, NULL},
{"/Options...",			NULL,   show_options, 0, NULL},
{"/New Directory...",		NULL,   new_directory, 0, NULL},
{"/Xterm Here",			NULL,  	xterm_here, 0, NULL},
{"/Window",			NULL,  	NULL, 0, "<Branch>"},
{"/Window/Parent, New Window",	NULL,   open_parent, 0, NULL},
{"/Window/Parent, Same Window",	NULL,   open_parent_same, 0, NULL},
{"/Window/New Window",		NULL,   new_window, 0, NULL},
{"/Window/Close Window",	C_"Q",  close_window, 0, NULL},
{"/Window/Enter Path",		NULL,  	enter_path, 0, NULL},
{"/Window/Separator",		NULL,  	NULL, 0, "<Separator>"},
{"/Window/Show ROX-Filer help",	NULL,   rox_help, 0, NULL},
};

static GtkItemFactoryEntry panel_menu_def[] = {
{"/Display",			NULL,	NULL, 0, "<Branch>"},
{"/Display/Large Icons",	NULL,   large, 0, "<RadioItem>"},
{"/Display/Small Icons",	NULL,   small, 0, "/Display/Large Icons"},
{"/Display/Full Info",		NULL,   full_info, 0, "/Display/Large Icons"},
{"/Display/Separator",		NULL,   NULL, 0, "<Separator>"},
{"/Display/Sort by Name",	NULL,   sort_name, 0, "<RadioItem>"},
{"/Display/Sort by Type",	NULL,   sort_type, 0, "/Display/Sort by Name"},
{"/Display/Sort by Date",	NULL,   sort_date, 0, "/Display/Sort by Name"},
{"/Display/Sort by Size",	NULL,   sort_size, 0, "/Display/Sort by Name"},
{"/Display/Separator",		NULL,   NULL, 0, "<Separator>"},
{"/Display/Show Hidden",   	NULL, 	hidden, 0, "<ToggleItem>"},
{"/Display/Refresh",	    	NULL, 	refresh, 0,	NULL},
{"/File",			NULL,	NULL, 	0, "<Branch>"},
{"/File/Help",		    	NULL,  	help, 0, NULL},
{"/File/Info",			NULL,  	show_file_info, 0, NULL},
{"/File/Delete",		NULL,	delete,	0, NULL},
{"/Open as directory",		NULL, 	open_as_dir, 0, NULL},
{"/Options...",			NULL,   show_options, 0, NULL},
{"/Close panel",		NULL, 	close_panel, 0, NULL},
{"/Separator",			NULL,	NULL, 0, "<Separator>"},
{"/Show ROX-Filer help",	NULL,   rox_help, 0, NULL},
};

typedef struct _FileStatus FileStatus;

/* This is for the 'file(1) says...' thing */
struct _FileStatus
{
	int	fd;	/* FD to read from, -1 if closed */
	int	input;	/* Input watcher tag if fd valid */
	GtkLabel *label;	/* Widget to output to */
	gboolean	start;	/* No output yet */
};

void menu_init()
{
	GtkItemFactory  	*item_factory;
	char			*menurc;
	GList			*items;

	/* This call starts returning strange values after a while, so get
	 * the result here during init.
	 */
	gdk_window_get_size(GDK_ROOT_PARENT(), &screen_width, &screen_height);

	filer_keys = gtk_accel_group_new();
	item_factory = gtk_item_factory_new(GTK_TYPE_MENU,
					    "<filer>",
					    filer_keys);
	gtk_item_factory_create_items(item_factory,
			sizeof(filer_menu_def) / sizeof(*filer_menu_def),
			filer_menu_def,
			NULL);
	filer_menu = gtk_item_factory_get_widget(item_factory, "<filer>");
	filer_file_menu = gtk_item_factory_get_widget(item_factory,
			"<filer>/File");
	filer_hidden_menu = gtk_item_factory_get_widget(item_factory,
			"<filer>/Display/Show Hidden");
	items = gtk_container_children(GTK_CONTAINER(filer_menu));
	filer_file_item = GTK_BIN(g_list_nth(items, 1)->data)->child;
	g_list_free(items);

	panel_keys = gtk_accel_group_new();
	item_factory = gtk_item_factory_new(GTK_TYPE_MENU,
					    "<panel>",
					    panel_keys);
	gtk_item_factory_create_items(item_factory,
			sizeof(panel_menu_def) / sizeof(*panel_menu_def),
			panel_menu_def,
			NULL);
	panel_menu = gtk_item_factory_get_widget(item_factory, "<panel>");
	panel_file_menu = gtk_item_factory_get_widget(item_factory, 
			"<panel>/File");
	panel_hidden_menu = gtk_item_factory_get_widget(item_factory,
			"<panel>/Display/Show Hidden");
	items = gtk_container_children(GTK_CONTAINER(panel_menu));
	panel_file_item = GTK_BIN(g_list_nth(items, 1)->data)->child;
	g_list_free(items);

	menurc = choices_find_path_load("menus");
	if (menurc)
		gtk_item_factory_parse_rc(menurc);

	gtk_accel_group_lock(panel_keys);

	gtk_signal_connect(GTK_OBJECT(filer_menu), "unmap_event",
			GTK_SIGNAL_FUNC(menu_closed), NULL);
	gtk_signal_connect(GTK_OBJECT(panel_menu), "unmap_event",
			GTK_SIGNAL_FUNC(menu_closed), NULL);
	gtk_signal_connect(GTK_OBJECT(filer_file_menu), "unmap_event",
			GTK_SIGNAL_FUNC(menu_closed), NULL);

	options_sections = g_slist_prepend(options_sections, &options);
	xterm_here_value = g_strdup("xterm");
	option_register("xterm_here", load_xterm_here);
}
 
/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options()
{
	GtkWidget	*table, *label;

	table = gtk_table_new(2, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 4);

	label = gtk_label_new("To set the keyboard short-cuts you simply open "
			"the menu over a filer window, move the pointer over "
			"the item you want to use and press a key. The key "
			"will appear next to the menu item and you can just "
			"press that key without opening the menu in future. "
			"To save the current menu short-cuts for next time, "
			"click the Save button at the bottom of this window.");
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 2, 0, 1);

	label = gtk_label_new("'Xterm here' program:");
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 1, 2);
	xterm_here_entry = gtk_entry_new();
	gtk_table_attach_defaults(GTK_TABLE(table), xterm_here_entry,
			1, 2, 1, 2);

	return table;
}

static char *load_xterm_here(char *data)
{
	g_free(xterm_here_value);
	xterm_here_value = g_strdup(data);
	return NULL;
}

static void update_options()
{
	gtk_entry_set_text(GTK_ENTRY(xterm_here_entry), xterm_here_value);
}

static void set_options()
{
	g_free(xterm_here_value);
	xterm_here_value = g_strdup(gtk_entry_get_text(
				GTK_ENTRY(xterm_here_entry)));
}

static void save_options()
{
	char	*menurc;

	menurc = choices_find_path_save("menus", TRUE);
	if (menurc)
		gtk_item_factory_dump_rc(menurc, NULL, TRUE);

	option_write("xterm_here", xterm_here_value);
}


static void items_sensitive(GtkWidget *menu, int from, int n, gboolean state)
{
	GList	*items, *item;

	items = gtk_container_children(GTK_CONTAINER(menu));

	item = g_list_nth(items, from);
	while (item && n--)
	{
		gtk_widget_set_sensitive(GTK_BIN(item->data)->child, state);
		item = item->next;
	}

	g_list_free(items);
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

void show_filer_menu(FilerWindow *filer_window, GdkEventButton *event,
		     int item)
{
	GString		*buffer;
	GtkWidget	*file_label, *file_menu;
	DirItem	*file_item;
	int		pos[2];
	
	pos[0] = event->x_root;
	pos[1] = event->y_root;

	window_with_focus = filer_window;

	if (filer_window->panel)
	{
		switch (filer_window->panel_side)
		{
			case TOP: 	pos[1] = -2; break;
			case BOTTOM: 	pos[1] = -1; break;
			case LEFT: 	pos[0] = -2; break;
			case RIGHT: 	pos[0] = -1; break;
		}
	}

	if (filer_window->panel)
		collection_clear_selection(filer_window->collection); /* ??? */

	if (filer_window->collection->number_selected == 0 && item >= 0)
	{
		collection_select_item(filer_window->collection, item);
		filer_window->temp_item_selected = TRUE;
	}
	else
		filer_window->temp_item_selected = FALSE;

	if (filer_window->panel)
	{
		file_label = panel_file_item;
		file_menu = panel_file_menu;

		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(panel_hidden_menu),
				filer_window->show_hidden);
	}
	else
	{
		file_label = filer_file_item;
		file_menu = filer_file_menu;

		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_hidden_menu),
				filer_window->show_hidden);
	}

	buffer = g_string_new(NULL);
	switch (filer_window->collection->number_selected)
	{
		case 0:
			g_string_assign(buffer, "Next Click");
			items_sensitive(file_menu, 0, 6, TRUE);
			break;
		case 1:
			items_sensitive(file_menu, 0, 6, TRUE);
			file_item = selected_item(filer_window->collection);
			g_string_sprintf(buffer, "%s '%s'",
					basetype_name(file_item),
					file_item->leafname);
			break;
		default:
			items_sensitive(file_menu, 0, 6, FALSE);
			g_string_sprintf(buffer, "%d items",
				filer_window->collection->number_selected);
			break;
	}

	gtk_label_set_text(GTK_LABEL(file_label), buffer->str);

	g_string_free(buffer, TRUE);

	if (filer_window->panel)
		popup_menu = panel_menu;
	else
		popup_menu = (event->state & GDK_CONTROL_MASK)
				? filer_file_menu
				: filer_menu;

	gtk_menu_popup(GTK_MENU(popup_menu), NULL, NULL, position_menu,
			(gpointer) pos, event->button, event->time);
}

static void menu_closed(GtkWidget *widget)
{
	if (window_with_focus == NULL || widget != popup_menu)
		return;			/* Close panel item chosen? */

	if (window_with_focus->temp_item_selected)
	{
		collection_clear_selection(window_with_focus->collection);
		window_with_focus->temp_item_selected = FALSE;
	}
}

void target_callback(Collection *collection, gint item, gpointer real_fn)
{
	g_return_if_fail(window_with_focus != NULL);
	g_return_if_fail(window_with_focus->collection == collection);
	g_return_if_fail(real_fn != NULL);
	
	collection_wink_item(collection, item);
	collection_clear_selection(collection);
	collection_select_item(collection, item);
	((CollectionTargetFunc)real_fn)(NULL, 0, collection);
	if (item < collection->number_of_items)
		collection_unselect_item(collection, item);
}

/* Actions */

/* Fake action to warn when a menu item does nothing */
static void not_yet(gpointer data, guint action, GtkWidget *widget)
{
	delayed_error("ROX-Filer", "Sorry, that feature isn't implemented yet");
}

static void large(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_style_set(window_with_focus, LARGE_ICONS);
}

static void small(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_style_set(window_with_focus, SMALL_ICONS);
}

static void full_info(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_style_set(window_with_focus, FULL_INFO);
}

static void sort_name(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_set_sort_fn(window_with_focus, sort_by_name);
}

static void sort_type(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_set_sort_fn(window_with_focus, sort_by_type);
}

static void sort_date(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_set_sort_fn(window_with_focus, sort_by_date);
}

static void sort_size(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_set_sort_fn(window_with_focus, sort_by_size);
}

static void hidden(gpointer data, guint action, GtkWidget *widget)
{
	gboolean new;
	GtkWidget	*item;
	
	g_return_if_fail(window_with_focus != NULL);

	item = window_with_focus->panel ? panel_hidden_menu : filer_hidden_menu;
	new = GTK_CHECK_MENU_ITEM(item)->active;

	filer_set_hidden(window_with_focus, new);
}

static void refresh(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	full_refresh();
	update_dir(window_with_focus, TRUE);
}

static void mount(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (window_with_focus->collection->number_selected == 0)
		collection_target(window_with_focus->collection,
				target_callback, mount);
	else
		action_mount(window_with_focus, NULL);
}

static void delete(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (window_with_focus->collection->number_selected == 0)
		collection_target(window_with_focus->collection,
				target_callback, delete);
	else
		action_delete(window_with_focus);
}

static void usage(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (window_with_focus->collection->number_selected == 0)
		collection_target(window_with_focus->collection,
				target_callback, usage);
	else
		action_usage(window_with_focus);
}

static gboolean copy_cb(char *initial, char *path)
{
	char	*new_dir, *slash;
	int	len;
	GString	*command;
	gboolean retval = TRUE;

	slash = strrchr(path, '/');
	if (!slash)
	{
		report_error("ROX-Filer", "Missing '/' in new pathname");
		return FALSE;
	}

	if (access(path, F_OK) == 0)
	{
		report_error("ROX-Filer",
				"An item with this name already exists");
		return FALSE;
	}

	len = slash - path;
	new_dir = g_malloc(len + 1);
	memcpy(new_dir, path, len);
	new_dir[len] = '\0';

	command = g_string_new(NULL);
	g_string_sprintf(command, "cp -a %s %s", initial, path);
	/* XXX: Use system. In fact, use action! */

	if (system(command->str))
	{
		g_string_append(command, " failed!");
		report_error("ROX-Filer", command->str);
		retval = FALSE;
	}

	g_string_free(command, TRUE);

	refresh_dirs(new_dir);
	return retval;
}

static void copy_item(gpointer data, guint action, GtkWidget *widget)
{
	Collection *collection;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected > 1)
	{
		report_error("ROX-Filer", "You cannot do this to more than "
				"one item at a time");
		return;
	}
	else if (collection->number_selected != 1)
		collection_target(collection, target_callback, copy_item);
	else
	{
		DirItem *item = selected_item(collection);

		savebox_show(window_with_focus, "Copy",
				window_with_focus->path,
				item->leafname,
				item->image, copy_cb);
	}
}

static gboolean rename_cb(char *initial, char *path)
{
	if (rename(initial, path))
	{
		report_error("ROX-Filer: rename()", g_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

static void rename_item(gpointer data, guint action, GtkWidget *widget)
{
	Collection *collection;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected > 1)
	{
		report_error("ROX-Filer", "You cannot do this to more than "
				"one item at a time");
		return;
	}
	else if (collection->number_selected != 1)
		collection_target(collection, target_callback, rename_item);
	else
	{
		DirItem *item = selected_item(collection);

		savebox_show(window_with_focus, "Rename",
				window_with_focus->path,
				item->leafname,
				item->image, rename_cb);
	}
}

static gboolean link_cb(char *initial, char *path)
{
	if (symlink(initial, path))
	{
		report_error("ROX-Filer: symlink()", g_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

static void link_item(gpointer data, guint action, GtkWidget *widget)
{
	Collection *collection;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected > 1)
	{
		report_error("ROX-Filer", "You cannot do this to more than "
				"one item at a time");
		return;
	}
	else if (collection->number_selected != 1)
		collection_target(collection, target_callback, link_item);
	else
	{
		DirItem *item = selected_item(collection);

		savebox_show(window_with_focus, "Symlink",
				window_with_focus->path,
				item->leafname,
				item->image, link_cb);
	}
}

static void open_file(gpointer data, guint action, GtkWidget *widget)
{
	Collection *collection;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected > 1)
	{
		report_error("ROX-Filer", "You cannot do this to more than "
				"one item at a time");
		return;
	}
	else if (collection->number_selected != 1)
		collection_target(collection, target_callback, open_file);
	else
		filer_openitem(window_with_focus,
				selected_item_number(collection),
				OPEN_SAME_WINDOW | OPEN_SHIFT);
}

/* Got some data from file(1) - stick it in the window. */
static void add_file_output(FileStatus *fs,
			    gint source, GdkInputCondition condition)
{
	char	buffer[20];
	char	*str;
	int	got;

	got = read(source, buffer, sizeof(buffer) - 1);
	if (got <= 0)
	{
		int	err = errno;
		gtk_input_remove(fs->input);
		close(source);
		fs->fd = -1;
		if (got < 0)
			delayed_error("ROX-Filer: file(1) says...",
					g_strerror(err));
		return;
	}
	buffer[got] = '\0';

	if (fs->start)
	{
		str = "";
		fs->start = FALSE;
	}
	else
		gtk_label_get(fs->label, &str);

	str = g_strconcat(str, buffer, NULL);
	gtk_label_set_text(fs->label, str);
	g_free(str);
}

static void file_info_destroyed(GtkWidget *widget, FileStatus *fs)
{
	if (fs->fd != -1)
	{
		gtk_input_remove(fs->input);
		close(fs->fd);
	}

	g_free(fs);
}

static void show_file_info(gpointer data, guint action, GtkWidget *widget)
{
	GtkWidget	*window, *table, *label, *button, *frame;
	GtkWidget	*file_label;
	GString		*gstring;
	char		*string;
	int		file_data[2];
	char		*path;
	char 		*argv[] = {"file", "-b", NULL, NULL};
	Collection 	*collection;
	DirItem		*file;
	struct stat	info;
	FileStatus 	*fs = NULL;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected > 1)
	{
		report_error("ROX-Filer", "You cannot do this to more than "
				"one item at a time");
		return;
	}
	else if (collection->number_selected != 1)
	{
		collection_target(collection, target_callback, show_file_info);
		return;
	}
	file = selected_item(collection);
	path = make_path(window_with_focus->path,
			file->leafname)->str;
	if (lstat(path, &info))
	{
		delayed_error("ROX-Filer", g_strerror(errno));
		return;
	}
	
	gstring = g_string_new(NULL);

	window = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_MOUSE);
	gtk_container_set_border_width(GTK_CONTAINER(window), 4);
	gtk_window_set_title(GTK_WINDOW(window), path);

	table = gtk_table_new(9, 2, FALSE);
	gtk_container_add(GTK_CONTAINER(window), table);
	gtk_table_set_row_spacings(GTK_TABLE(table), 8);
	gtk_table_set_col_spacings(GTK_TABLE(table), 4);
	
	label = gtk_label_new("Owner, group:");
	gtk_misc_set_alignment(GTK_MISC(label), 1, .5);
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_RIGHT);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);
	g_string_sprintf(gstring, "%s, %s", user_name(info.st_uid),
					    group_name(info.st_gid));
	label = gtk_label_new(gstring->str);
	gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 0, 1);
	
	label = gtk_label_new("Size:");
	gtk_misc_set_alignment(GTK_MISC(label), 1, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 1, 2);
	if (info.st_size >=2048)
	{
		g_string_sprintf(gstring, "%s (%ld bytes)",
				format_size((unsigned long) info.st_size),
				(unsigned long) info.st_size);
	}
	else
	{
		g_string_sprintf(gstring, "%ld bytes",
				(unsigned long) info.st_size);
	}
	label = gtk_label_new(gstring->str);
	gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 1, 2);
	
	label = gtk_label_new("Change time:");
	gtk_misc_set_alignment(GTK_MISC(label), 1, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 2, 3);
	g_string_sprintf(gstring, "%s", ctime(&info.st_ctime));
	g_string_truncate(gstring, gstring->len - 1);
	label = gtk_label_new(gstring->str);
	gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 2, 3);
	
	label = gtk_label_new("Modify time:");
	gtk_misc_set_alignment(GTK_MISC(label), 1, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 3, 4);
	g_string_sprintf(gstring, "%s", ctime(&info.st_mtime));
	g_string_truncate(gstring, gstring->len - 1);
	label = gtk_label_new(gstring->str);
	gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 3, 4);
	
	label = gtk_label_new("Access time:");
	gtk_misc_set_alignment(GTK_MISC(label), 1, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 4, 5);
	g_string_sprintf(gstring, "%s", ctime(&info.st_atime));
	g_string_truncate(gstring, gstring->len - 1);
	label = gtk_label_new(gstring->str);
	gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 4, 5);
	
	label = gtk_label_new("Permissions:");
	gtk_misc_set_alignment(GTK_MISC(label), 1, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 5, 6);
	label = gtk_label_new(pretty_permissions(info.st_mode));
	gtk_widget_set_style(label, fixed_style);
	gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 5, 6);
	
	label = gtk_label_new("MIME type:");
	gtk_misc_set_alignment(GTK_MISC(label), 1, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 6, 7);
	if (file->mime_type)
	{
		string = g_strconcat(file->mime_type->media_type, "/",
				file->mime_type->subtype, NULL);
		label = gtk_label_new(string);
		g_free(string);
	}
	else
		label = gtk_label_new("-");
	gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 6, 7);

	frame = gtk_frame_new("file(1) says...");
	gtk_table_attach_defaults(GTK_TABLE(table), frame, 0, 2, 7, 8);
	file_label = gtk_label_new("<nothing yet>");
	gtk_misc_set_padding(GTK_MISC(file_label), 4, 4);
	gtk_label_set_line_wrap(GTK_LABEL(file_label), TRUE);
	gtk_container_add(GTK_CONTAINER(frame), file_label);
	
	button = gtk_button_new_with_label("OK");
	gtk_window_set_focus(GTK_WINDOW(window), button);
	gtk_table_attach(GTK_TABLE(table), button, 0, 2, 8, 9,
			GTK_EXPAND | GTK_FILL | GTK_SHRINK, 0, 40, 4);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			gtk_widget_destroy, GTK_OBJECT(window));

	gtk_widget_show_all(window);
	gdk_flush();

	if (pipe(file_data))
	{
		g_string_sprintf(gstring, "pipe(): %s", g_strerror(errno));
		g_string_free(gstring, TRUE);
		return;
	}
	switch (fork())
	{
		case -1:
			g_string_sprintf(gstring, "fork(): %s",
					g_strerror(errno));
			gtk_label_set_text(GTK_LABEL(file_label), gstring->str);
			g_string_free(gstring, TRUE);
			close(file_data[0]);
			close(file_data[1]);
			break;
		case 0:
			/* We are the child */
			close(file_data[0]);
			dup2(file_data[1], STDOUT_FILENO);
			dup2(file_data[1], STDERR_FILENO);
#ifdef FILE_B_FLAG
			argv[2] = path;
#else
			argv[1] = path;
#endif
			if (execvp(argv[0], argv))
				fprintf(stderr, "execvp() error: %s\n",
						g_strerror(errno));
			_exit(0);
		default:
			/* We are the parent */
			close(file_data[1]);
			fs = g_new(FileStatus, 1);
			fs->label = GTK_LABEL(file_label);
			fs->fd = file_data[0];
			fs->start = TRUE;
			fs->input = gdk_input_add(fs->fd, GDK_INPUT_READ,
				(GdkInputFunction) add_file_output, fs);
			gtk_signal_connect(GTK_OBJECT(window), "destroy",
				GTK_SIGNAL_FUNC(file_info_destroyed), fs);
			g_string_free(gstring, TRUE);
			break;
	}
}

static void app_show_help(char *path)
{
	char		*help_dir;
	struct stat 	info;

	help_dir = g_strconcat(path, "/Help", NULL);
	
	if (stat(help_dir, &info))
		delayed_error("Application",
			"This is an application directory - you can "
			"run it as a program, or open it (hold down "
			"Shift while you open it). Most applications provide "
			"their own help here, but this one doesn't.");
	else
		filer_opendir(help_dir, FALSE, BOTTOM);
}

static void help(gpointer data, guint action, GtkWidget *widget)
{
	Collection 	*collection;
	DirItem	*item;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected > 1)
	{
		report_error("ROX-Filer", "You cannot do this to more than "
				"one item at a time");
		return;
	}
	else if (collection->number_selected != 1)
	{
		collection_target(collection, target_callback, help);
		return;
	}
	item = selected_item(collection);
	switch (item->base_type)
	{
		case TYPE_FILE:
			if (item->flags & ITEM_FLAG_EXEC_FILE)
				delayed_error("Executable file",
					"This is a file with an eXecute bit "
					"set - it can be run as a program.");
			else
				delayed_error("File",
					"This is a data file. Try using the "
					"Info menu item to find out more...");
			break;
		case TYPE_DIRECTORY:
			if (item->flags & ITEM_FLAG_APPDIR)
				app_show_help(
					make_path(window_with_focus->path,
					  item->leafname)->str);
			else if (item->flags & ITEM_FLAG_MOUNT_POINT)
				delayed_error("Mount point",
				"A mount point is a directory which another "
				"filing system can be mounted on. Everything "
				"on the mounted filesystem then appears to be "
				"inside the directory.");
			else
				delayed_error("Directory",
				"This is a directory. It contains an index to "
				"other items - open it to see the list.");
			break;
		case TYPE_CHAR_DEVICE:
		case TYPE_BLOCK_DEVICE:
			delayed_error("Device file",
				"Device files allow you to read from or write "
				"to a device driver as though it was an "
				"ordinary file.");
			break;
		case TYPE_PIPE:
			delayed_error("Named pipe",
				"Pipes allow different programs to "
				"communicate. One program writes data to the "
				"pipe while another one reads it out again.");
			break;
		case TYPE_SOCKET:
			delayed_error("Socket",
				"Sockets allow processes to communicate.");
			break;
		default:
			delayed_error("Unknown type", 
				"I couldn't find out what kind of file this "
				"is. Maybe it doesn't exist anymore or you "
				"don't have search permission on the directory "
				"it's in?");
			break;
	}
}

static void select_all(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	collection_select_all(window_with_focus->collection);
	window_with_focus->temp_item_selected = FALSE;
}

static void clear_selection(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	collection_clear_selection(window_with_focus->collection);
	window_with_focus->temp_item_selected = FALSE;
}

static void show_options(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	options_show(window_with_focus);
}

static gboolean new_directory_cb(char *initial, char *path)
{
	if (mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO))
	{
		report_error("mkdir", g_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

static void new_directory(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	savebox_show(window_with_focus, "Create directory",
			window_with_focus->path, "NewDir",
			default_pixmap + TYPE_DIRECTORY, new_directory_cb);
}

static void xterm_here(gpointer data, guint action, GtkWidget *widget)
{
	char	*argv[] = {NULL, NULL};

	argv[0] = xterm_here_value;

	g_return_if_fail(window_with_focus != NULL);

	if (!spawn_full(argv, window_with_focus->path))
		report_error("ROX-Filer", "Failed to fork() child "
					"process");
}

static void open_parent(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_opendir(make_path(window_with_focus->path, "/..")->str,
			FALSE, BOTTOM);
}

static void open_parent_same(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	change_to_parent(window_with_focus);
}

static void new_window(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_opendir(window_with_focus->path, FALSE, BOTTOM);
}

static void close_window(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	gtk_widget_destroy(window_with_focus->window);
}

static void enter_path(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	minibuffer_show(window_with_focus);
}

static void rox_help(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);
	
	filer_opendir(make_path(getenv("APP_DIR"), "Help")->str, FALSE, BOTTOM);
}

static void open_as_dir(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);
	
	filer_opendir(window_with_focus->path, FALSE, BOTTOM);
}

static void close_panel(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);
	
	gtk_widget_destroy(window_with_focus->window);
}
