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

enum
{
	ITEM_FLAG_SYMLINK 	= 0x1,	/* Is a symlink */
	ITEM_FLAG_APPDIR  	= 0x2,	/* Contains /AppInfo */
	ITEM_FLAG_MOUNT_POINT  	= 0x4,	/* Is in mtab or fstab */
	ITEM_FLAG_MOUNTED  	= 0x8,	/* Is in /etc/mtab */
};

struct _FilerWindow
{
	GtkWidget	*window;
	char		*path;		/* pathname */
	Collection	*collection;

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
	MaskedPixmap	*image;
};

extern FilerWindow 	*window_with_focus;
extern GHashTable	*child_to_filer;

/* Prototypes */
void filer_init();
void filer_opendir(char *path);
void scan_dir(FilerWindow *filer_window);

#endif /* _FILER_H */
