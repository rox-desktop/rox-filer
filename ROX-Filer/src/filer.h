/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#include <gtk/gtk.h>
#include <collection.h>
#include "pixmaps.h"
#include <sys/types.h>
#include <dirent.h>

typedef struct _FilerWindow FilerWindow;
typedef struct _FileItem FileItem;

enum
{
	ITEM_FLAG_SYMLINK = 0x1,
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

void filer_opendir(char *path);
