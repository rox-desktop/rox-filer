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

/* find.c - processes the find conditions
 *
 * A Condition is a tree structure. Each node has a test() fn which
 * can be used to see whether the current file matches, and a free() fn
 * which frees it. Both will recurse down the tree as needed.
 */

#include "config.h"

#include <string.h>
#include <fnmatch.h>

#include "find.h"

/* Static prototypes */
static FindCondition *parse_expression(guchar **expression);
static FindCondition *parse_case(guchar **expression);


#define EAT ((*expression)++)
#define NEXT (**expression)
#define SKIP while (NEXT == ' ' || NEXT == '\t') EAT


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Take a string and parse it, returning a condition object which
 * can be passed to find_test_condition() later. NULL if the string
 * is not a valid expression.
 */
FindCondition *find_compile(guchar *string)
{
	FindCondition 	*cond;
	guchar		**expression = &string;

	g_return_val_if_fail(string != NULL, NULL);

	cond = parse_expression(expression);
	if (!cond)
		return NULL;

	SKIP;
	if (NEXT != '\0')
	{
		cond->free(cond);
		cond = NULL;
	}

	return cond;
}

gboolean find_test_condition(FindCondition *condition, guchar *path)
{
	FindInfo	info;
	guchar		*slash;

	g_return_val_if_fail(condition != NULL, FALSE);
	g_return_val_if_fail(path != NULL, FALSE);

	info.fullpath = path;

	slash = strrchr(path, '/');
	info.leaf = slash ? slash + 1 : path;
	if (lstat(path, &info.stats))
		return FALSE;	/* XXX: Log error */

	return condition->test(condition, &info);
}

void find_condition_free(FindCondition *condition)
{
	g_return_if_fail(condition != NULL);

	condition->free(condition);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/*				TESTING CODE				*/

static gboolean test_leaf(FindCondition *condition, FindInfo *info)
{
	return fnmatch(condition->data1, info->leaf, 0) == 0;
}

static gboolean test_path(FindCondition *condition, FindInfo *info)
{
	return fnmatch(condition->data1, info->fullpath, FNM_PATHNAME) == 0;
}

static gboolean test_OR(FindCondition *condition, FindInfo *info)
{
	FindCondition	*first = (FindCondition *) condition->data1;
	FindCondition	*second = (FindCondition *) condition->data2;

	return first->test(first, info) || first->test(second, info);
}

/*				FREEING CODE				*/

/* Frees the structure and g_free()s both data items (NULL is OK) */
static void free_simple(FindCondition *condition)
{
	g_return_if_fail(condition != NULL);

	g_free(condition->data1);
	g_free(condition->data2);
	g_free(condition);
}

/* Treats data1 and data2 as conditions and frees recursively */
static void free_binary_op(FindCondition *condition)
{
	FindCondition	*first = (FindCondition *) condition->data1;
	FindCondition	*second = (FindCondition *) condition->data2;

	first->free(first);
	second->free(second);
	g_free(condition);
}

/* 				PARSING CODE				*/

/* These all work in the same way - you give them an expression and the
 * parse as much as they can, returning a condition for everything that
 * was parsed and updating the expression pointer to the first unknown
 * token. NULL indicates an error.
 */


/* An expression is a series of comma-separated cases, any of which
 * may match.
 */
static FindCondition *parse_expression(guchar **expression)
{
	FindCondition	*first, *second, *cond;

	first = parse_case(expression);
	if (!first)
		return NULL;

	SKIP;
	if (NEXT != ',')
		return first;
	EAT;

	second = parse_expression(expression);
	if (!second)
	{
		first->free(first);
		return NULL;
	}

	cond = g_new(FindCondition, 1);
	cond->test = &test_OR;
	cond->free = &free_binary_op;
	cond->data1 = first;
	cond->data2 = second;

	return cond;
}

static FindCondition *parse_case(guchar **expression)
{
	FindCondition	*cond = NULL;
	GString		*str;
	FindTest	test = &test_leaf;

	SKIP;
	if (NEXT != '\'')
		return NULL;
	EAT;

	str = g_string_new(NULL);

	while (NEXT != '\'')
	{
		guchar	c = NEXT;

		if (c == '\0')
			goto out;
		EAT;
		
		if (c == '\\' && NEXT == '\'')
		{
			c = NEXT;
			EAT;
		}

		if (c == '/')
			test = &test_path;

		g_string_append_c(str, c);
	}
	EAT;
	
	cond = g_new(FindCondition, 1);
	cond->test = test;
	cond->free = &free_simple;
	cond->data1 = str->str;
	cond->data2 = NULL;

out:
	g_string_free(str, cond ? FALSE : TRUE);

	return cond;
}
