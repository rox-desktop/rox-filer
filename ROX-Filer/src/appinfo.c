/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2002, the ROX-Filer team.
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

/* appinfo.c - querying the XMLwrapper.xml files */

/* Any valid application directory may contain a file called XMLwrapper.xml.
 * The format is:
 *
 * <?xml version="1.0"?>
 * <XMLwrapper>
 *   <Summary>Tooltip text</Summary>
 *   <About>
 *     <Purpose>...</Purpose>
 *     <Version>...</Version>
 *     <Authors>...</Authors>
 *     <License>...</License>
 *     <Homepage>...</Homepage>
 *     ...
 *   </About>
 *   <AppMenu>
 *     <Item label="..." option="..."/>
 *     ...
 *   </AppMenu>
 * </XMLwrapper>
 */

#include "config.h"

#include <string.h>

#include "global.h"

#include "appinfo.h"
#include "fscache.h"
#include "type.h"
#include "diritem.h"
#include "support.h"

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Load the XMLwrapper file for this application.
 *
 * Returns a pointer to the XMLwrapper structure, or NULL if this isn't
 * an application with a valid XMLwrapper file.
 *
 * appinfo_unref() the result.
 */
XMLwrapper *appinfo_get(guchar *app_dir, DirItem *item)
{
	XMLwrapper	*ai;
	guchar	*tmp;

	/* Is it even an application directory? */
	if (item->base_type != TYPE_DIRECTORY ||
			!(item->flags & ITEM_FLAG_APPDIR))
		return NULL;	/* Not an application */

	tmp = g_strconcat(app_dir, "/" APPINFO_FILENAME, NULL);
	ai = xml_cache_load(tmp);
	g_free(tmp);

	return ai;
}

/* Look for this section in the XMLwrapper document.
 * It may be any direct child node, or the root node itself (this is for
 * backwards compat with AppMenu; it may go soon).
 *
 * Returns an xmlNode or NULL if there isn't one.
 */
xmlNode *appinfo_get_section(XMLwrapper *ai, guchar *name)
{
	xmlNode *node;

	g_return_val_if_fail(ai != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);

	node = xmlDocGetRootElement(ai->doc);
	g_return_val_if_fail(node != NULL, NULL);

	if (strcmp(node->name, name) == 0)
		return node;

	return get_subnode(node, NULL, name);
}
