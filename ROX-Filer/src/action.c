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

#include "gui_support.h"
#include "filer.h"

#define BUFLEN 40

typedef struct _GUIside GUIside;
typedef void ActionChild(FilerWindow *filer_window,
			 int write_to_parent, int read_from_parent);

struct _GUIside
{
	int 		from_child;
	int 		to_child;
	int 		input_tag;
	GtkWidget 	*log;
};

/* Create two pipes, fork() a child and return a pointer to a GUIside struct
 * (NULL on failure). The child calls func().
 */
static GUIside *start_action(FilerWindow *filer_window, ActionChild *func)
{
	int		filedes[4];	/* 0 and 2 are for reading */
	GUIside		*retval;

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

	switch (fork())
	{
		case -1:
			report_error("ROX-Filer", g_strerror(errno));
			return NULL;
		case 0:
			/* We are the child */
			close(filedes[0]);
			close(filedes[3]);
			func(filer_window, filedes[1], filedes[2]);
			_exit(0);
	}

	/* We are the parent */
	close(filedes[1]);
	close(filedes[2]);
	retval = g_malloc(sizeof(GUIside));
	retval->from_child = filedes[0];
	retval->to_child = filedes[3];
	retval->log = NULL;

	return retval;
}

static void delete_cb(FilerWindow *filer_window, int to_parent, int from_parent)
{
	write(to_parent, "I am the child!!\n", 17);
	sleep(2);
}

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
		gtk_text_insert(GTK_TEXT(log), NULL, NULL, NULL,
				"Pipe closed by child.\n", -1);
		close(gui_side->to_child);
		close(gui_side->from_child);
		gdk_input_remove(gui_side->input_tag);
	}
}

void action_delete(FilerWindow *filer_window)
{
	GtkWidget	*window, *log;
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

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	
	log = gtk_text_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(window), log);
	gui_side->log = log;
	
	gui_side->input_tag = gdk_input_add(gui_side->from_child,
						GDK_INPUT_READ,
						got_delete_data, gui_side);

	gtk_widget_show_all(window);
}

