/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _MENU_H
#define _MENU_H

#include "filer.h"

extern GtkAccelGroup	*filer_keys;

void menu_init(void);
void menu_save(void);
void show_filer_menu(FilerWindow *filer_window, GdkEventButton *event,
		     int item);
#endif /* _MENU_H */
