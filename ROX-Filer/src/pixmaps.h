/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _PIXMAP_H
#define _PIXMAP_H

#include <gtk/gtk.h>
#include "fscache.h"

extern GFSCache *pixmap_cache;

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

	/* Toolbar */
	TOOLBAR_UP_ICON,
	TOOLBAR_HOME_ICON,
	TOOLBAR_REFRESH_ICON,
	
	/* End of list */
	LAST_DEFAULT_PIXMAP
};

typedef struct _MaskedPixmap MaskedPixmap;

#ifdef HAVE_IMLIB
#  ifdef PIXMAPS_C
#    define IMLIB_T	GdkImlibImage
#  else
#    define IMLIB_T	gpointer
#  endif
#endif

struct _MaskedPixmap
{
	int		ref;
#ifdef HAVE_IMLIB
	IMLIB_T		*image;
#endif
	GdkPixmap	*pixmap;	/* Full size image */
	GdkBitmap	*mask;
	int		width;
	int		height;

	/* If sm_pixmap is NULL then call pixmap_make_small() */
	GdkPixmap	*sm_pixmap;	/* Half-size (hopefully!) image */
	GdkBitmap	*sm_mask;
	int		sm_width;
	int		sm_height;
};

extern MaskedPixmap *default_pixmap[LAST_DEFAULT_PIXMAP];

void pixmaps_init(void);
void pixmap_ref(MaskedPixmap *mp);
void pixmap_unref(MaskedPixmap *mp);
void pixmap_make_small(MaskedPixmap *mp);

#endif /* _PIXMAP_H */
