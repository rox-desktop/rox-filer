/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <gtk/gtk.h>
#include <collection.h>

#include "main.h"
#include "gui_support.h"
#include "filer.h"
#include "mount.h"
#include "menu.h"
#include "dnd.h"
#include "options.h"
#include "choices.h"
#include "savebox.h"
#include "type.h"

int number_of_windows = 0;	/* Quit when this reaches 0 again... */

/* XXX: Maybe we shouldn't do so much work in a signal handler? */
static void child_died(int signum)
{
	int	    	status;
	int	    	child;
	FilerWindow	*filer_window;

	/* Find out which children exited. This also has the effect of
	 * allowing the children to die.
	 */
	do
	{
		child = waitpid(-1, &status, WNOHANG | WUNTRACED);

		if (child == 0 || child == -1)
			return;

		filer_window = g_hash_table_lookup(child_to_filer,
					(gpointer) child);
		if (filer_window)
			scan_dir(filer_window);

		if (!WIFSTOPPED(status))
			g_hash_table_remove(child_to_filer,
					(gpointer) child);

	} while (1);
}

#define BUFLEN 40
void stderr_cb(gpointer data, gint source, GdkInputCondition condition)
{
	char buf[BUFLEN];
	static GtkWidget *log = NULL;
	static GtkWidget *window = NULL;
	ssize_t len;

	if (!window)
	{
		window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_title(GTK_WINDOW(window), "ROX-Filer error log");
		gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
		gtk_window_set_default_size(GTK_WINDOW(window), 600, 300);
		gtk_signal_connect_object(GTK_OBJECT(window), "delete_event",
				gtk_widget_hide, GTK_OBJECT(window));
		log = gtk_text_new(NULL, NULL);
		gtk_container_add(GTK_CONTAINER(window), log);
	}

	if (!GTK_WIDGET_MAPPED(window))
		gtk_widget_show_all(window);
	
	len = read(source, buf, BUFLEN);
	if (len > 0)
		gtk_text_insert(GTK_TEXT(log), NULL, NULL, NULL, buf, len);
}

int main(int argc, char **argv)
{
	int		 stderr_pipe[2];
	struct sigaction act = {};

	gtk_init(&argc, &argv);
	choices_init("ROX-Filer");

	gui_support_init();
	menu_init();
	dnd_init();
	filer_init();
	mount_init();
	options_init();
	savebox_init();
	type_init();

	options_load();

	act.sa_handler = child_died;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGCHLD, &act, NULL);

	if (argc < 2)
		filer_opendir(getenv("HOME"), FALSE, BOTTOM);
	else
	{
		int	 i = 1;
		gboolean panel = FALSE;
		Side	 side = BOTTOM;

		while (i < argc)
		{
			if (argv[i][0] == '-')
			{
				switch (argv[i][1] + (argv[i][2] << 8))
				{
					case 't': side = TOP; break;
					case 'b': side = BOTTOM; break;
					case 'l': side = LEFT; break;
					case 'r': side = RIGHT; break;
					default:
						fprintf(stderr,
							"Bad option.\n");
						return EXIT_FAILURE;
				}
				panel = TRUE;
			}
			else
			{
				filer_opendir(argv[i], panel, side);
				panel = FALSE;
				side = BOTTOM;
			}
			i++;
		}
	}

	pipe(stderr_pipe);
	dup2(stderr_pipe[1], STDERR_FILENO);
	gdk_input_add(stderr_pipe[0], GDK_INPUT_READ, stderr_cb, NULL);
	fcntl(STDERR_FILENO, F_SETFD, 0);

	gtk_main();

	return EXIT_SUCCESS;
}
