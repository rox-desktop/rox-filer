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
#include <collection.h>

#include "filer.h"
#include "pixmaps.h"
#include "gui_support.h"
#include "support.h"

#define MAXURILEN 4096		/* Longest URI to allow */

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

/* Static prototypes */
static void create_uri_list(GString *string,
				Collection *collection,
				FilerWindow *filer_window);
static FileItem *selected_item(Collection *collection);
static gboolean drag_drop(GtkWidget 	*widget,
			GdkDragContext  *context,
			gint            x,
			gint            y,
			guint           time);
static gboolean provides(GdkDragContext *context, GdkAtom target);
static void set_xds_prop(GdkDragContext *context, char *text);
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
static gboolean load_file(char *pathname, char **data_out, long *length_out);
static GSList *uri_list_to_gslist(char *uri_list);
static void got_uri_list(GtkWidget 		*widget,
			 GdkDragContext 	*context,
			 GtkSelectionData 	*selection_data,
			 guint32             	time);
char *get_local_path(char *uri);

void dnd_init()
{
	XdndDirectSave0 = gdk_atom_intern("XdndDirectSave0", FALSE);
	text_plain = gdk_atom_intern("text/plain", FALSE);
	text_uri_list = gdk_atom_intern("text/uri-list", FALSE);
	application_octet_stream = gdk_atom_intern("application/octet-stream",
			FALSE);
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
char *get_local_path(char *uri)
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
			report_error("uri_list_to_gslist",
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

/*			DRAGGING FROM US			*/

/* The user has held the mouse button down over an item and moved - 
 * start a drag.
 *
 * We always provide text/uri-list. If we are dragging a single, regular file
 * then we also offer application/octet-stream.
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
	FileItem	*item;

	if (number_selected == 1)
		item = selected_item(collection);
	else
		item = NULL;
	
	widget = GTK_WIDGET(collection);
	
	target_list = gtk_target_list_new(target_table,
			item && item->base_type == TYPE_FILE ? 2 : 1);

	context = gtk_drag_begin(widget,
			target_list,
			GDK_ACTION_COPY,
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
				type = application_octet_stream;   /* XXX */
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

/* Load the file into memory. Return TRUE on success. */
static gboolean load_file(char *pathname, char **data_out, long *length_out)
{
	FILE		*file;
	long		length;
	char		*buffer;
	gboolean 	retval = FALSE;

	file = fopen(pathname, "r");

	if (!file)
	{
		report_error("Opening file for DND", g_strerror(errno));
		return FALSE;
	}

	fseek(file, 0, SEEK_END);
	length = ftell(file);

	buffer = malloc(length);
	if (buffer)
	{
		fseek(file, 0, SEEK_SET);
		fread(buffer, 1, length, file);

		if (ferror(file))
		{
			report_error("Loading file for DND",
						g_strerror(errno));
			g_free(buffer);
		}
		else
		{
			*data_out = buffer;
			*length_out = length;
			retval = TRUE;
		}
	}
	else
		report_error("Loading file for DND",
				"Can't allocate memory for buffer to "
				"transfer this file");

	fclose(file);

	return retval;
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
			GTK_DEST_DEFAULT_MOTION,
			target_table,
			sizeof(target_table) / sizeof(*target_table),
			GDK_ACTION_COPY | GDK_ACTION_MOVE
			| GDK_ACTION_LINK | GDK_ACTION_PRIVATE);

	gtk_signal_connect(GTK_OBJECT(widget), "drag_drop",
			GTK_SIGNAL_FUNC(drag_drop), filer_window);
	gtk_signal_connect(GTK_OBJECT(widget), "drag_data_received",
			GTK_SIGNAL_FUNC(drag_data_received), filer_window);
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
	
	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");
	g_return_val_if_fail(filer_window != NULL, TRUE);

	if (gtk_drag_get_source_widget(context) == widget)
	{
		/* Ignore drags within a single window */
		gtk_drag_finish(context, FALSE,	FALSE, time);	/* Failure */
		return TRUE;
	}

	if (provides(context, XdndDirectSave0))
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
						make_path(filer_window->path,
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
		error = "Sorry - I require a target type of text/uri-list or "
			"XdndDirectSave0.";

	if (error)
	{
		gtk_drag_finish(context, FALSE, FALSE, time);	/* Failure */
		
		report_error("ROX-Filer", error);
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
			report_error("drag_data_received", "Unknown target");
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

		scan_dir(filer_window);
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
	{
		report_error("ROX-Filer", error);
	}
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
	gboolean	using_XDS = TRUE;

	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");
	g_return_if_fail(filer_window != NULL);

	leafname = g_dataset_get_data(context, "leafname");
	
	if (!leafname)
	{
		using_XDS = FALSE;
		leafname = "UntitledData";	/* TODO: Find a better name */
	}

	fd = open(make_path(filer_window->path, leafname)->str,
		O_WRONLY | O_CREAT | O_EXCL | O_NOCTTY,
		S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH);

	if (fd == -1)
		error = g_strerror(errno);
	else
	{
		if (write(fd,
			selection_data->data,
			selection_data->length) != -1)
				error = g_strerror(errno);

		if (close(fd) != -1 && !error)
			error = g_strerror(errno);

		scan_dir(filer_window);
	}
	
	if (error)
	{
		set_xds_prop(context, "");
		gtk_drag_finish(context, FALSE, FALSE, time);	/* Failure */
		report_error("Error saving file", error);
	}
	else
		gtk_drag_finish(context, TRUE, FALSE, time);    /* Success! */
}

/* We've got a list of URIs from somewhere (probably another filer).
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
	char		**argv = NULL;	/* Command to exec, or NULL */
	GSList		*next_uri;

	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");
	g_return_if_fail(filer_window != NULL);

	uri_list = uri_list_to_gslist(selection_data->data);

	if (!uri_list)
	{
		/* No URIs in the list! */
		error = "No URIs in the text/uri-list (nothing to do!)";
	}
	else if ((!uri_list->next) && (!get_local_path(uri_list->data)))
	{
		/* There is one URI in the list, and it's not on the local
		 * machine. Get it via the X server if possible.
		 */
		error = "TODO: Fetch via X Server";
	}
	else
	{
		int		local_files = 0;
		const char	*start_args[] = {"xterm", "-wf",
					"-e", "cp", "-Riva"};
		int		argc = sizeof(start_args) / sizeof(char *);
		
		next_uri = uri_list;

		argv = g_malloc(sizeof(start_args) +
			sizeof(char *) * (g_slist_length(uri_list) + 2));
		memcpy(argv, start_args, sizeof(start_args));

		/* Either one local URI, or a list. If anything in the list
		 * isn't local then we are stuck.
		 */

		while (next_uri)
		{
			char	*path;

			path = get_local_path((char *) next_uri->data);

			if (path)
			{
				argv[argc++] = path;
				local_files++;
			}
			else
				error = "Some of these files are on a "
					"different machine - they will be "
					"ignored - sorry";

			next_uri = next_uri->next;
		}

		if (local_files < 1)
		{
			error = "None of these files are on the local machine "
				"- I can't operate on multiple remote files - "
				"sorry.";
			g_free(argv);
			argv = NULL;
		}
		else
		{
			argv[argc++] = filer_window->path;
			argv[argc++] = NULL;
		}
	}

	if (error)
	{
		gtk_drag_finish(context, FALSE, FALSE, time);	/* Failure */
		report_error("Error getting file list", error);
	}
	else
	{
		gtk_drag_finish(context, TRUE, FALSE, time);    /* Success! */
	}

	if (argv)
	{
		int	child;		/* Child process ID */
		
		child = spawn(argv);
		if (child)
			g_hash_table_insert(child_to_filer,
					(gpointer) child, filer_window);
		else
			report_error("ROX-Filer", "Failed to fork() child "
						"process");
		g_free(argv);
	}

	next_uri = uri_list;
	while (next_uri)
	{
		g_free(next_uri->data);
		next_uri = next_uri->next;
	}
	g_slist_free(uri_list);
}
