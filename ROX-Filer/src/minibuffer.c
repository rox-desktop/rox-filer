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

/* minibuffer.c - for handling the path entry box at the bottom */

#include "config.h"

#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <glob.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"

#include "options.h"
#include "collection.h"
#include "find.h"
#include "gui_support.h"
#include "support.h"
#include "minibuffer.h"
#include "filer.h"
#include "display.h"
#include "main.h"
#include "action.h"
#include "diritem.h"
#include "type.h"

static GList *shell_history = NULL;

/* Static prototypes */
static gint key_press_event(GtkWidget	*widget,
			GdkEventKey	*event,
			FilerWindow	*filer_window);
static void changed(GtkEditable *mini, FilerWindow *filer_window);
static gboolean find_next_match(FilerWindow *filer_window,
				char *pattern,
				int dir);
static gboolean find_exact_match(FilerWindow *filer_window, char *pattern);
static gboolean matches(Collection *collection, int item, char *pattern);
static void search_in_dir(FilerWindow *filer_window, int dir);
static guchar *mini_contents(FilerWindow *filer_window);
static void show_help(FilerWindow *filer_window);
#ifdef GTK2
static gboolean grab_focus(GtkWidget *minibuffer);
#endif

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void minibuffer_init(void)
{
	option_add_int("filer_beep_fail", 1, NULL);
	option_add_int("filer_beep_multi", 1, NULL);
}

/* Creates the minibuffer widgets, setting the appropriate fields
 * in filer_window.
 */
void create_minibuffer(FilerWindow *filer_window)
{
	GtkWidget *hbox, *label, *mini;

	hbox = gtk_hbox_new(FALSE, 0);
	
	gtk_box_pack_start(GTK_BOX(hbox),
			new_help_button((HelpFunc) show_help, filer_window),
			FALSE, TRUE, 0);

	label = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 2);

	mini = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), mini, TRUE, TRUE, 0);
	gtk_widget_set_name(mini, "fixed-style");
	gtk_signal_connect(GTK_OBJECT(mini), "key_press_event",
			GTK_SIGNAL_FUNC(key_press_event), filer_window);
	gtk_signal_connect(GTK_OBJECT(mini), "changed",
			GTK_SIGNAL_FUNC(changed), filer_window);

#ifdef GTK2
	/* Grabbing focus musn't select the text... */
	gtk_signal_connect_object(GTK_OBJECT(mini), "grab-focus",
		GTK_SIGNAL_FUNC(grab_focus),
		GTK_OBJECT(mini));
#endif

	filer_window->minibuffer = mini;
	filer_window->minibuffer_label = label;
	filer_window->minibuffer_area = hbox;
}

void minibuffer_show(FilerWindow *filer_window, MiniType mini_type)
{
	Collection	*collection;
	GtkEntry	*mini;
	int		pos = -1, i;
	
	g_return_if_fail(filer_window != NULL);
	g_return_if_fail(filer_window->minibuffer != NULL);

	mini = GTK_ENTRY(filer_window->minibuffer);

	filer_window->mini_type = MINI_NONE;
	gtk_label_set_text(GTK_LABEL(filer_window->minibuffer_label),
			mini_type == MINI_PATH ? _("Goto:") :
			mini_type == MINI_SHELL ? _("Shell:") :
			mini_type == MINI_SELECT_IF ? _("Select If:") :
			"?");

	collection = filer_window->collection;
	switch (mini_type)
	{
		case MINI_PATH:
			collection_move_cursor(collection, 0, 0);
			filer_window->mini_cursor_base =
					MAX(collection->cursor_item, 0);
			gtk_entry_set_text(mini,
					make_path(filer_window->path, "")->str);
			if (filer_window->temp_show_hidden)
			{
				display_set_hidden(filer_window, FALSE);
				filer_window->temp_show_hidden = FALSE;
			}
			break;
		case MINI_SELECT_IF:
			gtk_entry_set_text(mini, "");
			filer_window->mini_cursor_base = -1;	/* History */
			break;
		case MINI_SHELL:
			pos = 0;
			i = collection->cursor_item;
			if (collection->number_selected)
				gtk_entry_set_text(mini, " \"$@\"");
			else if (i > -1 && i < collection->number_of_items)
			{
				DirItem *item = (DirItem *) 
						collection->items[i].data;
				guchar *tmp;

				tmp = g_strconcat(" ", item->leafname, NULL);
				gtk_entry_set_text(mini, tmp);
				g_free(tmp);
			}
			else
				gtk_entry_set_text(mini, "");
			filer_window->mini_cursor_base = -1;	/* History */
			break;
		default:
			g_warning("Bad minibuffer type\n");
			return;
	}
	
	filer_window->mini_type = mini_type;

	gtk_entry_set_position(mini, pos);

	gtk_widget_show_all(filer_window->minibuffer_area);

	gtk_window_set_focus(GTK_WINDOW(filer_window->window),
			filer_window->minibuffer);
}

void minibuffer_hide(FilerWindow *filer_window)
{
	filer_window->mini_type = MINI_NONE;

	gtk_widget_hide(filer_window->minibuffer_area);
	gtk_window_set_focus(GTK_WINDOW(filer_window->window),
			GTK_WIDGET(filer_window->collection));

	if (filer_window->temp_show_hidden)
	{
		Collection *collection = filer_window->collection;
		int i = collection->cursor_item;
		DirItem *item = NULL;

		if (i >= 0 && i < collection->number_of_items)
			item = (DirItem *) collection->items[i].data;
		
		if (item == NULL || item->leafname[0] != '.')
			display_set_hidden(filer_window, FALSE);
		filer_window->temp_show_hidden = FALSE;
	}
}

/* Insert this leafname at the cursor (replacing the selection, if any).
 * Must be in SHELL mode.
 */
void minibuffer_add(FilerWindow *filer_window, guchar *leafname)
{
	guchar		*esc;
	GtkEditable 	*edit = GTK_EDITABLE(filer_window->minibuffer);
	GtkEntry 	*entry = GTK_ENTRY(edit);
	int		pos;

	g_return_if_fail(filer_window->mini_type == MINI_SHELL);

	esc = shell_escape(leafname);

	gtk_editable_delete_selection(edit);
	pos = gtk_editable_get_position(edit);

	if (pos > 0 && gtk_entry_get_text(entry)[pos - 1] != ' ')
		gtk_editable_insert_text(edit, " ", 1, &pos);

	gtk_editable_insert_text(edit, esc, strlen(esc), &pos);
	gtk_editable_set_position(edit, pos);

	g_free(esc);
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void show_help(FilerWindow *filer_window)
{
	guchar	*message;

	gtk_widget_grab_focus(filer_window->minibuffer);

	switch (filer_window->mini_type)
	{
		case MINI_PATH:
			message = _("Enter the name of a file and I'll display "
				"it for you. Press Tab to fill in the longest "
				"match. Escape to close the minibuffer.");
			break;
		case MINI_SHELL:
			message = _("Enter a shell command to execute. Click "
				"on a file to add it to the buffer.");
			break;
		case MINI_SELECT_IF:
			show_condition_help(NULL);
			return;
		default:
			message = "?!?";
			break;
	}

	delayed_error("%s", message);
}


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

	if (item == -1 || item >= collection->number_of_items ||
			!matches(collection, item, pattern))
	{
		gdk_beep();
		return;
	}
	
	if ((event->state & GDK_SHIFT_MASK) != 0)
		flags |= OPEN_SHIFT;

	filer_openitem(filer_window, item, flags);
}

/* Use the cursor item to fill in the minibuffer.
 * If there are multiple matches then fill in as much as possible and
 * (possibly) beep.
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
	{
		/* We didn't add anything... */
		if (option_get_int("filer_beep_fail"))
			gdk_beep();
	}
	else if (current_stem < shortest_stem)
	{
		guchar	*extra;

		extra = g_strndup(item->leafname + current_stem,
				shortest_stem - current_stem);
		gtk_entry_append_text(entry, extra);
		g_free(extra);
#ifdef GTK2
		gtk_entry_set_position(entry, -1);
#endif

		if (option_get_int("filer_beep_multi"))
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
#ifdef GTK2
		gtk_entry_set_position(entry, -1);
#endif
	}
}

/* This is an idle function because Gtk+ 2.0 changes text in a entry
 * with two signals; one to blank it and one to put the new text in.
 */
static gboolean path_changed(gpointer data)
{
	FilerWindow *filer_window = (FilerWindow *) data;
	GtkWidget *mini = filer_window->minibuffer;
	char	*new, *leaf;
	char	*path, *real;

	new = gtk_entry_get_text(GTK_ENTRY(mini));

	leaf = g_basename(new);
	if (leaf == new)
		path = g_strdup("/");
	else
		path = g_dirname(new);
	real = pathdup(path);
	g_free(path);

	if (strcmp(real, filer_window->path) != 0)
	{
		/* The new path is in a different directory */
		struct stat info;

		if (mc_stat(real, &info) == 0 && S_ISDIR(info.st_mode))
			filer_change_to(filer_window, real, leaf);
		else
			gdk_beep();
	}
	else
	{
		if (*leaf == '.')
		{
			if (!filer_window->show_hidden)
			{
				filer_window->temp_show_hidden = TRUE;
				display_set_hidden(filer_window, TRUE);
			}
		}
		else if (filer_window->temp_show_hidden)
		{
			display_set_hidden(filer_window, FALSE);
			filer_window->temp_show_hidden = FALSE;
		}
		
		if (find_exact_match(filer_window, leaf) == FALSE &&
		    find_next_match(filer_window, leaf, 0) == FALSE)
			gdk_beep();
	}
		
	g_free(real);

	return FALSE;
}

/* Look for an exact match, and move the cursor to it if found.
 * TRUE on success.
 */
static gboolean find_exact_match(FilerWindow *filer_window, char *pattern)
{
	Collection 	*collection = filer_window->collection;
	int		i;

	for (i = 0; i < collection->number_of_items; i++)
	{
		DirItem *item = (DirItem *) collection->items[i].data;

		if (strcmp(item->leafname, pattern) == 0)
		{
			collection_set_cursor_item(collection, i);
			return TRUE;
		}
	}

	return FALSE;
}

/* Find the next item in the collection that matches 'pattern'. Start from
 * mini_cursor_base and loop at either end. dir is 1 for a forward search,
 * -1 for backwards. 0 means forwards, but may stay the same.
 *
 * Does not automatically update mini_cursor_base.
 *
 * Returns TRUE if a match is found.
 */
static gboolean find_next_match(FilerWindow *filer_window,
				char *pattern,
				int dir)
{
	Collection *collection = filer_window->collection;
	int 	   base = filer_window->mini_cursor_base;
	int 	   item = base;
	gboolean   retval = TRUE;

	if (collection->number_of_items < 1)
		return FALSE;

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
		{
			retval = FALSE;
			break;		/* No (other) matches at all */
		}
	} while (!matches(collection, item, pattern));

	collection_set_cursor_item(collection, item);

	return retval;
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

static void add_to_history(guchar *line)
{
	guchar 	*last;
	
	last = shell_history ? (guchar *) shell_history->data : NULL;

	if (last && strcmp(last, line) == 0)
		return;			/* Duplicating last entry */
	
	shell_history = g_list_prepend(shell_history, g_strdup(line));
}

static void shell_done(FilerWindow *filer_window)
{
	if (filer_exists(filer_window))
		filer_update_dir(filer_window, TRUE);
}

/* Given a list of matches, return the longest stem. g_free() the result.
 * Special chars are escaped. If there is only a single (non-dir) match
 * then a trailing space is added.
 */
static guchar *best_match(FilerWindow *filer_window, glob_t *matches)
{
	gchar	*first = matches->gl_pathv[0];
	int	i;
	int	longest, path_len;
	guchar	*path, *tmp;

	longest = strlen(first);

	for (i = 1; i < matches->gl_pathc; i++)
	{
		int	j;
		guchar	*m = matches->gl_pathv[i];

		for (j = 0; j < longest; j++)
			if (m[j] != first[j])
				longest = j;
	}

	path_len = strlen(filer_window->path);
	if (strncmp(filer_window->path, first, path_len) == 0 &&
			first[path_len] == '/' && first[path_len + 1])
	{
		path = g_strndup(first + path_len + 1, longest - path_len - 1);
	}
	else
		path = g_strndup(first, longest);

	tmp = shell_escape(path);
	g_free(path);

	if (matches->gl_pathc == 1 && tmp[strlen(tmp) - 1] != '/')
	{
		path = g_strdup_printf("%s ", tmp);
		g_free(tmp);
		return path;
	}
	
	return tmp;
}

static void shell_tab(FilerWindow *filer_window)
{
	int	i;
	guchar	*entry;
	guchar	quote;
	int	pos;
	GString	*leaf;
	glob_t	matches;
	int	leaf_start;
	
	entry = gtk_entry_get_text(GTK_ENTRY(filer_window->minibuffer));
	pos = gtk_editable_get_position(GTK_EDITABLE(filer_window->minibuffer));
	leaf = g_string_new(NULL);

	quote = '\0';
	for (i = 0; i < pos; i++)
	{
		guchar	c = entry[i];
		
		if (leaf->len == 0)
			leaf_start = i;

		if (c == ' ')
		{
			g_string_truncate(leaf, 0);
			continue;
		}
		else if (c == '\\' && i + 1 < pos)
			c = entry[++i];
		else if (c == '"' || c == '\'')
		{
			guchar	cc;

			for (++i; i < pos; i++)
			{
				cc = entry[i];

				if (cc == '\\' && i + 1 < pos)
					cc = entry[++i];
				else if (entry[i] == c)
					break;
				g_string_append_c(leaf, entry[i]);
			}
			continue;
		}
		
		g_string_append_c(leaf, c);
	}

	if (leaf->len == 0)
		leaf_start = pos;

	if (leaf->str[0] != '/' && leaf->str[0] != '~')
	{
		g_string_prepend_c(leaf, '/');
		g_string_prepend(leaf, filer_window->path);
	}

	g_string_append_c(leaf, '*');

	if (glob(leaf->str,
#ifdef GLOB_TILDE
			GLOB_TILDE |
#endif
			GLOB_MARK, NULL, &matches) == 0)
	{
		if (matches.gl_pathc > 0)
		{
			guchar		*best;
			GtkEditable 	*edit =
				GTK_EDITABLE(filer_window->minibuffer);

			best = best_match(filer_window, &matches);

			gtk_editable_delete_text(edit, leaf_start, pos);
			gtk_editable_insert_text(edit, best, strlen(best),
						&leaf_start);
			gtk_editable_set_position(edit, leaf_start);

			g_free(best);
		}
		if (matches.gl_pathc != 1)
			gdk_beep();

		globfree(&matches);
	}

	g_string_free(leaf, TRUE);
}

/* Either execute the command or make it the default run action */
static void shell_return_pressed(FilerWindow *filer_window)
{
	GPtrArray	*argv;
	int		i;
	guchar		*entry;
	Collection	*collection = filer_window->collection;
	int		child;

	entry = mini_contents(filer_window);

	if (!entry)
		goto out;

	add_to_history(entry);

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

	child = fork();

	switch (child)
	{
		case -1:
			delayed_error(_("Failed to create child process"));
			break;
		case 0:	/* Child */
			/* Ensure output is noticed - send stdout to stderr */
			dup2(STDERR_FILENO, STDOUT_FILENO);
			close_on_exec(STDOUT_FILENO, FALSE);
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
		default:
			on_child_death(child,
					(CallbackFn) shell_done, filer_window);
			break;
	}

	g_ptr_array_free(argv, TRUE);

out:
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

/*			SELECT IF			*/

static void select_return_pressed(FilerWindow *filer_window, guint etime)
{
	FindCondition	*cond;
	int		i, n;
	guchar		*entry;
	Collection	*collection = filer_window->collection;
	FindInfo	info;

	entry = mini_contents(filer_window);

	if (!entry)
		goto out;

	add_to_history(entry);

	cond = find_compile(entry);
	if (!cond)
	{
		delayed_error(_("Invalid Find condition"));
		return;
	}

	info.now = time(NULL);
	info.prune = FALSE;	/* (don't care) */
	n = collection->number_of_items;

	collection->block_selection_changed++;

	/* If an item at the start is selected then we could lose the
	 * primary selection after checking that item and then need to
	 * gain it again at the end. Therefore, if anything is selected
	 * then select the last item until the end of the search.
	 */
	if (collection->number_selected)
		collection_select_item(collection, n - 1);
	
	for (i = 0; i < n; i++)
	{
		DirItem *item = (DirItem *) collection->items[i].data;

		info.leaf = item->leafname;
		info.fullpath = make_path(filer_window->path, info.leaf)->str;

		if (lstat(info.fullpath, &info.stats) == 0 &&
				find_test_condition(cond, &info))
			collection_select_item(collection, i);
		else
			collection_unselect_item(collection, i);
	}

	find_condition_free(cond);

	collection_unblock_selection_changed(collection, etime, TRUE);
out:
	minibuffer_hide(filer_window);
}


/*			EVENT HANDLERS			*/

static gint key_press_event(GtkWidget	*widget,
			GdkEventKey	*event,
			FilerWindow	*filer_window)
{
	if (event->keyval == GDK_Escape)
	{
		if (filer_window->mini_type == MINI_SHELL)
		{
			guchar	*line;
			
			line = mini_contents(filer_window);
			if (line)
				add_to_history(line);
		}

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
				case GDK_KP_Enter:
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
					shell_tab(filer_window);
					break;
				case GDK_Return:
				case GDK_KP_Enter:
					shell_return_pressed(filer_window);
					break;
				default:
					return FALSE;
			}
			break;
		case MINI_SELECT_IF:
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
				case GDK_KP_Enter:
					select_return_pressed(filer_window,
								event->time);
					break;
				default:
					return FALSE;
			}
			break;
		default:
			break;
	}

#ifndef GTK2
	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
#endif
	return TRUE;
}

static void changed(GtkEditable *mini, FilerWindow *filer_window)
{
	switch (filer_window->mini_type)
	{
		case MINI_PATH:
			gtk_idle_add(path_changed, filer_window);
			return;
		case MINI_SELECT_IF:
			set_find_string_colour(GTK_WIDGET(mini), 
				gtk_entry_get_text(
					GTK_ENTRY(filer_window->minibuffer)));
			return;
		default:
			break;
	}
}

/* Returns a string (which must NOT be freed), or NULL if the buffer
 * is blank (whitespace only).
 */
static guchar *mini_contents(FilerWindow *filer_window)
{
	guchar	*entry, *c;

	entry = gtk_entry_get_text(GTK_ENTRY(filer_window->minibuffer));

	for (c = entry; *c; c++)
		if (!isspace(*c))
			return entry;

	return NULL;
}

#ifdef GTK2
static gboolean grab_focus(GtkWidget *minibuffer)
{
	GtkWidgetClass *class;
	
	class = GTK_WIDGET_CLASS(gtk_type_class(GTK_TYPE_WIDGET));

	class->grab_focus(minibuffer);

	gtk_signal_emit_stop_by_name(GTK_OBJECT(minibuffer), "grab_focus");

	return 1;
}
#endif
