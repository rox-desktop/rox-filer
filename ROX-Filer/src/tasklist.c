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

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "global.h"

#include "tasklist.h"
#include "options.h"
#include "gui_support.h"
#include "main.h"

/* There is one of these for each window controlled by the window
 * manager (all tasks) in the _NET_CLIENT_LIST property.
 */
typedef struct _IconWindow IconWindow;

struct _IconWindow {
	GtkWidget *widget;	/* Widget used for icon when iconified */
	gchar *text;
	Window xwindow;
	gboolean iconified;
};

static GdkAtom xa_WM_STATE = GDK_NONE;
static GdkAtom xa_WM_NAME = GDK_NONE;
static GdkAtom xa_WM_ICON_NAME = GDK_NONE;
static GdkAtom xa_UTF8_STRING = GDK_NONE;
static GdkAtom xa_TEXT = GDK_NONE;
static GdkAtom xa__NET_WM_VISIBLE_NAME = GDK_NONE;
static GdkAtom xa__NET_WM_ICON_NAME = GDK_NONE;
static GdkAtom xa__NET_CLIENT_LIST = GDK_NONE;

/* We have selected destroy and property events on every window in
 * this table.
 */
static GHashTable *known = NULL;	/* XID -> IconWindow */

static Option o_tasklist_active;

/* Static prototypes */
static void remove_window(Window win);
static void tasklist_update(gboolean to_empty);
static GdkFilterReturn window_filter(GdkXEvent *xevent,
				     GdkEvent *event,
				     gpointer data);
static guint xid_hash(XID *xid);
static gboolean xid_equal(XID *a, XID *b);
static void state_changed(IconWindow *win);
static void tasklist_check_options(void);
static void show_icon(IconWindow *win);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void tasklist_init(void)
{
	option_add_int(&o_tasklist_active, "tasklist_active", FALSE);
	option_add_notify(tasklist_check_options);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void icon_win_free(IconWindow *win)
{
	g_return_if_fail(win->widget == NULL);

	if (win->text)
		g_free(win->text);
	g_free(win);
}

static void tasklist_set_active(gboolean active)
{
	static gboolean need_init = TRUE;
	static gboolean tasklist_active = FALSE;

	if (active == tasklist_active)
		return;
	tasklist_active = active;

	if (need_init)
	{
		GdkWindow *root;

		root = gdk_get_default_root_window();

		xa_WM_STATE = gdk_atom_intern("WM_STATE", FALSE);
		xa_WM_ICON_NAME = gdk_atom_intern("WM_ICON_NAME", FALSE);
		xa_WM_NAME = gdk_atom_intern("WM_NAME", FALSE);
		xa_UTF8_STRING = gdk_atom_intern("UTF8_STRING", FALSE);
		xa_TEXT = gdk_atom_intern("TEXT", FALSE);
		xa__NET_CLIENT_LIST =
				gdk_atom_intern("_NET_CLIENT_LIST", FALSE);
		xa__NET_WM_VISIBLE_NAME =
			gdk_atom_intern("_NET_WM_VISIBLE_NAME", FALSE);
		xa__NET_WM_ICON_NAME =
			gdk_atom_intern("_NET_WM_ICON_NAME", FALSE);
		
		known = g_hash_table_new_full((GHashFunc) xid_hash,
					      (GEqualFunc) xid_equal,
					      NULL,
					      (GDestroyNotify) icon_win_free);
		gdk_window_set_events(root, gdk_window_get_events(root) |
					GDK_PROPERTY_CHANGE_MASK);
		need_init = FALSE;
	}
	
	if (active)
		gdk_window_add_filter(NULL, window_filter, NULL);
	else
		gdk_window_remove_filter(NULL, window_filter, NULL);

	tasklist_update(!active);
}

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
static GArray *get_window_list(Window xwindow, GdkAtom atom)
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

static gchar *get_str(IconWindow *win, GdkAtom atom)
{
	Atom rtype;
	int format;
	gulong nitems;
	gulong bytes_after;
	char *data, *str = NULL;

	if (XGetWindowProperty(gdk_display, win->xwindow,
			gdk_x11_atom_to_xatom(atom),
			0, G_MAXLONG, False,
			AnyPropertyType,
			&rtype, &format, &nitems,
			&bytes_after, (guchar **) &data) == Success && data)
	{
		if (*data)
			str = g_strdup(data);
		XFree(data);
	}

	return str;
}

static void get_icon_name(IconWindow *win)
{
	if (win->text)
	{
		g_free(win->text);
		win->text = NULL;
	}

	win->text = get_str(win, xa__NET_WM_ICON_NAME);
	if (!win->text)
		win->text = get_str(win, xa__NET_WM_VISIBLE_NAME);
	if (!win->text)
		win->text = get_str(win, xa_WM_ICON_NAME);
	if (!win->text)
		win->text = get_str(win, xa_WM_NAME);
	if (!win->text)
		win->text = g_strdup(_("Window"));
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
	IconWindow *w;

	if (xev->type == PropertyNotify)
	{
		GdkAtom atom = gdk_x11_xatom_to_atom(xev->xproperty.atom);
		Window win = ((XPropertyEvent *) xev)->window;

		if (atom == xa_WM_STATE)
		{
			w = g_hash_table_lookup(known, &win);
			
			if (w)
			{
				gdk_error_trap_push();
				window_check_status(w);
				if (gdk_error_trap_pop() != Success)
					g_hash_table_remove(known, &win);
			}
		}

		if (atom == xa__NET_CLIENT_LIST)
			tasklist_update(FALSE);
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
		w->text = NULL;
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
 * If 'to_empty' is set them pretend all windows have disappeared.
 */
static void tasklist_update(gboolean to_empty)
{
	static GArray *old_mapping = NULL;
	GArray *mapping = NULL;
	int new_i, old_i;

	if (!old_mapping)
	{
		old_mapping = g_array_new(FALSE, FALSE, sizeof(Window));
	}

	if (to_empty)
		mapping = g_array_new(FALSE, FALSE, sizeof(Window));
	else
		mapping = get_window_list(gdk_x11_get_default_root_xwindow(),
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

/* Called when the user clicks on the button */
static void uniconify(IconWindow *win)
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

/* If the user tries to close the icon window, attempt to uniconify the
 * window instead.
 */
static gboolean widget_delete_event(GtkWidget *widget,
		GdkEvent *event, IconWindow *win)
{
	uniconify(win);
	return TRUE;
}

static void widget_destroyed(GtkWidget *widget, IconWindow *win)
{
	if (win->widget)
	{
		/* Window has been destroyed unexpectedly */
		win->widget = NULL;
		show_icon(win);
	}
}

static gint drag_start_x = -1;
static gint drag_start_y = -1;
static gboolean drag_started = FALSE;
static gint drag_off_x = -1;
static gint drag_off_y = -1;

static void icon_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      IconWindow *win)
{
	if (event->button == 1)
	{
		drag_start_x = event->x_root;
		drag_start_y = event->y_root;
		drag_started = FALSE;

		drag_off_x = event->x;
		drag_off_y = event->y;
	}
}

static gboolean icon_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       IconWindow *win)
{
	if (event->state & GDK_BUTTON1_MASK)
	{
		int dx = event->x_root - drag_start_x;
		int dy = event->y_root - drag_start_y;

		if (!drag_started)
		{
			if (abs(dx) < 5 && abs(dy) < 5)
				return FALSE;
			drag_started = TRUE;
		}

		gdk_window_move(win->widget->window,
				event->x_root - drag_off_x,
				event->y_root - drag_off_y);
	}

	return FALSE;
}

static void button_released(GtkWidget *widget, IconWindow *win)
{
	if (!drag_started)
		uniconify(win);
}

/* A window has been iconified -- display it on the screen */
static void show_icon(IconWindow *win)
{
	GtkWidget *button;

	g_return_if_fail(win->widget == NULL);

	win->widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(win->widget, "delete-event",
			G_CALLBACK(widget_delete_event), win);
	g_signal_connect(win->widget, "destroy",
			G_CALLBACK(widget_destroyed), win);
	button = gtk_button_new_with_label(win->text);
	gtk_container_add(GTK_CONTAINER(win->widget), button);

	gtk_widget_add_events(button, GDK_BUTTON1_MOTION_MASK);
	g_signal_connect(button, "button-press-event",
			G_CALLBACK(icon_button_press), win);
	g_signal_connect(button, "motion-notify-event",
			G_CALLBACK(icon_motion_notify), win);
	g_signal_connect(button, "released", G_CALLBACK(button_released), win);
	
	gtk_widget_realize(win->widget);
	make_panel_window(win->widget);

	{
		GdkAtom dock_type;

		dock_type = gdk_atom_intern("_NET_WM_WINDOW_TYPE_DOCK",
						FALSE);
		gdk_property_change(win->widget->window,
			gdk_atom_intern("_NET_WM_WINDOW_TYPE", FALSE),
			gdk_atom_intern("ATOM", FALSE), 32,
			GDK_PROP_MODE_REPLACE, (guchar *) &dock_type, 1);
	}
	
	gtk_widget_show_all(win->widget);
}

/* A window has been destroyed/expanded -- remove its icon */
static void hide_icon(IconWindow *win)
{
	GtkWidget *widget = win->widget;

	g_return_if_fail(widget != NULL);

	win->widget = NULL;
	gtk_widget_destroy(widget);
}

static void state_changed(IconWindow *win)
{
	if (win->iconified)
	{
		get_icon_name(win);
		show_icon(win);
	}
	else
		hide_icon(win);
}

static void tasklist_check_options(void)
{
	if (o_tasklist_active.has_changed)
		tasklist_set_active(o_tasklist_active.int_value);
}
