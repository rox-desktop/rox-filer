/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* apps.c - code for handling application directories */

#include "support.h"
#include "gui_support.h"

/* An application has been double-clicked (or run in some other way) */
void run_app(char *path)
{
	GString	*apprun;
	char	*argv[] = {NULL, NULL};

	apprun = g_string_new(path);
	argv[0] = g_string_append(apprun, "/AppRun")->str;

	if (!spawn(argv))
		report_error("ROX-Filer", "Failed to fork() child process");
	
	g_string_free(apprun, TRUE);
}
