/* vi: set cindent:
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

typedef struct _FilerWindow FilerWindow;
typedef struct _FileItem FileItem;
typedef enum {LEFT, RIGHT, TOP, BOTTOM} Side;

typedef enum
{
	ITEM_FLAG_SYMLINK 	= 0x01,	/* Is a symlink */
	ITEM_FLAG_APPDIR  	= 0x02,	/* Contains /AppInfo */
	ITEM_FLAG_MOUNT_POINT  	= 0x04,	/* Is in mtab or fstab */
	ITEM_FLAG_MOUNTED  	= 0x08,	/* Is in /etc/mtab */
	ITEM_FLAG_TEMP_ICON  	= 0x10,	/* Free icon after use */
	ITEM_FLAG_EXEC_FILE  	= 0x20,	/* File, and has an X bit set */
	ITEM_FLAG_MAY_DELETE	= 0x40, /* Delete on finishing scan */
} ItemFlags;

typedef enum
{
	FILER_NEEDS_RESCAN	= 0x01, /* Call may_rescan after scanning */
	FILER_UPDATING		= 0x02, /* (scanning) items may already exist */
} FilerFlags;

#include "type.h"

struct _FilerWindow
{
	GtkWidget	*window;
	char		*path;		/* pathname */
	Collection	*collection;
	gboolean	panel;
	gboolean	temp_item_selected;
	gboolean	show_hidden;
	FilerFlags	flags;
	Side		panel_side;
	time_t		m_time;		/* m-time at last scan */
	int 		(*sort_fn)(const void *a, const void *b);

	/* Scanning */
	DIR		*dir;
	gint		idle_scan_id;	/* (only if dir != NULL) */
	guint		scan_min_width;	/* Min width possible so far */
};

struct _FileItem
{
	char		*leafname;
	
	int		base_type;	/* (regular file, dir, pipe, etc) */
	MIME_type	*mime_type;	/* NULL, except for non-exec files */
	ItemFlags	flags;
	
	int		text_width;
	int		pix_width;
	int		pix_height;
	MaskedPixmap	*image;
};

extern FilerWindow 	*window_with_focus;
extern GHashTable	*child_to_filer;

/* Prototypes */
void filer_init();
void filer_opendir(char *path, gboolean panel, Side panel_side);
void update_dir(FilerWindow *filer_window);
void scan_dir(FilerWindow *filer_window);
FileItem *selected_item(Collection *collection);
void refresh_dirs(char *path);
void change_to_parent(FilerWindow *filer_window);

#endif /* _FILER_H */
