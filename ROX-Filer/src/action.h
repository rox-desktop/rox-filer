/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _ACTION_H
#define _ACTION_H

#include "filer.h"

void action_init(void);

void action_usage(FilerWindow *filer_window);
void action_mount(GList	*paths);
void action_delete(FilerWindow *filer_window);
void action_chmod(FilerWindow *filer_window);
void action_find(FilerWindow *filer_window);
void action_move(GSList *paths, char *dest);
void action_copy(GSList *paths, char *dest, char *leaf);
void action_link(GSList *paths, char *dest);
void show_condition_help(gpointer data);

#endif /* _ACTION_H */
