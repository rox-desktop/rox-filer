/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _DISPLAY_H
#define _DISPLAY_H

#define ROW_HEIGHT_LARGE 64

#include <gtk/gtk.h>
#include "collection.h"
#include "pixmaps.h"
#include <sys/types.h>
#include <dirent.h>

typedef enum {
	UNKNOWN_STYLE,
	LARGE_ICONS,
	SMALL_ICONS,
	LARGE_FULL_INFO,
	SMALL_FULL_INFO,
} DisplayStyle;

typedef enum {
	DETAILS_SUMMARY,
	DETAILS_SIZE,
} DetailsType;

extern DetailsType last_details_type;
extern DisplayStyle last_display_style;
extern gboolean last_show_hidden;
extern int (*last_sort_fn)(const void *a, const void *b);

#include "filer.h"

/* Prototypes */
void display_init();
void filer_details_set(FilerWindow *filer_window, DetailsType details);
void filer_style_set(FilerWindow *filer_window, DisplayStyle style);
char *details(FilerWindow *filer_window, DirItem *item);
void filer_set_hidden(FilerWindow *filer_window, gboolean hidden);
int sort_by_name(const void *item1, const void *item2);
int sort_by_type(const void *item1, const void *item2);
int sort_by_date(const void *item1, const void *item2);
int sort_by_size(const void *item1, const void *item2);
void filer_set_sort_fn(FilerWindow *filer_window,
			int (*fn)(const void *a, const void *b));
void filer_set_autoselect(FilerWindow *filer_window, guchar *leaf);
void shrink_width(FilerWindow *filer_window);
int calc_width(FilerWindow *filer_window, DirItem *item);

#endif /* _DISPLAY_H */
