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

/* pinboard.c - icons on the desktop background */

#include "config.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <signal.h>

#include "global.h"

#include "pinboard.h"
#include "main.h"
#include "dnd.h"
#include "pixmaps.h"
#include "type.h"
#include "choices.h"
#include "support.h"
#include "gui_support.h"
#include "options.h"
#include "diritem.h"
#include "bind.h"
#include "icon.h"
#include "run.h"
#include "appinfo.h"
#include "menu.h"
#include "xml.h"
#include "tasklist.h"
#include "panel.h"		/* For panel_mark_used() */

static gboolean tmp_icon_selected = FALSE;		/* When dragging */

struct _Pinboard {
	guchar		*name;		/* Leaf name */
	GList		*icons;
	GtkStyle	*style;

	gchar		*backdrop;	/* Pathname */
	BackdropStyle	backdrop_style;
	gint		to_backdrop_app; /* pipe FD, or -1 */
	gint		from_backdrop_app; /* pipe FD, or -1 */
	gint		input_tag;
	GString		*input_buffer;

	GtkWidget	*window;	/* Screen-sized window */
	GtkWidget	*fixed;
};

#define IS_PIN_ICON(obj) G_TYPE_CHECK_INSTANCE_TYPE((obj), pin_icon_get_type())

typedef struct _PinIconClass PinIconClass;
typedef struct _PinIcon PinIcon;

struct _PinIconClass {
	IconClass parent;
};

struct _PinIcon {
	Icon		icon;

	int		x, y;
	GtkWidget	*win;
	GtkWidget	*widget;	/* The drawing area for the icon */
	GtkWidget	*label;
};

/* The number of pixels between the bottom of the image and the top
 * of the text.
 */
#define GAP 4

/* The size of the border around the icon which is used when winking */
#define WINK_FRAME 2

/* Grid sizes */
#define GRID_STEP_FINE   2
#define GRID_STEP_MED    16
#define GRID_STEP_COARSE 32

/* Used in options */
#define CORNER_TOP_LEFT 0
#define CORNER_TOP_RIGHT 1
#define CORNER_BOTTOM_LEFT 2
#define CORNER_BOTTOM_RIGHT 3

#define DIR_HORZ 0
#define DIR_VERT 1

static PinIcon	*current_wink_icon = NULL;
static gint	wink_timeout;

/* Used for the text colours (only) in the icons (and tasklist windows) */
GdkColor pin_text_fg_col, pin_text_bg_col;
PangoFontDescription *pinboard_font = NULL;	/* NULL => Gtk default */

Pinboard	*current_pinboard = NULL;
static gint	loading_pinboard = 0;		/* Non-zero => loading */

/* The Icon that was used to start the current drag, if any */
Icon *pinboard_drag_in_progress = NULL;

static Option o_pinboard_clamp_icons, o_pinboard_grid_step;
static Option o_pinboard_fg_colour, o_pinboard_bg_colour;
static Option o_pinboard_tasklist, o_forward_buttons_13;
static Option o_iconify_start, o_iconify_dir;
static Option o_label_font;

/* Static prototypes */
static GType pin_icon_get_type(void);
static void set_size_and_style(PinIcon *pi);
static gint draw_icon(GtkWidget *widget, GdkEventExpose *event, PinIcon *pi);
static gint end_wink(gpointer data);
static gboolean button_release_event(GtkWidget *widget,
			    	     GdkEventButton *event,
                            	     PinIcon *pi);
static gboolean enter_notify(GtkWidget *widget,
			     GdkEventCrossing *event,
			     PinIcon *pi);
static gboolean button_press_event(GtkWidget *widget,
			    GdkEventButton *event,
                            PinIcon *pi);
static gint icon_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       PinIcon *pi);
static const char *pin_from_file(gchar *line);
static void snap_to_grid(int *x, int *y);
static void offset_from_centre(PinIcon *pi, int *x, int *y);
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    PinIcon		*pi);
static void drag_set_pinicon_dest(PinIcon *pi);
static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       PinIcon		*pi);
static gboolean bg_drag_motion(GtkWidget	*widget,
                               GdkDragContext	*context,
                               gint		x,
                               gint		y,
                               guint		time,
			       gpointer		data);
static gboolean bg_drag_leave(GtkWidget		*widget,
			      GdkDragContext	*context,
			      guint32		time,
			      gpointer		data);
static gboolean bg_expose(GtkWidget *window,
			  GdkEventExpose *event, gpointer data);
static void drag_end(GtkWidget *widget,
			GdkDragContext *context,
			PinIcon *pi);
static void reshape_all(void);
static void pinboard_check_options(void);
static void pinboard_load_from_xml(xmlDocPtr doc);
static void pinboard_clear(void);
static void pinboard_save(void);
static PinIcon *pin_icon_new(const char *pathname, const char *name);
static void pin_icon_destroyed(PinIcon *pi);
static void pin_icon_set_tip(PinIcon *pi);
static void pinboard_show_menu(GdkEventButton *event, PinIcon *pi);
static void create_pinboard_window(Pinboard *pinboard);
static void reload_backdrop(Pinboard *pinboard,
			    const gchar *backdrop,
			    BackdropStyle backdrop_style);
static void set_backdrop(const gchar *path, BackdropStyle style);
static void pinboard_reshape_icon(Icon *icon);
static gint draw_wink(GtkWidget *widget, GdkEventExpose *event, PinIcon *pi);
static void abandon_backdrop_app(Pinboard *pinboard);
static void drag_backdrop_dropped(GtkWidget	*frame,
			      GdkDragContext    *context,
			      gint              x,
			      gint              y,
			      GtkSelectionData  *selection_data,
			      guint             drag_info,
			      guint32           time,
			      GtkWidget		*dialog);
static void backdrop_response(GtkWidget *dialog, gint response, gpointer data);
static void find_free_rect(Pinboard *pinboard, GdkRectangle *rect);
static void update_pinboard_font(void);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void pinboard_init(void)
{
	option_add_string(&o_pinboard_fg_colour, "pinboard_fg_colour", "#fff");
	option_add_string(&o_pinboard_bg_colour, "pinboard_bg_colour", "#888");
	option_add_string(&o_label_font, "label_font", "");

	option_add_int(&o_pinboard_clamp_icons, "pinboard_clamp_icons", 1);
	option_add_int(&o_pinboard_grid_step, "pinboard_grid_step",
							GRID_STEP_COARSE);
	option_add_int(&o_pinboard_tasklist, "pinboard_tasklist", TRUE);
	option_add_int(&o_forward_buttons_13, "pinboard_forward_buttons_13",
			FALSE);

	option_add_int(&o_iconify_start, "iconify_start", CORNER_TOP_RIGHT);
	option_add_int(&o_iconify_dir, "iconify_dir", DIR_VERT);

	option_add_notify(pinboard_check_options);

	gdk_color_parse(o_pinboard_fg_colour.value, &pin_text_fg_col);
	gdk_color_parse(o_pinboard_bg_colour.value, &pin_text_bg_col);
	update_pinboard_font();
}

/* Load 'pb_<pinboard>' config file from Choices (if it exists)
 * and make it the current pinboard.
 * Any existing pinned items are removed. You must call this
 * at least once before using the pinboard. NULL disables the
 * pinboard.
 */
void pinboard_activate(const gchar *name)
{
	Pinboard	*old_board = current_pinboard;
	guchar		*path, *slash;

	/* Treat an empty name the same as NULL */
	if (name && !*name)
		name = NULL;

	if (old_board)
		pinboard_clear();

	if (!name)
	{
		if (number_of_windows < 1 && gtk_main_level() > 0)
			gtk_main_quit();

		gdk_property_delete(gdk_get_default_root_window(),
				gdk_atom_intern("_XROOTPMAP_ID", FALSE));
		return;
	}

	number_of_windows++;
	
	slash = strchr(name, '/');
	if (slash)
	{
		if (access(name, F_OK))
			path = NULL;	/* File does not (yet) exist */
		else
			path = g_strdup(name);
	}
	else
	{
		guchar	*leaf;

		leaf = g_strconcat("pb_", name, NULL);
		path = choices_find_path_load(leaf, PROJECT);
		g_free(leaf);
	}

	current_pinboard = g_new(Pinboard, 1);
	current_pinboard->name = g_strdup(name);
	current_pinboard->icons = NULL;
	current_pinboard->window = NULL;
	current_pinboard->backdrop = NULL;
	current_pinboard->backdrop_style = BACKDROP_NONE;
	current_pinboard->to_backdrop_app = -1;
	current_pinboard->from_backdrop_app = -1;
	current_pinboard->input_tag = -1;
	current_pinboard->input_buffer = NULL;

	create_pinboard_window(current_pinboard);

	loading_pinboard++;
	if (path)
	{
		xmlDocPtr doc;
		doc = xmlParseFile(path);
		if (doc)
		{
			pinboard_load_from_xml(doc);
			xmlFreeDoc(doc);
			reload_backdrop(current_pinboard,
					current_pinboard->backdrop,
					current_pinboard->backdrop_style);
		}
		else
		{
			parse_file(path, pin_from_file);
			info_message(_("Your old pinboard file has been "
					"converted to the new XML format."));
			pinboard_save();
		}
		g_free(path);
	}
	else
		pinboard_pin(home_dir, "Home",
				4 + ICON_WIDTH / 2,
				4 + ICON_HEIGHT / 2);
	loading_pinboard--;

	if (o_pinboard_tasklist.int_value)
		tasklist_set_active(TRUE);
}

/* Return the window of the current pinboard, or NULL.
 * Used to make sure lowering the panels doesn't lose them...
 */
GdkWindow *pinboard_get_window(void)
{
	if (current_pinboard)
		return current_pinboard->window->window;
	return NULL;
}

const char *pinboard_get_name(void)
{
	g_return_val_if_fail(current_pinboard != NULL, NULL);

	return current_pinboard->name;
}

/* Add widget to the pinboard. Caller is responsible for coping with pinboard
 * being cleared.
 */
void pinboard_add_widget(GtkWidget *widget)
{
	GtkRequisition req;
	GdkRectangle rect;

	g_return_if_fail(current_pinboard != NULL);

	gtk_fixed_put(GTK_FIXED(current_pinboard->fixed), widget, 0, 0);

	gtk_widget_size_request(widget, &req);

	rect.width = req.width;
	rect.height = req.height;
	find_free_rect(current_pinboard, &rect);
	
	gtk_fixed_move(GTK_FIXED(current_pinboard->fixed),
			widget, rect.x, rect.y);
}

/* Add a new icon to the background.
 * 'path' should be an absolute pathname.
 * 'x' and 'y' are the coordinates of the point in the middle of the text.
 * 'name' is the name to use. If NULL then the leafname of path is used.
 *
 * name and path are in UTF-8 for Gtk+-2.0 only.
 */
void pinboard_pin(const gchar *path, const gchar *name, int x, int y)
{
	GtkWidget	*align, *vbox;
	GdkWindow	*events;
	PinIcon		*pi;
	Icon		*icon;

	g_return_if_fail(path != NULL);
	g_return_if_fail(current_pinboard != NULL);

	pi = pin_icon_new(path, name);
	icon = (Icon *) pi;
	pi->x = x;
	pi->y = y;

	/* This is a bit complicated...
	 * 
	 * An icon needs to be a NO_WINDOW widget so that the image can
	 * blend with the background (A ParentRelative window also works, but
	 * is slow, causes the xfree86's memory consumption to grow without
	 * bound, and doesn't even get freed when the filer quits!).
	 *
	 * However, the icon also needs to have a window, so we get events
	 * delivered correctly. The solution is to float an InputOnly window
	 * over the icon. Since GtkButton works the same way, we just use
	 * that :-)
	 */

	 /* Button takes the initial ref of Icon */
	pi->win = gtk_button_new();
	gtk_container_set_border_width(GTK_CONTAINER(pi->win), WINK_FRAME);
	g_signal_connect(pi->win, "expose-event", G_CALLBACK(draw_wink), pi);
	gtk_button_set_relief(GTK_BUTTON(pi->win), GTK_RELIEF_NONE);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(pi->win), vbox);

	align = gtk_alignment_new(0.5, 0.5, 0, 0);
	pi->widget = gtk_hbox_new(FALSE, 0);	/* Placeholder */
	gtk_container_add(GTK_CONTAINER(align), pi->widget);
	
	gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, TRUE, 0);
	drag_set_pinicon_dest(pi);
	g_signal_connect(pi->win, "drag_data_get",
				G_CALLBACK(drag_data_get), NULL);

	pi->label = gtk_label_new(icon->item->leafname);
	gtk_label_set_line_wrap(GTK_LABEL(pi->label), TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), pi->label, TRUE, TRUE, 0);

	gtk_fixed_put(GTK_FIXED(current_pinboard->fixed), pi->win, 0, 0);

	snap_to_grid(&x, &y);
	pi->x = x;
	pi->y = y;
	gtk_widget_show_all(pi->win);
	pinboard_reshape_icon((Icon *) pi);
	
	gtk_widget_realize(pi->win);
	events = GTK_BUTTON(pi->win)->event_window;
	gdk_window_set_events(events,
			GDK_EXPOSURE_MASK |
			GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			GDK_BUTTON1_MOTION_MASK | GDK_ENTER_NOTIFY_MASK |
			GDK_BUTTON2_MOTION_MASK | GDK_BUTTON3_MOTION_MASK);
	g_signal_connect(pi->win, "enter-notify-event",
			G_CALLBACK(enter_notify), pi);
	g_signal_connect(pi->win, "button-press-event",
			G_CALLBACK(button_press_event), pi);
	g_signal_connect(pi->win, "button-release-event",
			G_CALLBACK(button_release_event), pi);
	g_signal_connect(pi->win, "motion-notify-event",
			G_CALLBACK(icon_motion_notify), pi);
	g_signal_connect(pi->win, "expose-event",
			G_CALLBACK(draw_icon), pi);
	g_signal_connect_swapped(pi->win, "style-set",
			G_CALLBACK(pinboard_reshape_icon), pi);
	g_signal_connect_swapped(pi->win, "destroy",
			G_CALLBACK(pin_icon_destroyed), pi);

	current_pinboard->icons = g_list_prepend(current_pinboard->icons, pi);
	pin_icon_set_tip(pi);

	if (!loading_pinboard)
		pinboard_save();
}

/* Put a border around the icon, briefly.
 * If icon is NULL then cancel any existing wink.
 * The icon will automatically unhighlight unless timeout is FALSE,
 * in which case you must call this function again (with NULL or another
 * icon) to remove the highlight.
 */
static void pinboard_wink_item(PinIcon *pi, gboolean timeout)
{
	PinIcon *old = current_wink_icon;

	if (old == pi)
		return;

	current_wink_icon = pi;

	if (old)
	{
		gtk_widget_queue_draw(old->win);
		gdk_window_process_updates(old->widget->window, TRUE);

		if (wink_timeout != -1)
			gtk_timeout_remove(wink_timeout);
	}

	if (pi)
	{
		gtk_widget_queue_draw(pi->win);
		gdk_window_process_updates(pi->widget->window, TRUE);

		if (timeout)
			wink_timeout = gtk_timeout_add(300, end_wink, NULL);
		else
			wink_timeout = -1;
	}
}

/* 'app' is saved as the new application to set the backdrop. It will then be
 * run, and should communicate with the filer as described in the manual.
 */
void pinboard_set_backdrop_app(const gchar *app)
{
	XMLwrapper *ai;
	DirItem *item;
	gboolean can_set;

	item = diritem_new("");
	diritem_restat(app, item, NULL);
	if (!(item->flags & ITEM_FLAG_APPDIR))
	{
		delayed_error(_("The backdrop handler must be an application "
				"directory. Drag an application directory "
				"into the Set Backdrop dialog box, or (for "
				"programmers) pass it to the SOAP "
				"SetBackdropApp method."));
		diritem_free(item);
		return;
	}
	
	ai = appinfo_get(app, item);
	diritem_free(item);

	can_set = ai && xml_get_section(ai, ROX_NS, "CanSetBackdrop") != NULL;
	if (ai)
		g_object_unref(ai);

	if (can_set)
		set_backdrop(app, BACKDROP_PROGRAM);
	else
		delayed_error(_("You can only set the backdrop to an image "
				"or to a program which knows how to "
				"manage ROX-Filer's backdrop.\n\n"
				"Programmers: the application's AppInfo.xml "
				"must contain the CanSetBackdrop element, as "
				"described in ROX-Filer's manual."));
}

/* Open a dialog box allowing the user to set the backdrop */
void pinboard_set_backdrop(void)
{
	GtkWidget *dialog, *frame, *label, *radio, *hbox;
	GtkBox *vbox;
	GtkTargetEntry 	targets[] = {
		{"text/uri-list", 0, TARGET_URI_LIST},
	};

	dialog = gtk_dialog_new_with_buttons(_("Set backdrop"), NULL,
			GTK_DIALOG_NO_SEPARATOR,
			GTK_STOCK_CLEAR, GTK_RESPONSE_NO,
			GTK_STOCK_OK, GTK_RESPONSE_OK,
			NULL);
	vbox = GTK_BOX(GTK_DIALOG(dialog)->vbox);

	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);

	label = gtk_label_new(_("Display backdrop image:"));
	gtk_misc_set_padding(GTK_MISC(label), 4, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
	gtk_box_pack_start(vbox, label, TRUE, TRUE, 4);

	/* The Centred, Scaled, Tiled radios... */
	hbox = gtk_hbox_new(TRUE, 2);
	gtk_box_pack_start(vbox, hbox, TRUE, TRUE, 4);
	
	radio = gtk_radio_button_new_with_label(NULL, _("Centred"));
	g_object_set_data(G_OBJECT(dialog), "radio_centred", radio);
	gtk_box_pack_start(GTK_BOX(hbox), radio, FALSE, TRUE, 0);

	radio = gtk_radio_button_new_with_label(
			gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio)),
			_("Scaled"));
	g_object_set_data(G_OBJECT(dialog), "radio_scaled", radio);
	gtk_box_pack_start(GTK_BOX(hbox), radio, FALSE, TRUE, 0);

	radio = gtk_radio_button_new_with_label(
			gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio)),
			_("Tiled"));
	gtk_box_pack_start(GTK_BOX(hbox), radio, FALSE, TRUE, 0);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), TRUE);
	
	/* The drop area... */
	frame = gtk_frame_new(NULL);
	gtk_box_pack_start(vbox, frame, TRUE, TRUE, 4);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 4);

	gtk_drag_dest_set(frame, GTK_DEST_DEFAULT_ALL,
			targets, sizeof(targets) / sizeof(*targets),
			GDK_ACTION_COPY);
	g_signal_connect(frame, "drag_data_received",
			G_CALLBACK(drag_backdrop_dropped), dialog);

	label = gtk_label_new(_("Drop an image here"));
	gtk_misc_set_padding(GTK_MISC(label), 10, 20);
	gtk_container_add(GTK_CONTAINER(frame), label);

	g_signal_connect(dialog, "response",
			 G_CALLBACK(backdrop_response), NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

	gtk_widget_show_all(dialog);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void backdrop_response(GtkWidget *dialog, gint response, gpointer data)
{
	if (response == GTK_RESPONSE_NO)
		set_backdrop(NULL, BACKDROP_NONE);
			
	gtk_widget_destroy(dialog);
}

static void drag_backdrop_dropped(GtkWidget	*frame,
			      GdkDragContext    *context,
			      gint              x,
			      gint              y,
			      GtkSelectionData  *selection_data,
			      guint             drag_info,
			      guint32           time,
			      GtkWidget		*dialog)
{
	struct stat info;
	const gchar *path = NULL;
	GList *uris;

	if (!selection_data->data)
		return; 		/* Timeout? */

	uris = uri_list_to_glist(selection_data->data);

	if (g_list_length(uris) == 1)
		path = get_local_path((guchar *) uris->data);
	g_list_free(uris);

	if (!path)
	{
		delayed_error(
			_("You should drop a single (local) image file "
			"onto the drop box - that image will be "
			"used for the desktop background. You can also "
			"drag certain applications onto this box."));
		return;
	}

	if (mc_stat(path, &info))
	{
		delayed_error(
			_("Can't access '%s':\n%s"), path,
			g_strerror(errno));
		return;
	}

	if (S_ISDIR(info.st_mode))
	{
		/* Use this program to set the backdrop */
		pinboard_set_backdrop_app(path);
	}
	else if (S_ISREG(info.st_mode))
	{
		GtkWidget *radio;

		radio = g_object_get_data(G_OBJECT(dialog), "radio_scaled");
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio)))
		{
			set_backdrop(path, BACKDROP_SCALE);
			return;
		}

		radio = g_object_get_data(G_OBJECT(dialog), "radio_centred");
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio)))
		{
			set_backdrop(path, BACKDROP_CENTRE);
			return;
		}

		set_backdrop(path, BACKDROP_TILE);
	}
	else
		delayed_error(_("Only files (and certain applications) can be "
				"used to set the background image."));
}

/* Do this in the idle loop so that we don't try to put an unmanaged
 * pinboard behind a managed panel (crashes some WMs).
 */
static gboolean recreate_pinboard(gchar *name)
{
	pinboard_activate(name);
	g_free(name);

	return FALSE;
}

static void pinboard_check_options(void)
{
	GdkColor	n_fg, n_bg;

	gdk_color_parse(o_pinboard_fg_colour.value, &n_fg);
	gdk_color_parse(o_pinboard_bg_colour.value, &n_bg);

	if (o_override_redirect.has_changed && current_pinboard)
	{
		gchar *name;
		name = g_strdup(current_pinboard->name);
		pinboard_activate(NULL);
		gtk_idle_add((GtkFunction) recreate_pinboard, name);
	}

	tasklist_set_active(o_pinboard_tasklist.int_value && current_pinboard);

	if (gdk_color_equal(&n_fg, &pin_text_fg_col) == 0 ||
		gdk_color_equal(&n_bg, &pin_text_bg_col) == 0 ||
		o_label_font.has_changed)
	{
		pin_text_fg_col = n_fg;
		pin_text_bg_col = n_bg;
		update_pinboard_font();

		if (current_pinboard)
		{
			GtkWidget *w = current_pinboard->window;
			GdkColormap *cm;

			cm = gtk_widget_get_colormap(w);

			gdk_colormap_alloc_color(cm, &n_bg, FALSE, TRUE);
			gtk_widget_modify_bg(w, GTK_STATE_NORMAL, &n_bg);

			/* Only redraw the background if there is no image */
			if (!current_pinboard->backdrop)
				reload_backdrop(current_pinboard,
						NULL, BACKDROP_NONE);
			
			reshape_all();
		}

		tasklist_style_changed();
	}
}

static gint end_wink(gpointer data)
{
	pinboard_wink_item(NULL, FALSE);
	return FALSE;
}

/* Sets the appearance from the options and updates the size request of
 * the image.
 */
static void set_size_and_style(PinIcon *pi)
{
	Icon		*icon = (Icon *) pi;
	MaskedPixmap	*image = icon->item->image;
	int		iwidth = image->width;
	int		iheight = image->height;

	gtk_widget_modify_fg(pi->label, GTK_STATE_PRELIGHT, &pin_text_fg_col);
	gtk_widget_modify_bg(pi->label, GTK_STATE_PRELIGHT, &pin_text_bg_col);
	gtk_widget_modify_fg(pi->label, GTK_STATE_NORMAL, &pin_text_fg_col);
	gtk_widget_modify_bg(pi->label, GTK_STATE_NORMAL, &pin_text_bg_col);
	widget_modify_font(pi->label, pinboard_font);

	gtk_label_set_text(GTK_LABEL(pi->label), icon->item->leafname);

	gtk_widget_set_size_request(pi->widget, iwidth, iheight);
}

static gint draw_icon(GtkWidget *widget, GdkEventExpose *event, PinIcon *pi)
{
	static GtkWidgetClass *parent_class = NULL;
	Icon		*icon = (Icon *) pi;
	DirItem		*item = icon->item;
	MaskedPixmap	*image = item->image;
	int		iwidth = image->width;
	int		iheight = image->height;
	int		x, y;
	GtkStateType	state = icon->selected ? GTK_STATE_SELECTED
					       : GTK_STATE_NORMAL;
	
	if (!parent_class)
	{
		gpointer c = ((GTypeInstance *) widget)->g_class;
		parent_class = (GtkWidgetClass *) g_type_class_peek_parent(c);
	}

	x = pi->widget->allocation.x;
	y = pi->widget->allocation.y;

	gdk_pixbuf_render_to_drawable_alpha(
			icon->selected ? image->pixbuf_lit : image->pixbuf,
			pi->widget->window,
			0, 0, 				/* src */
			x, y,				/* dest */
			iwidth, iheight,
			GDK_PIXBUF_ALPHA_FULL, 128,	/* (unused) */
			GDK_RGB_DITHER_NORMAL, 0, 0);

	if (item->flags & ITEM_FLAG_SYMLINK)
	{
		gdk_pixbuf_render_to_drawable_alpha(im_symlink->pixbuf,
				pi->widget->window,
				0, 0, 				/* src */
				x, y,
				-1, -1,
				GDK_PIXBUF_ALPHA_FULL, 128,	/* (unused) */
				GDK_RGB_DITHER_NORMAL, 0, 0);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		MaskedPixmap	*mp = item->flags & ITEM_FLAG_MOUNTED
					? im_mounted
					: im_unmounted;
					
		gdk_pixbuf_render_to_drawable_alpha(mp->pixbuf,
				pi->widget->window,
				0, 0, 				/* src */
				x, y,
				-1, -1,
				GDK_PIXBUF_ALPHA_FULL, 128,	/* (unused) */
				GDK_RGB_DITHER_NORMAL, 0, 0);
	}

	if (icon->selected)
	{
		gtk_paint_flat_box(pi->label->style, pi->label->window,
				state,
				GTK_SHADOW_NONE,
				NULL, pi->label, "text",
				pi->label->allocation.x,
				pi->label->allocation.y,
				pi->label->allocation.width,
				pi->label->allocation.height);
	}

	/* Draw children */
	(parent_class->expose_event)(widget, event);

	/* Stop the button effect */
	return TRUE;
}

static gint draw_wink(GtkWidget *widget, GdkEventExpose *event, PinIcon *pi)
{
	gint x, y, width, height;

	if (current_wink_icon != pi)
		return FALSE;

	x = widget->allocation.x;
	y = widget->allocation.y;
	width = widget->allocation.width;
	height = widget->allocation.height;

	gdk_draw_rectangle(widget->window,
			pi->widget->style->white_gc,
			FALSE,
			x, y, width - 1, height - 1);
	gdk_draw_rectangle(widget->window,
			pi->widget->style->black_gc,
			FALSE,
			x + 1, y + 1, width - 3, height - 3);

	return FALSE;
}

static gboolean enter_notify(GtkWidget *widget,
			     GdkEventCrossing *event,
			     PinIcon *pi)
{
	icon_may_update((Icon *) pi);

	return FALSE;
}

static void perform_action(PinIcon *pi, GdkEventButton *event)
{
	BindAction	action;
	Icon		*icon = (Icon *) pi;
	
	action = bind_lookup_bev(pi ? BIND_PINBOARD_ICON : BIND_PINBOARD,
				 event);

	/* Actions that can happen with or without an icon */
	switch (action)
	{
		case ACT_CLEAR_SELECTION:
			icon_select_only(NULL);
			return;
		case ACT_POPUP_MENU:
			dnd_motion_ungrab();
			pinboard_show_menu(event, pi);
			return;
		case ACT_IGNORE:
			return;
		default:
			break;
	}

	g_return_if_fail(pi != NULL);

	switch (action)
	{
		case ACT_OPEN_ITEM:
			dnd_motion_ungrab();
			pinboard_wink_item(pi, TRUE);
			if (event->type == GDK_2BUTTON_PRESS)
				icon_set_selected(icon, FALSE);
			run_diritem(icon->path, icon->item, NULL, NULL, FALSE);
			break;
		case ACT_EDIT_ITEM:
			dnd_motion_ungrab();
			pinboard_wink_item(pi, TRUE);
			if (event->type == GDK_2BUTTON_PRESS)
				icon_set_selected(icon, FALSE);
			run_diritem(icon->path, icon->item, NULL, NULL, TRUE);
			break;
		case ACT_PRIME_AND_SELECT:
			if (!icon->selected)
				icon_select_only(icon);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_AND_TOGGLE:
			icon_set_selected(icon, !icon->selected);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_FOR_DND:
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_TOGGLE_SELECTED:
			icon_set_selected(icon, !icon->selected);
			break;
		case ACT_SELECT_EXCL:
			icon_select_only(icon);
			break;
		default:
			g_warning("Unsupported action : %d\n", action);
			break;
	}
}

static void forward_to_root(GdkEventButton *event)
{
	XButtonEvent xev;

	if (event->type == GDK_BUTTON_PRESS)
	{
		xev.type = ButtonPress;
		XUngrabPointer(gdk_display, event->time);
	}
	else
		xev.type = ButtonRelease;

	xev.window = gdk_x11_get_default_root_xwindow();
	xev.root =  xev.window;
	xev.subwindow = None;
	xev.time = event->time;
	xev.x = event->x_root;	/* Needed for icewm */
	xev.y = event->y_root;
	xev.x_root = event->x_root;
	xev.y_root = event->y_root;
	xev.state = event->state;
	xev.button = event->button;
	xev.same_screen = True;

	XSendEvent(gdk_display, xev.window, False,
		ButtonPressMask | ButtonReleaseMask, (XEvent *) &xev);
}

#define FORWARDED_BUTTON(pi, b) ((b) == 2 || \
	(((b) == 3 || (b) == 1) && o_forward_buttons_13.int_value && !pi))

/* pi is NULL if this is a root event */
static gboolean button_release_event(GtkWidget *widget,
			    	     GdkEventButton *event,
                            	     PinIcon *pi)
{
	if (FORWARDED_BUTTON(pi, event->button))
		forward_to_root(event);
	else if (dnd_motion_release(event))
		return TRUE;

	perform_action(pi, event);
	
	return TRUE;
}

/* pi is NULL if this is a root event */
static gboolean button_press_event(GtkWidget *widget,
			    	   GdkEventButton *event,
                            	   PinIcon *pi)
{
	/* Just in case we've jumped in front of everything... */
	gdk_window_lower(current_pinboard->window->window);
	
	if (FORWARDED_BUTTON(pi, event->button))
		forward_to_root(event);
	else if (dnd_motion_press(widget, event))
		perform_action(pi, event);

	return TRUE;
}

static void start_drag(PinIcon *pi, GdkEventMotion *event)
{
	GtkWidget *widget = pi->win;
	Icon	  *icon = (Icon *) pi;

	if (!icon->selected)
	{
		tmp_icon_selected = TRUE;
		icon_select_only(icon);
	}
	
	g_return_if_fail(icon_selection != NULL);
	
	pinboard_drag_in_progress = icon;

	if (icon_selection->next == NULL)
		drag_one_item(widget, event, icon->path, icon->item, NULL);
	else
	{
		guchar	*uri_list;

		uri_list = icon_create_uri_list();
		drag_selection(widget, event, uri_list);
		g_free(uri_list);
	}
}

/* An icon is being dragged around... */
static gint icon_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       PinIcon *pi)
{
	if (motion_state == MOTION_READY_FOR_DND)
	{
		if (dnd_motion_moved(event))
			start_drag(pi, event);
		return TRUE;
	}

	return FALSE;
}

static void backdrop_from_xml(xmlNode *node)
{
	gchar *style;

	g_free(current_pinboard->backdrop);
	current_pinboard->backdrop = xmlNodeGetContent(node);

	style = xmlGetProp(node, "style");

	if (style)
	{
		current_pinboard->backdrop_style =
			g_strcasecmp(style, "Tiled") == 0 ? BACKDROP_TILE :
			g_strcasecmp(style, "Scaled") == 0 ? BACKDROP_SCALE :
			g_strcasecmp(style, "Centred") == 0 ? BACKDROP_CENTRE :
			g_strcasecmp(style, "Program") == 0 ? BACKDROP_PROGRAM :
							     BACKDROP_NONE;
		g_free(style);
	}
	else
		current_pinboard->backdrop_style = BACKDROP_TILE;
}

/* Create one pinboard icon for each icon in the doc */
static void pinboard_load_from_xml(xmlDocPtr doc)
{
	xmlNodePtr node, root;
	char	   *tmp, *label, *path;
	int	   x, y;

	root = xmlDocGetRootElement(doc);

	for (node = root->xmlChildrenNode; node; node = node->next)
	{
		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (strcmp(node->name, "backdrop") == 0)
		{
			backdrop_from_xml(node);
			continue;
		}
		if (strcmp(node->name, "icon") != 0)
			continue;

		tmp = xmlGetProp(node, "x");
		if (!tmp)
			continue;
		x = atoi(tmp);
		g_free(tmp);

		tmp = xmlGetProp(node, "y");
		if (!tmp)
			continue;
		y = atoi(tmp);
		g_free(tmp);

		label = xmlGetProp(node, "label");
		if (!label)
			label = g_strdup("<missing label>");
		path = xmlNodeGetContent(node);
		if (!path)
			path = g_strdup("<missing path>");

		pinboard_pin(path, label, x, y);

		g_free(path);
		g_free(label);
	}
}

/* Called for each line in the pinboard file while loading a new board.
 * Only used for old-format files when converting to XML.
 */
static const char *pin_from_file(gchar *line)
{
	gchar	*leaf = NULL;
	int	x, y, n;

	if (*line == '<')
	{
		gchar	*end;

		end = strchr(line + 1, '>');
		if (!end)
			return _("Missing '>' in icon label");

		leaf = g_strndup(line + 1, end - line - 1);

		line = end + 1;

		while (isspace(*line))
			line++;
		if (*line != ',')
			return _("Missing ',' after icon label");
		line++;
	}

	if (sscanf(line, " %d , %d , %n", &x, &y, &n) < 2)
		return NULL;		/* Ignore format errors */

	pinboard_pin(line + n, leaf, x, y);

	g_free(leaf);

	return NULL;
}

/* Write the current state of the pinboard to the current pinboard file */
static void pinboard_save(void)
{
	guchar	*save = NULL;
	guchar	*save_new = NULL;
	GList	*next;
	xmlDocPtr doc = NULL;
	xmlNodePtr root;

	g_return_if_fail(current_pinboard != NULL);

	if (strchr(current_pinboard->name, '/'))
		save = g_strdup(current_pinboard->name);
	else
	{
		guchar	*leaf;

		leaf = g_strconcat("pb_", current_pinboard->name, NULL);
		save = choices_find_path_save(leaf, PROJECT, TRUE);
		g_free(leaf);
	}

	if (!save)
		return;

	doc = xmlNewDoc("1.0");
	xmlDocSetRootElement(doc, xmlNewDocNode(doc, NULL, "pinboard", NULL));

	root = xmlDocGetRootElement(doc);

	if (current_pinboard->backdrop)
	{
		BackdropStyle style = current_pinboard->backdrop_style;
		xmlNodePtr tree;

		tree = xmlNewTextChild(root, NULL, "backdrop",
				current_pinboard->backdrop);
		xmlSetProp(tree, "style",
			style == BACKDROP_TILE   ? "Tiled" :
			style == BACKDROP_CENTRE ? "Centred" :
			style == BACKDROP_SCALE  ? "Scaled" :
						   "Program");
	}

	for (next = current_pinboard->icons; next; next = next->next)
	{
		xmlNodePtr tree;
		PinIcon *pi = (PinIcon *) next->data;
		Icon	*icon = (Icon *) pi;
		char *tmp;

		tree = xmlNewTextChild(root, NULL, "icon", icon->src_path);

		tmp = g_strdup_printf("%d", pi->x);
		xmlSetProp(tree, "x", tmp);
		g_free(tmp);
		
		tmp = g_strdup_printf("%d", pi->y);
		xmlSetProp(tree, "y", tmp);
		g_free(tmp);

		xmlSetProp(tree, "label", icon->item->leafname);
	}

	save_new = g_strconcat(save, ".new", NULL);
	if (save_xml_file(doc, save_new) || rename(save_new, save))
		delayed_error(_("Error saving pinboard %s: %s"),
				save, g_strerror(errno));
	g_free(save_new);

	g_free(save);
	if (doc)
		xmlFreeDoc(doc);
}

static void snap_to_grid(int *x, int *y)
{
	int step = o_pinboard_grid_step.int_value;

	*x = ((*x + step / 2) / step) * step;
	*y = ((*y + step / 2) / step) * step;
}

/* Convert (x,y) from a centre point to a window position */
static void offset_from_centre(PinIcon *pi, int *x, int *y)
{
	gboolean clamp = o_pinboard_clamp_icons.int_value;
	GtkRequisition req;

	gtk_widget_size_request(pi->win, &req);

	*x -= req.width >> 1;
	*y -= req.height >> 1;
	*x = CLAMP(*x, 0, screen_width - (clamp ? req.width : 0));
	*y = CLAMP(*y, 0, screen_height - (clamp ? req.height : 0));
}

/* Same as drag_set_dest(), but for pinboard icons */
static void drag_set_pinicon_dest(PinIcon *pi)
{
	GtkObject	*obj = GTK_OBJECT(pi->win);

	make_drop_target(pi->win, 0);

	g_signal_connect(obj, "drag_motion", G_CALLBACK(drag_motion), pi);
	g_signal_connect(obj, "drag_leave", G_CALLBACK(drag_leave), pi);
	g_signal_connect(obj, "drag_end", G_CALLBACK(drag_end), pi);
}

/* Called during the drag when the mouse is in a widget registered
 * as a drop target. Returns TRUE if we can accept the drop.
 */
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    PinIcon		*pi)
{
	GdkDragAction	action = context->suggested_action;
	const char	*type = NULL;
	Icon		*icon = (Icon *) pi;
	DirItem		*item = icon->item;

	if (gtk_drag_get_source_widget(context) == widget)
		goto out;	/* Can't drag something to itself! */

	if (icon->selected)
		goto out;	/* Can't drag a selection to itself */

	type = dnd_motion_item(context, &item);

	if (!item)
		type = NULL;
out:
	/* We actually must pretend to accept the drop, even if the
	 * directory isn't writeable, so that the spring-opening
	 * thing works.
	 */

	/* Don't allow drops to non-writeable directories */
	if (o_dnd_spring_open.int_value == FALSE &&
			type == drop_dest_dir &&
			access(icon->path, W_OK) != 0)
	{
		type = NULL;
	}

	g_dataset_set_data(context, "drop_dest_type", (gpointer) type);
	if (type)
	{
		gdk_drag_status(context, action, time);
		g_dataset_set_data_full(context, "drop_dest_path",
				g_strdup(icon->path), g_free);
		if (type == drop_dest_dir)
			dnd_spring_load(context, NULL);

		pinboard_wink_item(pi, FALSE);
	}
	else
		gdk_drag_status(context, 0, time);

	/* Always return TRUE to stop the pinboard getting the events */
	return TRUE;
}

static gboolean pinboard_shadow = FALSE;
static gint shadow_x, shadow_y;
#define SHADOW_SIZE (ICON_WIDTH)

static gboolean bg_expose(GtkWidget *widget,
			  GdkEventExpose *event, gpointer data)
{
	gpointer gclass = ((GTypeInstance *) widget)->g_class;

	/* TODO: Split large regions into smaller chunks... */

	gdk_window_begin_paint_region(widget->window, event->region);
	
	if (pinboard_shadow)
	{
		gdk_draw_rectangle(widget->window,
				widget->style->white_gc, FALSE,
				shadow_x, shadow_y,
				SHADOW_SIZE, SHADOW_SIZE);
		gdk_draw_rectangle(widget->window,
				widget->style->black_gc, FALSE,
				shadow_x + 1, shadow_y + 1,
				SHADOW_SIZE - 2, SHADOW_SIZE - 2);
	}

	((GtkWidgetClass *) gclass)->expose_event(widget, event);

	gdk_window_end_paint(widget->window);

	return TRUE;
}

/* Draw a 'shadow' under an icon being dragged, showing where
 * it will land.
 */
static void pinboard_set_shadow(gboolean on)
{
	GdkRectangle area;
	
	if (pinboard_shadow)
	{
		area.x = shadow_x;
		area.y = shadow_y;
		area.width = SHADOW_SIZE + 1;
		area.height = SHADOW_SIZE + 1;

		gdk_window_invalidate_rect(current_pinboard->window->window,
					&area, TRUE);
	}

	if (on)
	{
		int	old_x = shadow_x, old_y = shadow_y;

		gdk_window_get_pointer(current_pinboard->fixed->window,
					&shadow_x, &shadow_y, NULL);
		snap_to_grid(&shadow_x, &shadow_y);
		shadow_x -= SHADOW_SIZE / 2;
		shadow_y -= SHADOW_SIZE / 2;


		if (pinboard_shadow && shadow_x == old_x && shadow_y == old_y)
			return;

		area.x = shadow_x;
		area.y = shadow_y;
		area.width = SHADOW_SIZE + 1;
		area.height = SHADOW_SIZE + 1;

		gdk_window_invalidate_rect(current_pinboard->window->window,
					&area, TRUE);
	}

	pinboard_shadow = on;
}

/* Called when dragging some pinboard icons finishes */
void pinboard_move_icons(void)
{
	int	x = shadow_x, y = shadow_y;
	PinIcon	*pi = (PinIcon *) pinboard_drag_in_progress;
	int	width, height;

	g_return_if_fail(pi != NULL);

	x += SHADOW_SIZE / 2;
	y += SHADOW_SIZE / 2;
	snap_to_grid(&x, &y);

	if (pi->x == x && pi->y == y)
		return;

	pi->x = x;
	pi->y = y;
	gdk_drawable_get_size(pi->win->window, &width, &height);
	offset_from_centre(pi, &x, &y);

	fixed_move_fast(GTK_FIXED(current_pinboard->fixed), pi->win, x, y);

	pinboard_save();
}

static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       PinIcon		*pi)
{
	pinboard_wink_item(NULL, FALSE);
	dnd_spring_abort();
}

static gboolean bg_drag_leave(GtkWidget		*widget,
			      GdkDragContext	*context,
			      guint32		time,
			      gpointer		data)
{
	pinboard_set_shadow(FALSE);
	return TRUE;
}

static gboolean bg_drag_motion(GtkWidget	*widget,
                               GdkDragContext	*context,
                               gint		x,
                               gint		y,
                               guint		time,
			       gpointer		data)
{
	/* Dragging from the pinboard to the pinboard is not allowed */

	if (!provides(context, text_uri_list))
		return FALSE;

	pinboard_set_shadow(TRUE);
	
	gdk_drag_status(context,
			context->suggested_action == GDK_ACTION_ASK
				? GDK_ACTION_LINK : context->suggested_action,
			time);
	return TRUE;
}

static void drag_end(GtkWidget *widget,
		     GdkDragContext *context,
		     PinIcon *pi)
{
	pinboard_drag_in_progress = NULL;
	if (tmp_icon_selected)
	{
		icon_select_only(NULL);
		tmp_icon_selected = FALSE;
	}
}

/* Something which affects all the icons has changed - reshape
 * and redraw all of them.
 */
static void reshape_all(void)
{
	GList	*next;

	g_return_if_fail(current_pinboard != NULL);

	for (next = current_pinboard->icons; next; next = next->next)
	{
		Icon *icon = (Icon *) next->data;
		pinboard_reshape_icon(icon);
	}
}

/* Turns off the pinboard. Does not call gtk_main_quit. */
static void pinboard_clear(void)
{
	GList	*next;

	g_return_if_fail(current_pinboard != NULL);

	tasklist_set_active(FALSE);

	next = current_pinboard->icons;
	while (next)
	{
		PinIcon	*pi = (PinIcon *) next->data;

		next = next->next;

		gtk_widget_destroy(pi->win);
	}

	gtk_widget_destroy(current_pinboard->window);

	abandon_backdrop_app(current_pinboard);
	
	g_free(current_pinboard->name);
	g_free(current_pinboard);
	current_pinboard = NULL;

	number_of_windows--;
}

static gpointer parent_class;

static void pin_icon_destroy(Icon *icon)
{
	PinIcon *pi = (PinIcon *) icon;

	g_return_if_fail(pi->win != NULL);

	gtk_widget_destroy(pi->win);
}

static void pinboard_remove_items(void)
{
	g_return_if_fail(icon_selection != NULL);

	while (icon_selection)
		icon_destroy((Icon *) icon_selection->data);

	pinboard_save();
}

static void pin_icon_update(Icon *icon)
{
	pinboard_reshape_icon(icon);
	pinboard_save();
}

static gboolean pin_icon_same_group(Icon *icon, Icon *other)
{
	return IS_PIN_ICON(other);
}

static void pin_icon_class_init(gpointer gclass, gpointer data)
{
	IconClass *icon = (IconClass *) gclass;

	parent_class = g_type_class_peek_parent(gclass);
	
	icon->destroy = pin_icon_destroy;
	icon->redraw = pinboard_reshape_icon;
	icon->update = pin_icon_update;
	icon->remove_items = pinboard_remove_items;
	icon->same_group = pin_icon_same_group;
}

static void pin_icon_init(GTypeInstance *object, gpointer gclass)
{
}

static GType pin_icon_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (PinIconClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			pin_icon_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(PinIcon),
			0,			/* n_preallocs */
			pin_icon_init
		};

		type = g_type_register_static(icon_get_type(),
						"PinIcon", &info, 0);
	}

	return type;
}

static PinIcon *pin_icon_new(const char *pathname, const char *name)
{
	PinIcon *pi;
	Icon	  *icon;

	pi = g_object_new(pin_icon_get_type(), NULL);
	icon = (Icon *) pi;

	icon_set_path(icon, pathname, name);
	
	return pi;
}

/* Called when the window widget is somehow destroyed */
static void pin_icon_destroyed(PinIcon *pi)
{
	g_return_if_fail(pi->win != NULL);

	pi->win = NULL;

	pinboard_wink_item(NULL, FALSE);

	if (pinboard_drag_in_progress == (Icon *) pi)
		pinboard_drag_in_progress = NULL;

	if (current_pinboard)
		current_pinboard->icons =
			g_list_remove(current_pinboard->icons, pi);

	g_object_unref(pi);
}

/* Set the tooltip */
static void pin_icon_set_tip(PinIcon *pi)
{
	XMLwrapper	*ai;
	xmlNode 	*node;
	Icon		*icon = (Icon *) pi;

	g_return_if_fail(pi != NULL);

	ai = appinfo_get(icon->path, icon->item);

	if (ai && ((node = xml_get_section(ai, NULL, "Summary"))))
	{
		guchar *str;
		str = xmlNodeListGetString(node->doc,
				node->xmlChildrenNode, 1);
		if (str)
		{
			gtk_tooltips_set_tip(tooltips, pi->win, str, NULL);
			g_free(str);
		}
	}
	else
		gtk_tooltips_set_tip(tooltips, pi->widget, NULL, NULL);

	if (ai)
		g_object_unref(ai);
}

static void pinboard_show_menu(GdkEventButton *event, PinIcon *pi)
{
	int		pos[3];

	pos[0] = event->x_root;
	pos[1] = event->y_root;
	pos[2] = 1;

	icon_prepare_menu((Icon *) pi, TRUE);

	gtk_menu_popup(GTK_MENU(icon_menu), NULL, NULL,
			position_menu,
			(gpointer) pos, event->button, event->time);
}

static void create_pinboard_window(Pinboard *pinboard)
{
	GtkWidget	*win;

	g_return_if_fail(pinboard->window == NULL);

	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_style(win, gtk_widget_get_default_style());

	gtk_widget_modify_bg(win, GTK_STATE_NORMAL, &pin_text_bg_col);

	gtk_widget_set_double_buffered(win, FALSE);
	gtk_widget_set_app_paintable(win, TRUE);
	gtk_widget_set_name(win, "rox-pinboard");
	pinboard->window = win;
	pinboard->fixed = gtk_fixed_new();
	gtk_container_add(GTK_CONTAINER(win), pinboard->fixed);

	gtk_window_set_wmclass(GTK_WINDOW(win), "ROX-Pinboard", PROJECT);

	gtk_widget_set_size_request(win, screen_width, screen_height);
	gtk_widget_realize(win);
	gtk_window_move(GTK_WINDOW(win), 0, 0);
	make_panel_window(win);

	/* TODO: Use gdk function when it supports this type */
	{
		GdkAtom desktop_type;

		desktop_type = gdk_atom_intern("_NET_WM_WINDOW_TYPE_DESKTOP",
						FALSE);
		gdk_property_change(win->window,
			gdk_atom_intern("_NET_WM_WINDOW_TYPE", FALSE),
			gdk_atom_intern("ATOM", FALSE), 32,
			GDK_PROP_MODE_REPLACE, (guchar *) &desktop_type, 1);
	}

	gtk_widget_add_events(win,
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			GDK_EXPOSURE_MASK);
	g_signal_connect(win, "button-press-event",
			G_CALLBACK(button_press_event), NULL);
	g_signal_connect(win, "button-release-event",
			G_CALLBACK(button_release_event), NULL);
	g_signal_connect(pinboard->fixed, "expose_event",
			G_CALLBACK(bg_expose), NULL);

	/* Drag and drop handlers */
	drag_set_pinboard_dest(win);
	g_signal_connect(win, "drag_motion", G_CALLBACK(bg_drag_motion), NULL);
	g_signal_connect(win, "drag_leave", G_CALLBACK(bg_drag_leave), NULL);

	gtk_widget_show_all(win);
	gdk_window_lower(win->window);
}

/* Load image 'path' and scale according to 'style' */
static GdkPixmap *load_backdrop(const gchar *path, BackdropStyle style)
{
	GdkPixmap *pixmap;
	GdkPixbuf *pixbuf;
	GError *error = NULL;

	pixbuf = gdk_pixbuf_new_from_file(path, &error);
	if (error)
	{
		delayed_error(_("Error loading backdrop image:\n%s\n"
				"Backdrop removed."),
				error->message);
		g_error_free(error);
		set_backdrop(NULL, BACKDROP_NONE);
		return NULL;
	}

	if (style == BACKDROP_SCALE)
	{
		GdkPixbuf *old = pixbuf;

		pixbuf = gdk_pixbuf_scale_simple(old,
				screen_width, screen_height,
				GDK_INTERP_HYPER);

		g_object_unref(old);
	}
	else if (style == BACKDROP_CENTRE)
	{
		GdkPixbuf *old = pixbuf;
		int	  x, y, width, height;

		width = gdk_pixbuf_get_width(pixbuf);
		height = gdk_pixbuf_get_height(pixbuf);

		pixbuf = gdk_pixbuf_new(
				gdk_pixbuf_get_colorspace(pixbuf), 0,
				8, screen_width, screen_height);
		gdk_pixbuf_fill(pixbuf, 0);

		x = (screen_width - width) / 2;
		y = (screen_height - height) / 2;
		x = MAX(x, 0);
		y = MAX(y, 0);

		gdk_pixbuf_composite(old, pixbuf,
				x, y,
				MIN(screen_width, width),
				MIN(screen_height, height),
				x, y, 1, 1,
				GDK_INTERP_NEAREST, 255);
		g_object_unref(old);
	}

	gdk_pixbuf_render_pixmap_and_mask(pixbuf,
			&pixmap, NULL, 0);
	g_object_unref(pixbuf);

	return pixmap;
}

static void abandon_backdrop_app(Pinboard *pinboard)
{
	g_return_if_fail(pinboard != NULL);

	if (pinboard->to_backdrop_app != -1)
	{
		close(pinboard->to_backdrop_app);
		close(pinboard->from_backdrop_app);
		gtk_input_remove(pinboard->input_tag);
		g_string_free(pinboard->input_buffer, TRUE);
		pinboard->to_backdrop_app = -1;
		pinboard->from_backdrop_app = -1;
		pinboard->input_tag = -1;
		pinboard->input_buffer = NULL;
	}

	g_return_if_fail(pinboard->to_backdrop_app == -1);
	g_return_if_fail(pinboard->from_backdrop_app == -1);
	g_return_if_fail(pinboard->input_tag == -1);
	g_return_if_fail(pinboard->input_buffer == NULL);
}

/* A single line has been read from the child.
 * Processes the command, and replies 'ok' (or abandons the child on error).
 */
static void command_from_backdrop_app(Pinboard *pinboard, const gchar *command)
{
	BackdropStyle style;
	const char *ok = "ok\n";

	if (strncmp(command, "tile ", 5) == 0)
	{
		style = BACKDROP_TILE;
		command += 5;
	}
	else if (strncmp(command, "scale ", 6) == 0)
	{
		style = BACKDROP_SCALE;
		command += 6;
	}
	else if (strncmp(command, "centre ", 7) == 0)
	{
		style = BACKDROP_CENTRE;
		command += 7;
	}
	else
	{
		g_warning("Invalid command '%s' from backdrop app\n",
				command);
		abandon_backdrop_app(pinboard);
		return;
	}

	/* Load the backdrop. May abandon the program if loading fails. */
	reload_backdrop(pinboard, command, style);

	if (pinboard->to_backdrop_app == -1)
		return;

	while (*ok)
	{
		int sent;
		
		sent = write(pinboard->to_backdrop_app, ok, strlen(ok));
		if (sent <= 0)
		{
			/* Remote app quit? Not an error. */
			abandon_backdrop_app(pinboard);
			break;
		}
		ok += sent;
	}
}

static void backdrop_from_child(Pinboard *pinboard,
				int src, GdkInputCondition cond)
{
	char buf[256];
	int got;

	got = read(src, buf, sizeof(buf));

	if (got <= 0)
	{
		if (got < 0)
			g_warning("backdrop_from_child: %s\n",
					g_strerror(errno));
		abandon_backdrop_app(pinboard);
		return;
	}

	g_string_append_len(pinboard->input_buffer, buf, got);

	while (pinboard->from_backdrop_app != -1)
	{
		int len;
		char *nl, *command;

		nl = strchr(pinboard->input_buffer->str, '\n');
		if (!nl)
			return;		/* Haven't got a whole line yet */

		len = nl - pinboard->input_buffer->str;
		command = g_strndup(pinboard->input_buffer->str, len);
		g_string_erase(pinboard->input_buffer, 0, len + 1);

		command_from_backdrop_app(pinboard, command);

		g_free(command);
	}
}

static void reload_backdrop(Pinboard *pinboard,
			    const gchar *backdrop,
			    BackdropStyle backdrop_style)
{
	GtkStyle *style;

	if (backdrop && backdrop_style == BACKDROP_PROGRAM)
	{
		const char *argv[] = {NULL, "--backdrop", NULL};
		GError	*error = NULL;

		g_return_if_fail(pinboard->to_backdrop_app == -1);
		g_return_if_fail(pinboard->from_backdrop_app == -1);
		g_return_if_fail(pinboard->input_tag == -1);
		g_return_if_fail(pinboard->input_buffer == NULL);

		argv[0] = make_path(backdrop, "AppRun")->str;

		/* Run the program. It'll send us a SOAP message and we'll 
		 * get back here with a different style and image.
		 */

		if (g_spawn_async_with_pipes(NULL, (gchar **) argv, NULL,
				G_SPAWN_DO_NOT_REAP_CHILD |
				G_SPAWN_SEARCH_PATH,
				NULL, NULL,		/* Child setup fn */
				NULL,			/* Child PID */
				&pinboard->to_backdrop_app,
				&pinboard->from_backdrop_app,
				NULL,			/* Standard error */
				&error))
		{
			pinboard->input_buffer = g_string_new(NULL);
			pinboard->input_tag = gtk_input_add_full(
					pinboard->from_backdrop_app,
					GDK_INPUT_READ,
					(GdkInputFunction) backdrop_from_child,
					NULL, pinboard, NULL);
		}
		else
		{
			delayed_error("%s", error ? error->message : "(null)");
			g_error_free(error);
		}
		return;
	}

	/* Note: Copying a style does not ref the pixmaps! */
	
	style = gtk_style_copy(gtk_widget_get_style(pinboard->window));
	style->bg_pixmap[GTK_STATE_NORMAL] = NULL;

	if (backdrop)
		style->bg_pixmap[GTK_STATE_NORMAL] =
			load_backdrop(backdrop, backdrop_style);

	gdk_color_parse(o_pinboard_bg_colour.value,
			&style->bg[GTK_STATE_NORMAL]);

	gtk_widget_set_style(pinboard->window, style);

	g_object_unref(style);

	gtk_widget_queue_draw(pinboard->window);

	/* Also update root window property (for transparent xterms, etc) */
	if (style->bg_pixmap[GTK_STATE_NORMAL])
	{
		XID id = GDK_DRAWABLE_XID(style->bg_pixmap[GTK_STATE_NORMAL]);
		gdk_property_change(gdk_get_default_root_window(),
				gdk_atom_intern("_XROOTPMAP_ID", FALSE),
				gdk_atom_intern("PIXMAP", FALSE),
				32, GDK_PROP_MODE_REPLACE,
				(guchar *) &id, 1);
	}
	else
	{
		gdk_property_delete(gdk_get_default_root_window(),
				gdk_atom_intern("_XROOTPMAP_ID", FALSE));
	}
}

/* Set and save (path, style) as the new backdrop.
 * If style is BACKDROP_PROGRAM, the program is run to get the backdrop.
 * Otherwise, the image is displayed now.
 */
static void set_backdrop(const gchar *path, BackdropStyle style)
{
	g_return_if_fail((path == NULL && style == BACKDROP_NONE) ||
			 (path != NULL && style != BACKDROP_NONE));

	if (!current_pinboard)
	{
		if (!path)
			return;
		pinboard_activate("Default");
		delayed_error(_("No pinboard was in use... "
			"the 'Default' pinboard has been selected. "
			"Use 'rox -p=Default' to turn it on in "
			"future."));
		g_return_if_fail(current_pinboard != NULL);
	}

	/* We might have just run the old backdrop program and now
	 * we're going to set a new one! Seems a bit mean...
	 */

	abandon_backdrop_app(current_pinboard);

	g_free(current_pinboard->backdrop);
	current_pinboard->backdrop = g_strdup(path);
	current_pinboard->backdrop_style = style;
	reload_backdrop(current_pinboard,
			current_pinboard->backdrop,
			current_pinboard->backdrop_style);
	
	pinboard_save();
}

#define SEARCH_STEP 32

static void search_free(GdkRectangle *rect, GdkRegion *used,
			int *outer, int od, int omax,
			int *inner, int id, int imax)
{
	*outer = od > 0 ? 0 : omax;
	while (*outer >= 0 && *outer <= omax)
	{
		*inner = id > 0 ? 0 : imax;
		while (*inner >= 0 && *inner <= imax)
		{
			if (gdk_region_rect_in(used, rect) ==
					GDK_OVERLAP_RECTANGLE_OUT)
				return;
			*inner += id;
		}

		*outer += od;
	}

	rect->x = -1;
	rect->y = -1;
}

/* Finds a free area on the pinboard large enough for the width and height
 * of the given rectangle, by filling in the x and y fields of 'rect'.
 * The search order respects user preferences.
 * If no area is free, returns any old area.
 */
static void find_free_rect(Pinboard *pinboard, GdkRectangle *rect)
{
	GdkRegion *used;
	GList *next;
	GdkRectangle used_rect;
	int dx = SEARCH_STEP, dy = SEARCH_STEP;
	
	used = gdk_region_new();

	panel_mark_used(used);

	/* Subtract the used areas... */

	next = GTK_FIXED(pinboard->fixed)->children;
	for (; next; next = next->next)
	{
		GtkFixedChild *fix = (GtkFixedChild *) next->data;

		if (!GTK_WIDGET_VISIBLE(fix->widget))
			continue;

		used_rect.x = fix->x;
		used_rect.y = fix->y;
		used_rect.width = fix->widget->requisition.width;
		used_rect.height = fix->widget->requisition.height;

		gdk_region_union_with_rect(used, &used_rect);
	}

	/* Find the first free area (yes, this isn't exactly pretty, but
	 * it works). If you know a better (fast!) algorithm, let me know!
	 */


	if (o_iconify_start.int_value == CORNER_TOP_RIGHT ||
	    o_iconify_start.int_value == CORNER_BOTTOM_RIGHT)
		dx = -SEARCH_STEP;

	if (o_iconify_start.int_value == CORNER_BOTTOM_LEFT ||
	    o_iconify_start.int_value == CORNER_BOTTOM_RIGHT)
		dy = -SEARCH_STEP;

	if (o_iconify_dir.int_value == DIR_VERT)
	{
		search_free(rect, used,
			    &rect->x, dx, screen_width - rect->width,
			    &rect->y, dy, screen_height - rect->height);
	}
	else
	{
		search_free(rect, used,
			    &rect->y, dy, screen_height - rect->height,
			    &rect->x, dx, screen_width - rect->width);
	}
	
	gdk_region_destroy(used);

	if (rect->x == -1)
	{
		rect->x = 0;
		rect->y = 0;
	}
}

/* Icon's size, shape or appearance has changed - update the display */
static void pinboard_reshape_icon(Icon *icon)
{
	PinIcon	*pi = (PinIcon *) icon;
	int	x = pi->x, y = pi->y;

	set_size_and_style(pi);
	offset_from_centre(pi, &x, &y);

	if (pi->win->allocation.x != x || pi->win->allocation.y != y)
	{
		fixed_move_fast(GTK_FIXED(current_pinboard->fixed),
				pi->win, x, y);
	}
}

/* Sets the pinboard_font global from the option. Doesn't do anything else. */
static void update_pinboard_font(void)
{
	if (pinboard_font)
		pango_font_description_free(pinboard_font);
	pinboard_font = o_label_font.value[0] != '\0'
			? pango_font_description_from_string(o_label_font.value)
			: NULL;
}

