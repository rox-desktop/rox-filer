/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _PIXMAP_H
#define _PIXMAP_H

#include <gtk/gtk.h>
#include "fscache.h"

extern GFSCache *pixmap_cache;

extern MaskedPixmap *im_error;
extern MaskedPixmap *im_unknown;
extern MaskedPixmap *im_symlink;

extern MaskedPixmap *im_unmounted;
extern MaskedPixmap *im_mounted;
extern MaskedPixmap *im_multiple;
extern MaskedPixmap *im_exec_file;
extern MaskedPixmap *im_appdir;

extern MaskedPixmap *im_up_icon;
extern MaskedPixmap *im_home_icon;
extern MaskedPixmap *im_refresh_icon;
extern MaskedPixmap *im_help;

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

void pixmaps_init(void);
void pixmap_ref(MaskedPixmap *mp);
void pixmap_unref(MaskedPixmap *mp);
void pixmap_make_small(MaskedPixmap *mp);

#endif /* _PIXMAP_H */
