/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _MENU_H
#define _MENU_H

#define MENU_MARGIN 32

extern GtkAccelGroup	*filer_keys;
extern GtkWidget 	*popup_menu;
extern char		*xterm_here_value;

void menu_init(void);

GtkWidget *create_menu_options(void);
void menu_update_options(void);
void menu_set_options(void);
void menu_save_options(void);

void show_pinboard_menu(GdkEventButton *event, PinIcon *icon);
void show_filer_menu(FilerWindow *filer_window, GdkEventButton *event,
		     int item);
GtkWidget *menu_create(GtkItemFactoryEntry *def, int n_entries, guchar *name);
void menu_set_items_shaded(GtkWidget *menu, gboolean shaded, int from, int n);

/* Public menu handlers */
void menu_rox_help(gpointer data, guint action, GtkWidget *widget);
void menu_show_options(gpointer data, guint action, GtkWidget *widget);

#endif /* _MENU_H */
