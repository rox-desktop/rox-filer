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

/* The namespace for the SOAP envelope */
#define SOAP_ENV_NS "http://www.w3.org/2001/06/soap-envelope"

/* For debugging... */
#define SHOW(var, fmt) (g_print("[ " #var " = '%" #fmt "' ]\n", var))

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

/* A Collection is a GtkWidget used to arrange a grid of items.
 * Each filer window has its own one of these; the data field
 * of each item is the corresponding DirItem, shared with the
 * Directory and other Collections.
 */
typedef struct _Collection Collection;

/* Each item in a directory has a DirItem. The contains information from
 * stat()ing the file, plus a few other bits. There may be several of these
 * for a single file, if it appears (hard-linked) in several directories.
 * Each pinboard and panel icon also has one of these (not shared).
 */
typedef struct _DirItem DirItem;

/* This contains pixmaps and masks for an image, in various sizes */
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

/* A filesystem cache provides a quick and easy way to load files.
 * When a cache is created, functions to load and update files are
 * registered to it. Requesting an object from the cache will load
 * or update it as needed, or return the cached copy if the current
 * version of the file is already cached.
 * Caches are used to access directories, images and AppInfo.xml files.
 */
typedef struct _GFSCache GFSCache;

/* The minibuffer is a text field which appears at the bottom of
 * a filer window. It has four modes of operation:
 */
typedef enum {
	MINI_NONE,
	MINI_PATH,
	MINI_SHELL,
	MINI_SELECT_IF,
} MiniType;

typedef enum {		/* Values used in options, must start at 0 */
	LARGE_ICONS	= 0,
	SMALL_ICONS	= 1,
	HUGE_ICONS	= 2,
	UNKNOWN_STYLE
} DisplayStyle;

typedef enum {		/* Values used in options, must start at 0 */
	DETAILS_NONE		= 0,
	DETAILS_SUMMARY 	= 1,
	DETAILS_SIZE		= 2,
	DETAILS_PERMISSIONS	= 3,
	DETAILS_TYPE		= 4,
	DETAILS_TIMES		= 5,
} DetailsType;


#include <tree.h>

/* For very old versions of libxml... */
#ifndef xmlChildrenNode
#  define xmlChildrenNode childs
#endif
