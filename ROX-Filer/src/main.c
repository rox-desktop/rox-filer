/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#include <stdlib.h>

#include <gtk/gtk.h>
#include <collection.h>

#include "filer.h"
#include "menu.h"
#include "dnd.h"

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);

	menu_init();
	dnd_init();

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
