/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 *
 * Extended filesystem attribute support for MIME types
 */

#ifndef _XTYPES_H
#define _XTYPES_H

/* Prototypes */
MIME_type *xtype_get(const char *path);
int xtype_set(const char *path, const MIME_type *type);
void xtype_init(void);

int xtype_have_attr(const char *path);

/* path may be NULL to test for support in libc */
int xtype_supported(const char *path);

#endif
