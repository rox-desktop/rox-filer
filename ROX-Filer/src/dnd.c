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

/* Possible values for drop_dest_type (can also be NULL).
 * In either case, drop_dest_path is the app/file/dir to use.
 */
static char *drop_dest_prog = "drop_dest_prog";	/* Run a program */
static char *drop_dest_dir  = "drop_dest_dir";	/* Save to path */

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
char *get_local_path(char *uri);
static void run_with_files(char *path, GSList *uri_list);

void dnd_init()
{
	XdndDirectSave0 = gdk_atom_intern("XdndDirectSave0", FALSE);
	text_plain = gdk_atom_intern("text/plain", FALSE);
	text_uri_list = gdk_atom_intern("text/uri-list", FALSE);
	application_octet_stream = gdk_atom_intern("application/octet-stream",
			FALSE);
}

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

		scan_dir(filer_window);
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
		int		local_files = 0;
		const char	*start_args[] = {"xterm", "-wf", "-e"};
		int		argc = sizeof(start_args) / sizeof(char *);
		
		next_uri = uri_list;

		argv = g_malloc(sizeof(start_args) +
			sizeof(char *) * (g_slist_length(uri_list) + 4));
		memcpy(argv, start_args, sizeof(start_args));

		if (context->action == GDK_ACTION_MOVE)
		{
			argv[argc++] = "mv";
			argv[argc++] = "-iv";
		}
		else if (context->action == GDK_ACTION_LINK)
		{
			argv[argc++] = "ln";
			argv[argc++] = "-vis";
		}
		else
		{
			argv[argc++] = "cp";
			argv[argc++] = "-Riva";
		}

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
			argv[argc++] = dest_path;
			argv[argc++] = NULL;
		}
	}

	if (error)
	{
		gtk_drag_finish(context, FALSE, FALSE, time);	/* Failure */
		delayed_error("Error getting file list", error);
	}
	else if (send_reply)
	{
		gtk_drag_finish(context, TRUE, FALSE, time);    /* Success! */
		if (argv)
		{
			int	child;		/* Child process ID */
			
			child = spawn(argv);
			if (child)
				g_hash_table_insert(child_to_filer,
						(gpointer) child, filer_window);
			else
				delayed_error("ROX-Filer",
					"Failed to fork() child process");
			g_free(argv);
		}
	}

	next_uri = uri_list;
	while (next_uri)
	{
		g_free(next_uri->data);
		next_uri = next_uri->next;
	}
	g_slist_free(uri_list);
}
