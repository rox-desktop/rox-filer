/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* dnd.c - code for handling drag and drop */

#include <stdlib.h>
#include <stdio.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <gtk/gtk.h>
#include <collection.h>

#include "filer.h"
#include "pixmaps.h"
#include "gui_support.h"
#include "support.h"

enum
{
	TARGET_RAW,
	TARGET_URI_LIST,
	TARGET_XDS,
};

/* Static prototypes */
static void create_uri_list(GString *string,
				Collection *collection,
				FilerWindow *filer_window);
static FileItem *selected_item(Collection *collection);


static FileItem *selected_item(Collection *collection)
{
	int	i;
	
	g_return_val_if_fail(collection != NULL, NULL);
	g_return_val_if_fail(IS_COLLECTION(collection), NULL);
	g_return_val_if_fail(collection->number_selected == 1, NULL);

	for (i = 0; i < collection->number_of_items; i++)
		if (collection->items[i].selected)
			return (FileItem *) collection->items[i].data;

	g_warning("selected_item: number_selected is wrong\n");

	return NULL;
}

/* The user has held the mouse button down over an item and moved - 
 * start a drag.
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
	};
	
	widget = GTK_WIDGET(collection);
	
	target_list = gtk_target_list_new(target_table,
				number_selected == 1 ? 2 : 1);

	context = gtk_drag_begin(widget,
			target_list,
			GDK_ACTION_COPY,
			(event->state & GDK_BUTTON1_MASK) ? 1 : 2,
			(GdkEvent *) event);
	g_dataset_set_data(context, "filer_window", filer_window);

	image = number_selected == 1 ? selected_item(collection)->image
				     : &default_pixmap[TYPE_MULTIPLE];
	
	gtk_drag_set_icon_pixmap(context,
			gtk_widget_get_colormap(widget),
			image->pixmap,
			image->mask,
			0, 0);
}

/* Called when a remote app wants us to send it some data */
void drag_data_get(GtkWidget          *widget,
		   GdkDragContext     *context,
		   GtkSelectionData   *selection_data,
		   guint               info,
		   guint32             time)
{
	char		*to_send = "E";	/* Default to sending an error */
	int		to_send_length = 1;
	gboolean	delete_once_sent = FALSE;
	GdkAtom		type = XA_STRING;
	GString		*string;
	FilerWindow 	*filer_window;
	
	filer_window = g_dataset_get_data(context, "filer_window");
	g_return_if_fail(filer_window != NULL);

	switch (info)
	{
		case	TARGET_RAW:
			report_error("drag_data_get", "send file contents");
			/*
			to_send = gtk_editable_get_chars(GTK_EDITABLE(text),
					0, -1);
			to_send_length = gtk_text_get_length(GTK_TEXT(text));
			delete_once_sent = TRUE;
			type = text_plain;
			*/
			break;
		case	TARGET_URI_LIST:
			string = g_string_new(NULL);
			create_uri_list(string,
					COLLECTION(widget),
					filer_window);
			to_send = string->str;
			to_send_length = string->len;
			g_string_free(string, FALSE);
			break;
		default:
			report_error("drag_data_get",
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
}

/* Append all the URIs in the selection to the string */
static void create_uri_list(GString *string,
				Collection *collection,
				FilerWindow *filer_window)
{
	GString	*leader;
	int i, num_selected;

	leader = g_string_new("file://");
	g_string_append(leader, our_host_name());
	g_string_append(leader, filer_window->path);
	if (leader->str[leader->len - 1] != '/')
		g_string_append_c(leader, '/');

	num_selected = collection->number_selected;

	for (i = 0; num_selected > 0; i++)
	{
		if (collection->items[i].selected)
		{
			FileItem *item = (FileItem *) collection->items[i].data;
			
			g_string_append(string, leader->str);
			g_string_append(string, item->leafname);
			g_string_append(string, "\r\n");
			num_selected--;
		}
	}

	g_string_free(leader, TRUE);
}
