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

/* tasklist.c - code for tracking windows
 *
 * Loosly based on code in GNOME's libwnck.
 */

#include "config.h"

#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "global.h"

#include "tasklist.h"
#include "pinboard.h"

static GdkAtom xa_WM_STATE = GDK_NONE;

/* We have selected destroy and property events on every window in
 * this table.
 */
static GHashTable *known = NULL;	/* XID -> IconWindow */

/* Static prototypes */
static void remove_window(Window win);

/* From gdk */
static guint xid_hash(XID *xid)
{
	return *xid;
}

/* From gdk */
static gboolean xid_equal(XID *a, XID *b)
{
	return (*a == *b);
}

static int wincmp(const void *a, const void *b)
{
	const Window *aw = a;
	const Window *bw = b;

	if (*aw < *bw)
		return -1;
	else if (*aw > *bw)
		return 1;
	else
		return 0;
}

/* Read the list of WINDOWs from (xwindow,atom), returning them
 * in a (sorted) Array of Windows. On error, an empty array is
 * returned.
 * Free the array afterwards.
 */
static GArray *wnck_get_window_list(Window xwindow, GdkAtom atom)
{
	GArray *array;
	Atom type;
	int format;
	gulong nitems;
	gulong bytes_after;
	Window *data;
	int err, result;
	int i;

	array = g_array_new(FALSE, FALSE, sizeof(Window));
	
	gdk_error_trap_push();
	type = None;
	result = XGetWindowProperty(gdk_display,
			xwindow,
			gdk_x11_atom_to_xatom(atom),
			0, G_MAXLONG,
			False, XA_WINDOW, &type, &format, &nitems,
			&bytes_after, (guchar **)&data);  
	err = gdk_error_trap_pop();

	if (err != Success || result != Success)
		return array;

	if (type == XA_WINDOW)
	{
		for (i = 0; i < nitems; i++)
			g_array_append_val(array, data[i]);

		g_array_sort(array, wincmp);
	}

	XFree(data);

	return array;  
}

static void state_changed(IconWindow *win)
{
	if (win->iconified)
		pinboard_add_iconified_window(win);
	else
		pinboard_remove_iconified_window(win);
}

/* Call from within error_push/pop */
static void window_check_status(IconWindow *win)
{
	Atom type;
	int format;
	gulong nitems;
	gulong bytes_after;
	gint32 *data;
	gboolean iconic;

	if (XGetWindowProperty(gdk_display, win->xwindow,
			gdk_x11_atom_to_xatom(xa_WM_STATE),
			0, 1, False,
			gdk_x11_atom_to_xatom(xa_WM_STATE),
			&type, &format, &nitems,
			&bytes_after, (guchar **) &data) == Success && data)
	{
		iconic = data[0] == 3;
		XFree(data);
	}
	else
		iconic = FALSE;

	if (win->iconified == iconic)
		return;

	win->iconified = iconic;

	state_changed(win);
}

/* Called for all events on all windows */
static GdkFilterReturn window_filter(GdkXEvent *xevent,
				     GdkEvent *event,
				     gpointer data)
{
	XEvent *xev = (XEvent *) xevent;
	Window win;
	IconWindow *w;

	if (xev->type == PropertyNotify)
	{
		GdkAtom atom = gdk_x11_xatom_to_atom(xev->xproperty.atom);

		if (atom == xa_WM_STATE)
		{
			win = ((XPropertyEvent *) xev)->window;
			w = g_hash_table_lookup(known, &win);
			
			if (w)
			{
				gdk_error_trap_push();
				window_check_status(w);
				if (gdk_error_trap_pop() != Success)
					g_hash_table_remove(known, &win);
			}
		}
	}

	return GDK_FILTER_CONTINUE;
}

/* Window has been added to list of managed windows */
static void add_window(Window win)
{
	IconWindow *w;

	/* g_print("[ New window %ld ]\n", (long) win); */

	w = g_hash_table_lookup(known, &win);

	if (!w)
	{
		XWindowAttributes attr;
		
		gdk_error_trap_push();

		XGetWindowAttributes(gdk_display, win, &attr);

		XSelectInput(gdk_display, win, attr.your_event_mask |
			PropertyChangeMask);

		if (gdk_error_trap_pop() != Success)
			return;

		w = g_new(IconWindow, 1);
		w->widget = NULL;
		w->xwindow = win;
		w->iconified = FALSE;

		g_hash_table_insert(known, &w->xwindow, w);
	}

	gdk_error_trap_push();

	window_check_status(w);

	if (gdk_error_trap_pop() != Success)
		g_hash_table_remove(known, &win);
}

/* Window is no longer managed, but hasn't been destroyed yet */
static void remove_window(Window win)
{
	IconWindow *w;

	/* g_print("[ Remove window %ld ]\n", (long) win); */

	w = g_hash_table_lookup(known, &win);
	if (w)
	{
		if (w->iconified)
		{
			w->iconified = FALSE;
			state_changed(w);
		}

		g_hash_table_remove(known, &win);
	}
}

/* Make sure the window list is up-to-date. Call once to start, and then
 * everytime _NET_CLIENT_LIST changes.
 */
void update_client_list(void)
{
	static GArray *old_mapping = NULL;
	GArray *mapping = NULL;
	int new_i, old_i;

	if (!old_mapping)
	{
		old_mapping = g_array_new(FALSE, FALSE, sizeof(Window));
		xa_WM_STATE = gdk_atom_intern("WM_STATE", FALSE);
		known = g_hash_table_new_full((GHashFunc) xid_hash,
					      (GEqualFunc) xid_equal,
					      NULL, g_free);
		gdk_window_add_filter(NULL, window_filter, NULL);
	}

	mapping = wnck_get_window_list(gdk_x11_get_default_root_xwindow(),
			gdk_atom_intern("_NET_CLIENT_LIST", FALSE));

	new_i = 0;
	old_i = 0;
	while (new_i < mapping->len && old_i < old_mapping->len)
	{
		Window new = g_array_index(mapping, Window, new_i);
		Window old = g_array_index(old_mapping, Window, old_i);

		if (new == old)
		{
			new_i++;
			old_i++;
		}
		else if (new < old)
		{
			add_window(new);
			new_i++;
		}
		else
		{
			remove_window(old);
			old_i++;
		}
	}
	while (new_i < mapping->len)
	{
		add_window(g_array_index(mapping, Window, new_i));
		new_i++;
	}
	while (old_i < old_mapping->len)
	{
		remove_window(g_array_index(old_mapping, Window, old_i));
		old_i++;
	}

	g_array_free(old_mapping, TRUE);
	old_mapping = mapping;
}

void tasklist_uniconify(IconWindow *win)
{
	XClientMessageEvent sev;

	sev.type = ClientMessage;
	sev.display = gdk_display;
	sev.format = 32;
	sev.window = win->xwindow;
	sev.message_type = gdk_x11_atom_to_xatom(
			gdk_atom_intern("_NET_ACTIVE_WINDOW", FALSE));
	sev.data.l[0] = 0;

	gdk_error_trap_push();

	XSendEvent(gdk_display, DefaultRootWindow(gdk_display), False,
			SubstructureNotifyMask | SubstructureRedirectMask,
			(XEvent *) &sev);
	XSync (gdk_display, False);

	gdk_error_trap_pop();
}
