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

#include "main.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "pixmaps.h"
#include "menu.h"
#include "dnd.h"
#include "run.h"
#include "mount.h"
#include "type.h"
#include "options.h"
#include "minibuffer.h"
#include "pinboard.h"

#define PANEL_BORDER 2

extern int collection_menu_button;
extern gboolean collection_single_click;

FilerWindow 	*window_with_focus = NULL;
GList		*all_filer_windows = NULL;

static FilerWindow *window_with_selection = NULL;

/* Options bits */
static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();
static char *filer_single_click(char *data);
static char *filer_unique_windows(char *data);
static char *filer_menu_on_2(char *data);
static char *filer_new_window_on_1(char *data);
static char *filer_toolbar(char *data);

static OptionsSection options =
{
	N_("Filer window options"),
	create_options,
	update_options,
	set_options,
	save_options
};

/* The values correspond to the menu indexes in the option widget */
typedef enum {
	TOOLBAR_NONE 	= 0,
	TOOLBAR_NORMAL 	= 1,
	TOOLBAR_GNOME 	= 2,
} ToolbarType;
static ToolbarType o_toolbar = TOOLBAR_NORMAL;
static GtkWidget *menu_toolbar;

static gboolean o_single_click = TRUE;
static gboolean o_new_window_on_1 = FALSE;	/* Button 1 => New window */
gboolean o_unique_filer_windows = FALSE;
static GtkWidget *toggle_single_click;
static GtkWidget *toggle_new_window_on_1;
static GtkWidget *toggle_menu_on_2;
static GtkWidget *toggle_unique_filer_windows;

/* Static prototypes */
static void attach(FilerWindow *filer_window);
static void detach(FilerWindow *filer_window);
static void filer_window_destroyed(GtkWidget    *widget,
				   FilerWindow	*filer_window);
static void show_menu(Collection *collection, GdkEventButton *event,
		int number_selected, gpointer user_data);
static gint focus_in(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window);
static gint focus_out(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window);
static void add_item(FilerWindow *filer_window, DirItem *item);
static void toolbar_up_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_home_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void add_button(GtkWidget *box, MaskedPixmap *icon,
			GtkSignalFunc cb, FilerWindow *filer_window,
			char *label, char *tip);
static GtkWidget *create_toolbar(FilerWindow *filer_window);
static int filer_confirm_close(GtkWidget *widget, GdkEvent *event,
				FilerWindow *window);
static void update_display(Directory *dir,
			DirAction	action,
			GPtrArray	*items,
			FilerWindow *filer_window);
static void set_scanning_display(FilerWindow *filer_window, gboolean scanning);
static gboolean may_rescan(FilerWindow *filer_window, gboolean warning);
static void open_item(Collection *collection,
		gpointer item_data, int item_number,
		gpointer user_data);
static gboolean minibuffer_show_cb(FilerWindow *filer_window);
static FilerWindow *find_filer_window(char *path, FilerWindow *diff);
static void filer_set_title(FilerWindow *filer_window);

static GdkAtom xa_string;
enum
{
	TARGET_STRING,
	TARGET_URI_LIST,
};

static GdkCursor *busy_cursor = NULL;
static GtkTooltips *tooltips = NULL;

void filer_init()
{
	xa_string = gdk_atom_intern("STRING", FALSE);

	options_sections = g_slist_prepend(options_sections, &options);
	option_register("filer_new_window_on_1", filer_new_window_on_1);
	option_register("filer_menu_on_2", filer_menu_on_2);
	option_register("filer_single_click", filer_single_click);
	option_register("filer_unique_windows", filer_unique_windows);
	option_register("filer_toolbar", filer_toolbar);

	busy_cursor = gdk_cursor_new(GDK_WATCH);

	tooltips = gtk_tooltips_new();
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
		window_with_focus = NULL;

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

static void show_menu(Collection *collection, GdkEventButton *event,
		int item, gpointer user_data)
{
	show_filer_menu((FilerWindow *) user_data, event, item);
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

static void open_item(Collection *collection,
		gpointer item_data, int item_number,
		gpointer user_data)
{
	FilerWindow	*filer_window = (FilerWindow *) user_data;
	GdkEvent 	*event;
	GdkEventButton 	*bevent;
	GdkEventKey 	*kevent;
	OpenFlags	flags = 0;

	event = (GdkEvent *) gtk_get_current_event();

	bevent = (GdkEventButton *) event;
	kevent = (GdkEventKey *) event;

	switch (event->type)
	{
		case GDK_2BUTTON_PRESS:
		case GDK_BUTTON_PRESS:
		case GDK_BUTTON_RELEASE:
			if (bevent->state & GDK_SHIFT_MASK)
				flags |= OPEN_SHIFT;

			if (o_new_window_on_1 ^ (bevent->button == 1))
				flags |= OPEN_SAME_WINDOW;
			
			if (bevent->button != 1)
				flags |= OPEN_CLOSE_WINDOW;
			
			if (o_single_click == FALSE &&
				(bevent->state & GDK_CONTROL_MASK) != 0)
				flags ^= OPEN_SAME_WINDOW | OPEN_CLOSE_WINDOW;
			break;
		case GDK_KEY_PRESS:
			flags |= OPEN_SAME_WINDOW;
			if (kevent->state & GDK_SHIFT_MASK)
				flags |= OPEN_SHIFT;
			break;
		default:
			break;
	}

	filer_openitem(filer_window, item_number, flags);
}

/* Open the item (or add it to the shell command minibuffer) */
void filer_openitem(FilerWindow *filer_window, int item_number, OpenFlags flags)
{
	gboolean	shift = (flags & OPEN_SHIFT) != 0;
	gboolean	close_mini = flags & OPEN_FROM_MINI;
	gboolean	close_window = (flags & OPEN_CLOSE_WINDOW) != 0
					&& !filer_window->panel_type;
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

static gint focus_out(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window)
{
	/* TODO: Shade the cursor */

	return FALSE;
}

/* Handle keys that can't be bound with the menu */
static gint key_press_event(GtkWidget	*widget,
			GdkEventKey	*event,
			FilerWindow	*filer_window)
{
	switch (event->keyval)
	{
		case GDK_BackSpace:
			change_to_parent(filer_window);
			break;
		default:
			return FALSE;
	}

	return TRUE;
}

static void toolbar_help_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	filer_opendir(make_path(getenv("APP_DIR"), "Help")->str, PANEL_NO);
}

static void toolbar_refresh_clicked(GtkWidget *widget,
				    FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE &&
		((GdkEventButton *) event)->button != 1)
	{
		filer_opendir(filer_window->path, PANEL_NO);
	}
	else
	{
		full_refresh();
		filer_update_dir(filer_window, TRUE);
	}
}

static void toolbar_home_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE &&
		((GdkEventButton *) event)->button != 1)
	{
		filer_opendir(home_dir, PANEL_NO);
	}
	else
		filer_change_to(filer_window, home_dir, NULL);
}

static void toolbar_up_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE &&
		((GdkEventButton *) event)->button != 1)
	{
		filer_open_parent(filer_window);
	}
	else
		change_to_parent(filer_window);
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
	char	*real_path = pathdup(path);
	
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

	filer_window->directory = g_fscache_lookup(dir_cache,
						   filer_window->path);
	if (filer_window->directory)
	{
		g_free(filer_window->auto_select);
		filer_window->had_cursor =
			filer_window->collection->cursor_item != -1
			|| filer_window->had_cursor;
		filer_window->auto_select = from_dup;

		filer_set_title(filer_window);
		collection_set_cursor_item(filer_window->collection, -1);
		attach(filer_window);

		if (filer_window->mini_type == MINI_PATH)
			gtk_idle_add((GtkFunction) minibuffer_show_cb,
					filer_window);
	}
	else
	{
		char	*error;

		g_free(from_dup);
		error = g_strdup_printf(_("Directory '%s' is not accessible"),
				path);
		delayed_error(PROJECT, error);
		g_free(error);
		gtk_widget_destroy(filer_window->window);
	}
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
		filer_opendir(*copy ? copy : "/", PANEL_NO);
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

static int filer_confirm_close(GtkWidget *widget, GdkEvent *event,
				FilerWindow *window)
{
	/* TODO: We can open lots of these - very irritating! */
	return get_choice(_("Close panel?"),
		      _("You have tried to close a panel via the window "
			"manager - I usually find that this is accidental... "
			"really close?"),
			2, _("Remove"), _("Cancel")) != 0;
}

FilerWindow *filer_opendir(char *path, PanelType panel_type)
{
	GtkWidget	*hbox, *scrollbar, *collection;
	FilerWindow	*filer_window;
	GtkTargetEntry 	target_table[] =
	{
		{"text/uri-list", 0, TARGET_URI_LIST},
		{"STRING", 0, TARGET_STRING},
	};
	char		*real_path;
	
	real_path = pathdup(path);

	if (o_unique_filer_windows && panel_type == PANEL_NO)
	{
		FilerWindow *fw;
		
		fw = find_filer_window(real_path, NULL);
		
		if (fw)
		{
			    /* TODO: this should bring the window to the front
			     * at the same coordinates.
			     */
			    gtk_widget_hide(fw->window);
			    gtk_widget_show(fw->window);
			    g_free(real_path);
			    return fw;
		}
	}

	filer_window = g_new(FilerWindow, 1);
	filer_window->minibuffer = NULL;
	filer_window->minibuffer_label = NULL;
	filer_window->minibuffer_area = NULL;
	filer_window->path = real_path;
	filer_window->scanning = FALSE;
	filer_window->had_cursor = FALSE;
	filer_window->auto_select = NULL;
	filer_window->mini_type = MINI_NONE;

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
	filer_window->panel_type = panel_type;
	filer_window->temp_item_selected = FALSE;
	filer_window->sort_fn = last_sort_fn;
	filer_window->flags = (FilerFlags) 0;
	filer_window->details_type = last_details_type;
	filer_window->display_style = UNKNOWN_STYLE;

	filer_window->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	filer_set_title(filer_window);

	collection = collection_new(NULL);
	gtk_object_set_data(GTK_OBJECT(collection),
			"filer_window", filer_window);
	filer_window->collection = COLLECTION(collection);

	gtk_widget_add_events(filer_window->window, GDK_ENTER_NOTIFY);
	gtk_signal_connect(GTK_OBJECT(filer_window->window),
			"enter-notify-event",
			GTK_SIGNAL_FUNC(pointer_in), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "focus_in_event",
			GTK_SIGNAL_FUNC(focus_in), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "focus_out_event",
			GTK_SIGNAL_FUNC(focus_out), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "destroy",
			filer_window_destroyed, filer_window);

	gtk_signal_connect(GTK_OBJECT(filer_window->collection), "open_item",
			open_item, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "show_menu",
			show_menu, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "gain_selection",
			gain_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "lose_selection",
			lose_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "drag_selection",
			drag_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "drag_data_get",
			drag_data_get, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "selection_clear_event",
			GTK_SIGNAL_FUNC(collection_lose_selection), NULL);
	gtk_signal_connect (GTK_OBJECT(collection), "selection_get",
			GTK_SIGNAL_FUNC(selection_get), NULL);
	gtk_selection_add_targets(collection, GDK_SELECTION_PRIMARY,
			target_table,
			sizeof(target_table) / sizeof(*target_table));

	display_style_set(filer_window, last_display_style);
	drag_set_dest(filer_window);

	if (panel_type)
	{
		int		swidth, sheight, iwidth, iheight;
		GtkWidget	*frame, *win = filer_window->window;

		gtk_window_set_wmclass(GTK_WINDOW(win), "ROX-Panel",
				PROJECT);
		collection_set_panel(filer_window->collection, TRUE);
		gtk_signal_connect(GTK_OBJECT(filer_window->window),
				"delete_event",
				GTK_SIGNAL_FUNC(filer_confirm_close),
				filer_window);

		gdk_window_get_size(GDK_ROOT_PARENT(), &swidth, &sheight);
		iwidth = filer_window->collection->item_width;
		iheight = filer_window->collection->item_height;
		
		{
			int	height = iheight + PANEL_BORDER;
			int	y = panel_type == PANEL_TOP 
					? 0
					: sheight - height - PANEL_BORDER;

			gtk_widget_set_usize(collection, swidth, height);
			gtk_widget_set_uposition(win, 0, y);
		}

		frame = gtk_frame_new(NULL);
		gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
		gtk_container_add(GTK_CONTAINER(frame), collection);
		gtk_container_add(GTK_CONTAINER(win), frame);

		gtk_widget_show_all(frame);
		gtk_widget_realize(win);
		if (override_redirect)
			gdk_window_set_override_redirect(win->window, TRUE);
		make_panel_window(win->window);
	}
	else
	{
		GtkWidget	*vbox;
		int		col_height = ROW_HEIGHT_LARGE * 3;

		gtk_signal_connect(GTK_OBJECT(collection),
				"key_press_event",
				GTK_SIGNAL_FUNC(key_press_event), filer_window);
		gtk_window_set_default_size(GTK_WINDOW(filer_window->window),
			filer_window->display_style == LARGE_ICONS ? 400 : 512,
			o_toolbar == TOOLBAR_NONE ? col_height:
			o_toolbar == TOOLBAR_NORMAL ? col_height + 24 :
			col_height + 38);

		hbox = gtk_hbox_new(FALSE, 0);
		gtk_container_add(GTK_CONTAINER(filer_window->window),
					hbox);

		vbox = gtk_vbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
		
		if (o_toolbar != TOOLBAR_NONE)
		{
			GtkWidget *toolbar;
			
			toolbar = create_toolbar(filer_window);
			gtk_box_pack_start(GTK_BOX(vbox), toolbar,
					FALSE, TRUE, 0);
			gtk_widget_show_all(toolbar);
		}

		gtk_box_pack_start(GTK_BOX(vbox), collection, TRUE, TRUE, 0);

		create_minibuffer(filer_window);
		gtk_box_pack_start(GTK_BOX(vbox), filer_window->minibuffer_area,
					FALSE, TRUE, 0);

		scrollbar = gtk_vscrollbar_new(COLLECTION(collection)->vadj);
		gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, TRUE, 0);
		gtk_accel_group_attach(filer_keys,
				GTK_OBJECT(filer_window->window));
		gtk_window_set_focus(GTK_WINDOW(filer_window->window),
				collection);

		gtk_widget_show(hbox);
		gtk_widget_show(vbox);
		gtk_widget_show(scrollbar);
		gtk_widget_show(collection);
	}

	number_of_windows++;
	gtk_widget_show(filer_window->window);
	attach(filer_window);

	all_filer_windows = g_list_prepend(all_filer_windows, filer_window);

	return filer_window;
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

static GtkWidget *create_toolbar(FilerWindow *filer_window)
{
	GtkWidget	*frame, *box;

	if (o_toolbar == TOOLBAR_GNOME)
	{
		frame = gtk_handle_box_new();
		box = gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL,
				GTK_TOOLBAR_BOTH);
		gtk_container_set_border_width(GTK_CONTAINER(box), 2);
		gtk_toolbar_set_space_style(GTK_TOOLBAR(box),
					GTK_TOOLBAR_SPACE_LINE);
		gtk_toolbar_set_button_relief(GTK_TOOLBAR(box),
					GTK_RELIEF_NONE);
	}
	else
	{
		frame = gtk_frame_new(NULL);
		gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);

		box = gtk_hbutton_box_new();
		gtk_button_box_set_child_size_default(16, 16);
		gtk_hbutton_box_set_spacing_default(0);
		gtk_button_box_set_layout(GTK_BUTTON_BOX(box),
				GTK_BUTTONBOX_START);
	}

	gtk_container_add(GTK_CONTAINER(frame), box);

	add_button(box, im_up_icon,
			GTK_SIGNAL_FUNC(toolbar_up_clicked),
			filer_window,
			_("Up"), _("Change to parent directory"));
	add_button(box, im_home_icon,
			GTK_SIGNAL_FUNC(toolbar_home_clicked),
			filer_window,
			_("Home"), _("Change to home directory"));
	add_button(box, im_refresh_icon,
			GTK_SIGNAL_FUNC(toolbar_refresh_clicked),
			filer_window,
			_("Rescan"), _("Rescan directory contents"));
	add_button(box, im_help,
			GTK_SIGNAL_FUNC(toolbar_help_clicked),
			filer_window,
			_("Help"), _("Show ROX-Filer help"));

	return frame;
}

/* This is used to simulate a click when button 3 is used (GtkButton
 * normally ignores this). Currently, this button does not pop in -
 * this may be fixed in future versions of GTK+.
 */
static gint toolbar_other_button = 0;
static gint toolbar_adjust_pressed(GtkButton *button,
				GdkEventButton *event,
				FilerWindow *filer_window)
{
	gint	b = event->button;

	if ((b == 2 || b == 3) && toolbar_other_button == 0)
	{
		toolbar_other_button = event->button;
		gtk_grab_add(GTK_WIDGET(button));
		gtk_button_pressed(button);
	}

	return TRUE;
}

static gint toolbar_adjust_released(GtkButton *button,
				GdkEventButton *event,
				FilerWindow *filer_window)
{
	if (event->button == toolbar_other_button)
	{
		toolbar_other_button = 0;
		gtk_grab_remove(GTK_WIDGET(button));
		gtk_button_released(button);
	}

	return TRUE;
}

static void add_button(GtkWidget *box, MaskedPixmap *icon,
			GtkSignalFunc cb, FilerWindow *filer_window,
			char *label, char *tip)
{
	GtkWidget 	*button, *icon_widget;

	icon_widget = gtk_pixmap_new(icon->pixmap, icon->mask);

	if (o_toolbar == TOOLBAR_GNOME)
	{
		gtk_toolbar_append_element(GTK_TOOLBAR(box),
				GTK_TOOLBAR_CHILD_BUTTON,
				NULL,
				label,
				tip, NULL,
				icon_widget,
				cb, filer_window);
	}
	else
	{
		button = gtk_button_new();
		gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
		GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);

		gtk_container_add(GTK_CONTAINER(button), icon_widget);
		gtk_signal_connect(GTK_OBJECT(button), "button_press_event",
			GTK_SIGNAL_FUNC(toolbar_adjust_pressed), filer_window);
		gtk_signal_connect(GTK_OBJECT(button), "button_release_event",
			GTK_SIGNAL_FUNC(toolbar_adjust_released), filer_window);
		gtk_signal_connect(GTK_OBJECT(button), "clicked",
				cb, filer_window);

		gtk_tooltips_set_tip(tooltips, button, tip, NULL);

		gtk_container_add(GTK_CONTAINER(box), button);
	}
}

/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options()
{
	GtkWidget	*vbox, *menu, *hbox;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	toggle_new_window_on_1 =
		gtk_check_button_new_with_label(
			_("New window on button 1 (RISC OS style)"));
	gtk_box_pack_start(GTK_BOX(vbox), toggle_new_window_on_1,
			FALSE, TRUE, 0);

	toggle_menu_on_2 =
		gtk_check_button_new_with_label(
			_("Menu on button 2 (RISC OS style)"));
	gtk_box_pack_start(GTK_BOX(vbox), toggle_menu_on_2, FALSE, TRUE, 0);

	toggle_single_click =
		gtk_check_button_new_with_label(_("Single-click nagivation"));
	gtk_box_pack_start(GTK_BOX(vbox), toggle_single_click, FALSE, TRUE, 0);

	toggle_unique_filer_windows =
		gtk_check_button_new_with_label(_("Unique windows"));
	gtk_box_pack_start(GTK_BOX(vbox), toggle_unique_filer_windows,
			FALSE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(hbox),
			gtk_label_new(_("Toolbar type for new windows")),
			FALSE, TRUE, 0);
	menu_toolbar = gtk_option_menu_new();
	menu = gtk_menu_new();
	gtk_menu_append(GTK_MENU(menu),
			gtk_menu_item_new_with_label(_("None")));
	gtk_menu_append(GTK_MENU(menu),
			gtk_menu_item_new_with_label(_("Normal")));
	gtk_menu_append(GTK_MENU(menu),
			gtk_menu_item_new_with_label(_("GNOME")));
	gtk_option_menu_set_menu(GTK_OPTION_MENU(menu_toolbar), menu);
	gtk_box_pack_start(GTK_BOX(hbox), menu_toolbar, TRUE, TRUE, 0);

	return vbox;
}

/* Reflect current state by changing the widgets in the options box */
static void update_options()
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_new_window_on_1),
			o_new_window_on_1);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_menu_on_2),
			collection_menu_button == 2 ? 1 : 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_single_click),
			o_single_click);
	gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(toggle_unique_filer_windows),
			o_unique_filer_windows);
	gtk_option_menu_set_history(GTK_OPTION_MENU(menu_toolbar), o_toolbar);
}

/* Set current values by reading the states of the widgets in the options box */
static void set_options()
{
	GtkWidget 	*item, *menu;
	GList		*list;
	
	o_new_window_on_1 = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_new_window_on_1));

	collection_menu_button = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_menu_on_2)) ? 2 : 3;

	o_single_click = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_single_click));

	o_unique_filer_windows = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_unique_filer_windows));

	collection_single_click = o_single_click ? TRUE : FALSE;
	
	menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(menu_toolbar));
	item = gtk_menu_get_active(GTK_MENU(menu));
	list = gtk_container_children(GTK_CONTAINER(menu));
	o_toolbar = (ToolbarType) g_list_index(list, item);
	g_list_free(list);
}

static void save_options()
{
	option_write("filer_new_window_on_1", o_new_window_on_1 ? "1" : "0");
	option_write("filer_menu_on_2",
			collection_menu_button == 2 ? "1" : "0");
	option_write("filer_single_click", o_single_click ? "1" : "0");
	option_write("filer_unique_windows",
			o_unique_filer_windows ? "1" : "0");
	option_write("filer_toolbar", o_toolbar == TOOLBAR_NONE ? "None" :
				      o_toolbar == TOOLBAR_NORMAL ? "Normal" :
				      o_toolbar == TOOLBAR_GNOME ? "GNOME" :
				      "Unknown");
}

static char *filer_new_window_on_1(char *data)
{
	o_new_window_on_1 = atoi(data) != 0;
	return NULL;
}

static char *filer_menu_on_2(char *data)
{
	collection_menu_button = atoi(data) != 0 ? 2 : 3;
	return NULL;
}

static char *filer_single_click(char *data)
{
	o_single_click = atoi(data) != 0;
	collection_single_click = o_single_click ? TRUE : FALSE;
	return NULL;
}

static char *filer_unique_windows(char *data)
{
	o_unique_filer_windows = atoi(data) != 0;
	return NULL;
}

static char *filer_toolbar(char *data)
{
	if (g_strcasecmp(data, "None") == 0)
		o_toolbar = TOOLBAR_NONE;
	else if (g_strcasecmp(data, "Normal") == 0)
		o_toolbar = TOOLBAR_NORMAL;
	else if (g_strcasecmp(data, "GNOME") == 0)
		o_toolbar = TOOLBAR_GNOME;
	else
		return _("Unknown toolbar type");

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

		if (filer_window->panel_type == PANEL_NO &&
			filer_window != diff &&
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
	if (filer_window->scanning)
	{
		guchar	*title;

		title = g_strdup_printf(_("%s (Scanning)"), filer_window->path);
		gtk_window_set_title(GTK_WINDOW(filer_window->window),
				title);
		g_free(title);
	}
	else
		gtk_window_set_title(GTK_WINDOW(filer_window->window),
				filer_window->path);
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
