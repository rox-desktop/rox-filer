/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@users.sourceforge.net>.
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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#endif

#include <gtk/gtk.h>
#include "collection.h"

#include "global.h"

#include "main.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "display.h"
#include "mount.h"
#include "menu.h"
#include "dnd.h"
#include "options.h"
#include "choices.h"
#include "type.h"
#include "pixmaps.h"
#include "dir.h"
#include "action.h"
#include "i18n.h"
#include "remote.h"
#include "pinboard.h"
#include "run.h"
#include "toolbar.h"

int number_of_windows = 0;	/* Quit when this reaches 0 again... */
int to_error_log = -1;		/* Write here to log errors */

uid_t euid;
gid_t egid;
int ngroups;			/* Number of supplemental groups */
gid_t *supplemental_groups = NULL;

int home_dir_len;
char *home_dir, *app_dir;

/* Static prototypes */
static void show_features(void);


#define COPYING								\
	     N_("Copyright (C) 2000 Thomas Leonard.\n"			\
		"ROX-Filer comes with ABSOLUTELY NO WARRANTY,\n"	\
		"to the extent permitted by law.\n"			\
		"You may redistribute copies of ROX-Filer\n"		\
		"under the terms of the GNU General Public License.\n"	\
		"For more information about these matters, "		\
		"see the file named COPYING.\n")

#ifdef HAVE_GETOPT_LONG
#  define USAGE   N_("Try `ROX-Filer/AppRun --help' for more information.\n")
#  define SHORT_ONLY_WARNING ""
#else
#  define USAGE   N_("Try `ROX-Filer/AppRun -h' for more information.\n")
#  define SHORT_ONLY_WARNING	\
		_("NOTE: Your system does not support long options - \n" \
		"you must use the short versions instead.\n\n")
#endif

#define HELP N_("Usage: ROX-Filer/AppRun [OPTION]... [FILE]...\n"	\
       "Open each directory or file listed, or the current working\n"	\
       "directory if no arguments are given.\n\n"			\
       "  -b, --bottom=DIR	open DIR as a bottom-edge panel\n"	\
       "  -h, --help		display this help and exit\n"		      \
       "  -n, --new		start a new filer, even if already running\n"  \
       "  -o, --override	override window manager control of panels\n" \
       "  -p, --pinboard=PIN	use pinboard PIN as the pinboard\n"	\
       "  -t, --top=DIR		open DIR as a top-edge panel\n"		\
       "  -v, --version		display the version information and exit\n"   \
       "\nThe latest version can be found at:\n"			\
       "\thttp://rox.sourceforge.net\n"					\
       "\nReport bugs to <tal197@users.sourceforge.net>.\n")

#define SHORT_OPS "t:b:op:hvn"

#ifdef HAVE_GETOPT_LONG
static struct option long_opts[] =
{
	{"top", 1, NULL, 't'},
	{"bottom", 1, NULL, 'b'},
	{"override", 0, NULL, 'o'},
	{"pinboard", 1, NULL, 'p'},
	{"help", 0, NULL, 'h'},
	{"version", 0, NULL, 'v'},
	{"new", 0, NULL, 'n'},
	{NULL, 0, NULL, 0},
};
#endif

/* Take control of panels away from WM? */
gboolean override_redirect = FALSE;

/* Always start a new filer, even if one seems to be already running */
gboolean new_copy = FALSE;

/* Maps child PIDs to Callback pointers */
static GHashTable *death_callbacks = NULL;
static gboolean child_died_flag = FALSE;

/* This is called as a signal handler; simply ensures that
 * child_died_callback() will get called later.
 */
static void child_died(int signum)
{
	child_died_flag = TRUE;
	write(to_error_log, '\0', 1);	/* Wake up! */
}

static void child_died_callback(void)
{
	int	    	status;
	int	    	child;

	child_died_flag = FALSE;

	/* Find out which children exited and allow them to die */
	do
	{
		Callback	*cb;

		child = waitpid(-1, &status, WNOHANG);

		if (child == 0 || child == -1)
			return;

		cb = g_hash_table_lookup(death_callbacks, (gpointer) child);
		if (cb)
		{
			cb->callback(cb->data);
			g_hash_table_remove(death_callbacks, (gpointer) child);
		}

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
		GtkWidget	*hbox, *scrollbar;

		window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_title(GTK_WINDOW(window),
						_("ROX-Filer message log"));
		gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
		gtk_window_set_default_size(GTK_WINDOW(window), 600, 300);
		gtk_signal_connect_object(GTK_OBJECT(window), "delete_event",
				gtk_widget_hide, GTK_OBJECT(window));


		hbox = gtk_hbox_new(FALSE, 0);
		gtk_container_add(GTK_CONTAINER(window), hbox);
		scrollbar = gtk_vscrollbar_new(NULL);
		
		log = gtk_text_new(NULL,
				gtk_range_get_adjustment(GTK_RANGE(scrollbar)));
		gtk_box_pack_start(GTK_BOX(hbox), log, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, TRUE, 0);
	}

	len = read(source, buf, BUFLEN);
	
	if (child_died_flag)
	{
		child_died_callback();
		if (len == 1 && !*buf)
			return;
	}
	
	if (len > 0)
	{
		if (!GTK_WIDGET_MAPPED(window))
			gtk_widget_show_all(window);
		gtk_text_insert(GTK_TEXT(log), NULL, NULL, NULL, buf, len);
	}
}

/* The value that goes with an option */
#define VALUE (*optarg == '=' ? optarg + 1 : optarg)

int main(int argc, char **argv)
{
	int		 stderr_pipe[2];
	int		 i;
	struct sigaction act;
	guchar		*tmp;

	/* This is a list of \0 separated strings. Each string starts with a
	 * character indicating what kind of operation to perform:
	 *
	 * fFILE	open this file (or directory)
	 * dDIR		open this dir (even if it looks like an app)
	 * pPIN		display this pinboard
	 * tDIR		open DIR as a top-panel
	 * bDIR		open DIR as a bottom-panel
	 */
	GString		*to_open;

	to_open = g_string_new(NULL);

	home_dir = g_get_home_dir();
	home_dir_len = strlen(home_dir);
	app_dir = g_strdup(getenv("APP_DIR"));

	choices_init();
	i18n_init();

	if (app_dir)
		unsetenv("APP_DIR");
	else
	{
		g_warning("APP_DIR environment variable was unset!\n"
			"Use the AppRun script to invoke ROX-Filer...\n");
		app_dir = g_get_current_dir();
	}

	death_callbacks = g_hash_table_new(NULL, NULL);

#ifdef HAVE_LIBVFS
	mc_vfs_init();
#endif
	gtk_init(&argc, &argv);

	while (1)
	{
		int	c;
#ifdef HAVE_GETOPT_LONG
		int	long_index;
		c = getopt_long(argc, argv, SHORT_OPS,
				long_opts, &long_index);
#else
		c = getopt(argc, argv, SHORT_OPS);
#endif

		if (c == EOF)
			break;		/* No more options */
		
		switch (c)
		{
			case 'n':
				new_copy = TRUE;
				break;
			case 'o':
				override_redirect = TRUE;
				break;
			case 'v':
				fprintf(stderr, "ROX-Filer %s\n", VERSION);
				fprintf(stderr, _(COPYING));
				show_features();
				return EXIT_SUCCESS;
			case 'h':
				fprintf(stderr, _(HELP));
				fprintf(stderr, _(SHORT_ONLY_WARNING));
				return EXIT_SUCCESS;
			case 't':
			case 'b':
				g_string_append_c(to_open, '<');
				g_string_append_c(to_open, c);
				g_string_append_c(to_open, '>');
				tmp = pathdup(VALUE);
				g_string_append(to_open, tmp);
				g_free(tmp);
				break;
			case 'p':
				g_string_append(to_open, "<p>");
				g_string_append(to_open, VALUE);
				break;
			default:
				printf(_(USAGE));
				return EXIT_FAILURE;
		}
	}
	
	i = optind;
	while (i < argc)
	{
		tmp = pathdup(argv[i++]);

		g_string_append(to_open, "<f>");
		g_string_append(to_open, tmp);
	}

	if (to_open->len == 0)
	{
		guchar	*dir;

		dir = g_get_current_dir();
		g_string_sprintf(to_open, "<d>%s", dir);
		g_free(dir);
	}

	gui_support_init();
	if (remote_init(to_open, new_copy))
		return EXIT_SUCCESS;	/* Already running */
	pixmaps_init();

	dir_init();
	menu_init();
	dnd_init();
	filer_init();
	toolbar_init();
	display_init();
	mount_init();
	options_init();
	type_init();
	action_init();
	pinboard_init();

	options_load();

	/* Let child processes die */
	act.sa_handler = child_died;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &act, NULL);

	/* Ignore SIGPIPE - check for EPIPE errors instead */
	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGPIPE, &act, NULL);

	euid = geteuid();
	egid = getegid();
	ngroups = getgroups(0, NULL);
	if (ngroups < 0)
		ngroups = 0;
	else if (ngroups > 0)
	{
		supplemental_groups = g_malloc(sizeof(gid_t) * ngroups);
		getgroups(ngroups, supplemental_groups);
	}
	
	if (euid == 0)
	{
		if (get_choice(_("!!!DANGER!!!"),
			_("Running ROX-Filer as root is VERY dangerous. If I "
			"had a warranty (I don't) then doing this would "
			"void it"), 2,
			_("Don't click here"), _("Quit")) != 0)
			exit(EXIT_SUCCESS);
	}

	run_list(to_open->str);
	g_string_free(to_open, TRUE);
	
	pipe(stderr_pipe);
	close_on_exec(stderr_pipe[0], TRUE);
	close_on_exec(stderr_pipe[1], TRUE);
	gdk_input_add(stderr_pipe[0], GDK_INPUT_READ, stderr_cb, NULL);
	to_error_log = stderr_pipe[1];

	if (number_of_windows > 0)
		gtk_main();

	return EXIT_SUCCESS;
}

static void show_features(void)
{
	g_printerr("\n-- %s --\n\n", _("features set at compile time"));
	g_printerr("%s... %s\n", _("VFS support"),
#ifdef HAVE_LIBVFS
		_("Yes")
#else
		_("No (couldn't find a valid libvfs)")
#endif
		);
	g_printerr("%s... %s\n", _("ImLib support"),
#ifdef HAVE_IMLIB
		_("Yes")
#else
		_("No (the imlib-config command didn't work)")
#endif
		);
}

/* Register a function to be called when process number 'child' dies. */
void on_child_death(int child, CallbackFn callback, gpointer data)
{
	Callback	*cb;

	g_return_if_fail(callback != NULL);

	cb = g_new(Callback, 1);

	cb->callback = callback;
	cb->data = data;

	g_hash_table_insert(death_callbacks, (gpointer) child, cb);
}
