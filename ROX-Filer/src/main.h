/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _MAIN_H
#define _MAIN_H

#include <sys/types.h>

extern int number_of_windows;
extern int to_error_log;	/* Send messages here to log them */
extern gboolean override_redirect;

extern uid_t euid;
extern gid_t egid;
extern int ngroups;			/* Number of supplemental groups */
extern gid_t *supplemental_groups;
extern char *home_dir;
extern uid_t noaccess_uid;	/* Used when stat() fails */
extern gid_t noaccess_gid;	/* Used when stat() fails */

/* Prototypes */
int main(int argc, char **argv);

#endif /* _MAIN_H */
