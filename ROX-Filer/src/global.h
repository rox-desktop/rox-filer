/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

/* global.h is included by most of the other source files, just after
 * including config.h and the system header files, but before any other
 * ROX-Filer header files.
 */

#include <glib.h>

/* ROX-Filer's unique namespace (for use in SOAP messages) */
#define ROX_NS "http://rox.sourceforge.net/SOAP/ROX-Filer"

#define SOAP_ENV_NS "http://www.w3.org/2001/06/soap-envelope"

/* For debugging... */
#define SHOW(var) (g_print("[ " #var " = '%s' ]\n", var))

/* We typedef various pointers here to avoid creating unnecessary
 * dependencies on the other header files.
 */

/* Each filer window has one of these all to itself */
typedef struct _FilerWindow FilerWindow;

/* There is one Directory object per cached disk directory inode number.
 * Multiple FilerWindows may share a single directory. Directories
 * are cached, so a Directories may exist without any filer windows
 * referencing it at all.
 */
typedef struct _Directory Directory;

/* Each item in a directory has a DirItem. The contains information from
 * stat()ing the file, plus a few other bits. There may be several of these
 * for a single file, if it appears (hard-linked) in several directories.
 */
typedef struct _DirItem DirItem;

/* This contains pixmaps and masks for an image, and (sometimes) a
 * half-sized version.
 */
typedef struct _MaskedPixmap MaskedPixmap;

/* Each MIME type (eg 'text/plain') has one of these. It contains
 * a link to the image and the type's name (used so that the image can
 * be refreshed, amoung other things).
 */
typedef struct _MIME_type MIME_type;

/* There will be one of these if the pinboard is in use. It contains
 * the name of the pinboard and links to the pinned items inside.
 */
typedef struct _Pinboard Pinboard;

/* Each icon on the pinboard or a panel has an Icon structure. It contains
 * the name and path of the icon, as well as its DirItem.
 */
typedef struct _Icon Icon;

/* There is one of these for each panel window open. Panels work rather
 * like little pinboards, but with a more rigid layout.
 */
typedef struct _Panel Panel;

#include <tree.h>

/* For very old versions of libxml... */
#ifndef xmlChildrenNode
#  define xmlChildrenNode childs
#endif
