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

/* filer.c - code for handling filer windows */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include "collection.h"

#include "global.h"

#include "main.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "pixmaps.h"
#include "menu.h"
#include "dnd.h"
#include "dir.h"
#include "run.h"
#include "mount.h"
#include "type.h"
#include "options.h"
#include "minibuffer.h"
#include "pinboard.h"
#include "panel.h"
#include "toolbar.h"
#include "bind.h"

#define PANEL_BORDER 2

FilerWindow 	*window_with_focus = NULL;
GList		*all_filer_windows = NULL;

static FilerWindow *window_with_selection = NULL;

/* Options bits */
static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();
static char *filer_unique_windows(char *data);
static char *filer_initial_window_height(char *data);

static OptionsSection options =
{
	N_("Filer window options"),
	create_options,
	update_options,
	set_options,
	save_options
};

gboolean o_unique_filer_windows = FALSE;
static gint o_initial_window_height = 3;
static GtkWidget *toggle_unique_filer_windows;
static GtkAdjustment *adj_initial_window_height;

/* Static prototypes */
static void attach(FilerWindow *filer_window);
static void detach(FilerWindow *filer_window);
static void filer_window_destroyed(GtkWidget    *widget,
				   FilerWindow	*filer_window);
static gint focus_in(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window);
static void add_item(FilerWindow *filer_window, DirItem *item);
static void update_display(Directory *dir,
			DirAction	action,
			GPtrArray	*items,
			FilerWindow *filer_window);
static void set_scanning_display(FilerWindow *filer_window, gboolean scanning);
static gboolean may_rescan(FilerWindow *filer_window, gboolean warning);
static gboolean minibuffer_show_cb(FilerWindow *filer_window);
static FilerWindow *find_filer_window(char *path, FilerWindow *diff);
static void filer_set_title(FilerWindow *filer_window);
static gint coll_button_release(GtkWidget *widget,
			        GdkEventButton *event,
			        FilerWindow *filer_window);
static gint coll_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      FilerWindow *filer_window);
static gint coll_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       FilerWindow *filer_window);
static void perform_action(FilerWindow *filer_window, GdkEventButton *event);
static void filer_add_widgets(FilerWindow *filer_window);
static void filer_add_signals(FilerWindow *filer_window);

static GdkAtom xa_string;

static GdkCursor *busy_cursor = NULL;
static GdkCursor *crosshair = NULL;

void filer_init(void)
{
	xa_string = gdk_atom_intern("STRING", FALSE);

	options_sections = g_slist_prepend(options_sections, &options);
	option_register("filer_unique_windows", filer_unique_windows);
	option_register("filer_initial_window_height",
						filer_initial_window_height);

	busy_cursor = gdk_cursor_new(GDK_WATCH);
	crosshair = gdk_cursor_new(GDK_CROSSHAIR);
}

static gboolean if_deleted(gpointer item, gpointer removed)
{
	int	i = ((GPtrArray *) removed)->len;
	DirItem	**r = (DirItem **) ((GPtrArray *) removed)->pdata;
	char	*leafname = ((DirItem *) item)->leafname;

	while (i--)
	{
		if (strcmp(leafname, r[i]->leafname) == 0)
			return TRUE;
	}

	return FALSE;
}

static void update_item(FilerWindow *filer_window, DirItem *item)
{
	int	i;
	char	*leafname = item->leafname;

	if (leafname[0] == '.')
	{
		if (filer_window->show_hidden == FALSE || leafname[1] == '\0'
				|| (leafname[1] == '.' && leafname[2] == '\0'))
		return;
	}

	i = collection_find_item(filer_window->collection, item, dir_item_cmp);

	if (i >= 0)
		collection_draw_item(filer_window->collection, i, TRUE);
	else
		g_warning("Failed to find '%s'\n", item->leafname);
}

static void update_display(Directory *dir,
			DirAction	action,
			GPtrArray	*items,
			FilerWindow *filer_window)
{
	int	old_num;
	int	i;
	int	cursor = filer_window->collection->cursor_item;
	char	*as;
	Collection *collection = filer_window->collection;

	switch (action)
	{
		case DIR_ADD:
			as = filer_window->auto_select;

			old_num = collection->number_of_items;
			for (i = 0; i < items->len; i++)
			{
				DirItem *item = (DirItem *) items->pdata[i];

				add_item(filer_window, item);

				if (cursor != -1 || !as)
					continue;

				if (strcmp(as, item->leafname) != 0)
					continue;

				cursor = collection->number_of_items - 1;
				if (filer_window->had_cursor)
				{
					collection_set_cursor_item(collection,
							cursor);
					filer_window->mini_cursor_base = cursor;
				}
				else
					collection_wink_item(collection,
							cursor);
			}

			if (old_num != collection->number_of_items)
				collection_qsort(filer_window->collection,
						filer_window->sort_fn);
			break;
		case DIR_REMOVE:
			collection_delete_if(filer_window->collection,
					if_deleted,
					items);
			break;
		case DIR_START_SCAN:
			set_scanning_display(filer_window, TRUE);
			break;
		case DIR_END_SCAN:
			if (filer_window->window->window)
				gdk_window_set_cursor(
						filer_window->window->window,
						NULL);
			shrink_width(filer_window);
			if (filer_window->had_cursor &&
					collection->cursor_item == -1)
			{
				collection_set_cursor_item(collection, 0);
				filer_window->had_cursor = FALSE;
			}
			set_scanning_display(filer_window, FALSE);
			break;
		case DIR_UPDATE:
			for (i = 0; i < items->len; i++)
			{
				DirItem *item = (DirItem *) items->pdata[i];

				update_item(filer_window, item);
			}
			collection_qsort(filer_window->collection,
					filer_window->sort_fn);
			break;
	}
}

static void attach(FilerWindow *filer_window)
{
	gdk_window_set_cursor(filer_window->window->window, busy_cursor);
	collection_clear(filer_window->collection);
	filer_window->scanning = TRUE;
	dir_attach(filer_window->directory, (DirCallback) update_display,
			filer_window);
	filer_set_title(filer_window);
}

static void detach(FilerWindow *filer_window)
{
	g_return_if_fail(filer_window->directory != NULL);

	dir_detach(filer_window->directory,
			(DirCallback) update_display, filer_window);
	g_fscache_data_unref(dir_cache, filer_window->directory);
	filer_window->directory = NULL;
}

static void filer_window_destroyed(GtkWidget 	*widget,
				   FilerWindow 	*filer_window)
{
	all_filer_windows = g_list_remove(all_filer_windows, filer_window);

	if (window_with_selection == filer_window)
		window_with_selection = NULL;
	
	if (window_with_focus == filer_window)
	{
		if (popup_menu)
			gtk_menu_popdown(GTK_MENU(popup_menu));
		window_with_focus = NULL;
	}

	if (filer_window->directory)
		detach(filer_window);

	g_free(filer_window->auto_select);
	g_free(filer_window->path);
	g_free(filer_window);

	if (--number_of_windows < 1)
		gtk_main_quit();
}

/* Add a single object to a directory display */
static void add_item(FilerWindow *filer_window, DirItem *item)
{
	char		*leafname = item->leafname;
	int		item_width;

	if (leafname[0] == '.')
	{
		if (filer_window->show_hidden == FALSE || leafname[1] == '\0'
				|| (leafname[1] == '.' && leafname[2] == '\0'))
		return;
	}

	item_width = calc_width(filer_window, item); 
	if (item_width > filer_window->collection->item_width)
		collection_set_item_size(filer_window->collection,
					 item_width,
					 filer_window->collection->item_height);
	collection_insert(filer_window->collection, item);
}

/* Returns TRUE iff the directory still exists. */
static gboolean may_rescan(FilerWindow *filer_window, gboolean warning)
{
	Directory *dir;
	
	g_return_val_if_fail(filer_window != NULL, FALSE);

	/* We do a fresh lookup (rather than update) because the inode may
	 * have changed.
	 */
	dir = g_fscache_lookup(dir_cache, filer_window->path);
	if (!dir)
	{
		if (warning)
			delayed_error(PROJECT, _("Directory missing/deleted"));
		gtk_widget_destroy(filer_window->window);
		return FALSE;
	}
	if (dir == filer_window->directory)
		g_fscache_data_unref(dir_cache, dir);
	else
	{
		detach(filer_window);
		filer_window->directory = dir;
		attach(filer_window);
	}

	return TRUE;
}

/* Another app has grabbed the selection */
static gint collection_lose_selection(GtkWidget *widget,
				      GdkEventSelection *event)
{
	if (window_with_selection &&
			window_with_selection->collection == COLLECTION(widget))
	{
		FilerWindow *filer_window = window_with_selection;
		window_with_selection = NULL;
		collection_clear_selection(filer_window->collection);
	}

	return TRUE;
}

/* Someone wants us to send them the selection */
static void selection_get(GtkWidget *widget, 
		       GtkSelectionData *selection_data,
		       guint      info,
		       guint      time,
		       gpointer   data)
{
	GString	*reply, *header;
	FilerWindow 	*filer_window;
	int		i;
	Collection	*collection;

	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");

	reply = g_string_new(NULL);
	header = g_string_new(NULL);

	switch (info)
	{
		case TARGET_STRING:
			g_string_sprintf(header, " %s",
					make_path(filer_window->path, "")->str);
			break;
		case TARGET_URI_LIST:
			g_string_sprintf(header, " file://%s%s",
					our_host_name(),
					make_path(filer_window->path, "")->str);
			break;
	}

	collection = filer_window->collection;
	for (i = 0; i < collection->number_of_items; i++)
	{
		if (collection->items[i].selected)
		{
			DirItem *item =
				(DirItem *) collection->items[i].data;
			
			g_string_append(reply, header->str);
			g_string_append(reply, item->leafname);
		}
	}
	/* This works, but I don't think I like it... */
	/* g_string_append_c(reply, ' '); */
	
	gtk_selection_data_set(selection_data, xa_string,
			8, reply->str + 1, reply->len - 1);
	g_string_free(reply, TRUE);
	g_string_free(header, TRUE);
}

/* No items are now selected. This might be because another app claimed
 * the selection or because the user unselected all the items.
 */
static void lose_selection(Collection 	*collection,
			   guint	time,
			   gpointer 	user_data)
{
	FilerWindow *filer_window = (FilerWindow *) user_data;

	if (window_with_selection == filer_window)
	{
		window_with_selection = NULL;
		gtk_selection_owner_set(NULL,
				GDK_SELECTION_PRIMARY,
				time);
	}
}

static void gain_selection(Collection 	*collection,
			   guint	time,
			   gpointer 	user_data)
{
	FilerWindow *filer_window = (FilerWindow *) user_data;

	if (gtk_selection_owner_set(GTK_WIDGET(collection),
				GDK_SELECTION_PRIMARY,
				time))
	{
		window_with_selection = filer_window;
	}
	else
		collection_clear_selection(filer_window->collection);
}

/* Open the item (or add it to the shell command minibuffer) */
void filer_openitem(FilerWindow *filer_window, int item_number, OpenFlags flags)
{
	gboolean	shift = (flags & OPEN_SHIFT) != 0;
	gboolean	close_mini = flags & OPEN_FROM_MINI;
	gboolean	close_window = (flags & OPEN_CLOSE_WINDOW) != 0;
	GtkWidget	*widget;
	DirItem		*item = (DirItem *)
			filer_window->collection->items[item_number].data;
	guchar		*full_path;
	gboolean	wink = TRUE;
	Directory	*old_dir;

	widget = filer_window->window;
	if (filer_window->mini_type == MINI_SHELL)
	{
		minibuffer_add(filer_window, item->leafname);
		return;
	}

	if (item->base_type == TYPE_DIRECTORY)
	{
		/* Never close a filer window when opening a directory
		 * (click on a dir or click on an app with shift).
		 */
		if (shift || !(item->flags & ITEM_FLAG_APPDIR))
			close_window = FALSE;
	}

	full_path = make_path(filer_window->path, item->leafname)->str;
	if (shift && (item->flags & ITEM_FLAG_SYMLINK))
		wink = FALSE;

	old_dir = filer_window->directory;
	if (run_diritem(full_path, item,
			flags & OPEN_SAME_WINDOW ? filer_window : NULL,
			shift))
	{
		if (old_dir != filer_window->directory)
			return;

		if (close_window)
			gtk_widget_destroy(filer_window->window);
		else
		{
			if (wink)
				collection_wink_item(filer_window->collection,
						item_number);
			if (close_mini)
				minibuffer_hide(filer_window);
		}
	}
}

static gint pointer_in(GtkWidget *widget,
			GdkEventCrossing *event,
			FilerWindow *filer_window)
{
	may_rescan(filer_window, TRUE);
	return FALSE;
}

static gint focus_in(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window)
{
	window_with_focus = filer_window;

	return FALSE;
}

/* Move the cursor to the next selected item in direction 'dir'
 * (+1 or -1).
 */
static void next_selected(FilerWindow *filer_window, int dir)
{
	Collection 	*collection = filer_window->collection;
	int		to_check = collection->number_of_items;
	int 	   	item = collection->cursor_item;

	g_return_if_fail(dir == 1 || dir == -1);

	if (to_check > 0 && item == -1)
	{
		/* Cursor not currently on */
		if (dir == 1)
			item = 0;
		else
			item = collection->number_of_items - 1;

		if (collection->items[item].selected)
			goto found;
	}

	while (--to_check > 0)
	{
		item += dir;

		if (item >= collection->number_of_items)
			item = 0;
		else if (item < 0)
			item = collection->number_of_items - 1;

		if (collection->items[item].selected)
			goto found;
	}

	gdk_beep();
	return;
found:
	collection_set_cursor_item(collection, item);
}

static void return_pressed(FilerWindow *filer_window, GdkEventKey *event)
{
	Collection		*collection = filer_window->collection;
	int			item = collection->cursor_item;
	TargetFunc 		cb = filer_window->target_cb;
	gpointer		data = filer_window->target_data;
	OpenFlags		flags = OPEN_SAME_WINDOW;

	filer_target_mode(filer_window, NULL, NULL, NULL);
	if (item < 0 || item >= collection->number_of_items)
		return;

	if (cb)
	{
		cb(filer_window, item, data);
		return;
	}

	if (event->state & GDK_SHIFT_MASK)
		flags |= OPEN_SHIFT;

	filer_openitem(filer_window, item, flags);
}

/* Handle keys that can't be bound with the menu */
static gint key_press_event(GtkWidget	*widget,
			GdkEventKey	*event,
			FilerWindow	*filer_window)
{
	switch (event->keyval)
	{
		case GDK_Escape:
			filer_target_mode(filer_window, NULL, NULL, NULL);
			return FALSE;
		case GDK_Return:
			return_pressed(filer_window, event);
			break;
		case GDK_ISO_Left_Tab:
			next_selected(filer_window, -1);
			break;
		case GDK_Tab:
			next_selected(filer_window, 1);
			break;
		case GDK_BackSpace:
			change_to_parent(filer_window);
			break;
		default:
			return FALSE;
	}

	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
	return TRUE;
}

void change_to_parent(FilerWindow *filer_window)
{
	char	*copy;
	char	*slash;

	if (filer_window->path[0] == '/' && filer_window->path[1] == '\0')
		return;		/* Already in the root */
	
	copy = g_strdup(filer_window->path);
	slash = strrchr(copy, '/');

	if (slash)
	{
		*slash = '\0';
		filer_change_to(filer_window,
				*copy ? copy : "/",
				slash + 1);
	}
	else
		g_warning("No / in directory path!\n");

	g_free(copy);

}

/* Make filer_window display path. When finished, highlight item 'from', or
 * the first item if from is NULL. If there is currently no cursor then
 * simply wink 'from' (if not NULL).
 */
void filer_change_to(FilerWindow *filer_window, char *path, char *from)
{
	char	*from_dup;
	char	*real_path;
	Directory *new_dir;

	g_return_if_fail(filer_window != NULL);

	real_path = pathdup(path);
	new_dir  = g_fscache_lookup(dir_cache, real_path);

	if (!new_dir)
	{
		char	*error;

		error = g_strdup_printf(_("Directory '%s' is not accessible"),
				real_path);
		g_free(real_path);
		delayed_error(PROJECT, error);
		g_free(error);
		return;
	}
	
	if (o_unique_filer_windows)
	{
		FilerWindow *fw;
		
		fw = find_filer_window(real_path, filer_window);
		if (fw)
			gtk_widget_destroy(fw->window);
	}

	from_dup = from && *from ? g_strdup(from) : NULL;

	detach(filer_window);
	g_free(filer_window->path);
	filer_window->path = real_path;

	filer_window->directory = new_dir;

	g_free(filer_window->auto_select);
	filer_window->had_cursor = filer_window->collection->cursor_item != -1
				   || filer_window->had_cursor;
	filer_window->auto_select = from_dup;

	filer_set_title(filer_window);
	collection_set_cursor_item(filer_window->collection, -1);
	attach(filer_window);

	if (filer_window->mini_type == MINI_PATH)
		gtk_idle_add((GtkFunction) minibuffer_show_cb,
				filer_window);
}

void filer_open_parent(FilerWindow *filer_window)
{
	char	*copy;
	char	*slash;

	if (filer_window->path[0] == '/' && filer_window->path[1] == '\0')
		return;		/* Already in the root */
	
	copy = g_strdup(filer_window->path);
	slash = strrchr(copy, '/');

	if (slash)
	{
		*slash = '\0';
		filer_opendir(*copy ? copy : "/");
	}
	else
		g_warning("No / in directory path!\n");

	g_free(copy);
}

int selected_item_number(Collection *collection)
{
	int	i;
	
	g_return_val_if_fail(collection != NULL, -1);
	g_return_val_if_fail(IS_COLLECTION(collection), -1);
	g_return_val_if_fail(collection->number_selected == 1, -1);

	for (i = 0; i < collection->number_of_items; i++)
		if (collection->items[i].selected)
			return i;

	g_warning("selected_item: number_selected is wrong\n");

	return -1;
}

DirItem *selected_item(Collection *collection)
{
	int	item;

	item = selected_item_number(collection);

	if (item > -1)
		return (DirItem *) collection->items[item].data;
	return NULL;
}

/* Append all the URIs in the selection to the string */
static void create_uri_list(FilerWindow *filer_window, GString *string)
{
	Collection *collection = filer_window->collection;
	GString	*leader;
	int i, num_selected;

	leader = g_string_new("file://");
	if (!o_no_hostnames)
		g_string_append(leader, our_host_name());
	g_string_append(leader, filer_window->path);
	if (leader->str[leader->len - 1] != '/')
		g_string_append_c(leader, '/');

	num_selected = collection->number_selected;

	for (i = 0; num_selected > 0; i++)
	{
		if (collection->items[i].selected)
		{
			DirItem *item = (DirItem *) collection->items[i].data;
			
			g_string_append(string, leader->str);
			g_string_append(string, item->leafname);
			g_string_append(string, "\r\n");
			num_selected--;
		}
	}

	g_string_free(leader, TRUE);
}

/* Creates and shows a new filer window.
 * Returns the new filer window, or NULL on error.
 */
FilerWindow *filer_opendir(char *path)
{
	FilerWindow	*filer_window;
	char		*real_path;
	
	/* Get the real pathname of the directory and copy it */
	real_path = pathdup(path);

	/* If the user doesn't want duplicate windows then check
	 * for an existing one and close it if found.
	 */
	if (o_unique_filer_windows)
	{
		FilerWindow *fw;
		
		fw = find_filer_window(real_path, NULL);

		if (fw)
			gtk_widget_destroy(fw->window);
	}

	filer_window = g_new(FilerWindow, 1);
	filer_window->minibuffer = NULL;
	filer_window->minibuffer_label = NULL;
	filer_window->minibuffer_area = NULL;
	filer_window->temp_show_hidden = FALSE;
	filer_window->path = real_path;
	filer_window->scanning = FALSE;
	filer_window->had_cursor = FALSE;
	filer_window->auto_select = NULL;
	filer_window->toolbar_text = NULL;
	filer_window->target_cb = NULL;
	filer_window->mini_type = MINI_NONE;

	/* Finds the entry for this directory in the dir cache, creating
	 * a new one if needed. This does not cause a scan to start,
	 * so if a new entry is created then it will be empty.
	 */
	filer_window->directory = g_fscache_lookup(dir_cache,
						   filer_window->path);
	if (!filer_window->directory)
	{
		char	*error;

		error = g_strdup_printf(_("Directory '%s' not found."), path);
		delayed_error(PROJECT, error);
		g_free(error);
		g_free(filer_window->path);
		g_free(filer_window);
		return NULL;
	}

	filer_window->show_hidden = last_show_hidden;
	filer_window->temp_item_selected = FALSE;
	filer_window->sort_fn = last_sort_fn;
	filer_window->flags = (FilerFlags) 0;
	filer_window->details_type = DETAILS_SUMMARY;
	filer_window->display_style = UNKNOWN_STYLE;

	/* Add all the user-interface elements */
	filer_add_widgets(filer_window);

	/* Connect to all the signal handlers */
	filer_add_signals(filer_window);

	display_set_layout(filer_window, last_layout);

	gtk_widget_realize(filer_window->window);

	/* The collection is created empty and then attach() is called, which
	 * links the filer window to the entry in the directory cache we
	 * looked up / created above.
	 *
	 * The attach() function will immediately callback to the filer window
	 * to deliver a list of all known entries in the directory (so,
	 * collection->number_of_items may be valid after the call to
	 * attach() returns).
	 *
	 * BUT, if the directory was not in the cache (because it hadn't been
	 * opened it before) then the cached dir will be empty and nothing gets
	 * added until a while later when some entries are actually available.
	 */

	attach(filer_window);

	/* Make the window visible */
	number_of_windows++;
	all_filer_windows = g_list_prepend(all_filer_windows, filer_window);
	gtk_widget_show(filer_window->window);

	return filer_window;
}

/* This adds all the widgets to a new filer window. It is in a separate
 * function because filer_opendir() was getting too long...
 */
static void filer_add_widgets(FilerWindow *filer_window)
{
	GtkWidget *hbox, *vbox, *scrollbar, *collection;
	int	  col_height = ROW_HEIGHT_LARGE * o_initial_window_height;

	/* Create the top-level window widget */
	filer_window->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	filer_set_title(filer_window);

	/* The collection is the area that actually displays the files */
	collection = collection_new(NULL);

	gtk_object_set_data(GTK_OBJECT(collection),
			"filer_window", filer_window);
	filer_window->collection = COLLECTION(collection);

	gtk_window_set_default_size(GTK_WINDOW(filer_window->window),
		filer_window->display_style == LARGE_ICONS ? 400 : 512,
		o_toolbar == TOOLBAR_NONE ? col_height:
		o_toolbar == TOOLBAR_NORMAL ? col_height + 24 :
		col_height + 38);

	/* Scrollbar on the right, everything else on the left */
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(filer_window->window), hbox);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
	
	/* If there's a message that should go at the top of every
	 * window (eg 'Running as root'), add it here.
	 */
	if (show_user_message)
	{
		GtkWidget *label;

		label = gtk_label_new(show_user_message);
		gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);
		gtk_widget_show(label);
	}

	/* Create a frame for the toolbar, but don't show it unless we actually
	 * have a toolbar.
	 * (allows us to change the toolbar later)
	 */
	filer_window->toolbar_frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(filer_window->toolbar_frame),
			GTK_SHADOW_OUT);
	gtk_box_pack_start(GTK_BOX(vbox),
			filer_window->toolbar_frame, FALSE, TRUE, 0);

	/* If we want a toolbar, create it and put it in the frame */
	if (o_toolbar != TOOLBAR_NONE)
	{
		GtkWidget *toolbar;
		
		toolbar = toolbar_new(filer_window);
		gtk_container_add(GTK_CONTAINER(filer_window->toolbar_frame),
				toolbar);
		gtk_widget_show_all(filer_window->toolbar_frame);
	}

	/* Now add the area for displaying the files... */
	gtk_box_pack_start(GTK_BOX(vbox), collection, TRUE, TRUE, 0);

	/* And the minibuffer (hidden by default)... */
	create_minibuffer(filer_window);
	gtk_box_pack_start(GTK_BOX(vbox), filer_window->minibuffer_area,
				FALSE, TRUE, 0);

	/* Put the scrollbar on the left of everything else... */
	scrollbar = gtk_vscrollbar_new(COLLECTION(collection)->vadj);
	gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, TRUE, 0);

	/* Connect the menu's accelerator group to the window */
	gtk_accel_group_attach(filer_keys, GTK_OBJECT(filer_window->window));

	gtk_window_set_focus(GTK_WINDOW(filer_window->window), collection);

	gtk_widget_show(hbox);
	gtk_widget_show(vbox);
	gtk_widget_show(scrollbar);
	gtk_widget_show(collection);
}

static void filer_add_signals(FilerWindow *filer_window)
{
	GtkObject	*collection = GTK_OBJECT(filer_window->collection);
	GtkTargetEntry 	target_table[] =
	{
		{"text/uri-list", 0, TARGET_URI_LIST},
		{"STRING", 0, TARGET_STRING},
	};

	/* Events on the top-level window */
	gtk_widget_add_events(filer_window->window, GDK_ENTER_NOTIFY);
	gtk_signal_connect(GTK_OBJECT(filer_window->window),
			"enter-notify-event",
			GTK_SIGNAL_FUNC(pointer_in), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "focus_in_event",
			GTK_SIGNAL_FUNC(focus_in), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "destroy",
			filer_window_destroyed, filer_window);

	/* Events on the collection widget */
	gtk_widget_set_events(GTK_WIDGET(collection),
			GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
			GDK_BUTTON3_MOTION_MASK |
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

	gtk_signal_connect(collection, "gain_selection",
			gain_selection, filer_window);
	gtk_signal_connect(collection, "lose_selection",
			lose_selection, filer_window);
	gtk_signal_connect(collection, "selection_clear_event",
			GTK_SIGNAL_FUNC(collection_lose_selection), NULL);
	gtk_signal_connect(collection, "selection_get",
			GTK_SIGNAL_FUNC(selection_get), NULL);
	gtk_selection_add_targets(GTK_WIDGET(collection), GDK_SELECTION_PRIMARY,
			target_table,
			sizeof(target_table) / sizeof(*target_table));

	gtk_signal_connect(collection, "key_press_event",
			GTK_SIGNAL_FUNC(key_press_event), filer_window);
	gtk_signal_connect(collection, "button-release-event",
			GTK_SIGNAL_FUNC(coll_button_release), filer_window);
	gtk_signal_connect(collection, "button-press-event",
			GTK_SIGNAL_FUNC(coll_button_press), filer_window);
	gtk_signal_connect(collection, "motion-notify-event",
			GTK_SIGNAL_FUNC(coll_motion_notify), filer_window);

	/* Drag and drop events */
	gtk_signal_connect(collection, "drag_data_get", drag_data_get, NULL);
	drag_set_dest(filer_window);
}

static gint clear_scanning_display(FilerWindow *filer_window)
{
	if (filer_exists(filer_window))
		filer_set_title(filer_window);
	return FALSE;
}

static void set_scanning_display(FilerWindow *filer_window, gboolean scanning)
{
	if (scanning == filer_window->scanning)
		return;
	filer_window->scanning = scanning;

	if (scanning)
		filer_set_title(filer_window);
	else
		gtk_timeout_add(300, (GtkFunction) clear_scanning_display,
				filer_window);
}

/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options(void)
{
	GtkWidget	*vbox, *hbox, *label, *scale;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	toggle_unique_filer_windows =
		gtk_check_button_new_with_label(_("Unique windows"));
	OPTION_TIP(toggle_unique_filer_windows,
			_("If you open a directory and that directory is "
			"already displayed in another window, then this "
			"option causes the other window to be closed."));
	gtk_box_pack_start(GTK_BOX(vbox), toggle_unique_filer_windows,
			FALSE, TRUE, 0);

	adj_initial_window_height = GTK_ADJUSTMENT(gtk_adjustment_new(
							o_initial_window_height,
							2, 10, 1, 2, 0));
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);
	label = gtk_label_new(_("Initial window height "));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);
	scale = gtk_hscale_new(adj_initial_window_height);
	gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_LEFT);
	gtk_scale_set_digits(GTK_SCALE(scale), 0);
	gtk_box_pack_start(GTK_BOX(hbox), scale, TRUE, TRUE, 0);
	OPTION_TIP(scale, 
		_("This option sets the initial height of filer windows, "
		  "in rows of icons at Large Icons size."));
	
	return vbox;
}

/* Reflect current state by changing the widgets in the options box */
static void update_options()
{
	gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(toggle_unique_filer_windows),
			o_unique_filer_windows);
	gtk_adjustment_set_value(adj_initial_window_height,
				 o_initial_window_height);
}

/* Set current values by reading the states of the widgets in the options box */
static void set_options()
{
	o_unique_filer_windows = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_unique_filer_windows));

	o_initial_window_height = adj_initial_window_height->value;
}

static void save_options()
{
	guchar *str;
	
	option_write("filer_unique_windows",
			o_unique_filer_windows ? "1" : "0");
			
	str = g_strdup_printf("%d", o_initial_window_height);
	option_write("filer_initial_window_height", str);
	g_free(str);
}

static char *filer_unique_windows(char *data)
{
	o_unique_filer_windows = atoi(data) != 0;
	return NULL;
}

static char *filer_initial_window_height(char *data)
{
	o_initial_window_height = atoi(data);
	return NULL;
}

/* Note that filer_window may not exist after this call. */
void filer_update_dir(FilerWindow *filer_window, gboolean warning)
{
	if (may_rescan(filer_window, warning))
		dir_update(filer_window->directory, filer_window->path);
}

/* Refresh the various caches even if we don't think we need to */
void full_refresh(void)
{
	mount_update(TRUE);
}

/* See whether a filer window with a given path already exists
 * and is different from diff.
 */
static FilerWindow *find_filer_window(char *path, FilerWindow *diff)
{
	GList	*next = all_filer_windows;

	while (next)
	{
		FilerWindow *filer_window = (FilerWindow *) next->data;

		if (filer_window != diff &&
		    	strcmp(path, filer_window->path) == 0)
		{
			return filer_window;
		}

		next = next->next;
	}
	
	return NULL;
}

/* This path has been mounted/umounted/deleted some files - update all dirs */
void filer_check_mounted(char *path)
{
	GList	*next = all_filer_windows;
	char	*slash;
	int	len;

	len = strlen(path);

	while (next)
	{
		FilerWindow *filer_window = (FilerWindow *) next->data;

		next = next->next;

		if (strncmp(path, filer_window->path, len) == 0)
		{
			char	s = filer_window->path[len];

			if (s == '/' || s == '\0')
				filer_update_dir(filer_window, FALSE);
		}
	}

	slash = strrchr(path, '/');
	if (slash && slash != path)
	{
		guchar	*parent;

		parent = g_strndup(path, slash - path);

		refresh_dirs(parent);

		g_free(parent);
	}

	pinboard_may_update(path);
	panel_may_update(path);
}

/* Like minibuffer_show(), except that:
 * - It returns FALSE (to be used from an idle callback)
 * - It checks that the filer window still exists.
 */
static gboolean minibuffer_show_cb(FilerWindow *filer_window)
{
	if (filer_exists(filer_window))
		minibuffer_show(filer_window, MINI_PATH);
	return FALSE;
}

gboolean filer_exists(FilerWindow *filer_window)
{
	GList	*next;

	for (next = all_filer_windows; next; next = next->next)
	{
		FilerWindow *fw = (FilerWindow *) next->data;

		if (fw == filer_window)
			return TRUE;
	}

	return FALSE;
}

static void filer_set_title(FilerWindow *filer_window)
{
	guchar	*title = NULL;
	guchar	*scanning = filer_window->scanning ? _(" (Scanning)") : "";

	if (home_dir_len > 1 &&
		strncmp(filer_window->path, home_dir, home_dir_len) == 0)
	{
		guchar 	sep = filer_window->path[home_dir_len];

		if (sep == '\0' || sep == '/')
			title = g_strconcat("~",
					filer_window->path + home_dir_len,
					scanning,
					NULL);
	}
	
	if (!title)
		title = g_strconcat(filer_window->path, scanning, NULL);

	gtk_window_set_title(GTK_WINDOW(filer_window->window), title);
	g_free(title);
}

/* Reconnect to the same directory (used when the Show Hidden option is
 * toggled).
 */
void filer_detach_rescan(FilerWindow *filer_window)
{
	Directory *dir = filer_window->directory;
	
	g_fscache_data_ref(dir_cache, dir);
	detach(filer_window);
	filer_window->directory = dir;
	attach(filer_window);
}

static gint coll_button_release(GtkWidget *widget,
			        GdkEventButton *event,
			        FilerWindow *filer_window)
{
	if (dnd_motion_release(event))
	{
		if (motion_buttons_pressed == 0 &&
					filer_window->collection->lasso_box)
			collection_end_lasso(filer_window->collection, TRUE);
		return TRUE;
	}

	perform_action(filer_window, event);

	return TRUE;
}

static void perform_action(FilerWindow *filer_window, GdkEventButton *event)
{
	Collection	*collection = filer_window->collection;
	DirItem		*dir_item;
	int		item;
	BindAction	action;
	gboolean	press = event->type == GDK_BUTTON_PRESS;
	gboolean	selected = FALSE;
	OpenFlags	flags = 0;

	if (event->button > 3)
		return;

	item = collection_get_item(collection, event->x, event->y);

	if (filer_window->target_cb)
	{
		dnd_motion_ungrab();
		if (item != -1 && press && event->button == 1)
			filer_window->target_cb(filer_window, item,
					filer_window->target_data);
		filer_target_mode(filer_window, NULL, NULL, NULL);

		return;
	}

	action = bind_lookup_bev(
			item == -1 ? BIND_DIRECTORY : BIND_DIRECTORY_ICON,
			event);

	if (item != -1)
	{
		dir_item = (DirItem *) collection->items[item].data;
		selected = collection->items[item].selected;
	}
	else
		dir_item = NULL;

	switch (action)
	{
		case ACT_CLEAR_SELECTION:
			collection_clear_selection(collection);
			break;
		case ACT_TOGGLE_SELECTED:
			collection_toggle_item(collection, item);
			break;
		case ACT_SELECT_EXCL:
			collection_clear_except(collection, item);
			break;
		case ACT_EDIT_ITEM:
			flags |= OPEN_SHIFT;
			/* (no break) */
		case ACT_OPEN_ITEM:
			if (event->button != 1)
				flags |= OPEN_CLOSE_WINDOW;
			else
				flags |= OPEN_SAME_WINDOW;
			if (o_new_window_on_1)
				flags ^= OPEN_SAME_WINDOW;
			if (event->type == GDK_2BUTTON_PRESS)
				collection_unselect_item(collection, item);
			dnd_motion_ungrab();
			filer_openitem(filer_window, item, flags);
			break;
		case ACT_POPUP_MENU:
			dnd_motion_ungrab();
			show_filer_menu(filer_window, event, item);
			break;
		case ACT_PRIME_AND_SELECT:
			if (!selected)
				collection_clear_except(collection, item);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_AND_TOGGLE:
			collection_toggle_item(collection, item);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_FOR_DND:
			collection_wink_item(collection, item);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_IGNORE:
			if (press && event->button < 4)
			{
				if (item)
					collection_wink_item(collection, item);
				dnd_motion_start(MOTION_NONE);
			}
			break;
		case ACT_LASSO_CLEAR:
			collection_clear_selection(collection);
			/* (no break) */
		case ACT_LASSO_MODIFY:
			collection_lasso_box(collection, event->x, event->y);
			break;
		default:
			g_warning("Unsupported action : %d\n", action);
			break;
	}
}

static gint coll_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      FilerWindow *filer_window)
{
	collection_set_cursor_item(filer_window->collection, -1);

	if (dnd_motion_press(widget, event))
		perform_action(filer_window, event);

	return TRUE;
}

static gint coll_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       FilerWindow *filer_window)
{
	Collection	*collection = filer_window->collection;
	int		i;

	if (motion_state != MOTION_READY_FOR_DND)
		return TRUE;

	if (!dnd_motion_moved(event))
		return TRUE;

	i = collection_get_item(collection,
			event->x - (event->x_root - drag_start_x),
			event->y - (event->y_root - drag_start_y));
	if (i == -1)
		return TRUE;

	collection_wink_item(collection, -1);
	
	if (!collection->items[i].selected)
	{
		if (event->state & GDK_BUTTON1_MASK)
		{
			/* Select just this one */
			collection_clear_except(collection, i);
			filer_window->temp_item_selected = TRUE;
		}
		else
		{
			if (collection->number_selected == 0)
				filer_window->temp_item_selected = TRUE;
			collection_select_item(collection, i);
		}
	}

	g_return_val_if_fail(collection->number_selected > 0, TRUE);

	if (collection->number_selected == 1)
	{
		DirItem	*item = (DirItem *) collection->items[i].data;

		drag_one_item(widget, event,
			make_path(filer_window->path, item->leafname)->str,
			item);
	}
	else
	{
		GString *uris;
	
		uris = g_string_new(NULL);
		create_uri_list(filer_window, uris);
		drag_selection(widget, event, uris->str);
		g_string_free(uris, TRUE);
	}

	return TRUE;
}

/* Puts the filer window into target mode. When an item is chosen,
 * fn(filer_window, item, data) is called. 'reason' will be displayed
 * on the toolbar while target mode is active.
 *
 * Use fn == NULL to cancel target mode.
 */
void filer_target_mode(FilerWindow *filer_window,
			TargetFunc fn,
			gpointer data,
			char	 *reason)
{
	if (fn != filer_window->target_cb)
		gdk_window_set_cursor(
				GTK_WIDGET(filer_window->collection)->window,
				fn ? crosshair : NULL);

	filer_window->target_cb = fn;
	filer_window->target_data = data;

	if (filer_window->toolbar_text)
		gtk_label_set_text(GTK_LABEL(filer_window->toolbar_text),
				fn ? reason : "");
}
