/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _SUPPORT_H
#define _SUPPORT_H

#include <glib.h>
#include <sys/types.h>

typedef enum {NONE} SpawnFlags;

char *pathdup(char *path);
GString *make_path(char *dir, char *leaf);
char *our_host_name();
int spawn(char **argv);
int spawn_full(char **argv, char *dir, SpawnFlags flags);
void debug_free_string(void *data);
char *user_name(uid_t uid);
char *group_name(gid_t gid);

#endif /* _SUPPORT_H */
