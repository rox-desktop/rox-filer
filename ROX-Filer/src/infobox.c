/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2001, the ROX-Filer team.
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

/* infobox.c - code for showing a file's attributes */

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <parser.h>

#include <gtk/gtk.h>

#include "global.h"

#include "support.h"
#include "main.h"
#include "gui_support.h"
#include "diritem.h"
#include "type.h"
#include "infobox.h"
#include "appinfo.h"
#include "dnd.h"	/* For xa_string */

typedef struct _FileStatus FileStatus;

/* This is for the 'file(1) says...' thing */
struct _FileStatus
{
	int	fd;	/* FD to read from, -1 if closed */
	int	input;	/* Input watcher tag if fd valid */
	GtkLabel *label;	/* Widget to output to */
	gboolean	start;	/* No output yet */
};

/* Static prototypes */
static void refresh_info(GtkObject *window);
static GtkWidget *make_vbox(guchar *path);
static GtkWidget *make_clist(guchar *path, DirItem *item, xmlNode *about);
static GtkWidget *make_file_says(guchar *path);
static void file_info_destroyed(GtkWidget *widget, FileStatus *fs);
static void add_file_output(FileStatus *fs,
			    gint source, GdkInputCondition condition);
static guchar *pretty_type(DirItem *file, guchar *path);
static GtkWidget *make_vbox(guchar *path);
static void info_destroyed(gpointer data);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Create and display a new info box showing details about this item */
void infobox_new(guchar *path)
{
	GtkWidget	*window, *hbox, *vbox, *details, *button;
	
	g_return_if_fail(path != NULL);

	path = g_strdup(path);	/* Gets attached to window & freed later */

	window = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_MOUSE);
	gtk_container_set_border_width(GTK_CONTAINER(window), 4);
	gtk_window_set_title(GTK_WINDOW(window), path);

	vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	
	details = make_vbox(path);
	gtk_box_pack_start(GTK_BOX(vbox), details, TRUE, TRUE, 0);

	hbox = gtk_hbox_new(TRUE, 4);
	gtk_box_pack_end(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	button = gtk_button_new_with_label(_("Refresh"));
	GTK_WIDGET_SET_FLAGS(GTK_WIDGET(button), GTK_CAN_DEFAULT);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(refresh_info),
			GTK_OBJECT(window));
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);

	button = gtk_button_new_with_label(_("Cancel"));
	GTK_WIDGET_SET_FLAGS(GTK_WIDGET(button), GTK_CAN_DEFAULT);
	gtk_window_set_default(GTK_WINDOW(window), button);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_destroy),
			GTK_OBJECT(window));
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);

	gtk_object_set_data(GTK_OBJECT(window), "details", details);
	gtk_object_set_data_full(GTK_OBJECT(window), "path", path, g_free);

	gtk_signal_connect_object(GTK_OBJECT(window), "destroy",
			GTK_SIGNAL_FUNC(info_destroyed), NULL);

	number_of_windows++;
	gtk_widget_show_all(window);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void info_destroyed(gpointer data)
{
	if (--number_of_windows < 1)
		gtk_main_quit();
}

static void refresh_info(GtkObject *window)
{
	GtkWidget	*details, *vbox;
	guchar		*path;

	path = gtk_object_get_data(window, "path");
	details = gtk_object_get_data(window, "details");
	g_return_if_fail(details != NULL);
	g_return_if_fail(path != NULL);

	vbox = details->parent;
	gtk_widget_destroy(details);

	details = make_vbox(path);
	gtk_object_set_data(window, "details", details);
	gtk_box_pack_start(GTK_BOX(vbox), details, TRUE, TRUE, 0);
	gtk_widget_show_all(details);
}

/* Create the VBox widget that contains the details */
static GtkWidget *make_vbox(guchar *path)
{
	DirItem		*item;
	GtkWidget	*vbox, *list, *file;
	AppInfo		*ai;
	xmlNode 	*about = NULL;

	g_return_val_if_fail(path[0] == '/', NULL);
	
	item = diritem_new(NULL);
	diritem_restat(path, item);
	item->leafname = strrchr(path, '/') + 1;

	vbox = gtk_vbox_new(FALSE, 4);

	ai = appinfo_get(path, item);
	if (ai)
		about = appinfo_get_section(ai, "About");

	list = make_clist(path, item, about);
	gtk_box_pack_start(GTK_BOX(vbox), list, TRUE, TRUE, 0);

	if (!about)
	{
		file = make_file_says(path);
		gtk_box_pack_start(GTK_BOX(vbox), file, TRUE, TRUE, 0);
	}

	if (ai)
		appinfo_unref(ai);

	item->leafname = NULL;
	diritem_free(item);

	return vbox;
}

static guchar *selection_text = NULL;

/* The selection has changed - grab or release the primary selection */
static void set_selection(GtkCList *clist, int row, int col,
			  GdkEventButton *event, gpointer data)
{
	/* If we lose the selection when a row is unselected, there's a race
	 * and it doesn't work - therefore, keep the selection...
	 */
	if (clist->selection)
	{
		gchar *text;
		
		g_return_if_fail(gtk_clist_get_text(clist, row, 1, &text) == 1);

		gtk_selection_owner_set(GTK_WIDGET(clist),
				GDK_SELECTION_PRIMARY,
				event->time);
		g_free(selection_text);
		selection_text = g_strdup(text);
	}
}

static void get_selection(GtkCList *clist,
		         GtkSelectionData *selection_data,
		         guint      info,
		         guint      time,
		         gpointer   data)
{
	g_return_if_fail(selection_text != NULL);

	gtk_selection_data_set(selection_data, xa_string,
				8, selection_text, strlen(selection_text));
}

/* Create the CList with the file's details */
static GtkWidget *make_clist(guchar *path, DirItem *item, xmlNode *about)
{
	GtkCList	*table;
	GString		*gstring;
	struct stat	info;
	char		*data[] = {NULL, NULL, NULL};
	xmlNode 	*prop;
	GtkTargetEntry 	target_table[] = { {"STRING", 0, 0} };

	table = GTK_CLIST(gtk_clist_new(2));
	GTK_WIDGET_UNSET_FLAGS(GTK_WIDGET(table), GTK_CAN_FOCUS);
	gtk_clist_set_column_auto_resize(table, 0, TRUE);
	gtk_clist_set_column_auto_resize(table, 1, TRUE);
	gtk_clist_set_column_justification(table, 0, GTK_JUSTIFY_RIGHT);

	gtk_signal_connect(GTK_OBJECT(table), "select-row",
			GTK_SIGNAL_FUNC(set_selection), NULL);
	gtk_signal_connect_object(GTK_OBJECT(table), "selection-clear-event",
			GTK_SIGNAL_FUNC(gtk_clist_unselect_all),
			GTK_OBJECT(table));
	gtk_signal_connect(GTK_OBJECT(table), "selection_get",
			GTK_SIGNAL_FUNC(get_selection), NULL);
	gtk_selection_add_targets(GTK_WIDGET(table), GDK_SELECTION_PRIMARY,
			target_table,
			sizeof(target_table) / sizeof(*target_table));
	
	data[0] = _("Name:");
	data[1] = item->leafname;
	gtk_clist_append(table, data);

	if (lstat(path, &info))
	{
		data[0] = _("Error:");
		data[1] = g_strerror(errno);
		gtk_clist_append(table, data);
		return GTK_WIDGET(table);
	}
	
	gstring = g_string_new(NULL);

	g_string_sprintf(gstring, "%s, %s", user_name(info.st_uid),
					    group_name(info.st_gid));
	data[0] = _("Owner, Group:");
	data[1] = gstring->str;
	gtk_clist_append(table, data);
	
	if (info.st_size >= PRETTY_SIZE_LIMIT)
	{
		g_string_sprintf(gstring, "%s (%" SIZE_FMT " %s)",
			format_size(info.st_size),
			info.st_size, _("bytes"));
	}
	else
	{
		g_string_assign(gstring, 
				format_size(info.st_size));
	}
	data[0] = _("Size:");
	data[1] = gstring->str;
	gtk_clist_append(table, data);
	
	data[0] = _("Change time:");
	data[1] = pretty_time(&info.st_ctime);
	gtk_clist_append(table, data);
	
	data[0] = _("Modify time:");
	data[1] = pretty_time(&info.st_mtime);
	gtk_clist_append(table, data);
	
	data[0] = _("Access time:");
	data[1] = pretty_time(&info.st_atime);
	gtk_clist_append(table, data);

	g_string_free(gstring, TRUE);

	data[0] = _("Permissions:");
	data[1] = pretty_permissions(info.st_mode);
	gtk_clist_append(table, data);
	
	data[0] = _("Type:");
	data[1] = pretty_type(item, path);
	gtk_clist_append(table, data);
	g_free(data[1]);

	if (item->base_type != TYPE_DIRECTORY)
	{
	        data[0] = _("Run action:");
		data[1] = describe_current_command(item->mime_type);
		gtk_clist_append(table, data);
		g_free(data[1]);
	}

	if (about)
	{
		data[0] = data[1] = "";
		gtk_clist_append(table, data);
		gtk_clist_set_selectable(table, table->rows - 1, FALSE);
		for (prop = about->xmlChildrenNode; prop; prop = prop->next)
		{
			if (prop->type == XML_ELEMENT_NODE)
			{
				data[0] = g_strconcat((char *) prop->name,
						":", NULL);
				data[1] = xmlNodeListGetString(prop->doc,
						prop->xmlChildrenNode, 1);
				if (!data[1])
					data[1] = g_strdup("-");
				gtk_clist_append(table, data);
				g_free(data[0]);
				g_free(data[1]);
			}
		}
	}

	return GTK_WIDGET(table);
}
	
static GtkWidget *make_file_says(guchar *path)
{
	GtkWidget	*frame;
	GtkWidget	*file_label;
	int		file_data[2];
	char 		*argv[] = {"file", "-b", NULL, NULL};
	FileStatus 	*fs = NULL;
	guchar 		*tmp;

	frame = gtk_frame_new(_("file(1) says..."));
	file_label = gtk_label_new(_("<nothing yet>"));
	gtk_misc_set_padding(GTK_MISC(file_label), 4, 4);
	gtk_label_set_line_wrap(GTK_LABEL(file_label), TRUE);
	gtk_container_add(GTK_CONTAINER(frame), file_label);
	
	if (pipe(file_data))
	{
		tmp = g_strdup_printf("pipe(): %s", g_strerror(errno));
		gtk_label_set_text(GTK_LABEL(file_label), tmp);
		g_free(tmp);
		return frame;
	}

	switch (fork())
	{
		case -1:
			tmp = g_strdup_printf("pipe(): %s", g_strerror(errno));
			gtk_label_set_text(GTK_LABEL(file_label), tmp);
			g_free(tmp);
			close(file_data[0]);
			close(file_data[1]);
			break;
		case 0:
			/* We are the child */
			close(file_data[0]);
			dup2(file_data[1], STDOUT_FILENO);
			dup2(file_data[1], STDERR_FILENO);
#ifdef FILE_B_FLAG
			argv[2] = path;
#else
			tmp = strrchr(path, '/');
			argv[1] = tmp + 1;
			if (tmp > path)
				chdir(g_strndup(path, tmp - path));
			else
				chdir("/");
#endif
			if (execvp(argv[0], argv))
				fprintf(stderr, "execvp() error: %s\n",
						g_strerror(errno));
			_exit(0);
		default:
			/* We are the parent */
			close(file_data[1]);
			fs = g_new(FileStatus, 1);
			fs->label = GTK_LABEL(file_label);
			fs->fd = file_data[0];
			fs->start = TRUE;
			fs->input = gdk_input_add(fs->fd, GDK_INPUT_READ,
				(GdkInputFunction) add_file_output, fs);
			gtk_signal_connect(GTK_OBJECT(frame), "destroy",
				GTK_SIGNAL_FUNC(file_info_destroyed), fs);
			break;
	}

	return frame;
}

/* Got some data from file(1) - stick it in the window. */
static void add_file_output(FileStatus *fs,
			    gint source, GdkInputCondition condition)
{
	char	buffer[20];
	char	*str;
	int	got;

	got = read(source, buffer, sizeof(buffer) - 1);
	if (got <= 0)
	{
		int	err = errno;
		gtk_input_remove(fs->input);
		close(source);
		fs->fd = -1;
		if (got < 0)
			delayed_error(_("file(1) says... %s"),
					g_strerror(err));
		return;
	}
	buffer[got] = '\0';

	if (fs->start)
	{
		str = "";
		fs->start = FALSE;
	}
	else
		gtk_label_get(fs->label, &str);

	str = g_strconcat(str, buffer, NULL);
	gtk_label_set_text(fs->label, str);
	g_free(str);
}

static void file_info_destroyed(GtkWidget *widget, FileStatus *fs)
{
	if (fs->fd != -1)
	{
		gtk_input_remove(fs->input);
		close(fs->fd);
	}

	g_free(fs);
}

/* g_free() the result */
static guchar *pretty_type(DirItem *file, guchar *path)
{
	if (file->flags & ITEM_FLAG_SYMLINK)
	{
		char	*target;

		target = readlink_dup(path);
		if (target)
		{
			char *retval;

			retval = g_strdup_printf(_("Symbolic link to %s"),
						target);
			g_free(target);
			return retval;
		}

		return g_strdup(_("Symbolic link"));
	}

	if (file->flags & ITEM_FLAG_APPDIR)
		return g_strdup(_("ROX application"));

	if (file->flags & ITEM_FLAG_MOUNT_POINT)
		return g_strdup(_("Mount point"));

	if (file->mime_type)
		return g_strconcat(file->mime_type->media_type, "/",
				file->mime_type->subtype, NULL);

	return g_strdup("-");
}
