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

extern GtkWidget	*display_large_menu;	/* Display->Large With... */
extern GtkWidget	*display_small_menu;	/* Display->Small With... */

void menu_init(void);

GtkWidget *create_menu_options(void);
void menu_update_options(void);
void menu_set_options(void);
void menu_save_options(void);

void show_filer_menu(FilerWindow *filer_window, GdkEventButton *event,
		     int item);
GtkWidget *menu_create(GtkItemFactoryEntry *def, int n_entries, guchar *name);
void menu_set_items_shaded(GtkWidget *menu, gboolean shaded, int from, int n);
void show_style_menu(FilerWindow *filer_window,
			GdkEventButton *event,
			GtkWidget *menu);
void position_menu(GtkMenu *menu, gint *x, gint *y, gpointer data);

/* Public menu handlers */
void menu_rox_help(gpointer data, guint action, GtkWidget *widget);
void menu_show_options(gpointer data, guint action, GtkWidget *widget);
void open_home(gpointer data, guint action, GtkWidget *widget);

#endif /* _MENU_H */
