/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 1999, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
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

/* dnd.c - code for handling drag and drop */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <gtk/gtk.h>
#include "collection.h"

#include "filer.h"
#include "action.h"
#include "pixmaps.h"
#include "gui_support.h"
#include "support.h"
#include "options.h"
#include "run.h"

#define MAXURILEN 4096		/* Longest URI to allow */

/* Static prototypes */
static char *get_dest_path(FilerWindow *filer_window, GdkDragContext *context);
static void create_uri_list(GString *string,
				Collection *collection,
				FilerWindow *filer_window);
static gboolean drag_drop(GtkWidget 	*widget,
			GdkDragContext  *context,
			gint            x,
			gint            y,
			guint           time);
static gboolean provides(GdkDragContext *context, GdkAtom target);
static void set_xds_prop(GdkDragContext *context, char *text);
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time);
static void drag_leave(GtkWidget		*widget,
                           GdkDragContext	*context);
static void drag_data_received(GtkWidget      		*widget,
				GdkDragContext  	*context,
				gint            	x,
				gint            	y,
				GtkSelectionData 	*selection_data,
				guint               	info,
				guint32             	time);
static void got_data_xds_reply(GtkWidget 		*widget,
		  		GdkDragContext 		*context,
				GtkSelectionData 	*selection_data,
				guint32             	time);
static void got_data_raw(GtkWidget 		*widget,
			GdkDragContext 		*context,
			GtkSelectionData 	*selection_data,
			guint32             	time);
static GSList *uri_list_to_gslist(char *uri_list);
static void got_uri_list(GtkWidget 		*widget,
			 GdkDragContext 	*context,
			 GtkSelectionData 	*selection_data,
			 guint32             	time);
static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();
static char *load_no_hostnames(char *data);

/* Possible values for drop_dest_type (can also be NULL).
 * In either case, drop_dest_path is the app/file/dir to use.
 */
static char *drop_dest_prog = "drop_dest_prog";	/* Run a program */
static char *drop_dest_dir  = "drop_dest_dir";	/* Save to path */

static OptionsSection options =
{
	"Drag and Drop options",
	create_options,
	update_options,
	set_options,
	save_options
};

enum
{
	TARGET_RAW,
	TARGET_URI_LIST,
	TARGET_XDS,
};

GdkAtom XdndDirectSave0;
GdkAtom xa_text_plain;
GdkAtom text_uri_list;
GdkAtom application_octet_stream;

void dnd_init()
{
	XdndDirectSave0 = gdk_atom_intern("XdndDirectSave0", FALSE);
	xa_text_plain = gdk_atom_intern("text/plain", FALSE);
	text_uri_list = gdk_atom_intern("text/uri-list", FALSE);
	application_octet_stream = gdk_atom_intern("application/octet-stream",
			FALSE);

	options_sections = g_slist_prepend(options_sections, &options);
	option_register("dnd_no_hostnames", load_no_hostnames);
}

/*				OPTIONS				*/

static gboolean o_no_hostnames = FALSE;
static GtkWidget *toggle_no_hostnames;

/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options()
{
	GtkWidget	*vbox, *label;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	label = gtk_label_new("Some older applications don't support XDND "
			"fully and may need to have this option turned on. "
			"Use this if dragging files to an application shows "
			"a + sign on the pointer but the drop doesn't work.");
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);

	toggle_no_hostnames =
		gtk_check_button_new_with_label("Don't use hostnames");
	gtk_box_pack_start(GTK_BOX(vbox), toggle_no_hostnames, FALSE, TRUE, 0);

	return vbox;
}

static void update_options()
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_no_hostnames),
			o_no_hostnames);
}

static void set_options()
{
	o_no_hostnames = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_no_hostnames));
}

static void save_options()
{
	option_write("dnd_no_hostnames", o_no_hostnames ? "1" : "0");
}

static char *load_no_hostnames(char *data)
{
	o_no_hostnames = atoi(data) != 0;
	return NULL;
}

/*			SUPPORT FUNCTIONS			*/

static char *get_dest_path(FilerWindow *filer_window, GdkDragContext *context)
{
	char	*path;

	path = g_dataset_get_data(context, "drop_dest_path");

	return path ? path : filer_window->path;
}

/* Set the XdndDirectSave0 property on the source window for this context */
static void set_xds_prop(GdkDragContext *context, char *text)
{
	gdk_property_change(context->source_window,
			XdndDirectSave0,
			xa_text_plain, 8,
			GDK_PROP_MODE_REPLACE,
			text,
			strlen(text));
}

static char *get_xds_prop(GdkDragContext *context)
{
	guchar	*prop_text;
	gint	length;

	if (gdk_property_get(context->source_window,
			XdndDirectSave0,
			xa_text_plain,
			0, MAXURILEN,
			FALSE,
			NULL, NULL,
			&length, &prop_text) && prop_text)
	{
		/* Terminate the string */
		prop_text = g_realloc(prop_text, length + 1);
		prop_text[length] = '\0';
		return prop_text;
	}

	return NULL;
}

/* Is the sender willing to supply this target type? */
static gboolean provides(GdkDragContext *context, GdkAtom target)
{
	GList	    *targets = context->targets;

	while (targets && ((GdkAtom) targets->data != target))
		targets = targets->next;

	return targets != NULL;
}

/* Convert a list of URIs into a list of strings.
 * Lines beginning with # are skipped.
 * The text block passed in is zero terminated (after the final CRLF)
 */
static GSList *uri_list_to_gslist(char *uri_list)
{
	GSList   *list = NULL;

	while (*uri_list)
	{
		char	*linebreak;
		char	*uri;
		int	length;

		linebreak = strchr(uri_list, 13);

		if (!linebreak || linebreak[1] != 10)
		{
			delayed_error("uri_list_to_gslist",
					"Incorrect or missing line break "
					"in text/uri-list data");
			return list;
		}

		length = linebreak - uri_list;

		if (length && uri_list[0] != '#')
		{
			uri = g_malloc(sizeof(char) * (length + 1));
			strncpy(uri, uri_list, length);
			uri[length] = 0;
			list = g_slist_append(list, uri);
		}

		uri_list = linebreak + 2;
	}

	return list;
}

/* Append all the URIs in the selection to the string */
static void create_uri_list(GString *string,
				Collection *collection,
				FilerWindow *filer_window)
{
	GString	*leader;
	int i, num_selected;

	leader = g_string_new("file://");
	if (!o_no_hostnames)
		g_string_append(leader, our_host_name());
	g_string_append(leader, filer_window->path);
	if (leader->str[leader->len - 1] != '/')
		g_string_append_c(leader, '/');

	num_selected = collection->number_selected;

	for (i = 0; num_selected > 0; i++)
	{
		if (collection->items[i].selected)
		{
			DirItem *item = (DirItem *) collection->items[i].data;
			
			g_string_append(string, leader->str);
			g_string_append(string, item->leafname);
			g_string_append(string, "\r\n");
			num_selected--;
		}
	}

	g_string_free(leader, TRUE);
}

/*			DRAGGING FROM US			*/

/* The user has held the mouse button down over an item and moved - 
 * start a drag.
 *
 * We always provide text/uri-list. If we are dragging a single, regular file
 * then we also offer application/octet-stream and the type of the file.
 */
void drag_selection(Collection 		*collection,
		    GdkEventMotion 	*event,
		    gint		number_selected,
		    gpointer 		user_data)
{
	FilerWindow 	*filer_window = (FilerWindow *) user_data;
	GtkWidget	*widget;
	MaskedPixmap	*image;
	GdkDragContext 	*context;
	GtkTargetList   *target_list;
	GtkTargetEntry 	target_table[] =
	{
		{"text/uri-list", 0, TARGET_URI_LIST},
		{"application/octet-stream", 0, TARGET_RAW},
		{"", 0, TARGET_RAW},
	};
	DirItem	*item;

	if (number_selected == 1)
		item = selected_item(collection);
	else
		item = NULL;
	
	widget = GTK_WIDGET(collection);
	
	if (item && item->mime_type)
	{
		MIME_type *t = item->mime_type;
		
		target_table[2].target = g_strconcat(t->media_type, "/",
						     t->subtype, NULL);
		target_list = gtk_target_list_new(target_table, 3);
		g_free(target_table[2].target);
	}
	else
		target_list = gtk_target_list_new(target_table, 1);

	context = gtk_drag_begin(widget,
			target_list,
			GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK,
			(event->state & GDK_BUTTON1_MASK) ? 1 : 2,
			(GdkEvent *) event);
	g_dataset_set_data(context, "filer_window", filer_window);

	image = item ? item->image : &default_pixmap[TYPE_MULTIPLE];
	
	gtk_drag_set_icon_pixmap(context,
			gtk_widget_get_colormap(widget),
			image->pixmap,
			image->mask,
			0, 0);
}

/* Called when a remote app wants us to send it some data.
 * TODO: Maybe we should handle errors better (ie, let the remote app know
 * the drag has failed)?
 */
void drag_data_get(GtkWidget          		*widget,
			GdkDragContext     	*context,
			GtkSelectionData   	*selection_data,
			guint               	info,
			guint32             	time)
{
	char		*to_send = "E";	/* Default to sending an error */
	long		to_send_length = 1;
	gboolean	delete_once_sent = FALSE;
	GdkAtom		type = XA_STRING;
	GString		*string;
	FilerWindow 	*filer_window;
	DirItem	*item;
	
	filer_window = g_dataset_get_data(context, "filer_window");
	g_return_if_fail(filer_window != NULL);

	switch (info)
	{
		case	TARGET_RAW:
			item = selected_item(filer_window->collection);
			if (item && load_file(make_path(filer_window->path,
						item->leafname)->str,
						&to_send, &to_send_length))
			{
				delete_once_sent = TRUE;
				type = selection_data->type;
				break;
			}
			g_warning("drag_data_get: Can't find selected item\n");
			return;
		case	TARGET_URI_LIST:
			string = g_string_new(NULL);
			create_uri_list(string,
					COLLECTION(widget),
					filer_window);
			to_send = string->str;
			to_send_length = string->len;
			delete_once_sent = TRUE;
			g_string_free(string, FALSE);
			break;
		default:
			delayed_error("drag_data_get",
					"Internal error - bad info type\n");
			break;
	}

	gtk_selection_data_set(selection_data,
			type,
			8,
			to_send,
			to_send_length);

	if (delete_once_sent)
		g_free(to_send);

	collection_clear_selection(filer_window->collection);
}

/*			DRAGGING TO US				*/

/* Set up this filer window as a drop target. Called once, when the
 * filer window is first created.
 */
void drag_set_dest(GtkWidget *widget, FilerWindow *filer_window)
{
	GtkTargetEntry 	target_table[] =
	{
		{"text/uri-list", 0, TARGET_URI_LIST},
		{"XdndDirectSave0", 0, TARGET_XDS},
		{"application/octet-stream", 0, TARGET_RAW},
	};

	gtk_drag_dest_set(widget,
			0, /* GTK_DEST_DEFAULT_MOTION, */
			target_table,
			sizeof(target_table) / sizeof(*target_table),
			GDK_ACTION_COPY | GDK_ACTION_MOVE
			| GDK_ACTION_LINK | GDK_ACTION_PRIVATE);

	gtk_signal_connect(GTK_OBJECT(widget), "drag_motion",
			GTK_SIGNAL_FUNC(drag_motion), filer_window);
	gtk_signal_connect(GTK_OBJECT(widget), "drag_leave",
			GTK_SIGNAL_FUNC(drag_leave), filer_window);
	gtk_signal_connect(GTK_OBJECT(widget), "drag_drop",
			GTK_SIGNAL_FUNC(drag_drop), filer_window);
	gtk_signal_connect(GTK_OBJECT(widget), "drag_data_received",
			GTK_SIGNAL_FUNC(drag_data_received), filer_window);
}

/* Called during the drag when the mouse is in a widget registered
 * as a drop target. Returns TRUE if we can accept the drop.
 */
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time)
{
	FilerWindow 	*filer_window;
	DirItem		*item;
	int		item_number;
	GdkDragAction	action = context->suggested_action;
	char	 	*new_path = NULL;
	char		*type = NULL;

	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");
	g_return_val_if_fail(filer_window != NULL, TRUE);

	item_number = collection_get_item(filer_window->collection, x, y);

	item = item_number >= 0
		? (DirItem *) filer_window->collection->items[item_number].data
		: NULL;

	if (item && !(item->flags & (ITEM_FLAG_APPDIR | ITEM_FLAG_EXEC_FILE)))
		item = NULL;	/* Drop onto non-executable == no item */
	
	if (!item)
	{
		/* Drop onto the window background */
		collection_set_cursor_item(filer_window->collection,
				-1);

	    	if (gtk_drag_get_source_widget(context) == widget)
			goto out;

		if (access(filer_window->path, W_OK) != 0)
			goto out;	/* No write permission */

		if (filer_window->panel_type != PANEL_NO)
		{
			if (context->actions & GDK_ACTION_LINK)
			{
				action = GDK_ACTION_LINK;
				type = drop_dest_dir;
			}
		}
		else
			type = drop_dest_dir;

		if (type)
			new_path = g_strdup(filer_window->path);
	}
	else
	{
		/* Drop onto a program of some sort */
		if (!(provides(context, text_uri_list) ||
			 provides(context, application_octet_stream)))
			goto out;

		if (gtk_drag_get_source_widget(context) == widget)
		{
			Collection *collection = filer_window->collection;

			if (collection->items[item_number].selected)
				goto out;
		}
		
		/* Actually, we should probably allow any data type */
		type = drop_dest_prog;
		new_path = make_path(filer_window->path,
				item->leafname)->str;
		collection_set_cursor_item(filer_window->collection,
				item_number);
	}

out:
	g_dataset_set_data(context, "drop_dest_type", type);
	if (type)
	{
		gdk_drag_status(context, action, time);
		g_dataset_set_data_full(context, "drop_dest_path",
					g_strdup(new_path), g_free);
	}
	else
		g_free(new_path);

	return type != NULL;
}

/* Remove panel highlights */
static void drag_leave(GtkWidget		*widget,
                           GdkDragContext	*context)
{
	FilerWindow 	*filer_window;

	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");
	g_return_if_fail(filer_window != NULL);

	collection_set_cursor_item(filer_window->collection, -1);
}

/* User has tried to drop some data on us. Decide what format we would
 * like the data in.
 */
static gboolean drag_drop(GtkWidget 	*widget,
			GdkDragContext  *context,
			gint            x,
			gint            y,
			guint           time)
{
	char		*error = NULL;
	char		*leafname = NULL;
	FilerWindow	*filer_window;
	GdkAtom		target = GDK_NONE;
	char		*dest_path;
	char		*dest_type = NULL;
	
	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");
	g_return_val_if_fail(filer_window != NULL, TRUE);

	dest_path = g_dataset_get_data(context, "drop_dest_path");
	dest_type = g_dataset_get_data(context, "drop_dest_type");

	g_return_val_if_fail(dest_path != NULL, TRUE);

	if (dest_type == drop_dest_dir && provides(context, XdndDirectSave0))
	{
		leafname = get_xds_prop(context);
		if (leafname)
		{
			if (strchr(leafname, '/'))
			{
				error = "XDS protocol error: "
					"leafname may not contain '/'\n";
				g_free(leafname);

				leafname = NULL;
			}
			else
			{
				GString	*uri;

				uri = g_string_new(NULL);
				g_string_sprintf(uri, "file://%s%s",
						our_host_name(),
						make_path(dest_path,
							  leafname)->str);
				set_xds_prop(context, uri->str);
				g_string_free(uri, TRUE);

				target = XdndDirectSave0;
				g_dataset_set_data_full(context, "leafname",
						leafname, g_free);
			}
		}
		else
			error = "XdndDirectSave0 target provided, but the atom "
				"XdndDirectSave0 (type text/plain) did not "
					"contain a leafname\n";
	}
	else if (provides(context, text_uri_list))
		target = text_uri_list;
	else if (provides(context, application_octet_stream))
		target = application_octet_stream;
	else
	{
		if (dest_type == drop_dest_dir)
			error = "Sorry - I require a target type of "
				"text/uri-list or XdndDirectSave0.";
		else
			error = "Sorry - I require a target type of "
				"text/uri-list or application/octet-stream.";
	}

	if (error)
	{
		gtk_drag_finish(context, FALSE, FALSE, time);	/* Failure */
		
		delayed_error("ROX-Filer", error);
	}
	else
		gtk_drag_get_data(widget, context, target, time);

	return TRUE;
}

/* Called when some data arrives from the remote app (which we asked for
 * in drag_drop).
 */
static void drag_data_received(GtkWidget      		*widget,
				GdkDragContext  	*context,
				gint            	x,
				gint            	y,
				GtkSelectionData 	*selection_data,
				guint               	info,
				guint32             	time)
{
	if (!selection_data->data)
	{
		/* Timeout? */
		gtk_drag_finish(context, FALSE, FALSE, time);	/* Failure */
		return;
	}

	switch (info)
	{
		case TARGET_XDS:
			got_data_xds_reply(widget, context,
					selection_data, time);
			break;
		case TARGET_RAW:
			got_data_raw(widget, context, selection_data, time);
			break;
		case TARGET_URI_LIST:
			got_uri_list(widget, context, selection_data, time);
			break;
		default:
			gtk_drag_finish(context, FALSE, FALSE, time);
			delayed_error("drag_data_received", "Unknown target");
			break;
	}
}

static void got_data_xds_reply(GtkWidget 		*widget,
		  		GdkDragContext 		*context,
				GtkSelectionData 	*selection_data,
				guint32             	time)
{
	gboolean	mark_unsafe = TRUE;
	char		response = *selection_data->data;
	char		*error = NULL;

	if (selection_data->length != 1)
		response = '?';

	if (response == 'F')
	{
		/* Sender couldn't save there - ask for another
		 * type if possible.
		 */
		if (provides(context, application_octet_stream))
		{
			mark_unsafe = FALSE;	/* Wait and see */

			gtk_drag_get_data(widget, context,
					application_octet_stream, time);
		}
		else
			error = "Remote app can't or won't send me "
					"the data - sorry";
	}
	else if (response == 'S')
	{
		FilerWindow	*filer_window;

		/* Success - data is saved */
		mark_unsafe = FALSE;	/* It really is safe */
		gtk_drag_finish(context, TRUE, FALSE, time);

		filer_window = gtk_object_get_data(GTK_OBJECT(widget),
						   "filer_window");
		g_return_if_fail(filer_window != NULL);

		update_dir(filer_window, TRUE);
	}
	else if (response != 'E')
	{
		error = "XDS protocol error: "
			"return code should be 'S', 'F' or 'E'\n";
	}
	/* else: error has been reported by the sender */

	if (mark_unsafe)
	{
		set_xds_prop(context, "");
		/* Unsave also implies that the drag failed */
		gtk_drag_finish(context, FALSE, FALSE, time);
	}

	if (error)
		delayed_error("ROX-Filer", error);
}

static void got_data_raw(GtkWidget 		*widget,
			GdkDragContext 		*context,
			GtkSelectionData 	*selection_data,
			guint32             	time)
{
	FilerWindow	*filer_window;
	char		*leafname;
	int		fd;
	char		*error = NULL;
	char		*dest_path;

	g_return_if_fail(selection_data->data != NULL);

	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");
	g_return_if_fail(filer_window != NULL);
	dest_path = get_dest_path(filer_window, context);

	if (g_dataset_get_data(context, "drop_dest_type") == drop_dest_prog)
	{
		/* The data needs to be sent to an application */
		run_with_data(dest_path,
				selection_data->data, selection_data->length);
		gtk_drag_finish(context, TRUE, FALSE, time);    /* Success! */
		return;
	}

	leafname = g_dataset_get_data(context, "leafname");
	if (!leafname)
		leafname = "UntitledData";
	
	fd = open(make_path(dest_path, leafname)->str,
		O_WRONLY | O_CREAT | O_EXCL | O_NOCTTY,
			S_IRUSR | S_IRGRP | S_IROTH |
			S_IWUSR | S_IWGRP | S_IWOTH);

	if (fd == -1)
		error = g_strerror(errno);
	else
	{
		if (write(fd,
			selection_data->data,
			selection_data->length) == -1)
				error = g_strerror(errno);

		if (close(fd) == -1 && !error)
			error = g_strerror(errno);

		update_dir(filer_window, TRUE);
	}
	
	if (error)
	{
		if (provides(context, XdndDirectSave0))
			set_xds_prop(context, "");
		gtk_drag_finish(context, FALSE, FALSE, time);	/* Failure */
		delayed_error("Error saving file", error);
	}
	else
		gtk_drag_finish(context, TRUE, FALSE, time);    /* Success! */
}

/* We've got a list of URIs from somewhere (probably another filer window).
 * If the files are on the local machine then try to copy them ourselves,
 * otherwise, if there was only one file and application/octet-stream was
 * provided, get the data via the X server.
 */
static void got_uri_list(GtkWidget 		*widget,
			 GdkDragContext 	*context,
			 GtkSelectionData 	*selection_data,
			 guint32             	time)
{
	FilerWindow	*filer_window;
	GSList		*uri_list;
	char		*error = NULL;
	GSList		*next_uri;
	gboolean	send_reply = TRUE;
	char		*dest_path;
	char		*type;
	
	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");
	g_return_if_fail(filer_window != NULL);

	dest_path = get_dest_path(filer_window, context);
	type = g_dataset_get_data(context, "drop_dest_type");

	uri_list = uri_list_to_gslist(selection_data->data);

	if (!uri_list)
		error = "No URIs in the text/uri-list (nothing to do!)";
	else if (type == drop_dest_prog)
		run_with_files(dest_path, uri_list);
	else if ((!uri_list->next) && (!get_local_path(uri_list->data)))
	{
		/* There is one URI in the list, and it's not on the local
		 * machine. Get it via the X server if possible.
		 */

		if (provides(context, application_octet_stream))
		{
			char	*leaf;
			leaf = strrchr(uri_list->data, '/');
			if (leaf)
				leaf++;
			else
				leaf = uri_list->data;
			g_dataset_set_data_full(context, "leafname",
					g_strdup(leaf), g_free);
			gtk_drag_get_data(widget, context,
					application_octet_stream, time);
			send_reply = FALSE;
		}
		else
			error = "Can't get data from remote machine "
				"(application/octet-stream not provided)";
	}
	else
	{
		GSList		*local_paths = NULL;
		GSList		*next;

		/* Either one local URI, or a list. If everything in the list
		 * isn't local then we are stuck.
		 */

		for (next_uri = uri_list; next_uri; next_uri = next_uri->next)
		{
			char	*path;

			path = get_local_path((char *) next_uri->data);

			if (path)
				local_paths = g_slist_append(local_paths,
								g_strdup(path));
			else
				error = "Some of these files are on a "
					"different machine - they will be "
					"ignored - sorry";
		}

		if (!local_paths)
		{
			error = "None of these files are on the local machine "
				"- I can't operate on multiple remote files - "
				"sorry.";
		}
		else if (context->action == GDK_ACTION_MOVE)
			action_move(local_paths, dest_path);
		else if (context->action == GDK_ACTION_COPY)
			action_copy(local_paths, dest_path);
		else if (context->action == GDK_ACTION_LINK)
			action_link(local_paths, dest_path);
		else
			error = "Unknown action requested";

		for (next = local_paths; next; next = next->next)
			g_free(next->data);
		g_slist_free(local_paths);
	}

	if (error)
	{
		gtk_drag_finish(context, FALSE, FALSE, time);	/* Failure */
		delayed_error("Error getting file list", error);
	}
	else if (send_reply)
		gtk_drag_finish(context, TRUE, FALSE, time);    /* Success! */

	next_uri = uri_list;
	while (next_uri)
	{
		g_free(next_uri->data);
		next_uri = next_uri->next;
	}
	g_slist_free(uri_list);
}
