/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* dnd.c - code for handling drag and drop */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

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
static char *get_local_path(char *uri);
static void run_with_files(char *path, GSList *uri_list);
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
GdkAtom text_plain;
GdkAtom text_uri_list;
GdkAtom application_octet_stream;

void dnd_init()
{
	XdndDirectSave0 = gdk_atom_intern("XdndDirectSave0", FALSE);
	text_plain = gdk_atom_intern("text/plain", FALSE);
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
			text_plain, 8,
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
			text_plain,
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

/* Convert a URI to a local pathname (or NULL if it isn't local).
 * The returned pointer points inside the input string.
 * Possible formats:
 *	/path
 *	///path
 *	//host/path
 *	file://host/path
 */
static char *get_local_path(char *uri)
{
	char	*host;

	host = our_host_name();

	if (*uri == '/')
	{
		char    *path;

		if (uri[1] != '/')
			return uri;	/* Just a local path - no host part */

		path = strchr(uri + 2, '/');
		if (!path)
			return NULL;	    /* //something */

		if (path - uri == 2)
			return path;	/* ///path */
		if (strlen(host) == path - uri - 2 &&
			strncmp(uri + 2, host, path - uri - 2) == 0)
			return path;	/* //myhost/path */

		return NULL;	    /* From a different host */
	}
	else
	{
		if (strncasecmp(uri, "file:", 5))
			return NULL;	    /* Don't know this format */

		uri += 5;

		if (*uri == '/')
			return get_local_path(uri);

		return NULL;
	}
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
			FileItem *item = (FileItem *) collection->items[i].data;
			
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
	FileItem	*item;

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
	FileItem	*item;
	
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
				if (item->mime_type)
					type = type_to_atom(item->mime_type);
				else
					type = application_octet_stream;
				break;
			}
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

/* Decide if panel drag is OK, setting drop_dest_type and drop_dest_path
 * on the context as needed.
 */
static gboolean panel_drag_ok(FilerWindow 	*filer_window,
			      GdkDragContext 	*context,
			      int	     	item)
{
	FileItem 	*fileitem = NULL;
	char	 	*old_path;
	char	 	*new_path;
	char		*type;

	panel_set_timeout(NULL, 0);

	if (item >= 0)
		fileitem = (FileItem *)
			filer_window->collection->items[item].data;

	if (item == -1)
	{
		new_path = NULL;
	}
	else if (fileitem->flags & (ITEM_FLAG_APPDIR | ITEM_FLAG_EXEC_FILE))
	{
		if (provides(context, text_uri_list))
		{
			type = drop_dest_prog;
			new_path = make_path(filer_window->path,
					fileitem->leafname)->str;
		}
		else
			new_path = NULL;
	}
	else if (fileitem->base_type == TYPE_DIRECTORY)
	{
		type = drop_dest_dir;
		new_path = make_path(filer_window->path,
				fileitem->leafname)->str;
	}
	else
		new_path = NULL;
	
	if (new_path && access(new_path, W_OK))
		new_path = NULL;

	old_path = g_dataset_get_data(context, "drop_dest_path");
	if (old_path == new_path ||
		(old_path && new_path && strcmp(old_path, new_path) == 0))
	{
		return new_path != NULL;	/* Same as before */
	}

	if (new_path)
	{
		g_dataset_set_data(context, "drop_dest_type", type);
		g_dataset_set_data_full(context, "drop_dest_path",
					g_strdup(new_path), g_free);
	}
	else
	{
		item = -1;
		g_dataset_set_data(context, "drop_dest_path", NULL);
	}

	collection_set_cursor_item(filer_window->collection, item);

	return new_path != NULL;
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
	int		item;

	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");
	g_return_val_if_fail(filer_window != NULL, TRUE);

	if (gtk_drag_get_source_widget(context) == widget)
		return FALSE;	/* Not within a single widget! */

	if (filer_window->panel == FALSE)
	{
		if (access(filer_window->path, W_OK))
			return FALSE;	/* We can't write here */
		gdk_drag_status(context, context->suggested_action, time);
		return TRUE;
	}

	/* OK, this is a drag to a panel.
	 * Allow drags to directories, applications and X bit files only.
	 */
	item = collection_get_item(filer_window->collection, x, y);

	if (panel_drag_ok(filer_window, context, item))
	{
		gdk_drag_status(context, context->suggested_action, time);
		return TRUE;
	}
	
	return FALSE;
}

/* Remove panel highlights */
static void drag_leave(GtkWidget		*widget,
                           GdkDragContext	*context)
{
	FilerWindow 	*filer_window;

	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");
	g_return_if_fail(filer_window != NULL);

	panel_set_timeout(NULL, 0);
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
	GdkAtom		target;
	char		*dest_path;
	char		*dest_type;
	
	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");
	g_return_val_if_fail(filer_window != NULL, TRUE);

	dest_path = g_dataset_get_data(context, "drop_dest_path");
	if (dest_path == NULL)
	{
		if (filer_window->panel)
			error = "Bad drop on panel";
		else
		{
			dest_path = filer_window->path;
			dest_type = drop_dest_dir;
		}
	}
	else
	{
		dest_type = g_dataset_get_data(context, "drop_dest_type");
	}

	if (error)
	{
		/* Do nothing */
	}
	else if (dest_type == drop_dest_dir
			&& provides(context, XdndDirectSave0))
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
	else
	{
		if (dest_type == drop_dest_dir)
			error = "Sorry - I require a target type of "
				"text/uri-list or XdndDirectSave0.";
		else
			error = "Sorry - I require a target type of "
				"text/uri-list.";
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

		update_dir(filer_window);
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

	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");
	g_return_if_fail(filer_window != NULL);

	leafname = g_dataset_get_data(context, "leafname");
	if (!leafname)
		leafname = "UntitledData";
	
	dest_path = get_dest_path(filer_window, context);

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

		update_dir(filer_window);
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

/* Execute this program, passing all the URIs in the list as arguments.
 * URIs that are files on the local machine will be passed as simple
 * pathnames. The uri_list should be freed after this function returns.
 */
static void run_with_files(char *path, GSList *uri_list)
{
	char		**argv;
	int		argc = 0;
	struct stat 	info;

	if (stat(path, &info))
	{
		delayed_error("ROX-Filer", "Program not found - deleted?");
		return;
	}

	argv = g_malloc(sizeof(char *) * (g_slist_length(uri_list) + 2));

	if (S_ISDIR(info.st_mode))
		argv[argc++] = make_path(path, "AppRun")->str;
	else
		argv[argc++] = path;
	
	while (uri_list)
	{
		char *uri = (char *) uri_list->data;
		char *local;

		local = get_local_path(uri);
		if (local)
			argv[argc++] = local;
		else
			argv[argc++] = uri;
		uri_list = uri_list->next;
	}
	
	argv[argc++] = NULL;

	if (!spawn_full(argv, getenv("HOME"), 0))
		delayed_error("ROX-Filer", "Failed to fork() child process");
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
			action_move(local_paths, filer_window->path);
		else if (context->action == GDK_ACTION_COPY)
			action_copy(local_paths, filer_window->path);
		else if (context->action == GDK_ACTION_LINK)
			action_link(local_paths, filer_window->path);
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
