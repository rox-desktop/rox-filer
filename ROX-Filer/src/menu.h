/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _MENU_H
#define _MENU_H

extern GtkAccelGroup	*filer_keys;
extern GtkWidget 	*popup_menu;

void menu_init(void);

GtkWidget *create_menu_options(void);
void menu_update_options(void);
void menu_set_options(void);
void menu_save_options(void);

void show_pinboard_menu(GdkEventButton *event, PinIcon *icon);
void show_filer_menu(FilerWindow *filer_window, GdkEventButton *event,
		     int item);
#endif /* _MENU_H */
