/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@users.sourceforge.net>.
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

/* gui_support.c - general (GUI) support routines */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <stdarg.h>
#include <errno.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>

#include "global.h"

#include "main.h"
#include "gui_support.h"
#include "pixmaps.h"

GdkFont	   	*item_font = NULL;
GdkFont	   	*fixed_font = NULL;
GtkStyle   	*fixed_style = NULL;
gint		fixed_width;
GdkColor 	red = {0, 0xffff, 0, 0};
GdkGC		*red_gc = NULL;	/* Not automatically initialised */
gint		screen_width, screen_height;

static GdkAtom xa_cardinal;

static GtkWidget *current_dialog = NULL;

void gui_support_init()
{
	fixed_font = gdk_font_load("fixed");
	item_font = gtk_widget_get_default_style()->font;

	fixed_style = gtk_style_copy(gtk_widget_get_default_style());
	fixed_style->font = fixed_font;

	fixed_width = gdk_string_width(fixed_font, "m");

	xa_cardinal  = gdk_atom_intern("CARDINAL", FALSE);

	gdk_color_alloc(gtk_widget_get_default_colormap(), &red);

	/* This call starts returning strange values after a while, so get
	 * the result here during init.
	 */
	gdk_window_get_size(GDK_ROOT_PARENT(), &screen_width, &screen_height);
}

static void choice_clicked(GtkWidget *widget, gpointer number)
{
	int	*choice_return;

	choice_return = gtk_object_get_data(GTK_OBJECT(widget),
			"choice_return");

	if (choice_return)
		*choice_return = (int) number;
}

/* Open a modal dialog box showing a message.
 * The user can choose from a selection of buttons at the bottom.
 * Returns -1 if the window is destroyed, or the number of the button
 * if one is clicked (starting from zero).
 *
 * If a dialog is already open, returns -1 without waiting AND
 * brings the current dialog to the front.
 */
int get_choice(char *title,
	       char *message,
	       int number_of_buttons, ...)
{
	GtkWidget	*dialog;
	GtkWidget	*vbox, *action_area, *separator;
	GtkWidget	*text, *text_container;
	GtkWidget	*button = NULL, *default_button;
	int		i, retval;
	va_list	ap;
	int		choice_return;

	if (current_dialog)
	{
		gtk_widget_hide(current_dialog);
		gtk_widget_show(current_dialog);
		return -1;
	}

	current_dialog = dialog = gtk_window_new(GTK_WINDOW_DIALOG);
	GTK_WIDGET_UNSET_FLAGS(dialog, GTK_CAN_FOCUS);
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
	gtk_container_set_border_width(GTK_CONTAINER(dialog), 2);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(dialog), vbox);

	action_area = gtk_hbox_new(TRUE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(action_area), 2);
	gtk_box_pack_end(GTK_BOX(vbox), action_area, FALSE, TRUE, 0);

	separator = gtk_hseparator_new ();
	gtk_box_pack_end(GTK_BOX(vbox), separator, FALSE, TRUE, 2);

	text = gtk_label_new(message);
	gtk_label_set_line_wrap(GTK_LABEL(text), TRUE);
	text_container = gtk_event_box_new();
	gtk_container_set_border_width(GTK_CONTAINER(text_container), 40);
	gtk_container_add(GTK_CONTAINER(text_container), text);

	gtk_box_pack_start(GTK_BOX(vbox),
			text_container,
			TRUE, TRUE, 0);

	va_start(ap, number_of_buttons);

	default_button = NULL;
	for (i = 0; i < number_of_buttons; i++)
	{
		button = gtk_button_new_with_label(va_arg(ap, char *));
		GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
		gtk_misc_set_padding(GTK_MISC(GTK_BIN(button)->child), 16, 2);
		gtk_object_set_data(GTK_OBJECT(button), "choice_return",
				&choice_return);
		gtk_box_pack_start(GTK_BOX(action_area), button, TRUE, TRUE, 0);
		gtk_signal_connect(GTK_OBJECT(button), "clicked",
					choice_clicked, (gpointer) i);
		if (!default_button)
			default_button = button;
	}

	gtk_window_set_focus(GTK_WINDOW(dialog), default_button);
	gtk_window_set_default(GTK_WINDOW(dialog), default_button);
	gtk_container_set_focus_child(GTK_CONTAINER(action_area),
			default_button);

	va_end(ap);

	gtk_object_set_data(GTK_OBJECT(dialog), "choice_return",
				&choice_return);
	gtk_signal_connect(GTK_OBJECT(dialog), "destroy", choice_clicked,
			(gpointer) -1);

	choice_return = -2;

	gtk_widget_show_all(dialog);

	while (choice_return == -2)
		g_main_iteration(TRUE);

	retval = choice_return;

	if (retval != -1)
		gtk_widget_destroy(dialog);

	current_dialog = NULL;

	return retval;
}

/* Display a message in a window */
void report_error(char *title, char *message)
{
	g_return_if_fail(message != NULL);

	if (!title)
		title = _("Error");

	get_choice(title, message, 1, "OK");
}

void set_cardinal_property(GdkWindow *window, GdkAtom prop, guint32 value)
{
	gdk_property_change(window, prop, xa_cardinal, 32,
				GDK_PROP_MODE_REPLACE, (gchar *) &value, 1);
}

/* NB: Also used for pinned icons.
 * TODO: Set the level here too.
 */
void make_panel_window(GdkWindow *window)
{
	static gboolean need_init = TRUE;
	static GdkAtom	xa_state;

	if (override_redirect)
	{
		gdk_window_lower(window);
		gdk_window_set_override_redirect(window, TRUE);
		return;
	}

	if (need_init)
	{
		xa_state = gdk_atom_intern("_WIN_STATE", FALSE);
		need_init = FALSE;
	}
	
	gdk_window_set_decorations(window, 0);
	gdk_window_set_functions(window, 0);

	set_cardinal_property(window, xa_state,
			WIN_STATE_STICKY | WIN_STATE_HIDDEN |
			WIN_STATE_FIXED_POSITION | WIN_STATE_ARRANGE_IGNORE);
}

gint hide_dialog_event(GtkWidget *widget, GdkEvent *event, gpointer window)
{
	gtk_widget_hide((GtkWidget *) window);

	return TRUE;
}

static gboolean error_idle_cb(gpointer data)
{
	char	**error = (char **) data;
	
	report_error(error[0], error[1]);
	g_free(error[0]);
	g_free(error[1]);
	error[0] = error[1] = NULL;

	if (--number_of_windows == 0)
		gtk_main_quit();

	return FALSE;
}

/* Display an error next time we are idle */
void delayed_error(char *title, char *error)
{
	static char *delayed_error_data[2] = {NULL, NULL};
	gboolean already_open;
	
	g_return_if_fail(error != NULL);

	already_open = delayed_error_data[1] != NULL;

	g_free(delayed_error_data[0]);
	g_free(delayed_error_data[1]);

	delayed_error_data[0] = g_strdup(title);
	delayed_error_data[1] = g_strdup(error);
	
	if (already_open)
		return;

	gtk_idle_add(error_idle_cb, delayed_error_data);

	number_of_windows++;
}

/* Load the file into memory. Return TRUE on success.
 * Block is zero terminated (but this is not included in the length).
 */
gboolean load_file(char *pathname, char **data_out, long *length_out)
{
	FILE		*file;
	long		length;
	char		*buffer;
	gboolean 	retval = FALSE;

	file = fopen(pathname, "r");

	if (!file)
	{
		guchar	*message;

		message = g_strdup_printf("open(%s): %s",
				pathname, g_strerror(errno));
		delayed_error(PROJECT, message);
		g_free(message);
		return FALSE;
	}

	fseek(file, 0, SEEK_END);
	length = ftell(file);

	buffer = malloc(length + 1);
	if (buffer)
	{
		fseek(file, 0, SEEK_SET);
		fread(buffer, 1, length, file);

		if (ferror(file))
		{
			guchar *tmp;

			tmp = g_strdup_printf("%s: %s\n",
					pathname, g_strerror(errno));
			delayed_error(_("Error reading file"), tmp);
			g_free(tmp);
			g_free(buffer);
		}
		else
		{
			*data_out = buffer;
			*length_out = length;
			buffer[length] = '\0';
			retval = TRUE;
		}
	}
	else
		delayed_error(PROJECT,
			_("Can't allocate memory for buffer to "
				"load this file"));

	fclose(file);

	return retval;
}

GtkWidget *new_help_button(HelpFunc show_help, gpointer data)
{
	GtkWidget	*b, *icon;
	
	b = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
	icon = gtk_pixmap_new(im_help->pixmap, im_help->mask);
	gtk_container_add(GTK_CONTAINER(b), icon);
	gtk_signal_connect_object(GTK_OBJECT(b), "clicked", show_help, data);

	GTK_WIDGET_UNSET_FLAGS(b, GTK_CAN_FOCUS);

	return b;
}

/* Read file into memory. Call parse_line(guchar *line) for each line
 * in the file. Callback returns NULL on success, or an error message
 * if something went wrong. Only the first error is displayed to the user.
 */
void parse_file(char *path, ParseFunc *parse_line)
{
	char		*data;
	long		length;
	gboolean	seen_error = FALSE;

	if (load_file(path, &data, &length))
	{
		char *eol, *error;
		char *line = data;
		int  line_number = 1;

		while (line && *line)
		{
			eol = strchr(line, '\n');
			if (eol)
				*eol = '\0';

			error = parse_line(line);

			if (error && !seen_error)
			{
				GString *message;

				message = g_string_new(NULL);
				g_string_sprintf(message,
		_("Error in '%s' file at line %d: "
		"\n\"%s\"\n"
		"This may be due to upgrading from a previous version of "
		"ROX-Filer. Open the Options window and click on Save.\n"
		"Further errors will be ignored."),
					path,
					line_number,
					error);
				delayed_error(PROJECT, message->str);
				g_string_free(message, TRUE);
				seen_error = TRUE;
			}

			if (!eol)
				break;
			line = eol + 1;
			line_number++;
		}
		g_free(data);
	}
}

/* Sets up a proxy window for DnD on the specified X window.
 * Courtesy of Owen Taylor (taken from gmc).
 */
gboolean setup_xdnd_proxy(guint32 xid, GdkWindow *proxy_window)
{
	GdkAtom	xdnd_proxy_atom;
	Window	proxy_xid;
	Atom	type;
	int	format;
	unsigned long nitems, after;
	Window	*proxy_data;
	Window	proxy;
	guint32	old_warnings;

	XGrabServer(GDK_DISPLAY());

	xdnd_proxy_atom = gdk_atom_intern("XdndProxy", FALSE);
	proxy_xid = GDK_WINDOW_XWINDOW(proxy_window);
	type = None;
	proxy = None;

	old_warnings = gdk_error_warnings;

	gdk_error_code = 0;
	gdk_error_warnings = 0;

	/* Check if somebody else already owns drops on the root window */

	XGetWindowProperty(GDK_DISPLAY(), xid,
			   xdnd_proxy_atom, 0,
			   1, False, AnyPropertyType,
			   &type, &format, &nitems, &after,
			   (guchar **) &proxy_data);

	if (type != None)
	{
		if (format == 32 && nitems == 1)
			proxy = *proxy_data;

		XFree(proxy_data);
	}

	/* The property was set, now check if the window it points to exists
	 * and has a XdndProxy property pointing to itself.
	 */
	if (proxy)
	{
		XGetWindowProperty(GDK_DISPLAY(), proxy,
				    xdnd_proxy_atom, 0,
				    1, False, AnyPropertyType,
				    &type, &format, &nitems, &after,
				   (guchar **) &proxy_data);

		if (!gdk_error_code && type != None)
		{
			if (format == 32 && nitems == 1)
				if (*proxy_data != proxy)
					proxy = GDK_NONE;

			XFree(proxy_data);
		}
		else
			proxy = GDK_NONE;
	}

	if (!proxy)
	{
		/* OK, we can set the property to point to us */

		XChangeProperty(GDK_DISPLAY(), xid,
				xdnd_proxy_atom,
				gdk_atom_intern("WINDOW", FALSE),
				32, PropModeReplace,
				(guchar *) &proxy_xid, 1);
	}

	gdk_error_code = 0;
	gdk_error_warnings = old_warnings;

	XUngrabServer(GDK_DISPLAY());
	gdk_flush();

	if (!proxy)
	{
		/* Mark our window as a valid proxy window with a XdndProxy
		 * property pointing recursively;
		 */
		XChangeProperty(GDK_DISPLAY(), proxy_xid,
				xdnd_proxy_atom,
				gdk_atom_intern("WINDOW", FALSE),
				32, PropModeReplace,
				(guchar *) &proxy_xid, 1);
	}
	
	return !proxy;
}

/* xid is the window (usually the root) which points to the proxy */
void release_xdnd_proxy(guint32 xid)
{
	GdkAtom	xdnd_proxy_atom;

	xdnd_proxy_atom = gdk_atom_intern("XdndProxy", FALSE);

	XDeleteProperty(GDK_DISPLAY(), xid, xdnd_proxy_atom);
}

/* Looks for the proxy window to get root window clicks from the window
 * manager. Taken from gmc. NULL if there is no proxy window.
 */
GdkWindow *find_click_proxy_window(void)
{
	GdkAtom click_proxy_atom;
	Atom type;
	int format;
	unsigned long nitems, after;
	Window *proxy_data;
	Window proxy;
	guint32 old_warnings;
	GdkWindow *proxy_gdk_window;

	XGrabServer(GDK_DISPLAY());

	click_proxy_atom = gdk_atom_intern("_WIN_DESKTOP_BUTTON_PROXY", FALSE);
	type = None;
	proxy = None;

	old_warnings = gdk_error_warnings;

	gdk_error_code = 0;
	gdk_error_warnings = 0;

	/* Check if the proxy window exists */

	XGetWindowProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
			   click_proxy_atom, 0,
			   1, False, AnyPropertyType,
			   &type, &format, &nitems, &after,
			   (guchar **) &proxy_data);

	if (type != None)
	{
		if (format == 32 && nitems == 1)
			proxy = *proxy_data;

		XFree(proxy_data);
	}

	/* If the property was set, check if the window it points to exists
	 * and has a _WIN_DESKTOP_BUTTON_PROXY property pointing to itself.
	 */

	if (proxy)
	{
		XGetWindowProperty(GDK_DISPLAY(), proxy,
				   click_proxy_atom, 0,
				   1, False, AnyPropertyType,
				   &type, &format, &nitems, &after,
				   (guchar **) &proxy_data);

		if (!gdk_error_code && type != None)
		{
			if (format == 32 && nitems == 1)
				if (*proxy_data != proxy)
					proxy = GDK_NONE;

			XFree(proxy_data);
		}
		else
			proxy = GDK_NONE;
	}

	gdk_error_code = 0;
	gdk_error_warnings = old_warnings;

	XUngrabServer(GDK_DISPLAY());
	gdk_flush();

	if (proxy)
		proxy_gdk_window = gdk_window_foreign_new(proxy);
	else
		proxy_gdk_window = NULL;

	return proxy_gdk_window;
}

/* Returns the position of the pointer.
 * TRUE if any modifier keys or mouse buttons are pressed.
 */
gboolean get_pointer_xy(int *x, int *y)
{
	Window	root, child;
	int	win_x, win_y;
	unsigned int mask;
	
	XQueryPointer(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
			&root, &child, x, y, &win_x, &win_y, &mask);

	return mask != 0;
}

#define DECOR_BORDER 32

/* Centre the window at these coords */
void centre_window(GdkWindow *window, int x, int y)
{
	int	w, h;

	g_return_if_fail(window != NULL);

	gdk_window_get_size(window, &w, &h);
	
	x -= w / 2;
	y -= h / 2;

	gdk_window_move(window,
		CLAMP(x, DECOR_BORDER, screen_width - w - DECOR_BORDER),
		CLAMP(y, DECOR_BORDER, screen_height - h - DECOR_BORDER));
}

static void set_to_null(gpointer *data)
{
	*data = NULL;
}

/* Non-NULL => selector window is open */
static GtkColorSelectionDialog *current_csel_box = NULL;

static void get_new_colour(GtkWidget *ok, GtkWidget *button)
{
	GtkWidget	*csel;
	gdouble		c[4];
	GdkColor	colour;

	g_return_if_fail(current_csel_box != NULL);

	csel = current_csel_box->colorsel;

	gtk_color_selection_get_color(GTK_COLOR_SELECTION(csel), c);
	colour.red = c[0] * 0xffff;
	colour.green = c[1] * 0xffff;
	colour.blue = c[2] * 0xffff;

	button_patch_set_colour(button, &colour);
	
	gtk_widget_destroy(GTK_WIDGET(current_csel_box));
}

static void open_coloursel(GtkWidget *ok, GtkWidget *button)
{
	GtkColorSelectionDialog	*csel;
	GtkWidget		*dialog, *patch;
	gdouble			c[4];

	if (current_csel_box)
		gtk_widget_destroy(GTK_WIDGET(current_csel_box));

	dialog = gtk_color_selection_dialog_new(NULL);
	csel = GTK_COLOR_SELECTION_DIALOG(dialog);
	current_csel_box = csel;

	gtk_signal_connect_object(GTK_OBJECT(dialog), "destroy",
			GTK_SIGNAL_FUNC(set_to_null),
			(GtkObject *) &current_csel_box);
	gtk_widget_hide(csel->help_button);
	gtk_signal_connect_object(GTK_OBJECT(csel->cancel_button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_destroy),
			GTK_OBJECT(dialog));
	gtk_signal_connect(GTK_OBJECT(csel->ok_button), "clicked",
			GTK_SIGNAL_FUNC(get_new_colour), button);

	patch = GTK_BIN(button)->child;

	c[0] = ((gdouble) patch->style->bg[GTK_STATE_NORMAL].red) / 0xffff;
	c[1] = ((gdouble) patch->style->bg[GTK_STATE_NORMAL].green) / 0xffff;
	c[2] = ((gdouble) patch->style->bg[GTK_STATE_NORMAL].blue) / 0xffff;
	gtk_color_selection_set_color(GTK_COLOR_SELECTION(csel->colorsel), c);

	gtk_widget_show(dialog);
}

/* Turns a button (with no child) into a colour patch button.
 * When clicked, the button will open the colour selector window
 * and let the user pick a colour. Afterwards, the buttons changes
 * colour to the one chosen.
 */
void make_colour_patch(GtkWidget *button)
{
	GtkWidget	*da;

	da = gtk_drawing_area_new();
	gtk_drawing_area_size(GTK_DRAWING_AREA(da), 64, 12);
	gtk_container_add(GTK_CONTAINER(button), da);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(open_coloursel), button);
}

/* Returns a pointer to the style's background colour */
GdkColor *button_patch_get_colour(GtkWidget *button)
{
	return &(GTK_BIN(button)->child)->style->bg[GTK_STATE_NORMAL];
}

void button_patch_set_colour(GtkWidget *button, GdkColor *colour)
{
	GtkStyle   	*style;
	GtkWidget	*patch;

	patch = GTK_BIN(button)->child;

	style = gtk_style_copy(GTK_WIDGET(patch)->style);
	style->bg[GTK_STATE_NORMAL].red = colour->red;
	style->bg[GTK_STATE_NORMAL].green = colour->green;
	style->bg[GTK_STATE_NORMAL].blue = colour->blue;
	gtk_widget_set_style(patch, style);
	gtk_style_unref(style);

	if (GTK_WIDGET_REALIZED(patch))
		gdk_window_clear(patch->window);
}

static GtkWidget *current_wink_widget = NULL;
static gint	wink_timeout = -1;	/* Called when it's time to stop */
static gint	wink_destroy;		/* Called if the widget dies first */

static gboolean end_wink(gpointer data)
{
	gtk_drag_unhighlight(current_wink_widget);

	gtk_signal_disconnect(GTK_OBJECT(current_wink_widget), wink_destroy);

	current_wink_widget = NULL;

	return FALSE;
}

static void cancel_wink(void)
{
	gtk_timeout_remove(wink_timeout);
	end_wink(NULL);
}

static void wink_widget_died(gpointer data)
{
	current_wink_widget = NULL;
	gtk_timeout_remove(wink_timeout);
}

/* Draw a black box around this widget, briefly.
 * Note: uses the drag highlighting code for now.
 */
void wink_widget(GtkWidget *widget)
{
	g_return_if_fail(widget != NULL);
	
	if (current_wink_widget)
		cancel_wink();

	current_wink_widget = widget;
	gtk_drag_highlight(current_wink_widget);
	
	wink_timeout = gtk_timeout_add(300, (GtkFunction) end_wink, NULL);

	wink_destroy = gtk_signal_connect_object(GTK_OBJECT(widget), "destroy",
				GTK_SIGNAL_FUNC(wink_widget_died), NULL);
}

static gboolean idle_destroy_cb(GtkWidget *widget)
{
	gtk_widget_unref(widget);
	gtk_widget_destroy(widget);
	return FALSE;
}

/* Destroy the widget in an idle callback */
void destroy_on_idle(GtkWidget *widget)
{
	gtk_widget_ref(widget);
	gtk_idle_add((GtkFunction) idle_destroy_cb, widget);
}
