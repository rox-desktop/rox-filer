/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _PIXMAP_H
#define _PIXMAP_H

#include <gtk/gtk.h>

enum
{
	/* Base types */
	TYPE_ERROR,
	TYPE_UNKNOWN,
	TYPE_SYMLINK,
	TYPE_FILE,
	TYPE_DIRECTORY,
	TYPE_CHAR_DEVICE,
	TYPE_BLOCK_DEVICE,
	TYPE_PIPE,
	TYPE_SOCKET,

	/* Extended types */
	TYPE_UNMOUNTED,
	TYPE_MOUNTED,
	TYPE_EXEC_FILE,
	TYPE_MULTIPLE,
	TYPE_APPDIR,
	LAST_DEFAULT_PIXMAP,
};


typedef struct _MaskedPixmap MaskedPixmap;

struct _MaskedPixmap
{
	GdkPixmap	*pixmap;
	GdkBitmap	*mask;
};


extern MaskedPixmap	default_pixmap[LAST_DEFAULT_PIXMAP];


void load_default_pixmaps(GdkWindow *window);
void load_pixmap(GdkWindow *window, char *name, MaskedPixmap *image);
MaskedPixmap *load_pixmap_from(GtkWidget *window, char *path);

#endif /* _PIXMAP_H */
