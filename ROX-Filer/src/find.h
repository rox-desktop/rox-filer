/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#include <glib.h>

typedef struct _FindCondition FindCondition;

struct _FindCondition
{
	guchar	*name;
};

FindCondition *find_compile(guchar *string);
gboolean find_test_condition(FindCondition *condition, guchar *path);
void find_condition_free(FindCondition *condition);
