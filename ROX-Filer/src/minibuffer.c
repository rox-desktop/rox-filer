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

/* minibuffer.c - for handling the path entry box at the bottom */

#include "config.h"

#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "collection.h"
#include "gui_support.h"
#include "support.h"
#include "minibuffer.h"
#include "filer.h"
#include "main.h"

static GList *shell_history = NULL;

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
	gtk_widget_set_style(mini, fixed_style);
	gtk_signal_connect(GTK_OBJECT(mini), "key_press_event",
			GTK_SIGNAL_FUNC(key_press_event), filer_window);
	gtk_signal_connect(GTK_OBJECT(mini), "changed",
			GTK_SIGNAL_FUNC(changed), filer_window);

	return mini;
}

void minibuffer_show(FilerWindow *filer_window, MiniType mini_type)
{
	Collection	*collection;
	GtkEntry	*mini;
	
	g_return_if_fail(filer_window != NULL);
	g_return_if_fail(filer_window->minibuffer != NULL);

	mini = GTK_ENTRY(filer_window->minibuffer);

	filer_window->mini_type = MINI_NONE;

	switch (mini_type)
	{
		case MINI_PATH:
			collection = filer_window->collection;
			filer_window->mini_cursor_base =
					MAX(collection->cursor_item, 0);
			gtk_entry_set_text(mini,
					make_path(filer_window->path, "")->str);
			break;
		case MINI_SHELL:
			filer_window->mini_cursor_base = -1;	/* History */
			gtk_entry_set_text(mini, "");
			break;
		default:
			g_warning("Bad minibuffer type\n");
			return;
	}
	
	filer_window->mini_type = mini_type;

	gtk_entry_set_position(mini, -1);

	gtk_widget_show(filer_window->minibuffer);

	gtk_window_set_focus(GTK_WINDOW(filer_window->window),
			filer_window->minibuffer);
}

void minibuffer_hide(FilerWindow *filer_window)
{
	filer_window->mini_type = MINI_NONE;

	gtk_widget_hide(filer_window->minibuffer);
	gtk_window_set_focus(GTK_WINDOW(filer_window->window),
			GTK_WIDGET(filer_window->collection));
}

/* Insert this leafname at the cursor (replacing the selection, if any).
 * Must be in SHELL mode.
 */
void minibuffer_add(FilerWindow *filer_window, guchar *leafname)
{
	guchar		*esc;
	GtkEditable 	*edit = GTK_EDITABLE(filer_window->minibuffer);
	int		pos;

	g_return_if_fail(filer_window->mini_type == MINI_SHELL);

	if (strchr(leafname, ' '))
		esc = g_strdup_printf(" \"%s\"", leafname);
	else
		esc = g_strdup_printf(" %s", leafname);

	gtk_editable_delete_selection(edit);
	pos = gtk_editable_get_position(edit);
	gtk_editable_insert_text(edit, esc, strlen(esc), &pos);
	gtk_editable_set_position(edit, pos);

	g_free(esc);
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/*			PATH ENTRY			*/

static void path_return_pressed(FilerWindow *filer_window, GdkEventKey *event)
{
	Collection 	*collection = filer_window->collection;
	int		item = collection->cursor_item;
	char		*path, *pattern;
	int		flags = OPEN_FROM_MINI | OPEN_SAME_WINDOW;

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
	
	if ((event->state & GDK_SHIFT_MASK) != 0)
		flags |= OPEN_SHIFT;

	filer_openitem(filer_window, item, flags);
}

/* Use the cursor item to fill in the minibuffer.
 * If there are multiple matches the fill in as much as possible and beep.
 */
static void complete(FilerWindow *filer_window)
{
	GtkEntry	*entry;
	Collection 	*collection = filer_window->collection;
	int		cursor = collection->cursor_item;
	DirItem 	*item;
	int		shortest_stem = -1;
	int		current_stem;
	int		other;
	guchar		*text, *leaf;

	if (cursor < 0 || cursor >= collection->number_of_items)
	{
		gdk_beep();
		return;
	}

	entry = GTK_ENTRY(filer_window->minibuffer);
	
	item = (DirItem *) collection->items[cursor].data;

	text = gtk_entry_get_text(entry);
	leaf = strrchr(text, '/');
	if (!leaf)
	{
		gdk_beep();
		return;
	}

	leaf++;
	if (!matches(collection, cursor, leaf))
	{
		gdk_beep();
		return;
	}
	
	current_stem = strlen(leaf);

	/* Find the longest other match of this name. It it's longer than
	 * the currently entered text then complete only up to that length.
	 */
	for (other = 0; other < collection->number_of_items; other++)
	{
		DirItem *other_item = (DirItem *) collection->items[other].data;
		int	stem = 0;

		if (other == cursor)
			continue;

		while (other_item->leafname[stem] && item->leafname[stem])
		{
			if (other_item->leafname[stem] != item->leafname[stem])
				break;
			stem++;
		}

		/* stem is the index of the first difference */
		if (stem >= current_stem &&
				(shortest_stem == -1 || stem < shortest_stem))
			shortest_stem = stem;
	}

	if (current_stem == shortest_stem)
		gdk_beep();
	else if (current_stem < shortest_stem)
	{
		guchar	*extra;

		extra = g_strndup(item->leafname + current_stem,
				shortest_stem - current_stem);
		gtk_entry_append_text(entry, extra);
		g_free(extra);
		gdk_beep();
	}
	else
	{
		GString	*new;

		new = make_path(filer_window->path, item->leafname);

		if (item->base_type == TYPE_DIRECTORY &&
				(item->flags & ITEM_FLAG_APPDIR) == 0)
			g_string_append_c(new, '/');

		gtk_entry_set_text(entry, new->str);
	}
}

static void path_changed(GtkEditable *mini, FilerWindow *filer_window)
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
		if (mc_stat(real, &info) == 0 && S_ISDIR(info.st_mode))
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

/*			SHELL COMMANDS			*/

static void add_to_history(FilerWindow *filer_window)
{
	guchar 	*line, *last, *c;
	
	line = gtk_entry_get_text(GTK_ENTRY(filer_window->minibuffer));
	
	for (c = line; *c && isspace(*c); c++)
		;

	if (!*c)
		return;
	
	last = shell_history ? (guchar *) shell_history->data : NULL;

	if (last && strcmp(last, line) == 0)
		return;			/* Duplicating last entry */
	
	shell_history = g_list_prepend(shell_history, g_strdup(line));
}

static void shell_return_pressed(FilerWindow *filer_window, GdkEventKey *event)
{
	GPtrArray	*argv;
	int		i;
	guchar		*entry;
	Collection	*collection = filer_window->collection;

	add_to_history(filer_window);

	entry = gtk_entry_get_text(GTK_ENTRY(filer_window->minibuffer));
	
	argv = g_ptr_array_new();
	g_ptr_array_add(argv, "sh");
	g_ptr_array_add(argv, "-c");
	g_ptr_array_add(argv, entry);
	g_ptr_array_add(argv, "sh");

	for (i = 0; i < collection->number_of_items; i++)
	{
		DirItem *item = (DirItem *) collection->items[i].data;
		if (collection->items[i].selected)
			g_ptr_array_add(argv, item->leafname);
	}
	
	g_ptr_array_add(argv, NULL);

	switch (fork())
	{
		case -1:
			delayed_error("ROX-Filer", "Failed to create "
					"child process");
			return;
		case 0:	/* Child */
			dup2(to_error_log, STDOUT_FILENO);
			close_on_exec(STDOUT_FILENO, FALSE);
			dup2(to_error_log, STDERR_FILENO);
			close_on_exec(STDERR_FILENO, FALSE);
			if (chdir(filer_window->path))
				g_printerr("chdir(%s) failed: %s\n",
						filer_window->path,
						g_strerror(errno));
			execvp((char *) argv->pdata[0],
				(char **) argv->pdata);
			g_printerr("execvp(%s, ...) failed: %s\n",
					(char *) argv->pdata[0],
					g_strerror(errno));
			_exit(0);
	}

	g_ptr_array_free(argv, TRUE);

	minibuffer_hide(filer_window);
}

/* Move through the shell history */
static void shell_recall(FilerWindow *filer_window, int dir)
{
	guchar	*command;
	int	pos = filer_window->mini_cursor_base;

	pos += dir;
	if (pos >= 0)
	{
		command = g_list_nth_data(shell_history, pos);
		if (!command)
			return;
	}
	else
		command = "";

	if (pos < -1)
		pos = -1;
	filer_window->mini_cursor_base = pos;

	gtk_entry_set_text(GTK_ENTRY(filer_window->minibuffer), command);
}

/*			EVENT HANDLERS			*/

static gint key_press_event(GtkWidget	*widget,
			GdkEventKey	*event,
			FilerWindow	*filer_window)
{
	if (event->keyval == GDK_Escape)
	{
		if (filer_window->mini_type == MINI_SHELL)
			add_to_history(filer_window);
		minibuffer_hide(filer_window);
		return TRUE;
	}

	switch (filer_window->mini_type)
	{
		case MINI_PATH:
			switch (event->keyval)
			{
				case GDK_Up:
					search_in_dir(filer_window, -1);
					break;
				case GDK_Down:
					search_in_dir(filer_window, 1);
					break;
				case GDK_Return:
					path_return_pressed(filer_window,
								event);
					break;
				case GDK_Tab:
					complete(filer_window);
					break;
				default:
					return FALSE;
			}
			break;

		case MINI_SHELL:
			switch (event->keyval)
			{
				case GDK_Up:
					shell_recall(filer_window, 1);
					break;
				case GDK_Down:
					shell_recall(filer_window, -1);
					break;
				case GDK_Tab:
					break;
				case GDK_Return:
					shell_return_pressed(filer_window,
								event);
				default:
					return FALSE;
			}
			break;
		default:
			break;
	}

	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
	return TRUE;
}

static void changed(GtkEditable *mini, FilerWindow *filer_window)
{
	switch (filer_window->mini_type)
	{
		case MINI_PATH:
			path_changed(mini, filer_window);
			return;
		case MINI_SHELL:
			break;
		default:
			break;
	}
}
