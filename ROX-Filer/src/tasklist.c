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

#include <stdlib.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "global.h"

#include "tasklist.h"
#include "wrapped.h"
#include "options.h"
#include "gui_support.h"
#include "main.h"
#include "pinboard.h"
#include "pixmaps.h"

/* There is one of these for each window controlled by the window
 * manager (all tasks) in the _NET_CLIENT_LIST property.
 */
typedef struct _IconWindow IconWindow;

struct _IconWindow {
	GtkWidget *widget;	/* Widget used for icon when iconified */
	GtkWidget *label;
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
static GdkAtom xa__NET_WM_ICON_GEOMETRY = GDK_NONE;

/* We have selected destroy and property events on every window in
 * this table.
 */
static GHashTable *known = NULL;	/* XID -> IconWindow */

/* Static prototypes */
static void remove_window(Window win);
static void tasklist_update(gboolean to_empty);
static GdkFilterReturn window_filter(GdkXEvent *xevent,
				     GdkEvent *event,
				     gpointer data);
static guint xid_hash(XID *xid);
static gboolean xid_equal(XID *a, XID *b);
static void state_changed(IconWindow *win);
static void show_icon(IconWindow *win);
static void icon_win_free(IconWindow *win);
static void update_style(gpointer key, gpointer data, gpointer user_data);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void tasklist_set_active(gboolean active)
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
		xa__NET_WM_ICON_GEOMETRY =
			gdk_atom_intern("_NET_WM_ICON_GEOMETRY", FALSE);
		
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

/* User has changes the colours in the options box... */
void tasklist_style_changed(void)
{
	g_hash_table_foreach(known, update_style, NULL);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void icon_win_free(IconWindow *win)
{
	g_return_if_fail(win->widget == NULL);
	g_return_if_fail(win->label == NULL);

	if (win->text)
		g_free(win->text);
	g_free(win);
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

		if (array->len)
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

		if (gdk_error_trap_pop() != Success)
			return;
		gdk_error_trap_push();

		XSelectInput(gdk_display, win, attr.your_event_mask |
			PropertyChangeMask);
		gdk_flush();

		if (gdk_error_trap_pop() != Success)
			return;

		w = g_new(IconWindow, 1);
		w->widget = NULL;
		w->label = NULL;
		w->text = NULL;
		w->xwindow = win;
		w->iconified = FALSE;

		g_hash_table_insert(known, &w->xwindow, w);
	}

	gdk_error_trap_push();

	window_check_status(w);

#if 0
	set_iconify_pos(w);
#endif

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

		fixed_move_fast(GTK_FIXED(win->widget->parent),
				win->widget,
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

static GdkColormap* get_cmap(GdkPixmap *pixmap)
{
	GdkColormap *cmap;

	cmap = gdk_drawable_get_colormap(pixmap);
	if (cmap)
		g_object_ref(G_OBJECT(cmap));

	if (cmap == NULL)
	{
		if (gdk_drawable_get_depth(pixmap) == 1)
		{
			/* Masks don't need colourmaps */
			cmap = NULL;
		}
		else
		{
			/* Try system cmap */
			cmap = gdk_colormap_get_system();
			g_object_ref(G_OBJECT(cmap));
		}
	}

	/* Be sure we aren't going to blow up due to visual mismatch */
	if (cmap && (gdk_colormap_get_visual(cmap)->depth !=
			 gdk_drawable_get_depth(pixmap)))
		cmap = NULL;

	return cmap;
}

/* Copy a pixmap from the server to a client-side pixbuf */
static GdkPixbuf* pixbuf_from_pixmap(Pixmap xpixmap)
{
	GdkDrawable *drawable;
	GdkPixbuf *retval;
	GdkColormap *cmap;
	int width, height;

	retval = NULL;

	drawable = gdk_xid_table_lookup(xpixmap);

	if (drawable)
		g_object_ref(G_OBJECT(drawable));
	else
		drawable = gdk_pixmap_foreign_new(xpixmap);

	cmap = get_cmap(drawable);

	/* GDK is supposed to do this but doesn't in GTK 2.0.2,
	 * fixed in 2.0.3
	 */
	gdk_drawable_get_size(drawable, &width, &height);

	retval = gdk_pixbuf_get_from_drawable(NULL, drawable, cmap,
						0, 0, 0, 0, width, height);

	if (cmap)
		g_object_unref(G_OBJECT(cmap));
	g_object_unref(G_OBJECT(drawable));

	return retval;
}

/* Creates a new masked pixbuf from a non-masked pixbuf and a mask */
static GdkPixbuf* apply_mask(GdkPixbuf *pixbuf, GdkPixbuf *mask)
{
	int w, h;
	int i, j;
	GdkPixbuf *with_alpha;
	guchar *src;
	guchar *dest;
	int src_stride;
	int dest_stride;

	w = MIN(gdk_pixbuf_get_width(mask), gdk_pixbuf_get_width(pixbuf));
	h = MIN(gdk_pixbuf_get_height(mask), gdk_pixbuf_get_height(pixbuf));

	with_alpha = gdk_pixbuf_add_alpha(pixbuf, FALSE, 0, 0, 0);

	dest = gdk_pixbuf_get_pixels(with_alpha);
	src = gdk_pixbuf_get_pixels(mask);

	dest_stride = gdk_pixbuf_get_rowstride(with_alpha);
	src_stride = gdk_pixbuf_get_rowstride(mask);

	i = 0;
	while (i < h)
	{
		j = 0;
		while (j < w)
		{
			guchar *s = src + i * src_stride + j * 3;
			guchar *d = dest + i * dest_stride + j * 4;

			/* s[0] == s[1] == s[2], they are 255 if the bit was
			 * set, 0 otherwise
			 */
			if (s[0] == 0)
				d[3] = 0;   /* transparent */
			else
				d[3] = 255; /* opaque */

			++j;
		}

		++i;
	}

	return with_alpha;
}

/* Return a suitable icon for this window. unref the result.
 * Never returns NULL.
 */
static GdkPixbuf *get_image_for(IconWindow *win)
{
	static MaskedPixmap *default_icon = NULL;
	Pixmap pixmap = None;
	Pixmap mask = None;
	XWMHints *hints;
	GdkPixbuf *retval = NULL;

	/* Try the pixmap and mask in the old WMHints... */
	gdk_error_trap_push();
	hints = XGetWMHints(gdk_display, win->xwindow);

	if (hints)
	{
		if (hints->flags & IconPixmapHint)
			pixmap = hints->icon_pixmap;
		if (hints->flags & IconMaskHint)
			mask = hints->icon_mask;

		XFree(hints);
		hints = NULL;
	}

	if (pixmap != None)
	{
		GdkPixbuf *mask_pb = NULL;
		
		retval = pixbuf_from_pixmap(pixmap);

		if (retval && mask != None)
			mask_pb = pixbuf_from_pixmap(mask);

		if (mask_pb)
		{
			GdkPixbuf *masked;

			masked = apply_mask(retval, mask_pb);
			g_object_unref(G_OBJECT(mask_pb));

			if (masked)
			{
				g_object_unref(G_OBJECT(retval));
				retval = masked;
			}
		}
	}

	gdk_flush();

	gdk_error_trap_pop();
	
	if (!retval)
	{
		if (!default_icon)
			default_icon = load_pixmap("images/iconified.png");

		retval = default_icon->pixbuf;
		g_object_ref(retval);
	}

	return retval;
}

/* A window has been iconified -- display it on the screen */
static void show_icon(IconWindow *win)
{
	GdkPixbuf *pixbuf;
	GtkWidget *vbox;

	g_return_if_fail(win->widget == NULL);
	g_return_if_fail(win->label == NULL);

	win->widget = gtk_button_new();
	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(win->widget), vbox);

	pixbuf = get_image_for(win);

	gtk_box_pack_start(GTK_BOX(vbox), gtk_image_new_from_pixbuf(pixbuf),
			   FALSE, TRUE, 0);
	g_object_unref(pixbuf);

	gtk_button_set_relief(GTK_BUTTON(win->widget), GTK_RELIEF_NONE);
		
	win->label = wrapped_label_new(win->text, 180);

	update_style(NULL, win, NULL);

	gtk_box_pack_start(GTK_BOX(vbox), win->label, FALSE, TRUE, 0);

	gtk_widget_add_events(win->widget, GDK_BUTTON1_MOTION_MASK);
	g_signal_connect(win->widget, "button-press-event",
			G_CALLBACK(icon_button_press), win);
	g_signal_connect(win->widget, "motion-notify-event",
			G_CALLBACK(icon_motion_notify), win);
	g_signal_connect(win->widget, "released",
			G_CALLBACK(button_released), win);
	
	gtk_widget_show_all(vbox);	/* So the size comes out right */
	pinboard_add_widget(win->widget);
	gtk_widget_show(win->widget);
}

/* A window has been destroyed/expanded -- remove its icon */
static void hide_icon(IconWindow *win)
{
	GtkWidget *widget = win->widget;

	g_return_if_fail(widget != NULL);

	win->widget = NULL;
	win->label = NULL;
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

#if 0
/* Set the _NET_WM_ICON_GEOMETRY property, which indicates where this window
 * will be iconified to. Should be inside a push/pop.
 */
static void set_iconify_pos(IconWindow *win)
{
	gint32 data[4];

	data[0] = iconify_next_x;
	data[1] = iconify_next_y;
	data[2] = 100;
	data[3] = 32;

	XChangeProperty(gdk_display, win->xwindow,
			gdk_x11_atom_to_xatom(xa__NET_WM_ICON_GEOMETRY),
			XA_CARDINAL, 32, PropModeReplace, (guchar *) data, 4);
}
#endif

static void update_style(gpointer key, gpointer data, gpointer user_data)
{
	IconWindow *win = (IconWindow *) data;

	if (win->widget)
	{
		gtk_widget_modify_fg(win->label,
					GTK_STATE_NORMAL, &pin_text_fg_col);
		gtk_widget_modify_bg(win->label,
					GTK_STATE_NORMAL, &pin_text_bg_col);
	}
}

