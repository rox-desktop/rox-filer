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

/* action.c - code for handling the filer action windows.
 * These routines generally fork() and talk to us via pipes.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <dirent.h>

#include "action.h"
#include "string.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "main.h"

static GdkColor red = {0, 0xffff, 0, 0};

typedef struct _GUIside GUIside;
typedef void ActionChild(gpointer data);
typedef gboolean ForDirCB(char *path, char *dest_path);

struct _GUIside
{
	int 		from_child;	/* File descriptor */
	FILE		*to_child;
	int 		input_tag;	/* gdk_input_add() */
	GtkWidget 	*log, *window, *actions, *dir;
	int		child;		/* Process ID */
	int		errors;
	gboolean	show_info;	/* For Disk Usage */
	
	char		*next_dir;	/* NULL => no timer active */
	gint		next_timer;
};

/* These don't need to be in a structure because we fork() before
 * using them again.
 */
static int 	from_parent = 0;
static FILE	*to_parent = NULL;
static gboolean	quiet = FALSE;
static GString  *message = NULL;
static char     *action_dest = NULL;
static gboolean (*action_do_func)(char *source, char *dest);
static size_t	size_tally;	/* For Disk Usage */
static DirItem 	*mount_item;

/* Static prototypes */
static gboolean send();
static gboolean send_error();
static gboolean send_dir(char *dir);
static gboolean read_exact(int source, char *buffer, ssize_t len);
static void do_mount(FilerWindow *filer_window, DirItem *item);

static gboolean display_dir(gpointer data)
{
	GUIside	*gui_side = (GUIside *) data;

	gtk_label_set_text(GTK_LABEL(gui_side->dir), gui_side->next_dir + 1);
	g_free(gui_side->next_dir);
	gui_side->next_dir = NULL;
	
	return FALSE;
}

/* Called when the child sends us a message */
static void message_from_child(gpointer 	 data,
			       gint     	 source, 
			       GdkInputCondition condition)
{
	char buf[5];
	GUIside	*gui_side = (GUIside *) data;
	GtkWidget *log = gui_side->log;

	if (read_exact(source, buf, 4))
	{
		ssize_t message_len;
		char	*buffer;

		buf[4] = '\0';
		message_len = strtol(buf, NULL, 16);
		buffer = g_malloc(message_len + 1);
		if (message_len > 0 && read_exact(source, buffer, message_len))
		{
			buffer[message_len] = '\0';
			if (*buffer == '?')
				gtk_widget_set_sensitive(gui_side->actions,
							TRUE);
			else if (*buffer == '+')
			{
				refresh_dirs(buffer + 1);
				g_free(buffer);
				return;
			}
			else if (*buffer == 'm')
			{
				filer_check_mounted(buffer + 1);
				g_free(buffer);
				return;
			}
			else if (*buffer == '/')
			{
				if (gui_side->next_dir)
					g_free(gui_side->next_dir);
				else
					gui_side->next_timer =
						gtk_timeout_add(500,
								display_dir,
								gui_side);
				gui_side->next_dir = buffer;
				return;
			}
			else if (*buffer == '!')
				gui_side->errors++;

			gtk_text_insert(GTK_TEXT(log),
					NULL,
					*buffer == '!' ? &red : NULL,
					NULL,
					buffer + 1, message_len - 1);
			g_free(buffer);
			return;
		}
		g_print("Child died in the middle of a message.\n");
	}

	/* The child is dead */
	gui_side->child = 0;

	fclose(gui_side->to_child);
	close(gui_side->from_child);
	gdk_input_remove(gui_side->input_tag);

	if (gui_side->errors)
	{
		GString *report;
		report = g_string_new(NULL);
		g_string_sprintf(report, "There %s %d error%s.\n",
				gui_side->errors == 1 ? "was" : "were",
				gui_side->errors,
				gui_side->errors == 1 ? "" : "s");
		gtk_text_insert(GTK_TEXT(log), NULL, &red, NULL,
				report->str, report->len);

		g_string_free(report, TRUE);
	}
	else if (gui_side->show_info == FALSE)
		gtk_widget_destroy(gui_side->window);
}

/* Scans src_dir, updating dest_path whenever cb returns TRUE */
static void for_dir_contents(ForDirCB *cb, char *src_dir, char *dest_path)
{
	DIR	*d;
	struct dirent *ent;
	GSList  *list = NULL, *next;

	d = opendir(src_dir);
	if (!d)
	{
		send_error();
		return;
	}

	send_dir(src_dir);

	while ((ent = readdir(d)))
	{
		if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0'
			|| (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
			continue;
		list = g_slist_append(list, g_strdup(make_path(src_dir,
						       ent->d_name)->str));
	}
	closedir(d);

	if (!list)
		return;

	next = list;

	while (next)
	{
		if (cb((char *) next->data, dest_path))
		{
			g_string_sprintf(message, "+%s", dest_path);
			send();
		}

		g_free(next->data);
		next = next->next;
	}
	g_slist_free(list);
	return;
}

/* Read this many bytes into the buffer. TRUE on success. */
static gboolean read_exact(int source, char *buffer, ssize_t len)
{
	while (len > 0)
	{
		ssize_t got;
		got = read(source, buffer, len);
		if (got < 1)
			return FALSE;
		len -= got;
		buffer += got;
	}
	return TRUE;
}

/* Send 'message' to our parent process. TRUE on success. */
static gboolean send()
{
	char len_buffer[5];
	ssize_t len;

	g_return_val_if_fail(message->len < 0xffff, FALSE);

	sprintf(len_buffer, "%04x", message->len);
	fwrite(len_buffer, 1, 4, to_parent);
	len = fwrite(message->str, 1, message->len, to_parent);
	fflush(to_parent);
	return len == message->len;
}

/* Set the directory indicator at the top of the window */
static gboolean send_dir(char *dir)
{
	g_string_sprintf(message, "/%s", dir);
	return send();
}

static gboolean send_error()
{
	g_string_sprintf(message, "!ERROR: %s\n", g_strerror(errno));
	return send();
}

static void button_reply(GtkWidget *button, GUIside *gui_side)
{
	char *text;

	text = gtk_object_get_data(GTK_OBJECT(button), "send-code");
	g_return_if_fail(text != NULL);
	fputc(*text, gui_side->to_child);
	fflush(gui_side->to_child);

	gtk_widget_set_sensitive(gui_side->actions, FALSE);
}

/* Get one char from fd. Quit on error. */
static char reply(int fd)
{
	ssize_t len;
	char retval;
	
	len = read(fd, &retval, 1);
	if (len == 1)
		return retval;

	fprintf(stderr, "read() error: %s\n", g_strerror(errno));
	_exit(1);	/* Parent died? */
	return '!';
}

static void destroy_action_window(GtkWidget *widget, gpointer data)
{
	GUIside	*gui_side = (GUIside *) data;

	if (gui_side->child)
	{
		kill(gui_side->child, SIGTERM);
		fclose(gui_side->to_child);
		close(gui_side->from_child);
		gdk_input_remove(gui_side->input_tag);
	}

	if (gui_side->next_dir)
	{
		gtk_timeout_remove(gui_side->next_timer);
		g_free(gui_side->next_dir);
	}
	g_free(gui_side);
	
	if (--number_of_windows < 1)
		gtk_main_quit();
}

/* Create two pipes, fork() a child and return a pointer to a GUIside struct
 * (NULL on failure). The child calls func().
 */
static GUIside *start_action(gpointer data, ActionChild *func)
{
	int		filedes[4];	/* 0 and 2 are for reading */
	GUIside		*gui_side;
	int		child;
	GtkWidget	*vbox, *button, *control, *hbox, *scrollbar;

	if (pipe(filedes))
	{
		report_error("ROX-Filer", g_strerror(errno));
		return NULL;
	}

	if (pipe(filedes + 2))
	{
		close(filedes[0]);
		close(filedes[1]);
		report_error("ROX-Filer", g_strerror(errno));
		return NULL;
	}

	child = fork();
	switch (child)
	{
		case -1:
			report_error("ROX-Filer", g_strerror(errno));
			return NULL;
		case 0:
			/* We are the child */
			message = g_string_new(NULL);
			close(filedes[0]);
			close(filedes[3]);
			to_parent = fdopen(filedes[1], "wb");
			from_parent = filedes[2];
			func(data);
			_exit(0);
	}

	/* We are the parent */
	close(filedes[1]);
	close(filedes[2]);
	gui_side = g_malloc(sizeof(GUIside));
	gui_side->from_child = filedes[0];
	gui_side->to_child = fdopen(filedes[3], "wb");
	gui_side->log = NULL;
	gui_side->child = child;
	gui_side->errors = 0;
	gui_side->show_info = FALSE;

	gui_side->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(gui_side->window), 500, 100);
	gtk_signal_connect(GTK_OBJECT(gui_side->window), "destroy",
			GTK_SIGNAL_FUNC(destroy_action_window), gui_side);

	vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(gui_side->window), vbox);

	gui_side->dir = gtk_label_new("<dir>");
	gui_side->next_dir = NULL;
	gtk_misc_set_alignment(GTK_MISC(gui_side->dir), 0.5, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), gui_side->dir, FALSE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

	gui_side->log = gtk_text_new(NULL, NULL);
	gtk_box_pack_start(GTK_BOX(hbox), gui_side->log, TRUE, TRUE, 0);
	scrollbar = gtk_vscrollbar_new(GTK_TEXT(gui_side->log)->vadj);
	gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, TRUE, 0);

	gui_side->actions = gtk_hbox_new(TRUE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), gui_side->actions, FALSE, TRUE, 0);

	button = gtk_button_new_with_label("Always");
	gtk_object_set_data(GTK_OBJECT(button), "send-code", "A");
	gtk_box_pack_start(GTK_BOX(gui_side->actions), button, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			button_reply, gui_side);
	button = gtk_button_new_with_label("Yes");
	gtk_object_set_data(GTK_OBJECT(button), "send-code", "Y");
	gtk_box_pack_start(GTK_BOX(gui_side->actions), button, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			button_reply, gui_side);
	button = gtk_button_new_with_label("No");
	gtk_object_set_data(GTK_OBJECT(button), "send-code", "N");
	gtk_box_pack_start(GTK_BOX(gui_side->actions), button, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			button_reply, gui_side);
	gtk_widget_set_sensitive(gui_side->actions, FALSE);

	control = gtk_hbox_new(TRUE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), control, FALSE, TRUE, 0);
	
	button = gtk_button_new_with_label("Suspend");
	gtk_box_pack_start(GTK_BOX(control), button, TRUE, TRUE, 0);

	button = gtk_button_new_with_label("Resume");
	gtk_widget_set_sensitive(button, FALSE);
	gtk_box_pack_start(GTK_BOX(control), button, TRUE, TRUE, 0);

	button = gtk_button_new_with_label("Abort");
	gtk_box_pack_start(GTK_BOX(control), button, TRUE, TRUE, 0);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			gtk_widget_destroy, GTK_OBJECT(gui_side->window));

	gui_side->input_tag = gdk_input_add(gui_side->from_child,
						GDK_INPUT_READ,
						message_from_child,
						gui_side);

	return gui_side;
}

/* 			ACTIONS ON ONE ITEM 			*/

/* These may call themselves recursively, or ask questions, etc.
 * TRUE iff the directory containing dest_path needs to be rescanned.
 */

/* dest_path is the dir containing src_path.
 * Updates the global size_tally.
 */
static gboolean do_usage(char *src_path, char *dest_path)
{
	struct 		stat info;

	if (lstat(src_path, &info))
	{
		g_string_sprintf(message, "'%s:\n", src_path);
		send();
		send_error();
		return FALSE;
	}

	if (S_ISREG(info.st_mode) || S_ISLNK(info.st_mode))
		size_tally += info.st_size;
	else if (S_ISDIR(info.st_mode))
	{
		char *safe_path;
		safe_path = g_strdup(src_path);
		for_dir_contents(do_usage, safe_path, safe_path);
		g_free(safe_path);
	}

	return FALSE;
}

/* dest_path is the dir containing src_path */
static gboolean do_delete(char *src_path, char *dest_path)
{
	struct 		stat info;
	gboolean	write_prot;
	char		rep;

	if (lstat(src_path, &info))
	{
		send_error();
		return FALSE;
	}

	write_prot = S_ISLNK(info.st_mode) ? FALSE
					   : access(src_path, W_OK) != 0;
	if (quiet == 0 || write_prot)
	{
		g_string_sprintf(message, "?Delete %s'%s'?\n",
				write_prot ? "WRITE-PROTECTED " : " ",
				src_path);
		send();
		rep = reply(from_parent);
		if (rep == 'A')
			quiet = TRUE;
		else if (rep != 'Y')
			return FALSE;
	}
	if (S_ISDIR(info.st_mode))
	{
		char *safe_path;
		safe_path = g_strdup(src_path);
		for_dir_contents(do_delete, safe_path, safe_path);
		if (rmdir(safe_path))
		{
			g_free(safe_path);
			send_error();
			return FALSE;
		}
		g_string_assign(message, "'Directory deleted\n");
		send();
		g_free(safe_path);
	}
	else if (unlink(src_path) == 0)
	{
		g_string_sprintf(message, "'Deleted '%s'\n", src_path);
		send();
	}
	else
	{
		send_error();
		return FALSE;
	}

	return TRUE;
}

static gboolean do_copy(char *path, char *dest)
{
	char		*dest_path;
	char		*leaf;
	struct stat 	info;
	gboolean	retval = TRUE;

	leaf = strrchr(path, '/');
	if (!leaf)
		leaf = path;		/* Error? */
	else
		leaf++;

	dest_path = make_path(dest, leaf)->str;

	g_string_sprintf(message, "'Copying %s as %s\n", path, dest_path);
	send();

	if (access(dest_path, F_OK) == 0)
	{
		char	rep;
		g_string_sprintf(message, "?'%s' already exists - overwrite?\n",
				dest_path);
		send();
		
		rep = reply(from_parent);
		if (rep == 'A')
			quiet = TRUE;
		else if (rep != 'Y')
			return FALSE;
	}

	if (lstat(path, &info))
	{
		send_error();
		return FALSE;
	}
	
	if (S_ISDIR(info.st_mode))
	{
		char *safe_path, *safe_dest;
		struct stat 	dest_info;
		gboolean	exists;

		/* (we will do the update ourselves now, rather than
		 * afterwards)
		 */
		retval = FALSE;
		
		safe_path = g_strdup(path);
		safe_dest = g_strdup(dest_path);

		exists = !lstat(dest_path, &dest_info);

		if (exists && !S_ISDIR(dest_info.st_mode))
		{
			g_string_sprintf(message,
					"!ERROR: Destination already exists, "
					"but is not a directory\n");
		}
		else if (exists == FALSE && mkdir(dest_path, info.st_mode))
			send_error();
		else
		{
			if (!exists)
			{
				/* (just been created then) */
				g_string_sprintf(message, "+%s", dest);
				send();
			}
			
			for_dir_contents(do_copy, safe_path, safe_dest);
			/* Note: dest_path now invalid... */
		}

		g_free(safe_path);
		g_free(safe_dest);
	}
	else
	{
		char	*argv[] = {"cp", "-dpRf", NULL, NULL, NULL};

		argv[2] = path;
		argv[3] = dest_path;

		if (fork_exec_wait(argv))
		{
			g_string_sprintf(message, "!ERROR: %s\n",
					"Copy failed\n");
			send();
			retval = FALSE;
		}
	}

	return retval;
}

static gboolean do_move(char *path, char *dest)
{
	char		*dest_path;
	char		*leaf;
	gboolean	retval = TRUE;
	char		*argv[] = {"mv", "-f", NULL, NULL, NULL};

	leaf = strrchr(path, '/');
	if (!leaf)
		leaf = path;		/* Error? */
	else
		leaf++;

	dest_path = make_path(dest, leaf)->str;

	g_string_sprintf(message, "'Moving %s into %s...\n", path, dest);
	send();
	
	if (access(dest_path, F_OK) == 0)
	{
		char		rep;
		struct stat	info;

		g_string_sprintf(message, "?'%s' already exists - overwrite?\n",
				dest_path);
		send();
		
		rep = reply(from_parent);
		if (rep == 'A')
			quiet = TRUE;
		else if (rep != 'Y')
			return FALSE;

		if (lstat(dest_path, &info))
		{
			send_error();
			return FALSE;
		}

		if (S_ISDIR(info.st_mode))
		{
			char *safe_path, *safe_dest;

			safe_path = g_strdup(path);
			safe_dest = g_strdup(dest_path);
			for_dir_contents(do_move, safe_path, safe_dest);
			g_free(safe_dest);
			if (rmdir(safe_path))
			{
				g_free(safe_path);
				send_error();
				return FALSE;
			}
			g_string_assign(message, "'Directory deleted\n");
			send();
			g_free(safe_path);
			g_string_sprintf(message, "+%s", path);
			g_string_truncate(message, leaf - path);
			send();
			return TRUE;
		}
	}

	argv[2] = path;
	argv[3] = dest;

	if (fork_exec_wait(argv) == 0)
	{
		g_string_sprintf(message, "+%s", path);
		g_string_truncate(message, leaf - path);
		send();
	}
	else
	{
		g_string_sprintf(message, "!ERROR: Failed to move %s as %s\n",
				path, dest_path);
		retval = FALSE;
	}
	send();

	return retval;
}

static gboolean do_link(char *path, char *dest)
{
	char		*dest_path;
	char		*leaf;

	leaf = strrchr(path, '/');
	if (!leaf)
		leaf = path;		/* Error? */
	else
		leaf++;

	dest_path = make_path(dest, leaf)->str;

	if (symlink(path, dest_path))
	{
		send_error();
		return FALSE;
	}
	else
	{
		g_string_sprintf(message, "'Symlinked %s as %s\n",
				path, dest_path);
		send();
	}

	return TRUE;
}

/* Mount/umount this item */
/* XXX: Spaces? */
static void do_mount(FilerWindow *filer_window, DirItem *item)
{
	char		*command;
	char		*path;

	path = make_path(filer_window->path, item->leafname)->str;

	command = g_strdup_printf("%smount %s",
			item->flags & ITEM_FLAG_MOUNTED ? "u" : "", path);
	g_string_sprintf(message, "'> %s\n", command);
	send();
	if (system(command) == 0)
	{
		g_string_sprintf(message, "+%s", filer_window->path);
		send();
		g_string_sprintf(message, "m%s", path);
		send();
	}
	else
	{
		g_string_sprintf(message, "!Operation failed.\n");
		send();
	}
	g_free(command);
}

/*			CHILD MAIN LOOPS			*/

/* After forking, the child calls one of these functions */

static void usage_cb(gpointer data)
{
	FilerWindow *filer_window = (FilerWindow *) data;
	Collection *collection = filer_window->collection;
	DirItem   *item;
	int	left = collection->number_selected;
	int	i = -1;
	off_t	total_size = 0;

	send_dir(filer_window->path);

	while (left > 0)
	{
		i++;
		if (!collection->items[i].selected)
			continue;
		item = (DirItem *) collection->items[i].data;
		size_tally = 0;
		do_usage(make_path(filer_window->path,
					item->leafname)->str,
					filer_window->path);
		g_string_sprintf(message, "'%s: %s\n",
				item->leafname,
				format_size((unsigned long) size_tally));
		send();
		total_size += size_tally;
		left--;
	}
	
	g_string_sprintf(message, "'\nTotal: %s\n",
			format_size((unsigned long) total_size));
	send();
}

#ifdef DO_MOUNT_POINTS
static void mount_cb(gpointer data)
{
	FilerWindow 	*filer_window = (FilerWindow *) data;
	Collection	*collection = filer_window->collection;
	DirItem   	*item;
	int		i;
	gboolean	mount_points = FALSE;

	send_dir(filer_window->path);

	if (mount_item)
		do_mount(filer_window, mount_item);
	else
	{
		for (i = 0; i < collection->number_of_items; i++)
		{
			if (!collection->items[i].selected)
				continue;
			item = (DirItem *) collection->items[i].data;
			if (!(item->flags & ITEM_FLAG_MOUNT_POINT))
				continue;
			mount_points = TRUE;

			do_mount(filer_window, item);
		}
	
		if (!mount_points)
		{
			g_string_sprintf(message,
					"!No mount points selected!\n");
			send();
		}
	}

	g_string_sprintf(message, "'\nDone\n");
	send();
}
#endif

static void delete_cb(gpointer data)
{
	FilerWindow *filer_window = (FilerWindow *) data;
	Collection *collection = filer_window->collection;
	DirItem   *item;
	int	left = collection->number_selected;
	int	i = -1;

	send_dir(filer_window->path);

	while (left > 0)
	{
		i++;
		if (!collection->items[i].selected)
			continue;
		item = (DirItem *) collection->items[i].data;
		if (do_delete(make_path(filer_window->path,
						item->leafname)->str,
						filer_window->path))
		{
			g_string_sprintf(message, "+%s", filer_window->path);
			send();
		}
		left--;
	}
	
	g_string_sprintf(message, "'\nDone\n");
	send();
}

static void list_cb(gpointer data)
{
	GSList	*paths = (GSList *) data;

	while (paths)
	{
		send_dir((char *) paths->data);

		if (action_do_func((char *) paths->data, action_dest))
		{
			g_string_sprintf(message, "+%s", action_dest);
			send();
		}

		paths = paths->next;
	}

	g_string_sprintf(message, "'\nDone\n");
	send();
}

/*			EXTERNAL INTERFACE			*/

/* Count disk space used by selected items */
void action_usage(FilerWindow *filer_window)
{
	GUIside		*gui_side;
	Collection 	*collection;

	collection = filer_window->collection;

	if (collection->number_selected < 1)
	{
		report_error("ROX-Filer", "You need to select some items "
				"to count");
		return;
	}

	gui_side = start_action(filer_window, usage_cb);
	if (!gui_side)
		return;

	gui_side->show_info = TRUE;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), "Disk Usage");
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

/* Mount/unmount 'item', or all selected mount points if NULL. */
void action_mount(FilerWindow *filer_window, DirItem *item)
{
#ifdef DO_MOUNT_POINTS
	GUIside		*gui_side;
	Collection 	*collection;

	collection = filer_window->collection;

	if (item == NULL && collection->number_selected < 1)
	{
		report_error("ROX-Filer", "You need to select some items "
				"to mount or unmount");
		return;
	}

	mount_item = item;
	gui_side = start_action(filer_window, mount_cb);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), "Mount / Unmount");
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
#else
	report_error("ROX-Filer",
			"ROX-Filer does not yet support mount points on your "
			"system. Sorry.");
#endif /* DO_MOUNT_POINTS */
}

/* Deletes all selected items in the window */
void action_delete(FilerWindow *filer_window)
{
	GUIside		*gui_side;
	Collection 	*collection;

	collection = filer_window->collection;

	if (collection->number_selected < 1)
	{
		report_error("ROX-Filer", "You need to select some items "
				"to delete");
		return;
	}

	gui_side = start_action(filer_window, delete_cb);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), "Delete");
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

void action_copy(GSList *paths, char *dest)
{
	GUIside		*gui_side;

	action_dest = dest;
	action_do_func = do_copy;
	gui_side = start_action(paths, list_cb);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), "Copy");
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

void action_move(GSList *paths, char *dest)
{
	GUIside		*gui_side;

	action_dest = dest;
	action_do_func = do_move;
	gui_side = start_action(paths, list_cb);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), "Move");
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

void action_link(GSList *paths, char *dest)
{
	GUIside		*gui_side;

	action_dest = dest;
	action_do_func = do_link;
	gui_side = start_action(paths, list_cb);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), "Link");
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

