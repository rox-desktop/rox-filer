/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2001, the ROX-Filer team.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* appinfo.c - loading, caching and querying the AppInfo.xml files */

/* Any valid application directory may contain a file called AppInfo.xml.
 * The format is:
 *
 * <?xml version="1.0"?>
 * <AppMenu>
 *   <Item label="..." option="..."/>
 *   ...
 * </AppMenu>
 */

#include "config.h"

#include <parser.h>
#include <tree.h>

#include "global.h"

#include "appinfo.h"
#include "fscache.h"
#include "dir.h"
#include "type.h"

static GFSCache *appinfo_cache = NULL;

/* Static prototypes */
static AppInfo *load(char *pathname, gpointer data);
static void ref(AppInfo *dir, gpointer data);
static void unref(AppInfo *dir, gpointer data);
static int getref(AppInfo *dir, gpointer data);

/* Stores the tree for one application's AppInfo file */
struct _AppInfo {
	int	ref;
	xmlDocPtr doc;		/* Parsed AppInfo file */
};

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void appinfo_init(void)
{
	appinfo_cache = g_fscache_new((GFSLoadFunc) load,
				      (GFSRefFunc) ref,
				      (GFSRefFunc) unref,
				      (GFSGetRefFunc) getref,
				      NULL, NULL);
}

/* Load the AppInfo file for this application.
 *
 * Returns a pointer to the AppInfo structure, or NULL if this isn't
 * an application with a valid AppInfo file.
 *
 * appinfo_unref() the result.
 */
AppInfo *appinfo_get(guchar *app_dir, DirItem *item)
{
	AppInfo	*ai;
	guchar	*tmp;

	/* Is it even an application directory? */
	if (item->base_type != TYPE_DIRECTORY ||
			!(item->flags & ITEM_FLAG_APPDIR))
		return NULL;	/* Not an application */

	tmp = g_strconcat(app_dir, "/" APPINFO_FILENAME, NULL);
	ai = g_fscache_lookup(appinfo_cache, tmp);
	g_free(tmp);

	if (!ai)
	{
		/* Temp: look for the old AppMenu file */
		tmp = g_strconcat(app_dir, "/AppMenu", NULL);
		ai = g_fscache_lookup(appinfo_cache, tmp);
		g_free(tmp);
	}
	
	return ai;
}

void appinfo_unref(AppInfo *info)
{
	g_return_if_fail(info != NULL);

	g_fscache_data_unref(appinfo_cache, info);
}

/* Look for this section in the AppInfo document.
 * It may be any direct child node, or the root node itself (this is for
 * backwards compat with AppMenu; it may go soon).
 *
 * Returns an _xmlNode or NULL if there isn't one.
 */
struct _xmlNode *appinfo_get_section(AppInfo *ai, guchar *name)
{
	struct _xmlNode *node;

	g_return_val_if_fail(ai != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);

	node = xmlDocGetRootElement(ai->doc);
	g_return_val_if_fail(node != NULL, NULL);

	if (strcmp(node->name, name) == 0)
		return node;

	for (node = node->xmlChildrenNode; node; node = node->next)
	{
		if (strcmp(node->name, name) == 0)
			return node;
	}

	return NULL;
}

/****************************************************************
 *                      INTERNAL FUNCTIONS                      *
 ****************************************************************/

/* The pathname is the full path of the AppInfo file.
 * If we get here, assume that this really is an application.
 */
static AppInfo *load(char *pathname, gpointer data)
{
	xmlDocPtr doc;
	AppInfo *appinfo = NULL;

	doc = xmlParseFile(pathname);
	if (!doc)
		return NULL;	/* Bad XML */

	appinfo = g_new(AppInfo, 1);
	appinfo->ref = 1;
	appinfo->doc = doc;

	return appinfo;
}

static void ref(AppInfo *ai, gpointer data)
{
	if (ai)
		ai->ref++;
}

static void unref(AppInfo *ai, gpointer data)
{
	if (ai && --ai->ref == 0)
	{
		xmlFreeDoc(ai->doc);
		g_free(ai);
	}
}

static int getref(AppInfo *ai, gpointer data)
{
	return ai ? ai->ref : 0;
}
