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

/* minibuffer.c - for handling the path entry box at the bottom */

#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "collection.h"
#include "support.h"
#include "minibuffer.h"
#include "filer.h"

/* Static prototypes */
static gint key_press_event(GtkWidget	*widget,
			GdkEventKey	*event,
			FilerWindow	*filer_window);
static void changed(GtkEditable *mini, FilerWindow *filer_window);
static void find_next_match(FilerWindow *filer_window, char *pattern, int dir);
static gboolean matches(Collection *collection, int item, char *pattern);
static void search_in_dir(FilerWindow *filer_window, int dir);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/


GtkWidget *create_minibuffer(FilerWindow *filer_window)
{
	GtkWidget *mini;

	mini = gtk_entry_new();
	gtk_signal_connect(GTK_OBJECT(mini), "key_press_event",
			GTK_SIGNAL_FUNC(key_press_event), filer_window);
	gtk_signal_connect(GTK_OBJECT(mini), "changed",
			GTK_SIGNAL_FUNC(changed), filer_window);

	return mini;
}

/* Returns FALSE so that it can be used as an idle function */
gboolean minibuffer_show(FilerWindow *filer_window)
{
	Collection	*collection;
	GtkEntry	*mini;
	
	g_return_val_if_fail(filer_window != NULL, FALSE);
	g_return_val_if_fail(filer_window->minibuffer != NULL, FALSE);

	collection = filer_window->collection;
	filer_window->mini_cursor_base = MAX(collection->cursor_item, 0);

	mini = GTK_ENTRY(filer_window->minibuffer);

	gtk_entry_set_text(mini, make_path(filer_window->path, "")->str);
	gtk_entry_set_position(mini, -1);

	gtk_widget_show(filer_window->minibuffer);

	gtk_window_set_focus(GTK_WINDOW(filer_window->window),
			filer_window->minibuffer);

	return FALSE;
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void return_pressed(FilerWindow *filer_window, GdkEventKey *event)
{
	Collection 	*collection = filer_window->collection;
	int		item = collection->cursor_item;
	char		*path, *pattern;

	path = gtk_entry_get_text(GTK_ENTRY(filer_window->minibuffer));
	pattern = strrchr(path, '/');
	if (pattern)
		pattern++;
	else
		pattern = path;

	if (item == -1 || !matches(collection, item, pattern))
	{
		gdk_beep();
		return;
	}
	
	filer_openitem(filer_window, item,
			(event->state & GDK_SHIFT_MASK) != 0,
			FALSE);
}

/* Use the cursor item to fill in the minibuffer */
static void complete(FilerWindow *filer_window)
{
	Collection 	*collection = filer_window->collection;
	int		cursor = collection->cursor_item;
	DirItem 	*item;

	if (cursor < 0 || cursor >= collection->number_of_items)
	{
		gdk_beep();
		return;
	}

	item = (DirItem *) collection->items[cursor].data;
	
	gtk_entry_set_text(GTK_ENTRY(filer_window->minibuffer),
			make_path(filer_window->path, item->leafname)->str);
}

static gint key_press_event(GtkWidget	*widget,
			GdkEventKey	*event,
			FilerWindow	*filer_window)
{
	switch (event->keyval)
	{
		case GDK_Escape:
			gtk_widget_hide(widget);
			gtk_window_set_focus(GTK_WINDOW(filer_window->window),
					GTK_WIDGET(filer_window->collection));
			break;
		case GDK_Up:
			search_in_dir(filer_window, -1);
			break;
		case GDK_Down:
			search_in_dir(filer_window, 1);
			break;
		case GDK_Return:
			return_pressed(filer_window, event);
			break;
		case GDK_Tab:
			complete(filer_window);
			break;
		default:
			return FALSE;
	}

	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
	return TRUE;
}

static void changed(GtkEditable *mini, FilerWindow *filer_window)
{
	char	*new, *slash;
	char	*path, *real;

	new = gtk_entry_get_text(GTK_ENTRY(mini));
	if (*new == '/')
		new = g_strdup(new);
	else
		new = g_strdup_printf("/%s", new);

	slash = strrchr(new, '/');
	*slash = '\0';

	if (*new == '\0')
		path = "/";
	else
		path = new;

	real = pathdup(path);

	if (strcmp(real, filer_window->path) != 0)
	{
		struct stat info;
		if (stat(real, &info) == 0 && S_ISDIR(info.st_mode))
			filer_change_to(filer_window, real, slash + 1);
		else
			gdk_beep();
	}
	else
	{
		Collection 	*collection = filer_window->collection;
		int 		item;

		find_next_match(filer_window, slash + 1, 0);
		item = collection->cursor_item;
		if (item != -1 && !matches(collection, item, slash + 1))
			gdk_beep();
	}
		
	g_free(real);
	g_free(new);
}

/* Find the next item in the collection that matches 'pattern'. Start from
 * mini_cursor_base and loop at either end. dir is 1 for a forward search,
 * -1 for backwards. 0 means forwards, but may stay the same.
 *
 * Does not automatically update mini_cursor_base.
 */
static void find_next_match(FilerWindow *filer_window, char *pattern, int dir)
{
	Collection *collection = filer_window->collection;
	int 	   base = filer_window->mini_cursor_base;
	int 	   item = base;

	if (collection->number_of_items < 1)
		return;

	if (base < 0 || base>= collection->number_of_items)
		filer_window->mini_cursor_base = base = 0;

	do
	{
		/* Step to the next item */
		item += dir;

		if (item >= collection->number_of_items)
			item = 0;
		else if (item < 0)
			item = collection->number_of_items - 1;

		if (dir == 0)
			dir = 1;
		else if (item == base)
			break;		/* No (other) matches at all */


	} while (!matches(collection, item, pattern));

	collection_set_cursor_item(collection, item);
}

static gboolean matches(Collection *collection, int item_number, char *pattern)
{
	DirItem *item = (DirItem *) collection->items[item_number].data;
	
	return strncmp(item->leafname, pattern, strlen(pattern)) == 0;
}

/* Find next match and set base for future matches. */
static void search_in_dir(FilerWindow *filer_window, int dir)
{
	char	*path, *pattern;

	path = gtk_entry_get_text(GTK_ENTRY(filer_window->minibuffer));
	pattern = strrchr(path, '/');
	if (pattern)
		pattern++;
	else
		pattern = path;
	
	filer_window->mini_cursor_base = filer_window->collection->cursor_item;
	find_next_match(filer_window, pattern, dir);
	filer_window->mini_cursor_base = filer_window->collection->cursor_item;
}
