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

/* xml.c - A GObject wrapper for libxml documents */

#include "config.h"

#include "global.h"

#include "xml.h"

static gpointer parent_class = NULL;

static void xml_wrapper_finialize(GObject *object)
{
	XMLwrapper *xml = (XMLwrapper *) object;

	if (xml->doc)
	{
		xmlFreeDoc(xml->doc);
		xml->doc = NULL;
	}

	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void xml_wrapper_class_init(gpointer gclass, gpointer data)
{
	GObjectClass *object = (GObjectClass *) gclass;

	parent_class = g_type_class_peek_parent(gclass);

	object->finalize = xml_wrapper_finialize;
}

static void xml_wrapper_init(GTypeInstance *object, gpointer gclass)
{
	XMLwrapper *wrapper = (XMLwrapper *) object;

	wrapper->doc = NULL;
}

static GType xml_wrapper_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (XMLwrapperClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			xml_wrapper_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(XMLwrapper),
			0,			/* n_preallocs */
			xml_wrapper_init
		};

		type = g_type_register_static(G_TYPE_OBJECT, "XMLwrapper",
					      &info, 0);
	}

	return type;
}

XMLwrapper *xml_new(const char *pathname)
{
	xmlDocPtr doc = NULL;
	XMLwrapper *xml_data;

	if (pathname)
	{
		doc = xmlParseFile(pathname);
		if (!doc)
			return NULL;	/* Bad XML */
	}

	xml_data = g_object_new(xml_wrapper_get_type(), NULL);
	xml_data->doc = doc;

	return xml_data;
}
