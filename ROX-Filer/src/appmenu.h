/*
 * $Id$
 *
 * Diego Zamboni <zamboni@cerias.purdue.edu>
 */

#ifndef _APPMENU_H
#define _APPMENU_H

#include <gtk/gtk.h>

/* External interface */
void appmenu_add(const gchar *app_dir, DirItem *item, GtkWidget *menu);
void appmenu_remove(void);

#endif   /* _APPMENU_H */
