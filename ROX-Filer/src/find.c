/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
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

/* find.c - processes the find conditions */

#include "config.h"

#include <string.h>

#include "find.h"

/* Static prototypes */


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Take a string and parse it, returning a condition object which
 * can be passed to find_test_condition() later.
 */
FindCondition *find_compile(guchar *string)
{
	FindCondition *retval;

	retval = g_new(FindCondition, 1);
	retval->name = g_strdup(string);

	return retval;
}

gboolean find_test_condition(FindCondition *condition, guchar *path)
{
	guchar	*slash, *leaf;

	slash = strrchr(path, '/');
	leaf = slash ? slash + 1 : path;
		
	return g_strcasecmp(condition->name, leaf) == 0;
}

void find_condition_free(FindCondition *condition)
{
	g_free(condition->name);
	g_free(condition);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

