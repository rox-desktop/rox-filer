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

/* External interface */
void appmenu_init(void);
void appmenu_add(guchar *app_dir, DirItem *item, GtkWidget *menu);
void appmenu_remove(void);

#endif   /* _APPMENU_H */
