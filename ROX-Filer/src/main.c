/* vi: set cindent:
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

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>

#include <gtk/gtk.h>
#include "collection.h"

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
int to_error_log = -1;		/* Write here to log errors */

#define USAGE   "Try `ROX-Filer/AppRun --help' for more information.\n"

#define VERSION "ROX-Filer 0.1.4\n"					\
		"Copyright (C) 1999 Thomas Leonard.\n"			\
		"ROX-Filer comes with ABSOLUTELY NO WARRANTY,\n"	\
		"to the extent permitted by law.\n"			\
		"You may redistribute copies of ROX-Filer\n"		\
		"under the terms of the GNU General Public License.\n"	\
		"For more information about these matters, "		\
		"see the file named COPYING.\n"

#define HELP "Usage: ROX-Filer/AppRun [OPTION]... [DIR]...\n"		\
       "Open filer windows showing each directory listed, or $HOME \n"	\
       "if no directories are given.\n\n"				\
       "  -h, --help		display this help and exit\n"		      \
       "  -v, --version		display the version information and exit\n"   \
       "  -t, --top [DIR]	open DIR as a top-edge panel\n"		\
       "  -b, --bottom [DIR]	open DIR as a bottom-edge panel\n"	\
       "  -l, --left [DIR]	open DIR as a left-edge panel\n"	\
       "  -r, --right [DIR]	open DIR as a right-edge panel\n"	\
       "  -o, --override	override window manager control of panels\n" \
       "\n"								\
       "Report bugs to <tal197@ecs.soton.ac.uk>.\n"

static struct option long_opts[] =
{
	{"top", 1, NULL, 't'},
	{"bottom", 1, NULL, 'b'},
	{"left", 1, NULL, 'l'},
	{"right", 1, NULL, 'r'},
	{"override", 0, NULL, 'o'},
	{"help", 0, NULL, 'h'},
	{"version", 0, NULL, 'v'},
	{NULL, 0, NULL, 0},
};

/* Take control of panels away from WM? */
gboolean override_redirect = FALSE;

static void child_died(int signum)
{
	int	    	status;
	int	    	child;

	/* Find out which children exited and allow them to die */
	do
	{
		child = waitpid(-1, &status, WNOHANG);

		if (child == 0 || child == -1)
			return;

		/* fprintf(stderr, "Child %d exited\n", child); */

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
	struct sigaction act;
	GList		*panel_dirs = NULL;
	GList		*panel_sides = NULL;

	gtk_init(&argc, &argv);

	while (1)
	{
		int	long_index;
		int	c;
		
		c = getopt_long(argc, argv, "t:b:l:r:ohv",
				long_opts, &long_index);

		if (c == EOF)
			break;		/* No more options */
		
		switch (c)
		{
			case 'o':
				override_redirect = TRUE;
				break;
			case 'v':
				printf(VERSION);
				return EXIT_SUCCESS;
			case 'h':
				printf(HELP);
				return EXIT_SUCCESS;
			case 't':
			case 'b':
			case 'l':
			case 'r':
				panel_sides = g_list_prepend(panel_sides,
							(gpointer) c);
				panel_dirs = g_list_prepend(panel_dirs,
						*optarg == '=' ? optarg + 1
							: optarg);
				break;
			default:
				printf(USAGE);
				return EXIT_FAILURE;
		}
	}

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

	/* Let child processes die */
	act.sa_handler = child_died;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &act, NULL);

	if (geteuid() == 0)
	{
		if (get_choice("!!!DANGER!!!",
			"Running ROX-Filer as root is VERY dangerous. If I "
			"had a warranty (I don't) then doing this would "
			"void it", 2,
			"Don't click here", "Quit") != 0)
			exit(EXIT_SUCCESS);
	}

	if (optind == argc && !panel_dirs)
		filer_opendir(getenv("HOME"), FALSE, BOTTOM);
	else
	{
		int	 i = optind;
		GList	 *dir = panel_dirs;
		GList	 *side = panel_sides;

		while (dir)
		{
			int	c = (int) side->data;
			
			filer_opendir((char *) dir->data, TRUE,
					c == 't' ? TOP :
					c == 'b' ? BOTTOM :
					c == 'l' ? LEFT :
					RIGHT);
			dir = dir->next;
			side = side->next;
		}

		g_list_free(dir);
		g_list_free(side);
		
		while (i < argc)
			filer_opendir(argv[i++], FALSE, BOTTOM);
	}

	pipe(stderr_pipe);
	gdk_input_add(stderr_pipe[0], GDK_INPUT_READ, stderr_cb, NULL);
	to_error_log = stderr_pipe[1];

	gtk_main();

	return EXIT_SUCCESS;
}
