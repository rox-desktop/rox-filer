/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#include <gtk/gtk.h>
#include <collection.h>
#include "directory.h"

typedef struct _FilerWindow FilerWindow;
typedef struct _FileItem FileItem;

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
};

void filer_opendir(char *path);
