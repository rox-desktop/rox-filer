/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 *
 * Extended filesystem attribute support for MIME types
 */

#ifndef _XTYPES_H
#define _XTYPES_H

#if defined(HAVE_GETXATTR)
#define ATTR_MAN_PAGE "See the attr(5) man page for full details."
#elif defined(HAVE_ATTROPEN)
#define ATTR_MAN_PAGE "See the fsattr(5) man page for full details."
#else
#define ATTR_MAN_PAGE "You do not appear to have OS support." 
#endif 

/* Prototypes */
MIME_type *xtype_get(const char *path);
int xtype_set(const char *path, const MIME_type *type);
void xtype_init(void);

#endif
