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

/* We put all the global typedefs here to avoid creating dependencies
 * between header files.
 */

/* Each filer window has one of these all to itself */
typedef struct _FilerWindow FilerWindow;

/* There is one Directory object per cached disk directory inode number.
 * Multiple FilerWindows may share a single Directory. Directories
 * are cached, so a Directory may exist without any filer windows
 * referencing it at all.
 */
typedef struct _Directory Directory;

/* Each item in a directory has a DirItem. The contains information from
 * stat()ing the file, plus a few other bits. There may be several of these
 * for a single file, if it appears (hard-linked) in several directories.
 * Each pinboard and panel icon also has one of these (not shared).
 */
typedef struct _DirItem DirItem;

/* A Collection is a GtkWidget used to arrange a grid of items.
 * Each filer window has its own one of these; the data field
 * of each item is the corresponding DirItem, shared with the
 * FilerWindow's Directory and other windows' Collections.
 */
typedef struct _Collection Collection;

/* Each item in a Collection has one of these, which stores its selected
 * state, data, and view_data.
 */
typedef struct _CollectionItem   CollectionItem;

/* A viewport containing a Collection which also handles redraw */
typedef struct _ViewCollection ViewCollection;

/* This contains the pixbufs for an image, in various sizes */
typedef struct _MaskedPixmap MaskedPixmap;

/* Each MIME type (eg 'text/plain') has one of these. It contains
 * a link to the image and the type's name (used so that the image can
 * be refreshed, amoung other things).
 */
typedef struct _MIME_type MIME_type;

/* Icon is an abstract base class for pinboard and panel icons.
 * It contains the name and path of the icon, as well as its DirItem.
 */
typedef struct _Icon Icon;

/* There will be one of these if the pinboard is in use. It contains
 * the name of the pinboard and links to the pinned Icons inside.
 */
typedef struct _Pinboard Pinboard;

/* There is one of these for each panel window open. Panels work rather
 * like little pinboards, but with a more rigid layout.
 */
typedef struct _Panel Panel;

/* Each option has a static Option structure. This is initialised by
 * calling option_add_int() or similar. See options.c for details.
 * This structure is read-only.
 */
typedef struct _Option Option;

/* A filesystem cache provides a quick and easy way to load files.
 * When a cache is created, functions to load and update files are
 * registered to it. Requesting an object from the cache will load
 * or update it as needed, or return the cached copy if the current
 * version of the file is already cached.
 * Caches are used to access directories, images and XML files.
 */
typedef struct _GFSCache GFSCache;

/* Each cached XML file is represented by one of these */
typedef struct _XMLwrapper XMLwrapper;

/* The minibuffer is a text field which appears at the bottom of
 * a filer window. It has four modes of operation:
 */
typedef enum {
	MINI_NONE,
	MINI_PATH,
	MINI_SHELL,
	MINI_SELECT_IF,
} MiniType;

/* The next two correspond to the styles on the Display submenu: */

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
	DETAILS_UNKNOWN		= -1,
} DetailsType;

/* The namespaces for the SOAP messages */
#define SOAP_ENV_NS_OLD "http://www.w3.org/2001/06/soap-envelope"
#define SOAP_ENV_NS "http://www.w3.org/2001/12/soap-envelope"
#define SOAP_RPC_NS "http://www.w3.org/2001/12/soap-rpc"
#define ROX_NS "http://rox.sourceforge.net/SOAP/ROX-Filer"

#include <libxml/tree.h>
