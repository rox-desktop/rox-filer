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

/* This code is used to communicate between two copies of the filer:
 * If the filer is run and the same version of the filer is already
 * running on the same machine then the new copy simply asks the old
 * one deal with it and quits.
 */

#include "config.h"

#include <gdk/gdkx.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <gtk/gtk.h>
#include <gtk/gtkinvisible.h>
#include <parser.h>

#include "global.h"

#include "main.h"
#include "support.h"
#include "gui_support.h"
#include "run.h"
#include "remote.h"
#include "filer.h"
#include "pinboard.h"
#include "panel.h"

static GdkAtom filer_atom;	/* _ROX_FILER_VERSION_HOST */
static GdkAtom xsoap;		/* _XSOAP */

static GHashTable *rpc_calls = NULL; /* MethodName -> Function */

/* Static prototypes */
static GdkWindow *get_existing_ipc_window();
static gboolean get_ipc_property(GdkWindow *window, Window *r_xid);
static void soap_send(GtkWidget *from, GdkAtom prop, GdkWindow *dest);
static gboolean client_event(GtkWidget *window,
				 GdkEventClient *event,
				 gpointer data);
static xmlNodePtr rpc_OpenDir(xmlNodePtr method);
static xmlNodePtr rpc_CloseDir(xmlNodePtr method);
static xmlNodePtr rpc_Examine(xmlNodePtr method);
static xmlNodePtr rpc_Show(xmlNodePtr method);
static xmlNodePtr rpc_Pinboard(xmlNodePtr method);
static xmlNodePtr rpc_Panel(xmlNodePtr method);
static xmlNodePtr rpc_Run(xmlNodePtr method);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/


/* Try to get an already-running filer to handle things (only if
 * new_copy is FALSE); TRUE if we succeed.
 * Create an IPC widget so that future filers can contact us.
 */
gboolean remote_init(xmlDocPtr rpc, gboolean new_copy)
{
	guchar		*unique_id;
	GdkWindow	*existing_ipc_window;
	GtkWidget	*ipc_window;
	Window		xwindow;

	/* xmlDocDump(stdout, rpc); */

	rpc_calls = g_hash_table_new(g_str_hash, g_str_equal);

	g_hash_table_insert(rpc_calls, "Run", rpc_Run);
	g_hash_table_insert(rpc_calls, "OpenDir", rpc_OpenDir);
	g_hash_table_insert(rpc_calls, "Pinboard", rpc_Pinboard);
	g_hash_table_insert(rpc_calls, "Panel", rpc_Panel);
	g_hash_table_insert(rpc_calls, "CloseDir", rpc_CloseDir);
	g_hash_table_insert(rpc_calls, "Examine", rpc_Examine);
	g_hash_table_insert(rpc_calls, "Show", rpc_Show);

	/* Look for a property on the root window giving the IPC window
	 * of an already-running copy of this version of the filer, running
	 * on the same machine and with the same euid.
	 */
	unique_id = g_strdup_printf("_ROX_FILER_%d_%s_%s",
				(int) euid, VERSION, our_host_name());
	filer_atom = gdk_atom_intern(unique_id, FALSE);
	g_free(unique_id);

	xsoap = gdk_atom_intern("_XSOAP", FALSE);

	/* If we find a running copy, we'll need a window to put the
	 * SOAP message in before sending.
	 * If there's no running copy, we'll need a window to receive
	 * future SOAP message events.
	 * Either way, we'll need a window for it...
	 */
	ipc_window = gtk_invisible_new();
	gtk_widget_realize(ipc_window);

	existing_ipc_window = new_copy ? NULL : get_existing_ipc_window();
	if (existing_ipc_window)
	{
		xmlChar *mem;
		int	size;

		xmlDocDumpMemory(rpc, &mem, &size);
		g_return_val_if_fail(size > 0, FALSE);
		
		gdk_property_change(ipc_window->window, xsoap,
				gdk_x11_xatom_to_atom(XA_STRING), 8,
				GDK_PROP_MODE_REPLACE, mem, size);
		g_free(mem);

		soap_send(ipc_window, xsoap, existing_ipc_window);

		return TRUE;
	}

	xwindow = GDK_WINDOW_XWINDOW(ipc_window->window);
	
	/* Make the IPC window contain a property pointing to
	 * itself - this can then be used to check that it really
	 * is an IPC window.
	 */
	gdk_property_change(ipc_window->window, filer_atom,
			gdk_x11_xatom_to_atom(XA_WINDOW), 32,
			GDK_PROP_MODE_REPLACE,
			(void *) &xwindow, 1);

	/* Get notified when we get a message */
	gtk_signal_connect(GTK_OBJECT(ipc_window), "client-event",
			GTK_SIGNAL_FUNC(client_event), NULL);

	/* Make the root window contain a pointer to the IPC window */
	gdk_property_change(GDK_ROOT_PARENT(), filer_atom,
			gdk_x11_xatom_to_atom(XA_WINDOW), 32,
			GDK_PROP_MODE_REPLACE,
			(void *) &xwindow, 1);

	return FALSE;
}

/* Executes the RPC call(s) in the given SOAP message and returns
 * the reply.
 */
xmlDocPtr run_soap(xmlDocPtr soap)
{
	xmlNodePtr body, node;

	g_return_val_if_fail(soap != NULL, NULL);

	/* Make sure we don't quit before doing the whole list
	 * (there's a command that closes windows)
	 */
	number_of_windows++;

	node = xmlDocGetRootElement(soap);
	if (!node->ns || strcmp(node->ns->href, SOAP_ENV_NS) != 0)
		goto bad_soap;

	body = get_subnode(node, SOAP_ENV_NS, "Body");
	if (!body)
		goto bad_soap;

	for (node = body->xmlChildrenNode; node; node = node->next)
	{
		xmlNodePtr (*fn)(xmlNodePtr method);

		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (node->ns == NULL || strcmp(node->ns->href, ROX_NS) != 0)
		{
			g_warning("Unknown namespace %s",
					node->ns ? node->ns->href
						 : (xmlChar *) "(none)");
			continue;
		}
		
		fn = g_hash_table_lookup(rpc_calls, node->name);
		if (fn)
			fn(node);
		else
			g_warning("Unknown method '%s'", node->name);
	}

	goto out;

bad_soap:
	g_warning("Bad SOAP message received!");

out:
	number_of_windows--;

	return NULL;
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Get the remote IPC window of the already-running filer if there
 * is one.
 */
static GdkWindow *get_existing_ipc_window(void)
{
	Window		xid, xid_confirm;
	GdkWindow	*window;

	if (!get_ipc_property(GDK_ROOT_PARENT(), &xid))
		return NULL;

	if (gdk_window_lookup(xid))
		return NULL;	/* Stale handle which we now own */

	window = gdk_window_foreign_new(xid);
	if (!window)
		return NULL;

	if (!get_ipc_property(window, &xid_confirm) || xid_confirm != xid)
		return NULL;

	return window;
}

/* Returns the 'rox_atom' property of 'window' */
static gboolean get_ipc_property(GdkWindow *window, Window *r_xid)
{
	guchar		*data;
	gint		format, length;
	gboolean	retval = FALSE;
	
	if (gdk_property_get(window, filer_atom,
			gdk_x11_xatom_to_atom(XA_WINDOW), 0, 4,
			FALSE, NULL, &format, &length, &data) && data)
	{
		if (format == 32 && length == 4)
		{
			retval = TRUE;
			*r_xid = *((Window *) data);
		}
		g_free(data);
	}

	return retval;
}

static gboolean client_event(GtkWidget *window,
				 GdkEventClient *event,
				 gpointer user_data)
{
	gint	grab_len = 4096;
	gint	length;
	guchar	*data;
	GdkWindow *src_window;
	GdkAtom prop;

	if (event->message_type != xsoap)
		return FALSE;

	src_window = gdk_window_foreign_new(event->data.l[0]);
	g_return_val_if_fail(src_window != NULL, FALSE);
	prop = gdk_x11_xatom_to_atom(event->data.l[1]);

	while (1)
	{
		xmlDocPtr doc;

		if (!(gdk_property_get(src_window, prop,
				gdk_x11_xatom_to_atom(XA_STRING), 0, grab_len,
				TRUE, NULL, NULL,
				&length, &data) && data))
			return TRUE;	/* Error? */

		if (length >= grab_len)
		{
			/* Didn't get all of it - try again */
			grab_len <<= 1;
			g_free(data);
			continue;
		}

		data = g_realloc(data, length + 1);
		data[length] = '\0';	/* libxml seems to need this */
		
		doc = xmlParseMemory(g_strndup(data, length), length);
		g_free(data);

		run_soap(doc);
		if (number_of_windows == 0)
			gtk_main_quit();

		xmlFreeDoc(doc);

		return TRUE;
	}

	return FALSE;
}

/* The RPC handlers all work in the same way -- they get passed the
 * method node in the RPC call and they return the result node, or
 * NULL if there isn't a result.
 */

static xmlNodePtr rpc_OpenDir(xmlNodePtr method)
{
	xmlNodePtr path_node;
	char	   *path;
	
	path_node = get_subnode(method, ROX_NS, "Filename");

	g_return_val_if_fail(path_node != NULL, NULL);

	path = xmlNodeGetContent(path_node);

	filer_opendir(path, NULL);

	g_free(path);
	
	return NULL;
}

static xmlNodePtr rpc_Run(xmlNodePtr method)
{
	xmlNodePtr path_node;
	char	   *path;
	
	path_node = get_subnode(method, ROX_NS, "Filename");

	g_return_val_if_fail(path_node != NULL, NULL);

	path = xmlNodeGetContent(path_node);

	run_by_path(path);

	g_free(path);
	
	return NULL;
}

static xmlNodePtr rpc_CloseDir(xmlNodePtr method)
{
	xmlNodePtr path_node;
	char	   *path;
	
	path_node = get_subnode(method, ROX_NS, "Filename");

	g_return_val_if_fail(path_node != NULL, NULL);

	path = xmlNodeGetContent(path_node);

	filer_close_recursive(path);

	g_free(path);
	
	return NULL;
}

static xmlNodePtr rpc_Examine(xmlNodePtr method)
{
	xmlNodePtr path_node;
	char	   *path;
	
	path_node = get_subnode(method, ROX_NS, "Filename");

	g_return_val_if_fail(path_node != NULL, NULL);

	path = xmlNodeGetContent(path_node);

	examine(path);

	g_free(path);
	
	return NULL;
}

static xmlNodePtr rpc_Show(xmlNodePtr method)
{
	xmlNodePtr node;
	char	   *dir, *leaf;
	
	node = get_subnode(method, ROX_NS, "Directory");
	g_return_val_if_fail(node != NULL, NULL);
	dir = xmlNodeGetContent(node);

	node = get_subnode(method, ROX_NS, "Leafname");
	g_return_val_if_fail(node != NULL, NULL);
	leaf = xmlNodeGetContent(node);

	/* XXX: Seems silly to join them only to split again later... */
	open_to_show(make_path(dir, leaf)->str);

	g_free(dir);
	g_free(leaf);
	
	return NULL;
}
		
static xmlNodePtr rpc_Pinboard(xmlNodePtr method)
{
	xmlNodePtr node;
	char	   *value = NULL;
	
	node = get_subnode(method, ROX_NS, "Name");

	if (node)
		value = xmlNodeGetContent(node);

	pinboard_activate(value);

	g_free(value);
	
	return NULL;
}

static xmlNodePtr rpc_Panel(xmlNodePtr method)
{
	xmlNodePtr name, side;
	char	   *value = NULL, *side_name = NULL;
	
	name = get_subnode(method, ROX_NS, "Name");
	side = get_subnode(method, ROX_NS, "Side");

	if (!side)
	{
		g_warning("Panel requires a Side");
		return NULL;
	}

	if (name)
		value = xmlNodeGetContent(name);
	side_name = xmlNodeGetContent(side);

	if (strcmp(side_name, "Top") == 0)
		panel_new(value, PANEL_TOP);
	else if (strcmp(side_name, "Bottom") == 0)
		panel_new(value, PANEL_BOTTOM);
	else if (strcmp(side_name, "Left") == 0)
		panel_new(value, PANEL_LEFT);
	else if (strcmp(side_name, "Right") == 0)
		panel_new(value, PANEL_RIGHT);
	else
		g_warning("Unknown panel side '%s'", side_name);

	g_free(value);
	g_free(side_name);
	
	return NULL;
}

static void soap_done(GtkWidget *widget, GdkEventProperty *event, gpointer data)
{
	GdkAtom	prop = (GdkAtom) data;

	if (prop != event->atom)
		return;

	/* TODO: If we got a reply, display it here */

	gtk_main_quit();
}

gboolean too_slow(gpointer data)
{
	g_warning("Existing ROX-Filer process is not responding! Try with -n");
	gtk_main_quit();

	return 0;
}

/* Send the SOAP message in property 'prop' on 'from' to 'dest' */
static void soap_send(GtkWidget *from, GdkAtom prop, GdkWindow *dest)
{
	GdkEventClient event;

	event.data.l[0] = GDK_WINDOW_XWINDOW(from->window);
	event.data.l[1] = gdk_x11_atom_to_xatom(prop);
	event.data_format = 32;
	event.message_type = xsoap;
	
	gtk_widget_add_events(from, GDK_PROPERTY_CHANGE_MASK);
	gtk_signal_connect(GTK_OBJECT(from), "property-notify-event",
				GTK_SIGNAL_FUNC(soap_done),
				GINT_TO_POINTER(prop));

	gdk_event_send_client_message((GdkEvent *) &event,
				      GDK_WINDOW_XWINDOW(dest));

	gtk_timeout_add(10000, too_slow, NULL);
			
	gtk_main();
}
