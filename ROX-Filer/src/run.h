/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _RUN_H
#define _RUN_H

#include <gtk/gtk.h>

void run_app(char *path);
void run_with_files(char *path, GSList *uri_list);
void run_with_data(char *path, gpointer data, gulong length);
gboolean run_by_path(guchar *full_path);
gboolean run_diritem(guchar *full_path,
		     DirItem *item,
		     FilerWindow *filer_window,
		     gboolean edit);
void show_item_help(guchar *path, DirItem *item);
void run_list(guchar *to_open);

#endif /* _RUN_H */
