/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2001, the ROX-Filer team.
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
#include <string.h>
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
#include "panel.h"
#include "run.h"
#include "toolbar.h"
#include "bind.h"
#include "icon.h"
#include "appinfo.h"

int number_of_windows = 0;	/* Quit when this reaches 0 again... */
static int to_wakeup_pipe = -1;	/* Write here to get noticed */

uid_t euid;
gid_t egid;
int ngroups;			/* Number of supplemental groups */
gid_t *supplemental_groups = NULL;

/* Message to display at the top of each filer window */
guchar *show_user_message = NULL;

int home_dir_len;
char *home_dir, *app_dir;

/* Static prototypes */
static void show_features(void);


#define COPYING								\
	     N_("Copyright (C) 2001 Thomas Leonard.\n"			\
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
       "  -b, --bottom=PANEL	open PAN as a bottom-edge panel\n"	\
       "  -d, --dir=DIR		open DIR as directory (not application)\n"  \
       "  -h, --help		display this help and exit\n"		\
       "  -l, --left=PANEL	open PAN as a left-edge panel\n"	\
       "  -m, --mime-type=FILE	print MIME type of FILE and exit\n" \
       "  -n, --new		start a new filer, even if already running\n"  \
       "  -o, --override	override window manager control of panels\n" \
       "  -p, --pinboard=PIN	use pinboard PIN as the pinboard\n"	\
       "  -r, --right=PANEL	open PAN as a right-edge panel\n"	\
       "  -s, --show=FILE	open a directory showing FILE\n"	\
       "  -t, --top=PANEL	open PANEL as a top-edge panel\n"	\
       "  -u, --user		show user name in each window \n"	\
       "  -v, --version		display the version information and exit\n"   \
       "  -x, --examine=FILE	FILE has changed - re-examine it\n"	\
       "\nThe latest version can be found at:\n"			\
       "\thttp://rox.sourceforge.net\n"					\
       "\nReport bugs to <tal197@users.sourceforge.net>.\n")

#define SHORT_OPS "d:t:b:l:r:op:s:hvnux:m:"

#ifdef HAVE_GETOPT_LONG
static struct option long_opts[] =
{
	{"dir", 1, NULL, 'd'},
	{"top", 1, NULL, 't'},
	{"bottom", 1, NULL, 'b'},
	{"left", 1, NULL, 'l'},
	{"override", 0, NULL, 'o'},
	{"pinboard", 1, NULL, 'p'},
	{"right", 1, NULL, 'r'},
	{"help", 0, NULL, 'h'},
	{"version", 0, NULL, 'v'},
	{"user", 0, NULL, 'u'},
	{"new", 0, NULL, 'n'},
	{"show", 1, NULL, 's'},
	{"examine", 1, NULL, 'x'},
	{"mime-type", 1, NULL, 'm'},
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
	write(to_wakeup_pipe, "\0", 1);	/* Wake up! */
}

static void child_died_callback(void)
{
	int	    	status;
	gint	    	child;

	child_died_flag = FALSE;

	/* Find out which children exited and allow them to die */
	do
	{
		Callback	*cb;

		child = waitpid(-1, &status, WNOHANG);

		if (child == 0 || child == -1)
			return;

		cb = g_hash_table_lookup(death_callbacks,
				GINT_TO_POINTER(child));
		if (cb)
		{
			cb->callback(cb->data);
			g_hash_table_remove(death_callbacks,
					GINT_TO_POINTER(child));
		}

	} while (1);
}

#define BUFLEN 40
/* When data is written to_wakeup_pipe, this gets called from the event
 * loop some time later. Useful for getting out of signal handlers, etc.
 */
void wake_up_cb(gpointer data, gint source, GdkInputCondition condition)
{
	char buf[BUFLEN];

	read(source, buf, BUFLEN);
	
	if (child_died_flag)
		child_died_callback();
}

/* The value that goes with an option */
#define VALUE (*optarg == '=' ? optarg + 1 : optarg)

int main(int argc, char **argv)
{
	int		 wakeup_pipe[2];
	int		 i;
	struct sigaction act;
	guchar		*tmp, *dir, *slash;
	gboolean	show_user = FALSE;

	/* This is a list of \0 separated strings. Each string starts with a
	 * character indicating what kind of operation to perform:
	 *
	 * fFILE	open this file (or directory)
	 * dDIR		open DIR as a directory (not as an application)
	 * eDIR		shift-open this file (edit)
	 * pPIN		display this pinboard
	 * [lrtb]PANEL	open PANEL as a {left, right, top, bottom} panel
	 * sFILE	open a directory to show FILE
	 * xFILE        eXamine FILE
	 */
	GString		*to_open;

	to_open = g_string_new(NULL);

	home_dir = g_get_home_dir();
	home_dir_len = strlen(home_dir);
	app_dir = g_strdup(getenv("APP_DIR"));

	choices_init();
	i18n_init();

	if (!app_dir)
	{
		g_warning("APP_DIR environment variable was unset!\n"
			"Use the AppRun script to invoke ROX-Filer...\n");
		app_dir = g_get_current_dir();
	}
#ifdef HAVE_UNSETENV 
	else
	{
		/* Don't pass it on to our child processes... */
		unsetenv("APP_DIR");
	}
#endif

	death_callbacks = g_hash_table_new(NULL, NULL);

#ifdef HAVE_LIBVFS
	mc_vfs_init();
#endif

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
			case 'd':
		        case 'x':
				/* Argument is a path */
				tmp = pathdup(VALUE);
				g_string_append_c(to_open, '<');
				g_string_append_c(to_open, c);
				g_string_append_c(to_open, '>');
				g_string_append(to_open, tmp);
				g_free(tmp);
				break;
			case 's':
				tmp = g_strdup(VALUE);
				slash = strrchr(tmp, '/');
				if (slash)
					*slash = '\0';

				dir = pathdup(tmp);
				g_string_append(to_open, "<s>");
				g_string_append(to_open, dir);
				g_free(dir);
				
				if (slash)
				{
					*slash = '/';
					g_string_append(to_open, slash);
				}

				g_free(tmp);
				break;
			case 'l':
			case 'r':
			case 't':
			case 'b':
			case 'p':
				/* Argument is a leaf (or starts with /) */
				g_string_append_c(to_open, '<');
				g_string_append_c(to_open, c);
				g_string_append_c(to_open, '>');
				g_string_append(to_open, VALUE);
				break;
			case 'u':
				show_user = TRUE;
				break;
		        case 'm':
				{
					MIME_type *type;
					type_init();
					type = type_get_type(VALUE);
					printf("%s/%s\n", type->media_type,
							  type->subtype);
				}
				return EXIT_SUCCESS;
			default:
				printf(_(USAGE));
				return EXIT_FAILURE;
		}
	}

	gtk_init(&argc, &argv);

	if (euid == 0 || show_user)
		show_user_message = g_strdup_printf( _("Running as user '%s'"), 
				user_name(euid));
	
	i = optind;
	while (i < argc)
	{
		tmp = pathdup(argv[i++]);

		g_string_append(to_open, "<f>");
		g_string_append(to_open, tmp);

		g_free(tmp);
	}

	if (to_open->len == 0)
	{
		guchar	*dir;

		dir = g_get_current_dir();
		g_string_sprintf(to_open, "<d>%s", dir);
		g_free(dir);
	}

	option_add_int("dnd_no_hostnames", 1, NULL);

	gui_support_init();
	if (remote_init(to_open, new_copy))
		return EXIT_SUCCESS;	/* Already running */

	/* Put ourselves into the background (so 'rox' always works the
	 * same, whether we're already running or not).
	 * Not for -n, though (helps when debugging).
	 */
	if (!new_copy)
	{
		pid_t child;

		child = fork();
		if (child > 0)
			_exit(0);	/* Parent exits */
		/* Otherwise we're the child (or an error occurred - ignore
		 * it!).
		 */
	}
	
	pixmaps_init();

	dnd_init();
	bind_init();
	dir_init();
	menu_init();
	minibuffer_init();
	filer_init();
	toolbar_init();
	display_init();
	mount_init();
	options_init();
	type_init();
	action_init();
	appinfo_init();

	icon_init();
	pinboard_init();
	panel_init();

	options_load();

	pipe(wakeup_pipe);
	close_on_exec(wakeup_pipe[0], TRUE);
	close_on_exec(wakeup_pipe[1], TRUE);
	gdk_input_add(wakeup_pipe[0], GDK_INPUT_READ, wake_up_cb, NULL);
	to_wakeup_pipe = wakeup_pipe[1];

	/* If the pipe is full then we're going to get woken up anyway... */
	set_blocking(to_wakeup_pipe, FALSE);

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

	run_list(to_open->str);
	g_string_free(to_open, TRUE);
	
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
}

/* Register a function to be called when process number 'child' dies. */
void on_child_death(gint child, CallbackFn callback, gpointer data)
{
	Callback	*cb;

	g_return_if_fail(callback != NULL);

	cb = g_new(Callback, 1);

	cb->callback = callback;
	cb->data = data;

	g_hash_table_insert(death_callbacks, GINT_TO_POINTER(child), cb);
}
