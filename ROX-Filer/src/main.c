/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>

#include <gtk/gtk.h>
#include <collection.h>

#include "filer.h"
#include "mount.h"
#include "menu.h"
#include "dnd.h"

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

	menu_init();
	dnd_init();
	filer_init();
	mount_init();

	signal(SIGCHLD, child_died);

	if (argc < 2)
		filer_opendir(getenv("HOME"));
	else
	{
		int	i;
		for (i = 1; i < argc; i++)
			filer_opendir(argv[i]);
	}

	gtk_main();

	return EXIT_SUCCESS;
}
