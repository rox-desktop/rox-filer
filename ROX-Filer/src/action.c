/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* action.c - code for handling the filer action windows.
 * These routines generally fork() and talk to us via pipes.
 */

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
typedef void ForDirCB(char *path);

struct _GUIside
{
	int 		from_child;	/* File descriptor */
	FILE		*to_child;
	int 		input_tag;	/* gdk_input_add() */
	GtkWidget 	*log, *window, *actions;
	int		child;		/* Process ID */
	int		errors;
};

/* These don't need to be in a structure because we fork() before
 * using them again.
 */
static int 	from_parent = 0;
static FILE	*to_parent = NULL;
static gboolean	quiet = FALSE;
static GString  *message = NULL;
static char     *action_dest = NULL;
static void     (*action_do_func)(char *source, char *dest);

/* Static prototypes */
static gboolean send();
static gboolean send_error();
static gboolean read_exact(int source, char *buffer, ssize_t len);

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
	else
		gtk_widget_destroy(gui_side->window);
}

static void for_dir_contents(char *dir, ForDirCB *cb)
{
	DIR	*d;
	struct dirent *ent;
	GSList  *list = NULL, *next;

	d = opendir(dir);
	if (!d)
	{
		send_error();
		return;
	}

	while ((ent = readdir(d)))
	{
		if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0'
			|| (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
			continue;
		list = g_slist_append(list, g_strdup(make_path(dir,
						       ent->d_name)->str));
	}
	closedir(d);

	if (!list)
		return;

	next = list;

	while (next)
	{
		cb((char *) next->data);
		g_string_sprintf(message, "+%s", dir);
		send();

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

	g_free(data);
	
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
	GtkWidget	*vbox, *button, *control;

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

	gui_side->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(gui_side->window), 500, 100);
	gtk_signal_connect(GTK_OBJECT(gui_side->window), "destroy",
			GTK_SIGNAL_FUNC(destroy_action_window), gui_side);

	vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(gui_side->window), vbox);

	gui_side->log = gtk_text_new(NULL, NULL);
	gtk_box_pack_start(GTK_BOX(vbox), gui_side->log, TRUE, TRUE, 0);

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

/* These may call themselves recursively, or ask questions, etc */

static void do_delete(char *path)
{
	struct 		stat info;
	gboolean	write_prot;
	char		rep;

	if (lstat(path, &info))
	{
		send_error();
		return;
	}

	write_prot = access(path, W_OK) != 0;
	if (quiet == 0 || write_prot)
	{
		g_string_sprintf(message, "?Delete %s'%s'?\n",
				write_prot ? "WRITE-PROTECTED " : " ",
				path);
		send();
		rep = reply(from_parent);
		if (rep == 'A')
			quiet = TRUE;
		else if (rep != 'Y')
			return;
	}
	if (S_ISDIR(info.st_mode))
	{
		char *safe_path;
		safe_path = g_strdup(path);
		g_string_sprintf(message, "'Scanning in '%s'\n", safe_path);
		send();
		for_dir_contents(safe_path, do_delete);
		if (rmdir(safe_path))
		{
			g_free(safe_path);
			send_error();
			return;
		}
		g_string_assign(message, "'Directory deleted\n");
		send();
		g_free(safe_path);
	}
	else if (unlink(path) == 0)
	{
		g_string_sprintf(message, "'Deleted '%s'\n", path);
		send();
	}
	else
		send_error();
}

static void do_copy(char *path, char *dest)
{
	char		*dest_path;
	char		*leaf;

	leaf = strrchr(path, '/');
	if (!leaf)
		leaf = path;		/* Error? */
	else
		leaf++;

	dest_path = make_path(dest, leaf)->str;

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
			return;
	}

	g_string_sprintf(message, "cp -a %s %s", path, dest_path);
	if (system(message->str) == 0)
		g_string_sprintf(message, "'Copied %s as %s\n",
				path, dest_path);
	else
		g_string_sprintf(message, "!ERROR: Failed to copy %s as %s\n",
				path, dest_path);
	send();
}

static void do_move(char *path, char *dest)
{
	char		*dest_path;
	char		*leaf;

	leaf = strrchr(path, '/');
	if (!leaf)
		leaf = path;		/* Error? */
	else
		leaf++;

	dest_path = make_path(dest, leaf)->str;

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
			return;
	}

	g_string_sprintf(message, "mv -f %s %s", path, dest_path);
	if (system(message->str) == 0)
	{
		g_string_sprintf(message, "+%s", path);
		g_string_truncate(message, leaf - path);
		send();
		g_string_sprintf(message, "'Moved %s as %s\n",
				path, dest_path);
	}
	else
		g_string_sprintf(message, "!ERROR: Failed to move %s as %s\n",
				path, dest_path);
	send();
}

static void do_link(char *path, char *dest)
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
		send_error();
	else
	{
		g_string_sprintf(message, "'Symlinked %s as %s\n",
				path, dest_path);
		send();
	}
}

/*			CHILD MAIN LOOPS			*/

/* After forking, the child calls one of these functions */

static void delete_cb(gpointer data)
{
	FilerWindow *filer_window = (FilerWindow *) data;
	Collection *collection = filer_window->collection;
	FileItem   *item;
	int	left = collection->number_selected;
	int	i = -1;

	while (left > 0)
	{
		i++;
		if (!collection->items[i].selected)
			continue;
		item = (FileItem *) collection->items[i].data;
		do_delete(make_path(filer_window->path, item->leafname)->str);
		g_string_sprintf(message, "+%s", filer_window->path);
		send();
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
		action_do_func((char *) paths->data, action_dest);
		g_string_sprintf(message, "+%s", action_dest);
		send();

		paths = paths->next;
	}

	g_string_sprintf(message, "'\nDone\n");
	send();
}

/*			EXTERNAL INTERFACE			*/

/* Deletes all selected items in the window */
void action_delete(FilerWindow *filer_window)
{
	GUIside		*gui_side;
	Collection 	*collection;

	collection = window_with_focus->collection;

	if (collection->number_selected < 1)
	{
		report_error("ROX-Filer", "You need to select some items "
				"first");
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

