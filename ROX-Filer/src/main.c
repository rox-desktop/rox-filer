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

int main(int argc, char **argv)
{
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

	signal(SIGCHLD, child_died);

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

	gtk_main();

	return EXIT_SUCCESS;
}
