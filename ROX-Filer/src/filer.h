/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _FILER_H
#define _FILER_H

#include <gtk/gtk.h>
#include "collection.h"
#include "pixmaps.h"
#include <sys/types.h>
#include <dirent.h>
#include "mount.h"
#include "dir.h"

typedef struct _FilerWindow FilerWindow;
typedef enum {PANEL_NO, PANEL_TOP, PANEL_BOTTOM} PanelType;
typedef enum {UNKNOWN_STYLE, LARGE_ICONS, SMALL_ICONS, FULL_INFO} DisplayStyle;

typedef enum
{
	OPEN_SHIFT		= 0x01,	/* Do ShiftOpen */
	OPEN_SAME_WINDOW	= 0x02, /* Directories open in same window */
	OPEN_CLOSE_WINDOW	= 0x04, /* Opening files closes the window */
	OPEN_FROM_MINI		= 0x08,	/* Non-dir => close minibuffer */
} OpenFlags;

typedef enum
{
	FILER_NEEDS_RESCAN	= 0x01, /* Call may_rescan after scanning */
	FILER_UPDATING		= 0x02, /* (scanning) items may already exist */
} FilerFlags;

#include "type.h"

struct _FilerWindow
{
	GtkWidget	*window;
	gboolean	scanning;	/* State of the 'scanning' indicator */
	char		*path;		/* pathname */
	Collection	*collection;
	gboolean	temp_item_selected;
	gboolean	show_hidden;
	FilerFlags	flags;
	PanelType	panel_type;
	int 		(*sort_fn)(const void *a, const void *b);
	DisplayStyle	display_style;

	Directory	*directory;

	gboolean	had_cursor;	/* (before changing directory) */
	char		*auto_select;	/* If it we find while scanning */

	GtkWidget	*minibuffer;
	int		mini_cursor_base;
};

extern FilerWindow 	*window_with_focus;
extern GHashTable	*child_to_filer;
extern gboolean 	o_unique_filer_windows;

/* Prototypes */
void filer_init();
FilerWindow *filer_opendir(char *path, PanelType panel_type);
void update_dir(FilerWindow *filer_window, gboolean warning);
void scan_dir(FilerWindow *filer_window);
int selected_item_number(Collection *collection);
DirItem *selected_item(Collection *collection);
void change_to_parent(FilerWindow *filer_window);
void filer_style_set(FilerWindow *filer_window, DisplayStyle style);
char *details(DirItem *item);
void filer_set_hidden(FilerWindow *filer_window, gboolean hidden);
int sort_by_name(const void *item1, const void *item2);
int sort_by_type(const void *item1, const void *item2);
int sort_by_date(const void *item1, const void *item2);
int sort_by_size(const void *item1, const void *item2);
void filer_set_sort_fn(FilerWindow *filer_window,
			int (*fn)(const void *a, const void *b));
void full_refresh(void);
void filer_openitem(FilerWindow *filer_window, int item_number,
		OpenFlags flags);
void filer_check_mounted(char *path);
void filer_change_to(FilerWindow *filer_window, char *path, char *from);
void filer_set_autoselect(FilerWindow *filer_window, guchar *leaf);

#endif /* _FILER_H */
