/*
 * $Id$
 */

#ifndef _APPINFO_H
#define _APPINFO_H

#include <parser.h>
#include "fscache.h"

/* Name of the XML file where the info is stored */
#define APPINFO_FILENAME		"AppInfo.xml"

typedef struct _AppInfo AppInfo;

/* External interface */
void appinfo_init(void);
AppInfo *appinfo_get(guchar *app_dir, DirItem *item);
void appinfo_unref(AppInfo *info);
xmlNode *appinfo_get_section(AppInfo *ai, guchar *name);

#endif   /* _APPINFO_H */
