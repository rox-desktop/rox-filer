/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _DISPLAY_H
#define _DISPLAY_H

#define ROW_HEIGHT_LARGE 64

#include <gtk/gtk.h>
#include <sys/types.h>
#include <dirent.h>

/* Prototypes */
void display_init();
void display_set_layout(FilerWindow  *filer_window,
			DisplayStyle style,
			DetailsType  details);
void display_set_hidden(FilerWindow *filer_window, gboolean hidden);
int sort_by_name(const void *item1, const void *item2);
int sort_by_type(const void *item1, const void *item2);
int sort_by_date(const void *item1, const void *item2);
int sort_by_size(const void *item1, const void *item2);
void display_set_sort_fn(FilerWindow *filer_window,
			int (*fn)(const void *a, const void *b));
void display_set_autoselect(FilerWindow *filer_window, guchar *leaf);
void shrink_grid(FilerWindow *filer_window);
void calc_size(FilerWindow *filer_window, DirItem *item,
		int *width, int *height);

void draw_large_icon(GtkWidget *widget,
		     GdkRectangle *area,
		     DirItem  *item,
		     gboolean selected);
void draw_string(GtkWidget *widget,
		GdkFont *font,
		char	*string,
		int	len,
		int 	x,
		int 	y,
		int 	width,
		int	area_width,
		GtkStateType selection_state,
		gboolean selected,
		gboolean box);
gboolean display_is_truncated(FilerWindow *filer_window, int i);
void display_change_size(FilerWindow *filer_window, gboolean bigger);

#endif /* _DISPLAY_H */
