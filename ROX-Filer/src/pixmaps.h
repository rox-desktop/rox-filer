/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _PIXMAP_H
#define _PIXMAP_H

#include <gtk/gtk.h>

#ifndef GTK2
# ifndef PIXMAPS_C
  typedef struct _GdkPixbuf GdkPixbuf;
# endif
#endif

extern GFSCache *pixmap_cache;

extern MaskedPixmap *im_error;
extern MaskedPixmap *im_unknown;
extern MaskedPixmap *im_symlink;

extern MaskedPixmap *im_unmounted;
extern MaskedPixmap *im_mounted;
extern MaskedPixmap *im_multiple;
extern MaskedPixmap *im_exec_file;
extern MaskedPixmap *im_appdir;

extern MaskedPixmap *im_help;
extern MaskedPixmap *im_dirs;

/* Note: don't change these two - thumbnails saving assumes 96x96 */
#define HUGE_WIDTH 96
#define HUGE_HEIGHT 96

#define ICON_HEIGHT 48
#define ICON_WIDTH 48

#define SMALL_HEIGHT 18
#define SMALL_WIDTH 22

/* When a MaskedPixmap is created we load the image from disk and
 * scale the pixbuf down to the 'huge' size (if it's bigger).
 * The 'normal' pixmap and mask are created automatically - you have
 * to request the other sizes.
 *
 * Note that any of the masks be also be NULL if the image has no
 * mask.
 */
struct _MaskedPixmap
{
	int		ref;
	GdkPixbuf	*huge_pixbuf;	/* 'Huge' source image */
	int		huge_width, huge_height;

	/* If huge_pixmap is NULL then call pixmap_make_huge() */
	GdkPixmap	*huge_pixmap;	/* Huge image */
	GdkBitmap	*huge_mask;

	GdkPixmap	*pixmap;	/* Normal size image (always valid) */
	GdkBitmap	*mask;
	int		width, height;

	/* If sm_pixmap is NULL then call pixmap_make_small() */
	GdkPixmap	*sm_pixmap;	/* Small image */
	GdkBitmap	*sm_mask;
	int		sm_width, sm_height;
};

void pixmaps_init(void);
void pixmap_ref(MaskedPixmap *mp);
void pixmap_unref(MaskedPixmap *mp);
void pixmap_make_huge(MaskedPixmap *mp);
void pixmap_make_small(MaskedPixmap *mp);
MaskedPixmap *load_pixmap(char *name);

#endif /* _PIXMAP_H */
