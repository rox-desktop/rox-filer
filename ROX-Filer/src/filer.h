/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _FILER_H
#define _FILER_H

#include <gtk/gtk.h>
#include <collection.h>
#include "pixmaps.h"
#include <sys/types.h>
#include <dirent.h>

typedef struct _FilerWindow FilerWindow;
typedef struct _FileItem FileItem;
typedef enum {LEFT, RIGHT, TOP, BOTTOM} Side;

enum
{
	ITEM_FLAG_SYMLINK 	= 0x01,	/* Is a symlink */
	ITEM_FLAG_APPDIR  	= 0x02,	/* Contains /AppInfo */
	ITEM_FLAG_MOUNT_POINT  	= 0x04,	/* Is in mtab or fstab */
	ITEM_FLAG_MOUNTED  	= 0x08,	/* Is in /etc/mtab */
	ITEM_FLAG_TEMP_ICON  	= 0x10,	/* Free icon after use */
};

struct _FilerWindow
{
	GtkWidget	*window;
	char		*path;		/* pathname */
	Collection	*collection;
	gboolean	panel;
	gboolean	temp_item_selected;
	Side		panel_side;

	/* Scanning */
	DIR		*dir;
	gint		idle_scan_id;	/* (only if dir != NULL) */
};

struct _FileItem
{
	char		*leafname;
	
	int		base_type;	/* (regular file, dir, pipe, etc) */
	int		flags;
	
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
void scan_dir(FilerWindow *filer_window);

#endif /* _FILER_H */
