/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#include <gtk/gtk.h>
#include <collection.h>
#include "directory.h"
#include "pixmaps.h"

typedef struct _FilerWindow FilerWindow;
typedef struct _FileItem FileItem;

enum
{
	ITEM_FLAG_SYMLINK = 0x1,
};

struct _FilerWindow
{
	GtkWidget	*window;
	Collection	*collection;
	Directory	*dir;
};

struct _FileItem
{
	char		*leafname;
	int		text_width;
	int		pix_width;
	MaskedPixmap	*image;
	int		flags;
};

void filer_opendir(char *path);
