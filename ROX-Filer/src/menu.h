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

GtkWidget *create_menu_options(void);
void menu_update_options(void);
void menu_set_options(void);
void menu_save_options(void);

void show_filer_menu(FilerWindow *filer_window, GdkEventButton *event,
		     int item);
#endif /* _MENU_H */
