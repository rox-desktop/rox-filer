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

/* This code is used to communicate between two copies of the filer:
 * If the filer is run and the same version of the filer is already
 * running on the same machine then the new copy simply asks the old
 * one deal with it and quits.
 */

#include "config.h"

#include <string.h>

#include <gdk/gdkx.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <gtk/gtk.h>
#include <gtk/gtkinvisible.h>
#include <libxml/parser.h>

#include "global.h"

#include "main.h"
#include "support.h"
#include "gui_support.h"
#include "run.h"
#include "remote.h"
#include "filer.h"
#include "pinboard.h"
#include "panel.h"
#include "action.h"
#include "type.h"
#include "display.h"
#include "xml.h"
#include "diritem.h"

static GdkAtom filer_atom;	/* _ROX_FILER_EUID_VERSION_HOST */
static GdkAtom filer_atom_any;	/* _ROX_FILER_EUID_HOST */
static GdkAtom xsoap;		/* _XSOAP */

typedef struct _SOAP_call SOAP_call;
typedef xmlNodePtr (*SOAP_func)(GList *args);

struct _SOAP_call {
	SOAP_func func;
	gchar **required_args;
	gchar **optional_args;
};

static GHashTable *rpc_calls = NULL; /* MethodName -> Function */

/* Static prototypes */
static GdkWindow *get_existing_ipc_window();
static gboolean get_ipc_property(GdkWindow *window, Window *r_xid);
static void soap_send(GtkWidget *from, GdkAtom prop, GdkWindow *dest);
static gboolean client_event(GtkWidget *window,
				 GdkEventClient *event,
				 gpointer data);
static void soap_register(char *name, SOAP_func func, char *req, char *opt);
static xmlNodePtr soap_invoke(xmlNode *method);

static xmlNodePtr rpc_Version(GList *args);
static xmlNodePtr rpc_OpenDir(GList *args);
static xmlNodePtr rpc_CloseDir(GList *args);
static xmlNodePtr rpc_Examine(GList *args);
static xmlNodePtr rpc_Show(GList *args);
static xmlNodePtr rpc_Pinboard(GList *args);
static xmlNodePtr rpc_Panel(GList *args);
static xmlNodePtr rpc_Run(GList *args);
static xmlNodePtr rpc_Copy(GList *args);
static xmlNodePtr rpc_Move(GList *args);
static xmlNodePtr rpc_Link(GList *args);
static xmlNodePtr rpc_FileType(GList *args);
static xmlNodePtr rpc_Mount(GList *args);

static xmlNodePtr rpc_PanelAdd(GList *args);
static xmlNodePtr rpc_PinboardAdd(GList *args);
static xmlNodePtr rpc_SetBackdrop(GList *args);

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

	soap_register("Version", rpc_Version, NULL, NULL);

	soap_register("Run", rpc_Run, "Filename", NULL);
	soap_register("OpenDir", rpc_OpenDir, "Filename", "Style,Details,Sort");
	soap_register("CloseDir", rpc_CloseDir, "Filename", NULL);
	soap_register("Examine", rpc_Examine, "Filename", NULL);
	soap_register("Show", rpc_Show, "Directory,Leafname", NULL);

	soap_register("Pinboard", rpc_Pinboard, NULL, "Name");
	soap_register("Panel", rpc_Panel, "Side", "Name");

	soap_register("FileType", rpc_FileType, "Filename", NULL);

	soap_register("Copy", rpc_Copy, "From,To", "Leafname,Quiet");
	soap_register("Move", rpc_Move, "From,To", "Leafname,Quiet");
	soap_register("Link", rpc_Link, "From,To", "Leafname");
	soap_register("Mount", rpc_Mount, "MountPoints", "OpenDir,Quiet");

	soap_register("SetBackdrop", rpc_SetBackdrop, "App,Path", "Style");
	soap_register("PinboardAdd", rpc_PinboardAdd, "Path,X,Y", "Label");
	soap_register("PanelAdd", rpc_PanelAdd, "Side,Path", "Label,After");

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
	g_signal_connect(ipc_window, "client-event",
			G_CALLBACK(client_event), NULL);

	/* Make the root window contain a pointer to the IPC window */
	gdk_property_change(gdk_get_default_root_window(), filer_atom,
			gdk_x11_xatom_to_atom(XA_WINDOW), 32,
			GDK_PROP_MODE_REPLACE,
			(void *) &xwindow, 1);

	/* Also have a property without the version number, for programs
	 * that are happy to talk to any version of the filer.
	 */
	unique_id = g_strdup_printf("_ROX_FILER_%d_%s",
				(int) euid, our_host_name());
	filer_atom_any = gdk_atom_intern(unique_id, FALSE);
	g_free(unique_id);
	/* On the IPC window... */
	gdk_property_change(ipc_window->window, filer_atom_any,
			gdk_x11_xatom_to_atom(XA_WINDOW), 32,
			GDK_PROP_MODE_REPLACE,
			(void *) &xwindow, 1);
	/* ... and on the root */
	gdk_property_change(gdk_get_default_root_window(), filer_atom_any,
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
	xmlNodePtr body, node, rep_body, reply;
	xmlDocPtr rep_doc = NULL;

	g_return_val_if_fail(soap != NULL, NULL);

	/* Make sure we don't quit before doing the whole list
	 * (there's a command that closes windows)
	 */
	number_of_windows++;

	node = xmlDocGetRootElement(soap);
	if (!node->ns)
		goto bad_soap;

	if (strcmp(node->ns->href, SOAP_ENV_NS) != 0 &&
	    strcmp(node->ns->href, SOAP_ENV_NS_OLD) != 0)
		goto bad_soap;

	body = get_subnode(node, SOAP_ENV_NS, "Body");
	if (!body)
		body = get_subnode(node, SOAP_ENV_NS_OLD, "Body");
	if (!body)
		goto bad_soap;

	for (node = body->xmlChildrenNode; node; node = node->next)
	{
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (node->ns == NULL || strcmp(node->ns->href, ROX_NS) != 0)
		{
			g_warning("Unknown namespace %s",
					node->ns ? node->ns->href
						 : (xmlChar *) "(none)");
			continue;
		}
		
		reply = soap_invoke(node);

		if (reply)
		{
			if (!rep_doc)
				rep_doc = soap_new(&rep_body);
			xmlAddChild(rep_body, reply);
		}
	}

	goto out;

bad_soap:
	g_warning("Bad SOAP message received!");

out:
	number_of_windows--;

	return rep_doc;
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Register this function to handle SOAP calls to method 'name'.
 * 'req' and 'opt' are comma separated lists of argument names, in the order
 * that they are to be delivered to the function.
 * NULL will be passed for an opt argument if not given.
 * Otherwise, the parameter is the xmlNode from the call.
 */
static void soap_register(char *name, SOAP_func func, char *req, char *opt)
{
	SOAP_call *call;

	call = g_new(SOAP_call, 1);
	call->func = func;
	call->required_args = req ? g_strsplit(req, ",", 0) : NULL;
	call->optional_args = opt ? g_strsplit(opt, ",", 0) : NULL;

	g_hash_table_insert(rpc_calls, g_strdup(name), call);
}

/* Get the remote IPC window of the already-running filer if there
 * is one.
 */
static GdkWindow *get_existing_ipc_window(void)
{
	Window		xid, xid_confirm;
	GdkWindow	*window;

	if (!get_ipc_property(gdk_get_default_root_window(), &xid))
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

static char *read_property(GdkWindow *window, GdkAtom prop, gint *out_length)
{
	gint	grab_len = 4096;
	gint	length;
	guchar	*data;

	while (1)
	{
		if (!(gdk_property_get(window, prop,
				gdk_x11_xatom_to_atom(XA_STRING), 0, grab_len,
				FALSE, NULL, NULL,
				&length, &data) && data))
			return NULL;	/* Error? */

		if (length >= grab_len)
		{
			/* Didn't get all of it - try again */
			grab_len <<= 1;
			g_free(data);
			continue;
		}

		data = g_realloc(data, length + 1);
		data[length] = '\0';	/* libxml seems to need this */
		*out_length = length;

		return data;
	}
}

static gboolean client_event(GtkWidget *window,
				 GdkEventClient *event,
				 gpointer user_data)
{
	GdkWindow *src_window;
	GdkAtom prop;
	xmlDocPtr reply;
	guchar	*data;
	gint	length;
	xmlDocPtr doc;

	if (event->message_type != xsoap)
		return FALSE;

	src_window = gdk_window_foreign_new(event->data.l[0]);
	g_return_val_if_fail(src_window != NULL, FALSE);
	prop = gdk_x11_xatom_to_atom(event->data.l[1]);

	data = read_property(src_window, prop, &length);
	if (!data)
		return TRUE;

	doc = xmlParseMemory(g_strndup(data, length), length);
	g_free(data);

	reply = run_soap(doc);
	if (number_of_windows == 0)
		gtk_main_quit();

	xmlFreeDoc(doc);

	if (reply)
	{
		/* Send reply back... */
		xmlChar *mem;
		int	size;

		xmlDocDumpMemory(reply, &mem, &size);
		g_return_val_if_fail(size > 0, TRUE);

		gdk_property_change(src_window, prop,
			gdk_x11_xatom_to_atom(XA_STRING), 8,
			GDK_PROP_MODE_REPLACE, mem, size);
		g_free(mem);
	}
	else
		gdk_property_delete(src_window, prop);

	return TRUE;
}

/* Some handy functions for processing SOAP RPC arguments... */

/* Returns TRUE, FALSE, or -1 (if argument is unspecified) */
static int bool_value(xmlNode *arg)
{
	int answer;
	char *optval;

	if (!arg)
	        return -1;

	optval = xmlNodeGetContent(arg);
	answer = text_to_boolean(optval, -1);	/* XXX: Report error? */
	g_free(optval);

	return answer;
}

/* Returns the text of this arg as a string, or NULL if the (optional)
 * argument wasn't supplied. Returns "" if the arg is empty.
 * g_free() the result.
 */
static char *string_value(xmlNode *arg)
{
	char *retval;

	if (!arg)
		return NULL;
	
	retval = xmlNodeGetContent(arg);

	return retval ? retval : g_strdup("");
}

/* Returns the text of this arg as an int, or the default value if not
 * supplied or not an int.
 */
static int int_value(xmlNode *arg, int def)
{
	char *str, *end;
	int i;

	if (!arg)
		return def;
	
	str = xmlNodeGetContent(arg);
	if (!str || !str[0])
		return def;

	i = (int) strtol(str, &end, 0);

	return (end > str) ? i : def;
}

/* Return a list of strings, one for each child node of arg.
 * g_list_free the list, and g_free each string.
 */
static GList *list_value(xmlNode *arg)
{
	GList *list = NULL;
	xmlNode *node;

	for (node = arg->xmlChildrenNode; node; node = node->next)
	{
		if (node->type != XML_ELEMENT_NODE)
			continue;

		list = g_list_append(list, string_value(node));
	}

	return list;
}

#define ARG(n) ((xmlNode *) g_list_nth(args, n)->data)

/* The RPC handlers all work in the same way -- they get passed a list of
 * xmlNode arguments from the RPC call and they return the result node, or
 * NULL if there isn't a result.
 */

static xmlNodePtr rpc_Version(GList *args)
{
	xmlNodePtr reply;

	reply = xmlNewNode(NULL, "rox:VersionResponse");
	xmlNewNs(reply, SOAP_RPC_NS, "soap");
	xmlNewChild(reply, NULL, "soap:result", VERSION);

	return reply;
}

/* Args: Path, [Style, Details, Sort] */
static xmlNodePtr rpc_OpenDir(GList *args)
{
	char	   *path;
	char       *style, *details, *sort;
	FilerWindow *fwin;

	path = string_value(ARG(0));
	style = string_value(ARG(1));
	details = string_value(ARG(2));
	sort = string_value(ARG(3));

	fwin = filer_opendir(path, NULL);
	g_free(path);

	if (style)
	{
		DisplayStyle ds;

		ds = !g_strcasecmp(style, "Large") ? LARGE_ICONS :
		     !g_strcasecmp(style, "Small") ? SMALL_ICONS :
		     !g_strcasecmp(style, "Huge")  ? HUGE_ICONS :
		     				     UNKNOWN_STYLE;
		if (ds == UNKNOWN_STYLE)
			g_warning("Unknown style '%s'\n", style);
		else
			display_set_layout(fwin, ds, fwin->details_type);

		g_free(style);
	}

	if (details)
	{
		DetailsType dt;
		
		dt = !g_strcasecmp(details, "None") ? DETAILS_NONE :
		     !g_strcasecmp(details, "Summary") ? DETAILS_SUMMARY :
		     !g_strcasecmp(details, "Size") ? DETAILS_SIZE :
		     !g_strcasecmp(details, "Type") ? DETAILS_TYPE :
		     !g_strcasecmp(details, "Times") ? DETAILS_TIMES :
		     !g_strcasecmp(details, "Permissions")
		     				? DETAILS_PERMISSIONS :
						  DETAILS_UNKNOWN;

		if (dt == DETAILS_UNKNOWN)
			g_warning("Unknown details type '%s'\n", details);
		else
			display_set_layout(fwin, fwin->display_style, dt);
		
		g_free(details);
	}

	if (sort)
	{
		int (*cmp)(const void *, const void *);

		cmp = !g_strcasecmp(sort, "Name") ? sort_by_name :
		      !g_strcasecmp(sort, "Type") ? sort_by_type :
		      !g_strcasecmp(sort, "Date") ? sort_by_date :
 		      !g_strcasecmp(sort, "Size") ? sort_by_size :
		     				     NULL;
		if (!cmp)
			g_warning("Unknown sorting criteria '%s'\n", sort);
		else
			display_set_sort_fn(fwin, cmp);

		g_free(sort);
	}

	return NULL;
}

static xmlNodePtr rpc_Run(GList *args)
{
	char	   *path;

	path = string_value(ARG(0));
	run_by_path(path);
	g_free(path);

	return NULL;
}

static xmlNodePtr rpc_CloseDir(GList *args)
{
	char	   *path;

	path = string_value(ARG(0));
	filer_close_recursive(path);
	g_free(path);

	return NULL;
}

static xmlNodePtr rpc_Examine(GList *args)
{
	char	   *path;

	path = string_value(ARG(0));
	examine(path);
	g_free(path);

	return NULL;
}

static xmlNodePtr rpc_Show(GList *args)
{
	char	   *dir, *leaf;
	
	dir = string_value(ARG(0));
	leaf = string_value(ARG(1));

	/* XXX: Seems silly to join them only to split again later... */
	open_to_show(make_path(dir, leaf)->str);

	g_free(dir);
	g_free(leaf);

	return NULL;
}
		
static xmlNodePtr rpc_Pinboard(GList *args)
{
	char *name = NULL;

	name = string_value(ARG(0));
	pinboard_activate(name);
	g_free(name);

	return NULL;
}

/* args = App, [Path, Style] */
static xmlNodePtr rpc_SetBackdrop(GList *args)
{
	char *app, *path, *style;
	BackdropStyle s = BACKDROP_TILE;

	app = string_value(ARG(0));
	path = string_value(ARG(1));

	if (!path)
	{
		pinboard_set_backdrop_from_program(app, NULL, BACKDROP_NONE);
		goto out;
	}

	style = string_value(ARG(2));
	if (style)
	{
		s = !g_strcasecmp(style, "Tiled")   ? BACKDROP_TILE :
		    !g_strcasecmp(style, "Centred") ? BACKDROP_CENTRE :
		    !g_strcasecmp(style, "Scaled")  ? BACKDROP_SCALE :
						      BACKDROP_NONE;
		if (s == BACKDROP_NONE)
			g_warning("Unknown style '%s'\n", style);
		g_free(style);
	}

	pinboard_set_backdrop_from_program(app, path, s);
out:
	g_free(path);
	g_free(app);

	return NULL;
}
	
/* args = Path, X, Y, [Label] */
static xmlNodePtr rpc_PinboardAdd(GList *args)
{
	char *path = NULL;
	gchar *name;
	int x, y;

	path = string_value(ARG(0));
	x = int_value(ARG(1), 0);
	y = int_value(ARG(2), 0);
	name = string_value(ARG(3));
	
	pinboard_pin(path, name, x, y);

	g_free(path);

	return NULL;
}

/* Returns PANEL_NUMBER_OF_SIDES if name is invalid */
static PanelSide panel_name_to_side(gchar *side)
{
	if (strcmp(side, "Top") == 0)
		return PANEL_TOP;
	else if (strcmp(side, "Bottom") == 0)
		return PANEL_BOTTOM;
	else if (strcmp(side, "Left") == 0)
		return PANEL_LEFT;
	else if (strcmp(side, "Right") == 0)
		return PANEL_RIGHT;
	else
		g_warning("Unknown panel side '%s'", side);
	return PANEL_NUMBER_OF_SIDES;
}

/* args = Side, [Name] */
static xmlNodePtr rpc_Panel(GList *args)
{
	PanelSide side;
	char *name, *side_name;

	side_name = string_value(ARG(0));
	side = panel_name_to_side(side_name);
	g_free(side_name);
	g_return_val_if_fail(side != PANEL_NUMBER_OF_SIDES, NULL);

	name = string_value(ARG(1));

	panel_new(name, side);

	g_free(name);

	return NULL;
}

/* args = Side, Path, [Label, After] */
static xmlNodePtr rpc_PanelAdd(GList *args)
{
	PanelSide side;
	char *path, *side_name, *label;
	gboolean after = FALSE;
	int tmp;

	side_name = string_value(ARG(0));
	side = panel_name_to_side(side_name);
	g_free(side_name);
	g_return_val_if_fail(side != PANEL_NUMBER_OF_SIDES, NULL);

	path = string_value(ARG(1));
	label = string_value(ARG(2));

	tmp = bool_value(ARG(3));
	after = (tmp == -1) ? FALSE : tmp;

	panel_add(side, path, label, after);

	g_free(path);

	return NULL;
}

static xmlNodePtr rpc_Copy(GList *args)
{
	GList *from;
	char *to;
	char *leaf;
	int quiet;

	from = list_value(ARG(0));
	to = string_value(ARG(1));
	leaf = string_value(ARG(2));
	quiet = bool_value(ARG(3));

	if (from)
		action_copy(from, to, leaf, quiet);

	g_list_foreach(from, (GFunc) g_free, NULL);
	g_list_free(from);
	g_free(to);
	g_free(leaf);

	return NULL;
}

static xmlNodePtr rpc_Move(GList *args)
{
	GList *from;
	char *to;
	char *leaf;
	int quiet;

	from = list_value(ARG(0));
	to = string_value(ARG(1));
	leaf = string_value(ARG(2));
	quiet = bool_value(ARG(3));

	if (from)
		action_move(from, to, leaf, quiet);

	g_list_foreach(from, (GFunc) g_free, NULL);
	g_list_free(from);
	g_free(to);
	g_free(leaf);

	return NULL;
}

static xmlNodePtr rpc_Link(GList *args)
{
	GList *from;
	char *to;
	char *leaf;

	from = list_value(ARG(0));
	to = string_value(ARG(1));
	leaf = string_value(ARG(2));

	if (from)
		action_link(from, to, leaf);

	g_list_foreach(from, (GFunc) g_free, NULL);
	g_list_free(from);
	g_free(to);
	g_free(leaf);

	return NULL;
}

static xmlNodePtr rpc_FileType(GList *args)
{
	MIME_type *type;
	char *path, *tname;
	xmlNodePtr reply;

	path = string_value(ARG(0));
	type = type_get_type(path);
	g_free(path);
	
	reply = xmlNewNode(NULL, "rox:FileTypeResponse");
	tname = g_strconcat(type->media_type, "/", type->subtype, NULL);

	xmlNewNs(reply, SOAP_RPC_NS, "soap");
	xmlNewChild(reply, NULL, "soap:result", tname);
	g_free(tname);

	return reply;
}

static xmlNodePtr rpc_Mount(GList *args)
{
	GList *paths;
	int	open_dir, quiet;

	paths = list_value(ARG(0));
	open_dir = bool_value(ARG(1));
	quiet = bool_value(ARG(2));

	if (open_dir == -1)
		open_dir = TRUE;
	
	if (paths)
		action_mount(paths, open_dir, quiet);

	g_list_foreach(paths, (GFunc) g_free, NULL);
	g_list_free(paths);

	return NULL;
}

static void soap_done(GtkWidget *widget, GdkEventProperty *event, gpointer data)
{
	GdkAtom	prop = (GdkAtom) data;

	if (prop != event->atom)
		return;

	/* If we got a reply, display it here */
	if (event->state == GDK_PROPERTY_NEW_VALUE)
	{
		gint length;

		data = read_property(event->window, event->atom, &length);

		if (data)
			puts(data);
	}

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
	g_signal_connect(from, "property-notify-event",
			 G_CALLBACK(soap_done), GINT_TO_POINTER(prop));

	gdk_event_send_client_message((GdkEvent *) &event,
				      GDK_WINDOW_XWINDOW(dest));

	gtk_timeout_add(10000, too_slow, NULL);
			
	gtk_main();
}

/* Lookup this method in rpc_calls and invoke it.
 * Returns the SOAP reply or fault, or NULL if this method
 * doesn't return anything.
 */
static xmlNodePtr soap_invoke(xmlNode *method)
{
	GList *args = NULL;
	SOAP_call *call;
	gchar **arg;
	xmlNodePtr retval = NULL;
	GHashTable *name_to_node;
	xmlNode	*node;

	call = g_hash_table_lookup(rpc_calls, method->name);
	if (!call)
	{
		xmlNodePtr reply;
		gchar *err;

		err = g_strdup_printf(_("Attempt to invoke unknown SOAP "
					"method '%s'"), method->name);
		reply = xmlNewNode(NULL, "env:Fault");
		xmlNewNs(reply, SOAP_RPC_NS, "rpc");
		xmlNewNs(reply, SOAP_ENV_NS, "env");
		xmlNewChild(reply, NULL, "faultcode",
						"rpc:ProcedureNotPresent");
		xmlNewChild(reply, NULL, "faultstring", err);
		g_free(err);
		return reply;
	}

	name_to_node = g_hash_table_new(g_str_hash, g_str_equal);
	for (node = method->xmlChildrenNode; node; node = node->next)
	{
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (node->ns == NULL || strcmp(node->ns->href, ROX_NS) != 0)
			continue;

		g_hash_table_insert(name_to_node, (gchar *) node->name, node);
	}

	if (call->required_args)
	{
		for (arg = call->required_args; *arg; arg++)
		{
			node = g_hash_table_lookup(name_to_node, *arg);
			if (!node)
			{
				g_warning("Missing required argument '%s' "
					"in call to method '%s'", *arg,
					method->name);
				goto out;
			}

			args = g_list_append(args, node);
		}
	}

	if (call->optional_args)
	{
		for (arg = call->optional_args; *arg; arg++)
			args = g_list_append(args,
				g_hash_table_lookup(name_to_node, *arg));
	}

	retval = call->func(args);

out:
	g_hash_table_destroy(name_to_node);
	g_list_free(args);

	return retval;
}
