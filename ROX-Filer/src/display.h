/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _DISPLAY_H
#define _DISPLAY_H

#define MAX_ICON_HEIGHT 48
#define MAX_ICON_WIDTH 48
#define ROW_HEIGHT_LARGE 64

#include <gtk/gtk.h>
#include "collection.h"
#include <sys/types.h>
#include <dirent.h>

typedef enum {
	LARGE_ICONS = 0,
	SMALL_ICONS = 1,
	LARGE_FULL_INFO,
	SMALL_FULL_INFO,
	UNKNOWN_STYLE,
} DisplayStyle;

typedef enum {
	DETAILS_NONE,		/* Used in options */
	DETAILS_SUMMARY,
	DETAILS_SIZE,
	DETAILS_PERMISSIONS,
	DETAILS_TYPE,
	DETAILS_TIMES,
} DetailsType;

extern guchar *last_layout;
extern gboolean last_show_hidden;
extern int (*last_sort_fn)(const void *a, const void *b);

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
void shrink_width(FilerWindow *filer_window);
int calc_width(FilerWindow *filer_window, DirItem *item);

void draw_large_icon(GtkWidget *widget,
		     GdkRectangle *area,
		     DirItem  *item,
		     gboolean selected);
void draw_string(GtkWidget *widget,
		GdkFont *font,
		char	*string,
		int 	x,
		int 	y,
		int 	width,
		int	area_width,
		gboolean selected,
		gboolean box);
gboolean display_is_truncated(FilerWindow *filer_window, int i);

#endif /* _DISPLAY_H */
