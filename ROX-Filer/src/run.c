/*
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

/* run.c */

#include <sys/stat.h>

#include "stdlib.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "menu.h"

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

