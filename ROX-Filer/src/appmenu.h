/*
 * $Id$
 *
 * Diego Zamboni <zamboni@cerias.purdue.edu>
 */

#ifndef _APPMENU_H
#define _APPMENU_H

#include <gtk/gtk.h>
#include "fscache.h"

/* Name of the file where the menu is stored */
#define APPMENU_FILENAME		"AppMenu"

/* Store appmenus */
typedef struct _AppMenus {
	GList	*items;   	/* Raw menu items for the application menu */
	int	ref;
	GtkWidget *last_menu;  	/* Last menu on which this AppMenu was posted */
	guchar	*app_dir;	/* Path of application (when menu appeared) */
} AppMenus;

extern GFSCache *appmenu_cache;

/* External interface */
void appmenu_init(void);
void appmenu_add(AppMenus *dir, GtkWidget *menu);
void appmenu_remove(GtkWidget *menu);
AppMenus *appmenu_query(guchar *dir, DirItem *app);

#endif   /* _APPMENU_H */
