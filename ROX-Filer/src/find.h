/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#include <sys/stat.h>
#include <time.h>

typedef struct _FindCondition FindCondition;
typedef struct _FindInfo FindInfo;
typedef gboolean (*FindTest)(FindCondition *condition, FindInfo *info);
typedef void (*FindFree)(FindCondition *condition);

struct _FindInfo
{
	guchar		*fullpath;
	guchar		*leaf;
	struct stat	stats;
	time_t		now;
	gboolean	prune;
};

FindCondition *find_compile(guchar *string);
gboolean find_test_condition(FindCondition *condition, FindInfo *info);
void find_condition_free(FindCondition *condition);
