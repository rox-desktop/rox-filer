/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _ACTION_H
#define _ACTION_H

#include "filer.h"

void action_usage(FilerWindow *filer_window);
void action_mount(FilerWindow *filer_window);
void action_delete(FilerWindow *filer_window);
void action_move(GSList *paths, char *dest);
void action_copy(GSList *paths, char *dest);
void action_link(GSList *paths, char *dest);

#endif /* _ACTION_H */
