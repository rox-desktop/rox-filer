/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _MAIN_H
#define _MAIN_H

#include <glib.h>
#include <sys/types.h>

typedef struct _Callback Callback;
typedef void (*CallbackFn)(gpointer data);

struct _Callback
{
	CallbackFn	callback;
	gpointer	data;
};

extern int number_of_windows;
extern int to_error_log;	/* Send messages here to log them */
extern gboolean override_redirect;

extern uid_t euid;
extern gid_t egid;
extern int ngroups;			/* Number of supplemental groups */
extern gid_t *supplemental_groups;
extern char *home_dir, *app_dir;

/* Prototypes */
int main(int argc, char **argv);
void on_child_death(int child, CallbackFn callback, gpointer data);

#endif /* _MAIN_H */
