/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* apps.c - code for handling application directories */

#include <sys/stat.h>

#include "stdlib.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"

/* An application has been double-clicked (or run in some other way) */
void run_app(char *path)
{
	GString	*apprun;
	char	*argv[] = {NULL, NULL};

	apprun = g_string_new(path);
	argv[0] = g_string_append(apprun, "/AppRun")->str;

	if (!spawn_full(argv, getenv("HOME"), 0))
		report_error("ROX-Filer", "Failed to fork() child process");
	
	g_string_free(apprun, TRUE);
}

void app_show_help(char *path)
{
	char		*help_dir;
	struct stat 	info;

	help_dir = g_strconcat(path, "/Help", NULL);
	
	if (stat(help_dir, &info))
		report_error("Application",
			"This is an application directory - you can "
			"run it as a program, or open it (hold down "
			"Shift while you open it). Most applications provide "
			"their own help here, but this one doesn't.");
	else
		filer_opendir(help_dir, FALSE, BOTTOM);
}
