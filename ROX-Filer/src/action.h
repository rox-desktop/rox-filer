/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _ACTION_H
#define _ACTION_H

#include <gtk/gtk.h>

void action_init(void);

void action_usage(FilerWindow *filer_window);
void action_mount(GList	*paths, gboolean open_dir, int quiet);
void action_delete(FilerWindow *filer_window);
void action_chmod(FilerWindow *filer_window);
void action_find(FilerWindow *filer_window);
void action_move(GList *paths, char *dest, char *leaf, int quiet);
void action_copy(GList *paths, char *dest, char *leaf, int quiet);
void action_link(GList *paths, char *dest, char *leaf);
void show_condition_help(gpointer data);
void set_find_string_colour(GtkWidget *widget, guchar *string);

#endif /* _ACTION_H */
