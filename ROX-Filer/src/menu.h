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

void show_filer_menu(FilerWindow *filer_window, GdkEvent *event, int item);
GtkItemFactory *menu_create(GtkItemFactoryEntry *def,
			int n_entries, guchar *name);
void menu_set_items_shaded(GtkWidget *menu, gboolean shaded, int from, int n);
void position_menu(GtkMenu *menu, gint *x, gint *y,
#ifdef GTK2
		gboolean  *push_in,
#endif
		gpointer data);

/* Public menu handlers */
void menu_rox_help(gpointer data, guint action, GtkWidget *widget);
void menu_show_options(gpointer data, guint action, GtkWidget *widget);
void open_home(gpointer data, guint action, GtkWidget *widget);
void menu_show_shift_action(GtkWidget *menu_item, DirItem *item, gboolean next);

#endif /* _MENU_H */
