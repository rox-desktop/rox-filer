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
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <dirent.h>

#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "main.h"

#define BUFLEN 40

typedef struct _GUIside GUIside;
typedef void ActionChild(FilerWindow *filer_window);
typedef void ForDirCB(char *path);

struct _GUIside
{
	int 		from_child;	/* File descriptor */
	FILE		*to_child;
	int 		input_tag;	/* gdk_input_add() */
	GtkWidget 	*log, *window, *actions;
	int		child;		/* Process ID */
};

/* These don't need to be in a structure because we fork() before
 * using them.
 */
static int 	from_parent = 0;
static FILE	*to_parent = NULL;
static gboolean	quiet = FALSE;

/* Static prototypes */
static gboolean send(FILE *file, char *string);

static void for_dir_contents(char *dir, ForDirCB *cb)
{
	DIR	*d;
	struct dirent *ent;
	GSList  *list = NULL, *next;

	d = opendir(dir);
	if (!d)
	{
		send(to_parent, g_strerror(errno));
		send(to_parent, "\n");
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
		g_free(next->data);
		next = next->next;
	}
	g_slist_free(list);
	return;
}

/* Send a string to fd. TRUE on success. */
static gboolean send(FILE *file, char *string)
{
	ssize_t len;

	len = strlen(string);
	return fwrite(string, sizeof(char), len, file) == len;
}

static void button_reply(GtkWidget *button, GUIside *gui_side)
{
	char *text;

	text = gtk_object_get_data(GTK_OBJECT(button), "send-code");
	g_return_if_fail(text != NULL);
	fputc(*text, gui_side->to_child);
	fflush(gui_side->to_child);
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

	fclose(gui_side->to_child);
	close(gui_side->from_child);
	gdk_input_remove(gui_side->input_tag);
	
	if (gui_side->child)
		kill(gui_side->child, SIGTERM);

	g_free(data);
	
	if (--number_of_windows < 1)
		gtk_main_quit();
}

/* Create two pipes, fork() a child and return a pointer to a GUIside struct
 * (NULL on failure). The child calls func().
 */
static GUIside *start_action(FilerWindow *filer_window, ActionChild *func)
{
	int		filedes[4];	/* 0 and 2 are for reading */
	GUIside		*gui_side;
	int		child;
	GtkWidget	*vbox, *button;

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
			close(filedes[0]);
			close(filedes[3]);
			to_parent = fdopen(filedes[1], "wb");
			from_parent = filedes[2];
			func(filer_window);
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

	gui_side->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(gui_side->window), 400, 100);
	gtk_signal_connect(GTK_OBJECT(gui_side->window), "destroy",
			GTK_SIGNAL_FUNC(destroy_action_window), gui_side);

	vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(gui_side->window), vbox);

	gui_side->log = gtk_text_new(NULL, NULL);
	gtk_box_pack_start(GTK_BOX(vbox), gui_side->log, TRUE, TRUE, 0);

	gui_side->actions = gtk_hbox_new(TRUE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), gui_side->actions, FALSE, TRUE, 0);
	
	button = gtk_button_new_with_label("Abort");
	gtk_box_pack_end(GTK_BOX(gui_side->actions), button, TRUE, TRUE, 0);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			gtk_widget_destroy, GTK_OBJECT(gui_side->window));

	return gui_side;
}

/* And now, the individual actions... */

/* Delete one item - may need to recurse, or ask questions */
static void do_delete(char *path)
{
	struct 	stat info;
	char 	*error;

	send(to_parent, path);
	send(to_parent, ": ");
	
	if (stat(path, &info) == 0)
	{
		char	rep;
		
		if (!quiet)
		{
			send(to_parent, "Delete?\n");
			fflush(to_parent);
			rep = reply(from_parent);
			if (rep == 'Q')
				quiet = TRUE;
			else if (rep != 'Y')
				return;
		}
		if (S_ISDIR(info.st_mode))
		{
			char *safe_path;
			safe_path = g_strdup(path);
			send(to_parent, "Scanning...\n");
			fflush(to_parent);
			for_dir_contents(safe_path, do_delete);
			if (rmdir(safe_path))
				error = g_strerror(errno);
			else
				error = "Directory deleted\n";
			g_free(safe_path);
		}
		else if (unlink(path) == 0)
			error = "OK";
		else
			error = g_strerror(errno);
	}
	else
		error = g_strerror(errno);

	send(to_parent, error);
	send(to_parent, "\n");
	fflush(to_parent);
}

/* The child executes this... */
static void delete_cb(FilerWindow *filer_window)
{
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
		left--;
	}
	
	send(to_parent, "\nDone\n");
	fflush(to_parent);
	sleep(5);
}

/* Called when the child sends us a message */
static void got_delete_data(gpointer 		data,
			    gint     		source, 
			    GdkInputCondition 	condition)
{
	char buf[BUFLEN];
	ssize_t len;
	GUIside	*gui_side = (GUIside *) data;
	GtkWidget *log = gui_side->log;

	len = read(source, buf, BUFLEN);
	if (len > 0)
		gtk_text_insert(GTK_TEXT(log), NULL, NULL, NULL, buf, len);
	else
	{
		/* The child is dead */
		gui_side->child = 0;
		gtk_widget_destroy(gui_side->window);
	}
}

/* Called from outside - deletes all selected items */
void action_delete(FilerWindow *filer_window)
{
	GUIside		*gui_side;
	Collection 	*collection;
	GtkWidget	*button;

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

	gui_side->input_tag = gdk_input_add(gui_side->from_child,
						GDK_INPUT_READ,
						got_delete_data, gui_side);

	number_of_windows++;

	button = gtk_button_new_with_label("Quiet");
	gtk_object_set_data(GTK_OBJECT(button), "send-code", "Q");
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

	gtk_widget_show_all(gui_side->window);
}

