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

/* filer.c - code for handling filer windows */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <netdb.h>
#include <sys/param.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"

#include "collection.h"
#include "display.h"
#include "main.h"
#include "fscache.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "choices.h"
#include "pixmaps.h"
#include "menu.h"
#include "dnd.h"
#include "dir.h"
#include "diritem.h"
#include "run.h"
#include "type.h"
#include "options.h"
#include "minibuffer.h"
#include "icon.h"
#include "toolbar.h"
#include "bind.h"
#include "appinfo.h"
#include "mount.h"
#include "xml.h"
#include "view_iface.h"
#include "view_collection.h"

static XMLwrapper *groups = NULL;

/* This is rather badly named. It's actually the filer window which received
 * the last key press or Menu click event.
 */
FilerWindow 	*window_with_focus = NULL;

GList		*all_filer_windows = NULL;

static FilerWindow *window_with_primary = NULL;

/* Static prototypes */
static void attach(FilerWindow *filer_window);
static void detach(FilerWindow *filer_window);
static void filer_window_destroyed(GtkWidget    *widget,
				   FilerWindow	*filer_window);
static void update_display(Directory *dir,
			DirAction	action,
			GPtrArray	*items,
			FilerWindow *filer_window);
static void set_scanning_display(FilerWindow *filer_window, gboolean scanning);
static gboolean may_rescan(FilerWindow *filer_window, gboolean warning);
static gboolean minibuffer_show_cb(FilerWindow *filer_window);
static FilerWindow *find_filer_window(const char *sym_path, FilerWindow *diff);
static void filer_add_widgets(FilerWindow *filer_window);
static void filer_add_signals(FilerWindow *filer_window);
static void filer_size_for(FilerWindow *filer_window,
			   int w, int h, int n, gboolean allow_shrink);

static void set_selection_state(FilerWindow *filer_window, gboolean normal);
static void filer_next_thumb(GObject *window, const gchar *path);
static void start_thumb_scanning(FilerWindow *filer_window);
static void filer_options_changed(void);
static void set_style_by_number_of_items(FilerWindow *filer_window);

static GdkCursor *busy_cursor = NULL;
static GdkCursor *crosshair = NULL;

/* Indicates whether the filer's display is different to the machine it
 * is actually running on.
 */
static gboolean not_local = FALSE;

static Option o_filer_change_size, o_filer_change_size_num;
static Option o_filer_size_limit, o_short_flag_names;
Option o_filer_auto_resize, o_unique_filer_windows;

void filer_init(void)
{
	const gchar *ohost;
	const gchar *dpy;
	gchar *dpyhost, *tmp;
  
	option_add_int(&o_filer_size_limit, "filer_size_limit", 75);
	option_add_int(&o_filer_auto_resize, "filer_auto_resize",
							RESIZE_ALWAYS);
	option_add_int(&o_unique_filer_windows, "filer_unique_windows", 0);

	option_add_int(&o_short_flag_names, "filer_short_flag_names", FALSE);

	option_add_int(&o_filer_change_size, "filer_change_size", TRUE);
	option_add_int(&o_filer_change_size_num, "filer_change_size_num", 30); 

	option_add_notify(filer_options_changed);

	busy_cursor = gdk_cursor_new(GDK_WATCH);
	crosshair = gdk_cursor_new(GDK_CROSSHAIR);

	/* Is the display on the local machine, or are we being
	 * run remotely? See filer_set_title().
	 */
	ohost = our_host_name();
	dpy = gdk_get_display();
	dpyhost = g_strdup(dpy);
	tmp = strchr(dpyhost, ':');
	if (tmp) 
	        *tmp = '\0';

	if (dpyhost[0] && strcmp(ohost, dpyhost) != 0)
	{
	        /* Try the cannonical name for dpyhost (see our_host_name()
	         * in support.c).
		 */
	        struct hostent *ent;
		
		ent = gethostbyname(dpyhost);
		if (!ent || strcmp(ohost, ent->h_name) != 0)
		        not_local = TRUE;
	}
	
	g_free(dpyhost);
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

/* Resize the filer window to w x h pixels, plus border (not clamped) */
static void filer_window_set_size(FilerWindow *filer_window,
				  int w, int h,
				  gboolean allow_shrink)
{
	GtkWidget *window;

	g_return_if_fail(filer_window != NULL);

	if (filer_window->scrollbar)
		w += filer_window->scrollbar->allocation.width;
	
	if (o_toolbar.int_value != TOOLBAR_NONE)
		h += filer_window->toolbar->allocation.height;
	if (filer_window->message)
		h += filer_window->message->allocation.height;

	window = filer_window->window;

	if (GTK_WIDGET_VISIBLE(window))
	{
		gint x, y;
		GtkRequisition	*req = &window->requisition;
		GdkWindow *gdk_window = window->window;

		w = MAX(req->width, w);
		h = MAX(req->height, h);
		gdk_window_get_position(gdk_window, &x, &y);

		if (!allow_shrink)
		{
			gint	old_w, old_h;

			gdk_drawable_get_size(gdk_window, &old_w, &old_h);
			w = MAX(w, old_w);
			h = MAX(h, old_h);

			if (w == old_w && h == old_h)
				return;
		}

		if (x + w > screen_width || y + h > screen_height)
		{
			if (x + w > screen_width)
				x = screen_width - w - 4;
			if (y + h > screen_height)
				y = screen_height - h - 4;
			gdk_window_move_resize(gdk_window, x, y, w, h);
		}
		else
			gdk_window_resize(gdk_window, w, h);
	}
	else
		gtk_window_set_default_size(GTK_WINDOW(window), w, h);
}

/* Resize the window to fit the items currently in the Directory.
 * This should be used once the Directory has been fully scanned, otherwise
 * the window will appear too small. When opening a directory for the first
 * time, the names will be known but not the types and images. We can
 * still make a good estimate of the size.
 */
void filer_window_autosize(FilerWindow *filer_window, gboolean allow_shrink)
{
	Collection	*collection = filer_window->collection;
	int 		n;

	n = collection->number_of_items;
	n = MAX(n, 2);

	filer_size_for(filer_window,
			collection->item_width,
			collection->item_height,
			n, allow_shrink);
}

/* Choose a good size for this window, assuming n items of size (w, h) */
static void filer_size_for(FilerWindow *filer_window,
			   int w, int h, int n, gboolean allow_shrink)
{
	int 		x;
	int		rows, cols;
	int 		max_x, max_rows;
	const float	r = 2.5;
	int		t = 0;
	int		size_limit;
	int		space = 0;

	size_limit = o_filer_size_limit.int_value;

	/* Get the extra height required for the toolbar and minibuffer,
	 * if visible.
	 */
	if (o_toolbar.int_value != TOOLBAR_NONE)
		t = filer_window->toolbar->allocation.height;
	if (filer_window->message)
		t += filer_window->message->allocation.height;
	if (GTK_WIDGET_VISIBLE(filer_window->minibuffer_area))
	{
		GtkRequisition req;

		gtk_widget_size_request(filer_window->minibuffer_area, &req);
		space = req.height + 2;
		t += space;
	}

	max_x = (size_limit * screen_width) / 100;
	max_rows = (size_limit * screen_height) / (h * 100);

	/* Aim for a size where
	 * 	   x = r(y + t + h),		(1)
	 * unless that's too wide.
	 *
	 * t = toolbar (and minibuffer) height
	 * r = desired (width / height) ratio
	 *
	 * Want to display all items:
	 * 	   (x/w)(y/h) = n
	 * 	=> xy = nwh
	 *	=> x(x/r - t - h) = nwh		(from 1)
	 *	=> xx - x.rt - hr(1 - nw) = 0
	 *	=> 2x = rt +/- sqrt(rt.rt + 4hr(nw - 1))
	 * Now,
	 * 	   4hr(nw - 1) > 0
	 * so
	 * 	   sqrt(rt.rt + ...) > rt
	 *
	 * So, the +/- must be +:
	 * 	
	 *	=> x = (rt + sqrt(rt.rt + 4hr(nw - 1))) / 2
	 *
	 * ( + w - 1 to round up)
	 */
	x = (r * t + sqrt(r*r*t*t + 4*h*r * (n*w - 1))) / 2 + w - 1;

	/* Limit x */
	if (x > max_x)
		x = max_x;

	cols = x / w;
	cols = MAX(cols, 1);

	/* Choose rows to display all items given our chosen x.
	 * Don't make the window *too* big!
	 */
	rows = (n + cols - 1) / cols;
	if (rows > max_rows)
		rows = max_rows;

	/* Leave some room for extra icons, but only in Small Icons mode
	 * otherwise it takes up too much space.
	 * Also, don't add space if the minibuffer is open.
	 */
	if (space == 0)
		space = filer_window->display_style == SMALL_ICONS ? h : 2;

	filer_window_set_size(filer_window,
			w * MAX(cols, 1),
			h * MAX(rows, 1) + space,
			allow_shrink);
}

/* Called on a timeout while scanning or when scanning ends
 * (whichever happens first).
 */
static gint open_filer_window(FilerWindow *filer_window)
{
	view_style_changed(filer_window->view, 0);

	if (filer_window->open_timeout)
	{
		gtk_timeout_remove(filer_window->open_timeout);
		filer_window->open_timeout = 0;
	}

	if (!GTK_WIDGET_VISIBLE(filer_window->window))
	{
		set_style_by_number_of_items(filer_window);
		filer_window_autosize(filer_window, TRUE);
		gtk_widget_show(filer_window->window);
	}

	return FALSE;
}

static void update_display(Directory *dir,
			DirAction	action,
			GPtrArray	*items,
			FilerWindow *filer_window)
{
	Collection *collection = filer_window->collection;
	ViewIface *view = (ViewIface *) filer_window->view;

	switch (action)
	{
		case DIR_ADD:
			view_add_items(view, items);
			/* Open and resize if currently hidden */
			open_filer_window(filer_window);
			break;
		case DIR_REMOVE:
			view_delete_if(view, if_deleted, items);
			break;
		case DIR_START_SCAN:
			set_scanning_display(filer_window, TRUE);
			toolbar_update_info(filer_window);
			break;
		case DIR_END_SCAN:
			if (filer_window->window->window)
				gdk_window_set_cursor(
						filer_window->window->window,
						NULL);
			set_scanning_display(filer_window, FALSE);
			toolbar_update_info(filer_window);
			open_filer_window(filer_window);

			if (filer_window->had_cursor &&
					collection->cursor_item == -1)
			{
				collection_set_cursor_item(collection, 0);
				filer_window->had_cursor = FALSE;
			}
			if (filer_window->auto_select)
				display_set_autoselect(filer_window,
						filer_window->auto_select);
			g_free(filer_window->auto_select);
			filer_window->auto_select = NULL;

			filer_create_thumbs(filer_window);

			if (filer_window->thumb_queue)
				start_thumb_scanning(filer_window);
			break;
		case DIR_UPDATE:
			view_update_items(view, items);
			break;
	}
}

static void attach(FilerWindow *filer_window)
{
	gdk_window_set_cursor(filer_window->window->window, busy_cursor);
	view_clear(filer_window->view);
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
	g_object_unref(filer_window->directory);
	filer_window->directory = NULL;
}

static void filer_window_destroyed(GtkWidget 	*widget,
				   FilerWindow 	*filer_window)
{
	all_filer_windows = g_list_remove(all_filer_windows, filer_window);

	g_object_set_data(G_OBJECT(widget), "filer_window", NULL);

	if (window_with_primary == filer_window)
		window_with_primary = NULL;
	
	if (window_with_focus == filer_window)
	{
		menu_popdown();
		window_with_focus = NULL;
	}

	if (filer_window->directory)
		detach(filer_window);

	if (filer_window->open_timeout)
	{
		gtk_timeout_remove(filer_window->open_timeout);
		filer_window->open_timeout = 0;
	}

	if (filer_window->thumb_queue)
	{
		g_list_foreach(filer_window->thumb_queue, (GFunc) g_free, NULL);
		g_list_free(filer_window->thumb_queue);
	}

	tooltip_show(NULL);

	g_free(filer_window->auto_select);
	g_free(filer_window->real_path);
	g_free(filer_window->sym_path);
	g_free(filer_window);

	one_less_window();
}

/* Returns TRUE iff the directory still exists. */
static gboolean may_rescan(FilerWindow *filer_window, gboolean warning)
{
	Directory *dir;
	
	g_return_val_if_fail(filer_window != NULL, FALSE);

	/* We do a fresh lookup (rather than update) because the inode may
	 * have changed.
	 */
	dir = g_fscache_lookup(dir_cache, filer_window->real_path);
	if (!dir)
	{
		if (warning)
			info_message(_("Directory missing/deleted"));
		gtk_widget_destroy(filer_window->window);
		return FALSE;
	}
	if (dir == filer_window->directory)
		g_object_unref(dir);
	else
	{
		detach(filer_window);
		filer_window->directory = dir;
		attach(filer_window);
	}

	return TRUE;
}

/* No items are now selected. This might be because another app claimed
 * the selection or because the user unselected all the items.
 */
void filer_lost_selection(FilerWindow *filer_window, guint time)
{
	if (window_with_primary == filer_window)
	{
		window_with_primary = NULL;
		gtk_selection_owner_set(NULL, GDK_SELECTION_PRIMARY, time);
	}
}

/* Another app has claimed the primary selection */
static void filer_lost_primary(GtkWidget *window,
		        GdkEventSelection *event,
		        gpointer user_data)
{
	FilerWindow *filer_window = (FilerWindow *) user_data;
	
	if (window_with_primary && window_with_primary == filer_window)
	{
		window_with_primary = NULL;
		set_selection_state(filer_window, FALSE);
	}
}

/* Someone wants us to send them the selection */
static void selection_get(GtkWidget *widget, 
		       GtkSelectionData *selection_data,
		       guint      info,
		       guint      time,
		       gpointer   data)
{
	GString	*reply, *header;
	FilerWindow 	*filer_window = (FilerWindow *) data;
	ViewIter	iter;
	DirItem		*item;

	reply = g_string_new(NULL);
	header = g_string_new(NULL);

	switch (info)
	{
		case TARGET_STRING:
			g_string_sprintf(header, " %s",
				make_path(filer_window->sym_path, "")->str);
			break;
		case TARGET_URI_LIST:
			g_string_sprintf(header, " file://%s%s",
				our_host_name_for_dnd(),
				make_path(filer_window->sym_path, "")->str);
			break;
	}

	view_get_iter(filer_window->view, &iter, VIEW_ITER_SELECTED);

	while ((item = iter.next(&iter)))
	{
		g_string_append(reply, header->str);
		g_string_append(reply, item->leafname);
	}
	
	if (reply->len > 0)
		gtk_selection_data_set(selection_data, xa_string,
				8, reply->str + 1, reply->len - 1);
	else
	{
		g_warning("Attempt to paste empty selection!");
		gtk_selection_data_set(selection_data, xa_string, 8, "", 0);
	}

	g_string_free(reply, TRUE);
	g_string_free(header, TRUE);
}

/* Selection has been changed -- try to grab the primary selection
 * if we don't have it. Also called when clicking on an insensitive selection
 * to regain primary.
 */
void filer_selection_changed(FilerWindow *filer_window, gint time)
{
	if (window_with_primary == filer_window)
		return;		/* Already got it */

	if (!filer_window->collection->number_selected)
		return;		/* Nothing selected */

	if (filer_window->temp_item_selected == FALSE &&
		gtk_selection_owner_set(GTK_WIDGET(filer_window->window),
				GDK_SELECTION_PRIMARY,
				time))
	{
		window_with_primary = filer_window;
		set_selection_state(filer_window, TRUE);
	}
	else
		set_selection_state(filer_window, FALSE);
}

/* Open the item (or add it to the shell command minibuffer) */
void filer_openitem(FilerWindow *filer_window, int item_number, OpenFlags flags)
{
	gboolean	shift = (flags & OPEN_SHIFT) != 0;
	gboolean	close_mini = flags & OPEN_FROM_MINI;
	gboolean	close_window = (flags & OPEN_CLOSE_WINDOW) != 0;
	DirItem		*item = (DirItem *)
			filer_window->collection->items[item_number].data;
	guchar		*full_path;
	gboolean	wink = TRUE;
	Directory	*old_dir;

	if (filer_window->mini_type == MINI_SHELL)
	{
		minibuffer_add(filer_window, item->leafname);
		return;
	}

	if (!item->image)
		dir_update_item(filer_window->directory, item->leafname);

	if (item->base_type == TYPE_DIRECTORY)
	{
		/* Never close a filer window when opening a directory
		 * (click on a dir or click on an app with shift).
		 */
		if (shift || !(item->flags & ITEM_FLAG_APPDIR))
			close_window = FALSE;
	}

	full_path = make_path(filer_window->sym_path, item->leafname)->str;
	if (shift && (item->flags & ITEM_FLAG_SYMLINK))
		wink = FALSE;

	old_dir = filer_window->directory;
	if (run_diritem(full_path, item,
			flags & OPEN_SAME_WINDOW ? filer_window : NULL,
			filer_window,
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

static gint pointer_out(GtkWidget *widget,
			GdkEventCrossing *event,
			FilerWindow *filer_window)
{
	tooltip_show(NULL);
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
	OpenFlags		flags = 0;

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
	if (event->state & GDK_MOD1_MASK)
		flags |= OPEN_CLOSE_WINDOW;
	else
		flags |= OPEN_SAME_WINDOW;

	filer_openitem(filer_window, item, flags);
}

/* Makes sure that 'groups' is up-to-date, reloading from file if it has
 * changed. If no groups were loaded and there is no file then initialised
 * groups to an empty document.
 * Return the node for the 'name' group.
 */
static xmlNode *group_find(char *name)
{
	xmlNode *node;
	gchar *path;

	/* Update the groups, if possible */
	path = choices_find_path_load("Groups.xml", PROJECT);
	if (path)
	{
		XMLwrapper *wrapper;
		wrapper = xml_cache_load(path);
		if (wrapper)
		{
			if (groups)
				g_object_unref(groups);
			groups = wrapper;
		}

		g_free(path);
	}

	if (!groups)
	{
		groups = xml_new(NULL);
		groups->doc = xmlNewDoc("1.0");

		xmlDocSetRootElement(groups->doc,
			xmlNewDocNode(groups->doc, NULL, "groups", NULL));
		return NULL;
	}

	node = xmlDocGetRootElement(groups->doc);

	for (node = node->xmlChildrenNode; node; node = node->next)
	{
		guchar	*gid;

		gid = xmlGetProp(node, "name");

		if (!gid)
			continue;

		if (strcmp(name, gid) != 0)
			continue;

		g_free(gid);

		return node;
	}

	return NULL;
}

static void group_save(FilerWindow *filer_window, char *name)
{
	xmlNode	*group;
	guchar	*save_path;
	DirItem *item;
	ViewIter iter;

	group = group_find(name);
	if (group)
	{
		xmlUnlinkNode(group);
		xmlFreeNode(group);
	}
	group = xmlNewChild(xmlDocGetRootElement(groups->doc),
			NULL, "group", NULL);
	xmlSetProp(group, "name", name);

	xmlNewChild(group, NULL, "directory", filer_window->sym_path);

	view_get_iter(filer_window->view, &iter, VIEW_ITER_SELECTED);

	while ((item = iter.next(&iter)))
		xmlNewChild(group, NULL, "item", item->leafname);

	save_path = choices_find_path_save("Groups.xml", PROJECT, TRUE);
	if (save_path)
	{
		save_xml_file(groups->doc, save_path);
		g_free(save_path);
	}
}

static void group_restore(FilerWindow *filer_window, char *name)
{
	GHashTable *in_group;
	Collection *collection = filer_window->collection;
	int	j, n;
	char	*path;
	xmlNode	*group, *node;

	group = group_find(name);

	if (!group)
	{
		report_error(_("Group %s is not set. Select some files "
			     "and press Ctrl+%s to set the group. Press %s "
			     "on its own to reselect the files later.\n"
			     "Make sure NumLock is on if you use the keypad."),
			     name, name, name);
		return;
	}

	node = get_subnode(group, NULL, "directory");
	g_return_if_fail(node != NULL);
	path = xmlNodeListGetString(groups->doc, node->xmlChildrenNode, 1);
	g_return_if_fail(path != NULL);

	if (strcmp(path, filer_window->sym_path) != 0)
		filer_change_to(filer_window, path, NULL);
	g_free(path);

	/* If an item at the start is selected then we could lose the
	 * primary selection after checking that item and then need to
	 * gain it again at the end. Therefore, if anything is selected
	 * then select the last item until the end of the search.
	 */
	n = collection->number_of_items;
	if (collection->number_selected)
		collection_select_item(collection, n - 1);

	in_group = g_hash_table_new(g_str_hash, g_str_equal);
	for (node = group->xmlChildrenNode; node; node = node->next)
	{
		gchar *leaf;
		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (strcmp(node->name, "item") != 0)
			continue;

		leaf = xmlNodeListGetString(groups->doc,
						node->xmlChildrenNode, 1);
		if (!leaf)
			g_warning("Missing leafname!\n");
		else
			g_hash_table_insert(in_group, leaf, filer_window);
	}
	
	for (j = 0; j < n; j++)
	{
		DirItem *item = (DirItem *) collection->items[j].data;

		if (g_hash_table_lookup(in_group, item->leafname))
			collection_select_item(collection, j);
		else
			collection_unselect_item(collection, j);
	}

	g_hash_table_foreach(in_group, (GHFunc) g_free, NULL);
	g_hash_table_destroy(in_group);
}

/* Handle keys that can't be bound with the menu */
static gint key_press_event(GtkWidget	*widget,
			GdkEventKey	*event,
			FilerWindow	*filer_window)
{
	GtkWidget *focus = GTK_WINDOW(widget)->focus_widget;
	guint key = event->keyval;
	char group[2] = "1";

	window_with_focus = filer_window;

	/* Delay setting up the keys until now to speed loading... */
	if (!filer_keys)
		ensure_filer_menu();	/* Gets the keys working... */

	if (!g_slist_find(filer_keys->acceleratables, widget))
		gtk_window_add_accel_group(GTK_WINDOW(filer_window->window),
					   filer_keys);

	if (focus && focus != widget &&
	    gtk_widget_get_toplevel(focus) == widget)
		if (gtk_widget_event(focus, (GdkEvent *) event))
			return TRUE;	/* Handled */

	switch (key)
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
		case GDK_backslash:
			tooltip_show(NULL);
			show_filer_menu(filer_window, (GdkEvent *) event,
					filer_window->collection->cursor_item);
			break;
		default:
			if (key >= GDK_0 && key <= GDK_9)
				group[0] = key - GDK_0 + '0';
			else if (key >= GDK_KP_0 && key <= GDK_KP_9)
				group[0] = key - GDK_KP_0 + '0';
			else
				return FALSE;


			if (event->state & GDK_CONTROL_MASK)
				group_save(filer_window, group);
			else
				group_restore(filer_window, group);
	}

	return TRUE;
}

void filer_open_parent(FilerWindow *filer_window)
{
	char	*dir;
	const char *current = filer_window->sym_path;

	if (current[0] == '/' && current[1] == '\0')
		return;		/* Already in the root */
	
	dir = g_dirname(current);
	filer_opendir(dir, filer_window);
	g_free(dir);
}

void change_to_parent(FilerWindow *filer_window)
{
	char	*dir;
	const char *current = filer_window->sym_path;

	if (current[0] == '/' && current[1] == '\0')
		return;		/* Already in the root */
	
	dir = g_dirname(current);
	filer_change_to(filer_window, dir, g_basename(current));
	g_free(dir);
}

/* Removes trailing /s from path (modified in place) */
static void tidy_sympath(gchar *path)
{
	int l;
	
	g_return_if_fail(path != NULL);

	l = strlen(path);
	while (l > 1 && path[l - 1] == '/')
	{
		l--;
		path[l] = '\0';
	}
}

/* Make filer_window display path. When finished, highlight item 'from', or
 * the first item if from is NULL. If there is currently no cursor then
 * simply wink 'from' (if not NULL).
 */
void filer_change_to(FilerWindow *filer_window,
			const char *path, const char *from)
{
	char	*from_dup;
	char	*sym_path, *real_path;
	Directory *new_dir;

	g_return_if_fail(filer_window != NULL);

	filer_cancel_thumbnails(filer_window);

	tooltip_show(NULL);

	sym_path = g_strdup(path);
	real_path = pathdup(path);
	new_dir  = g_fscache_lookup(dir_cache, real_path);

	if (!new_dir)
	{
		delayed_error(_("Directory '%s' is not accessible"),
				sym_path);
		g_free(real_path);
		g_free(sym_path);
		return;
	}
	
	if (o_unique_filer_windows.int_value)
	{
		FilerWindow *fw;
		
		fw = find_filer_window(sym_path, filer_window);
		if (fw)
			gtk_widget_destroy(fw->window);
	}

	from_dup = from && *from ? g_strdup(from) : NULL;

	detach(filer_window);
	g_free(filer_window->real_path);
	g_free(filer_window->sym_path);
	filer_window->real_path = real_path;
	filer_window->sym_path = sym_path;
	tidy_sympath(filer_window->sym_path);

	filer_window->directory = new_dir;

	g_free(filer_window->auto_select);
	filer_window->had_cursor = filer_window->collection->cursor_item != -1
				   || filer_window->had_cursor;
	filer_window->auto_select = from_dup;

	filer_set_title(filer_window);
	if (filer_window->window->window)
		gdk_window_set_role(filer_window->window->window,
				    filer_window->sym_path);
	collection_set_cursor_item(filer_window->collection, -1);

	attach(filer_window);
	
	set_style_by_number_of_items(filer_window);

	if (o_filer_auto_resize.int_value == RESIZE_ALWAYS)
		filer_window_autosize(filer_window, TRUE);

	if (filer_window->mini_type == MINI_PATH)
		gtk_idle_add((GtkFunction) minibuffer_show_cb,
				filer_window);
}

/* Returns a list containing the full (sym) pathname of every selected item.
 * You must g_free() each item in the list.
 */
GList *filer_selected_items(FilerWindow *filer_window)
{
	GList	*retval = NULL;
	guchar	*dir = filer_window->sym_path;
	ViewIter iter;
	DirItem *item;

	view_get_iter(filer_window->view, &iter, VIEW_ITER_SELECTED);
	while ((item = iter.next(&iter)))
	{
		retval = g_list_prepend(retval,
				g_strdup(make_path(dir, item->leafname)->str));
	}

	return g_list_reverse(retval);
}

DirItem *filer_selected_item(FilerWindow *filer_window)
{
	Collection *collection = filer_window->collection;
	int	item;

	item = collection_selected_item_number(collection);

	if (item > -1)
		return (DirItem *) collection->items[item].data;
	return NULL;
}

/* Creates and shows a new filer window.
 * If src_win != NULL then display options can be taken from that source window.
 * Returns the new filer window, or NULL on error.
 * Note: if unique windows is in use, may return an existing window.
 */
FilerWindow *filer_opendir(const char *path, FilerWindow *src_win)
{
	FilerWindow	*filer_window;
	char		*real_path;
	DisplayStyle    dstyle;
	DetailsType     dtype;

	/* Get the real pathname of the directory and copy it */
	real_path = pathdup(path);

	if (o_unique_filer_windows.int_value)
	{
		FilerWindow	*same_dir_window;
		
		same_dir_window = find_filer_window(path, NULL);

		if (same_dir_window)
		{
			gtk_window_present(GTK_WINDOW(same_dir_window->window));
			return same_dir_window;
		}
	}

	filer_window = g_new(FilerWindow, 1);
	filer_window->message = NULL;
	filer_window->minibuffer = NULL;
	filer_window->minibuffer_label = NULL;
	filer_window->minibuffer_area = NULL;
	filer_window->temp_show_hidden = FALSE;
	filer_window->sym_path = g_strdup(path);
	filer_window->real_path = real_path;
	filer_window->scanning = FALSE;
	filer_window->had_cursor = FALSE;
	filer_window->auto_select = NULL;
	filer_window->toolbar_text = NULL;
	filer_window->target_cb = NULL;
	filer_window->mini_type = MINI_NONE;
	filer_window->selection_state = GTK_STATE_INSENSITIVE;
	filer_window->toolbar = NULL;
	filer_window->toplevel_vbox = NULL;

	tidy_sympath(filer_window->sym_path);

	/* Finds the entry for this directory in the dir cache, creating
	 * a new one if needed. This does not cause a scan to start,
	 * so if a new entry is created then it will be empty.
	 */
	filer_window->directory = g_fscache_lookup(dir_cache, real_path);
	if (!filer_window->directory)
	{
		delayed_error(_("Directory '%s' not found."), path);
		g_free(filer_window->real_path);
		g_free(filer_window->sym_path);
		g_free(filer_window);
		return NULL;
	}

	filer_window->temp_item_selected = FALSE;
	filer_window->flags = (FilerFlags) 0;
	filer_window->details_type = DETAILS_SUMMARY;
	filer_window->display_style = UNKNOWN_STYLE;
	filer_window->thumb_queue = NULL;
	filer_window->max_thumbs = 0;

	if (src_win && o_display_inherit_options.int_value)
	{
	        filer_window->sort_fn = src_win->sort_fn;
		dstyle = src_win->display_style;
		dtype = src_win->details_type;
		filer_window->show_hidden = src_win->show_hidden;
		filer_window->show_thumbs = src_win->show_thumbs;
	}
	else
	{
	        int i = o_display_sort_by.int_value;
		filer_window->sort_fn = i == 0 ? sort_by_name :
		                        i == 1 ? sort_by_type :
		                        i == 2 ? sort_by_date :
		                        sort_by_size;
		
		dstyle = o_display_size.int_value;
		dtype = o_display_details.int_value;
		filer_window->show_hidden =
			o_display_show_hidden.int_value;
		filer_window->show_thumbs =
			o_display_show_thumbs.int_value;
	}

	/* Add all the user-interface elements & realise */
	filer_add_widgets(filer_window);
	if (src_win)
		gtk_window_set_position(GTK_WINDOW(filer_window->window),
					GTK_WIN_POS_MOUSE);

	/* Connect to all the signal handlers */
	filer_add_signals(filer_window);

	display_set_layout(filer_window, dstyle, dtype);

	/* Open the window after a timeout, or when scanning stops.
	 * Do this before attaching, because attach() might tell us to
	 * stop scanning (if a scan isn't needed).
	 */
	filer_window->open_timeout = gtk_timeout_add(500,
					  (GtkFunction) open_filer_window,
					  filer_window);

	/* The collection is created empty and then attach() is called, which
	 * links the filer window to the entry in the directory cache we
	 * looked up / created above.
	 *
	 * The attach() function will immediately callback to the filer window
	 * to deliver a list of all known entries in the directory (so,
	 * collection->number_of_items may be valid after the call to
	 * attach() returns).
	 *
	 * If the directory was not in the cache (because it hadn't been
	 * opened it before) then the types and icons for the entries are
	 * not know, but the list of names is.
	 */

	attach(filer_window);

	number_of_windows++;
	all_filer_windows = g_list_prepend(all_filer_windows, filer_window);

	return filer_window;
}

/* This adds all the widgets to a new filer window. It is in a separate
 * function because filer_opendir() was getting too long...
 */
static void filer_add_widgets(FilerWindow *filer_window)
{
	GtkWidget *hbox, *vbox, *collection;

	/* Create the top-level window widget */
	filer_window->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	filer_set_title(filer_window);

	/* This property is cleared when the window is destroyed.
	 * You can thus ref filer_window->window and use this to see
	 * if the window no longer exists.
	 */
	g_object_set_data(G_OBJECT(filer_window->window),
			"filer_window", filer_window);
	
	/* The view is the area that actually displays the files */
	filer_window->view = VIEW(view_collection_new(filer_window));
	gtk_widget_show(GTK_WIDGET(filer_window->view));
	collection = GTK_WIDGET(filer_window->collection);	/* XXX */
	g_object_set_data(G_OBJECT(collection), "filer_window", filer_window);

	/* Scrollbar on the right, everything else on the left */
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(filer_window->window), hbox);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start_defaults(GTK_BOX(hbox), vbox);
	filer_window->toplevel_vbox = GTK_BOX(vbox);
	
	/* If we want a toolbar, create it now */
	toolbar_update_toolbar(filer_window);

	/* If there's a message that should be displayed in each window (eg
	 * 'Running as root'), add it here.
	 */
	if (show_user_message)
	{
		filer_window->message = gtk_label_new(show_user_message);
		gtk_box_pack_start(GTK_BOX(vbox), filer_window->message,
				   FALSE, TRUE, 0);
		gtk_widget_show(filer_window->message);
	}

	/* Now add the area for displaying the files.
	 * The collection is one huge window that goes in a Viewport.
	 */
	gtk_box_pack_start_defaults(GTK_BOX(vbox),
				    GTK_WIDGET(filer_window->view));
	filer_window->scrollbar =
		gtk_vscrollbar_new(filer_window->collection->vadj);

	/* And the minibuffer (hidden to start with) */
	create_minibuffer(filer_window);
	gtk_box_pack_start(GTK_BOX(vbox), filer_window->minibuffer_area,
				FALSE, TRUE, 0);

	/* And the thumbnail progress bar (also hidden) */
	{
		GtkWidget *cancel;

		filer_window->thumb_bar = gtk_hbox_new(FALSE, 2);
		gtk_box_pack_start(GTK_BOX(vbox), filer_window->thumb_bar,
				FALSE, TRUE, 0);

		filer_window->thumb_progress = gtk_progress_bar_new();
		
		gtk_box_pack_start(GTK_BOX(filer_window->thumb_bar),
				filer_window->thumb_progress, TRUE, TRUE, 0);

		cancel = gtk_button_new_with_label(_("Cancel"));
		GTK_WIDGET_UNSET_FLAGS(cancel, GTK_CAN_FOCUS);
		gtk_box_pack_start(GTK_BOX(filer_window->thumb_bar),
				cancel, FALSE, TRUE, 0);
		g_signal_connect_swapped(cancel, "clicked",
				G_CALLBACK(filer_cancel_thumbnails),
				filer_window);
	}

	/* Put the scrollbar on the left of everything else... */
	gtk_box_pack_start(GTK_BOX(hbox),
			filer_window->scrollbar, FALSE, TRUE, 0);

	gtk_window_set_focus(GTK_WINDOW(filer_window->window), collection);

	gtk_widget_show(hbox);
	gtk_widget_show(vbox);
	gtk_widget_show(filer_window->scrollbar);
	gtk_widget_show(collection);

	gtk_widget_realize(filer_window->window);
	
	gdk_window_set_role(filer_window->window->window,
			    filer_window->sym_path);

	filer_window_set_size(filer_window, 4, 4, TRUE);
}

static void filer_add_signals(FilerWindow *filer_window)
{
	GtkObject	*collection = GTK_OBJECT(filer_window->collection);
	GtkTargetEntry 	target_table[] =
	{
		{"text/uri-list", 0, TARGET_URI_LIST},
		{"STRING", 0, TARGET_STRING},
		{"COMPOUND_TEXT", 0, TARGET_STRING},/* XXX: Treats as STRING */
	};

	/* Events on the top-level window */
	gtk_widget_add_events(filer_window->window, GDK_ENTER_NOTIFY);
	g_signal_connect(filer_window->window, "enter-notify-event",
			G_CALLBACK(pointer_in), filer_window);
	g_signal_connect(filer_window->window, "leave-notify-event",
			G_CALLBACK(pointer_out), filer_window);
	g_signal_connect(filer_window->window, "destroy",
			G_CALLBACK(filer_window_destroyed), filer_window);

	g_signal_connect(filer_window->window, "selection_clear_event",
			G_CALLBACK(filer_lost_primary), filer_window);

	/* Events on the collection widget */
	gtk_widget_set_events(GTK_WIDGET(collection),
			GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
			GDK_BUTTON3_MOTION_MASK | GDK_POINTER_MOTION_MASK |
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

	g_signal_connect(filer_window->window, "selection_get",
			G_CALLBACK(selection_get), filer_window);
	gtk_selection_add_targets(GTK_WIDGET(filer_window->window),
			GDK_SELECTION_PRIMARY,
			target_table,
			sizeof(target_table) / sizeof(*target_table));

	g_signal_connect(filer_window->window, "key_press_event",
			G_CALLBACK(key_press_event), filer_window);

	/* Drag and drop events */
	g_signal_connect(collection, "drag_data_get",
			GTK_SIGNAL_FUNC(drag_data_get), NULL);
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

/* Note that filer_window may not exist after this call */
void filer_update_dir(FilerWindow *filer_window, gboolean warning)
{
	if (may_rescan(filer_window, warning))
		dir_update(filer_window->directory, filer_window->sym_path);
}

void filer_update_all(void)
{
	GList	*next = all_filer_windows;

	while (next)
	{
		FilerWindow *filer_window = (FilerWindow *) next->data;

		/* Updating directory may remove it from list -- stop sending
		 * patches to move this line!
		 */
		next = next->next;

		filer_update_dir(filer_window, TRUE);
	}
}

/* Refresh the various caches even if we don't think we need to */
void full_refresh(void)
{
	mount_update(TRUE);
}

/* See whether a filer window with a given path already exists
 * and is different from diff.
 */
static FilerWindow *find_filer_window(const char *sym_path, FilerWindow *diff)
{
	GList	*next;

	for (next = all_filer_windows; next; next = next->next)
	{
		FilerWindow *filer_window = (FilerWindow *) next->data;

		if (filer_window != diff &&
		    	strcmp(sym_path, filer_window->sym_path) == 0)
			return filer_window;
	}
	
	return NULL;
}

/* This path has been mounted/umounted/deleted some files - update all dirs */
void filer_check_mounted(const char *real_path)
{
	GList	*next = all_filer_windows;
	gchar	*parent;
	int	len;

	len = strlen(real_path);

	while (next)
	{
		FilerWindow *filer_window = (FilerWindow *) next->data;

		next = next->next;

		if (strncmp(real_path, filer_window->real_path, len) == 0)
		{
			char	s = filer_window->real_path[len];

			if (s == '/' || s == '\0')
				filer_update_dir(filer_window, FALSE);
		}
	}

	parent = g_dirname(real_path);
	refresh_dirs(parent);
	g_free(parent);

	icons_may_update(real_path);
}

/* Close all windows displaying 'path' or subdirectories of 'path' */
void filer_close_recursive(const char *path)
{
	GList	*next = all_filer_windows;
	gchar	*real;
	int	len;

	real = pathdup(path);
	len = strlen(real);

	while (next)
	{
		FilerWindow *filer_window = (FilerWindow *) next->data;

		next = next->next;

		if (strncmp(real, filer_window->real_path, len) == 0)
		{
			char s = filer_window->real_path[len];

			if (len == 1 || s == '/' || s == '\0')
				gtk_widget_destroy(filer_window->window);
		}
	}
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

/* TRUE iff filer_window points to an existing FilerWindow
 * structure.
 */
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

/* Make sure the window title is up-to-date */
void filer_set_title(FilerWindow *filer_window)
{
	guchar	*title = NULL;
	guchar	*flags = "";

	if (filer_window->scanning || filer_window->show_hidden ||
				filer_window->show_thumbs)
	{
		if (o_short_flag_names.int_value)
		{
			flags = g_strconcat(" +",
				filer_window->scanning ? _("S") : "",
				filer_window->show_hidden ? _("A") : "",
				filer_window->show_thumbs ? _("T") : "",
				NULL);
		}
		else
		{
			flags = g_strconcat(" (",
				filer_window->scanning ? _("Scanning, ") : "",
				filer_window->show_hidden ? _("All, ") : "",
				filer_window->show_thumbs ? _("Thumbs, ") : "",
				NULL);
			flags[strlen(flags) - 2] = ')';
		}
	}

	if (not_local)
	        title = g_strconcat("//", our_host_name(),
			    filer_window->sym_path, flags, NULL);
	
	if (!title && home_dir_len > 1 &&
		strncmp(filer_window->sym_path, home_dir, home_dir_len) == 0)
	{
		guchar 	sep = filer_window->sym_path[home_dir_len];

		if (sep == '\0' || sep == '/')
			title = g_strconcat("~",
					filer_window->sym_path + home_dir_len,
					flags,
					NULL);
	}
	
	if (!title)
		title = g_strconcat(filer_window->sym_path, flags, NULL);

	gtk_window_set_title(GTK_WINDOW(filer_window->window), title);
	g_free(title);

	if (flags[0] != '\0')
		g_free(flags);
}

/* Reconnect to the same directory (used when the Show Hidden option is
 * toggled). This has the side-effect of updating the window title.
 */
void filer_detach_rescan(FilerWindow *filer_window)
{
	Directory *dir = filer_window->directory;
	
	g_object_ref(dir);
	detach(filer_window);
	filer_window->directory = dir;
	attach(filer_window);
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
			const char *reason)
{
	TargetFunc old_fn = filer_window->target_cb;

	if (fn != old_fn)
		gdk_window_set_cursor(
				GTK_WIDGET(filer_window->collection)->window,
				fn ? crosshair : NULL);

	filer_window->target_cb = fn;
	filer_window->target_data = data;

	if (filer_window->toolbar_text == NULL)
		return;

	if (fn)
		gtk_label_set_text(
			GTK_LABEL(filer_window->toolbar_text), reason);
	else if (o_toolbar_info.int_value)
	{
		if (old_fn)
			toolbar_update_info(filer_window);
	}
	else
		gtk_label_set_text(GTK_LABEL(filer_window->toolbar_text), "");
}

static void set_selection_state(FilerWindow *filer_window, gboolean normal)
{
	GtkStateType old_state = filer_window->selection_state;

	filer_window->selection_state = normal
			? GTK_STATE_SELECTED : GTK_STATE_INSENSITIVE;

	if (old_state != filer_window->selection_state
	    && filer_window->collection->number_selected)
		gtk_widget_queue_draw(GTK_WIDGET(filer_window->collection));
}

void filer_cancel_thumbnails(FilerWindow *filer_window)
{
	gtk_widget_hide(filer_window->thumb_bar);

	g_list_foreach(filer_window->thumb_queue, (GFunc) g_free, NULL);
	g_list_free(filer_window->thumb_queue);
	filer_window->thumb_queue = NULL;
	filer_window->max_thumbs = 0;
}

/* Generate the next thumb for this window. The collection object is
 * unref'd when there is nothing more to do.
 * If the collection no longer has a filer window, nothing is done.
 */
static gboolean filer_next_thumb_real(GObject *window)
{
	FilerWindow *filer_window;
	gchar	*path;
	int	done, total;

	filer_window = g_object_get_data(window, "filer_window");

	if (!filer_window)
	{
		g_object_unref(window);
		return FALSE;
	}
		
	if (!filer_window->thumb_queue)
	{
		filer_cancel_thumbnails(filer_window);
		g_object_unref(window);
		return FALSE;
	}

	total = filer_window->max_thumbs;
	done = total - g_list_length(filer_window->thumb_queue);

	path = (gchar *) filer_window->thumb_queue->data;

	pixmap_background_thumb(path, (GFunc) filer_next_thumb, window);

	filer_window->thumb_queue = g_list_remove(filer_window->thumb_queue,
						  path);
	g_free(path);

	gtk_progress_bar_set_fraction(
			GTK_PROGRESS_BAR(filer_window->thumb_progress),
			done / (float) total);

	return FALSE;
}

/* path is the thumb just loaded, if any.
 * collection is unref'd (eventually).
 */
static void filer_next_thumb(GObject *window, const gchar *path)
{
	if (path)
		dir_force_update_path(path);

	gtk_idle_add((GtkFunction) filer_next_thumb_real, window);
}

static void start_thumb_scanning(FilerWindow *filer_window)
{
	if (GTK_WIDGET_VISIBLE(filer_window->thumb_bar))
		return;		/* Already scanning */

	gtk_widget_show_all(filer_window->thumb_bar);

	g_object_ref(G_OBJECT(filer_window->window));
	filer_next_thumb(G_OBJECT(filer_window->window), NULL);
}

/* Set this image to be loaded some time in the future */
void filer_create_thumb(FilerWindow *filer_window, const gchar *path)
{
	filer_window->max_thumbs++;

	filer_window->thumb_queue = g_list_append(filer_window->thumb_queue,
						  g_strdup(path));

	if (filer_window->scanning)
		return;			/* Will start when scan ends */

	start_thumb_scanning(filer_window);
}

/* If thumbnail display is on, look through all the items in this directory
 * and start creating or updating the thumbnails as needed.
 */
void filer_create_thumbs(FilerWindow *filer_window)
{
	DirItem *item;
	ViewIter iter;

	if (!filer_window->show_thumbs)
		return;

	view_get_iter(filer_window->view, &iter, 0);

	while ((item = iter.next(&iter)))
	{
		MaskedPixmap *pixmap;
		gchar    *path;
		gboolean found;

		 if (item->base_type != TYPE_FILE)
			 continue;

		 if (strcmp(item->mime_type->media_type, "image") != 0)
			 continue;

		path = make_path(filer_window->real_path, item->leafname)->str;

		pixmap = g_fscache_lookup_full(pixmap_cache, path,
				FSCACHE_LOOKUP_ONLY_NEW, &found);
		if (pixmap)
			g_object_unref(pixmap);

		/* If we didn't get an image, it could be because:
		 *
		 * - We're loading the image now. found is TRUE,
		 *   and we'll update the item later.
		 * - We tried to load the image and failed. found
		 *   is TRUE.
		 * - We haven't tried loading the image. found is
		 *   FALSE, and we start creating the thumb here.
		 */
		if (!found)
			filer_create_thumb(filer_window, path);
	}
}

static void filer_options_changed(void)
{
	if (o_short_flag_names.has_changed)
	{
		GList *next;

		for (next = all_filer_windows; next; next = next->next)
		{
			FilerWindow *filer_window = (FilerWindow *) next->data;

			filer_set_title(filer_window);
		}
	}
}

/* Change to Large or Small icons depending on the number of items
 * in the directory, subject to options.
 */
static void set_style_by_number_of_items(FilerWindow *filer_window)
{
	int n;
	
	g_return_if_fail(filer_window != NULL);

	if (!o_filer_change_size.int_value)
		return;		/* Don't auto-set style */
	
	if (filer_window->display_style != LARGE_ICONS &&
	    filer_window->display_style != SMALL_ICONS)
		return;		/* Only change between these two styles */

	n = filer_window->collection->number_of_items;

	if (n >= o_filer_change_size_num.int_value)
		display_set_layout(filer_window, SMALL_ICONS,
				   filer_window->details_type);
	else
		display_set_layout(filer_window, LARGE_ICONS,
				   filer_window->details_type);
}

/* Append interesting information to this GString */
void filer_add_tip_details(FilerWindow *filer_window,
			   GString *tip, DirItem *item)
{
	guchar	*fullpath = NULL;

	fullpath = make_path(filer_window->real_path, item->leafname)->str;

	if (item->flags & ITEM_FLAG_SYMLINK)
	{
		char *target;

		target = readlink_dup(fullpath);
		if (target)
		{
			g_string_append(tip, _("Symbolic link to "));
			g_string_append(tip, target);
			g_string_append_c(tip, '\n');
			g_free(target);
		}
	}
	
	if (item->flags & ITEM_FLAG_APPDIR)
	{
		XMLwrapper *info;
		xmlNode *node;

		info = appinfo_get(fullpath, item);
		if (info && ((node = xml_get_section(info, NULL, "Summary"))))
		{
			guchar *str;
			str = xmlNodeListGetString(node->doc,
					node->xmlChildrenNode, 1);
			if (str)
			{
				g_string_append(tip, str);
				g_string_append_c(tip, '\n');
				g_free(str);
			}
		}
		if (info)
			g_object_unref(info);
	}

	if (!g_utf8_validate(item->leafname, -1, NULL))
		g_string_append(tip,
			_("This filename is not valid UTF-8. "
			  "You should rename it.\n"));
}
