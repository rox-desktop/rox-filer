/*
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
pid_t spawn(char **argv);
pid_t spawn_full(char **argv, char *dir);
void debug_free_string(void *data);
char *user_name(uid_t uid);
char *group_name(gid_t gid);
char *format_size(unsigned long size);
int fork_exec_wait(char **argv);

#endif /* _SUPPORT_H */
