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

/* infobox.c - code for showing a file's attributes */

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <libxml/parser.h>

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
#include "run.h"	/* For show_item_help() */
#include "xml.h"
#include "mount.h"

typedef struct _FileStatus FileStatus;

/* This is for the 'file(1) says...' thing */
struct _FileStatus
{
	int	fd;	/* FD to read from, -1 if closed */
	int	input;	/* Input watcher tag if fd valid */
	GtkLabel *label;	/* Widget to output to */
	gchar	*text;	/* String so far */
};

/* Static prototypes */
static void refresh_info(GObject *window);
static GtkWidget *make_vbox(guchar *path);
static GtkWidget *make_details(guchar *path, DirItem *item);
static GtkWidget *make_about(guchar *path, xmlNode *about);
static GtkWidget *make_file_says(guchar *path);
static void add_file_output(FileStatus *fs,
			    gint source, GdkInputCondition condition);
static guchar *pretty_type(DirItem *file, guchar *path);
static void got_response(GObject *window, gint response, gpointer data);
static void file_info_destroyed(GtkWidget *widget, FileStatus *fs);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Open each item in a new infobox. Confirms if there are a large
 * number of items to show.
 */
void infobox_show_list(GList *paths)
{
	int n;

	n = g_list_length(paths);

	if (n >= 10)
	{
		gchar *message;
		int button;

		message = g_strdup_printf(
			_("Are you sure you want to open %d windows?"), n);
		button = get_choice(_("File Information"),
				message, 2, _("Cancel"), _("Show Info"));
		g_free(message);
		if (button != 1)
			return;
	}

	g_list_foreach(paths, (GFunc) infobox_new, NULL);
}

/* Create and display a new info box showing details about this item */
void infobox_new(const gchar *pathname)
{
	GtkWidget	*window, *details;
	gchar		*path;

	g_return_if_fail(pathname != NULL);

	path = g_strdup(pathname); /* Gets attached to window & freed later */

	window = gtk_dialog_new_with_buttons(
			g_utf8_validate(path, -1, NULL) ? path
							: _("(bad utf-8)"),
				NULL, GTK_DIALOG_NO_SEPARATOR,
				GTK_STOCK_CANCEL, GTK_RESPONSE_DELETE_EVENT,
				GTK_STOCK_REFRESH, GTK_RESPONSE_APPLY,
				NULL);

	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_MOUSE);

	details = make_vbox(path);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->vbox),
			   details, TRUE, TRUE, 0);

	g_object_set_data(G_OBJECT(window), "details", details);
	g_object_set_data_full(G_OBJECT(window), "path", path, g_free);

	g_signal_connect(window, "response", G_CALLBACK(got_response), NULL);

	number_of_windows++;
	gtk_widget_show_all(window);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void show_help_clicked(const gchar *path)
{
	DirItem *item;

	item = diritem_new("");
	diritem_restat(path, item, NULL);
	show_item_help(path, item);
	diritem_free(item);
}

static void got_response(GObject *window, gint response, gpointer data)
{
	if (response == GTK_RESPONSE_APPLY)
		refresh_info(window);
	else
	{
		gtk_widget_destroy(GTK_WIDGET(window));
		one_less_window();
	}
}

static void refresh_info(GObject *window)
{
	GtkWidget	*details, *vbox;
	guchar		*path;

	path = g_object_get_data(window, "path");
	details = g_object_get_data(window, "details");
	g_return_if_fail(details != NULL);
	g_return_if_fail(path != NULL);

	vbox = details->parent;
	gtk_widget_destroy(details);

	details = make_vbox(path);
	g_object_set_data(window, "details", details);
	gtk_box_pack_start(GTK_BOX(vbox), details, TRUE, TRUE, 0);
	gtk_widget_show_all(details);
}

/* Create the VBox widget that contains the details.
 * Note that 'path' must not be freed until the vbox is destroyed.
 */
static GtkWidget *make_vbox(guchar *path)
{
	DirItem		*item;
	GtkWidget	*vbox, *list, *frame;
	XMLwrapper	*ai;
	xmlNode 	*about = NULL;
	gchar		*help_dir;

	g_return_val_if_fail(path[0] == '/', NULL);
	
	item = diritem_new(g_basename(path));
	diritem_restat(path, item, NULL);

	vbox = gtk_vbox_new(FALSE, 4);

	ai = appinfo_get(path, item);
	if (ai)
		about = xml_get_section(ai, NULL, "About");

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	list = make_details(path, item);
	gtk_container_add(GTK_CONTAINER(frame), list);
	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);

	help_dir = g_strconcat(path, "/Help", NULL);
	if (access(help_dir, F_OK) == 0)
	{
		GtkWidget *button, *align;

		align = gtk_alignment_new(0.5, 0.5, 0, 0);
		
		button = button_new_mixed(GTK_STOCK_JUMP_TO,
				_("Show _Help Files"));
		gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, TRUE, 0);
		gtk_container_add(GTK_CONTAINER(align), button);
		g_signal_connect_swapped(button, "clicked", 
				G_CALLBACK(show_help_clicked),
				path);
	}
	g_free(help_dir);

	if (about)
	{
		frame = gtk_frame_new(NULL);
		gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
		list = make_about(path, about);
		gtk_container_add(GTK_CONTAINER(frame), list);
		gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
	}
	else
		gtk_box_pack_start(GTK_BOX(vbox), make_file_says(path), TRUE,
				   TRUE, 0);

	if (ai)
		g_object_unref(ai);

	diritem_free(item);

	return vbox;
}

/* The selection has changed - grab or release the primary selection */
static void set_selection(GtkTreeView *view, gpointer data)
{
	static GtkClipboard *primary = NULL;
	GtkTreeModel *model;
	GtkTreePath *path = NULL;
	GtkTreeIter iter;
	gchar	*text;

	gtk_tree_view_get_cursor(view, &path, NULL);
	if (!path)
		return;

	if (!primary)
		primary = gtk_clipboard_get(gdk_atom_intern("PRIMARY", FALSE));
	
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_path_free(path);

	gtk_tree_model_get(model, &iter, 1, &text, -1);

	gtk_clipboard_set_text(primary, text, -1);

	g_free(text);
}

static void add_row(GtkListStore *store, const gchar *label, const gchar *data)
{
	GtkTreeIter	iter;

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, label, 1, data, -1);
}

/* Create an empty list view, ready to place some data in */
static void make_list(GtkListStore **list_store, GtkWidget **list_view)
{
	GtkListStore	*store;
	GtkWidget	*view;
	GtkCellRenderer *cell_renderer;

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(G_OBJECT(store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

	cell_renderer = gtk_cell_renderer_text_new();
	g_object_set(G_OBJECT(cell_renderer), "xalign", 1.0, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
			0, NULL, cell_renderer, "text", 0, NULL);

	cell_renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
			1, NULL, cell_renderer, "text", 1, NULL);

	g_signal_connect(view, "cursor_changed",
			G_CALLBACK(set_selection), NULL);

	*list_store = store;
	*list_view = view;
}
	
/* Create the TreeView widget with the file's details */
static GtkWidget *make_details(guchar *path, DirItem *item)
{
	GtkListStore	*store;
	GtkWidget	*view;
	GString		*gstring;
	struct stat	info;
	gchar		*tmp, *tmp2;

	make_list(&store, &view);
	
	if (g_utf8_validate(path, -1, NULL))
		add_row(store, _("Name:"), item->leafname);
	else
	{
		gchar *u8;
		u8 = to_utf8(path);
		add_row(store, _("Name:"), u8);
		g_free(u8);
	}

	if (lstat(path, &info))
	{
		add_row(store, _("Error:"), g_strerror(errno));
		return view;
	}

	tmp = g_dirname(path);
	tmp2 = pathdup(tmp);
	if (strcmp(tmp, tmp2) != 0)
		add_row(store, _("Real directory:"), tmp2);
	g_free(tmp);
	g_free(tmp2);

	gstring = g_string_new(NULL);

	g_string_sprintf(gstring, "%s, %s", user_name(info.st_uid),
					    group_name(info.st_gid));
	add_row(store, _("Owner, Group:"), gstring->str);
	
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
	add_row(store, _("Size:"), gstring->str);

	tmp = pretty_time(&info.st_ctime);
	add_row(store, _("Change time:"), tmp);
	g_free(tmp);
	
	tmp = pretty_time(&info.st_mtime);
	add_row(store, _("Modify time:"), tmp);
	g_free(tmp);

	tmp = pretty_time(&info.st_atime);
	add_row(store, _("Access time:"), tmp);
	g_free(tmp);

	g_string_free(gstring, TRUE);

	add_row(store, _("Permissions:"), pretty_permissions(info.st_mode));

	tmp = pretty_type(item, path);
	add_row(store, _("Type:"), tmp);
	g_free(tmp);

	if (item->base_type != TYPE_DIRECTORY)
	{
		tmp = describe_current_command(item->mime_type);
		add_row(store, _("Run action:"), tmp);
		g_free(tmp);
	}

	return view;
}
	
/* Create the TreeView widget with the application's details, or
 * the file(1) summary (for non-apps).
 */
static GtkWidget *make_about(guchar *path, xmlNode *about)
{
	GtkListStore	*store;
	GtkWidget	*view;
	xmlNode 	*prop;
	gchar		*tmp;

	g_return_val_if_fail(about != NULL, NULL);

	make_list(&store, &view);

	for (prop = about->xmlChildrenNode; prop; prop = prop->next)
	{
		if (prop->type == XML_ELEMENT_NODE)
		{
			char *l;

			l = g_strconcat((char *) prop->name, ":", NULL);
			tmp = xmlNodeListGetString(prop->doc,
					prop->xmlChildrenNode, 1);
			if (!tmp)
				tmp = g_strdup("-");
			add_row(store, l, tmp);
			g_free(l);
			g_free(tmp);
		}
	}

	return view;
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
			argv[1] = (char *) g_basename(path);
			chdir(g_dirname(path));
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
			fs->text = g_strdup("");
			fs->input = gtk_input_add_full(fs->fd, GDK_INPUT_READ,
				(GdkInputFunction) add_file_output,
				NULL, fs, NULL);
			g_signal_connect(frame, "destroy",
				G_CALLBACK(file_info_destroyed), fs);
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

	str = g_strconcat(fs->text, buffer, NULL);
	g_free(fs->text);
	fs->text = str;
	
	str = g_strdup(fs->text);
	g_strstrip(str);
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

	g_free(fs->text);
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
	{
		MountPoint *mp;
		const char *mounted;

		mounted = mount_is_mounted(path, NULL, NULL)
			  ? _("mounted") : _("unmounted");

		mp = g_hash_table_lookup(fstab_mounts, path);
		if (mp)
			return g_strdup_printf(_("Mount point for %s (%s)"),
						mp->name, mounted);
		return g_strdup_printf(_("Mount point (%s)"), mounted);
	}

	if (file->mime_type)
		return g_strconcat(file->mime_type->media_type, "/",
				file->mime_type->subtype, NULL);

	return g_strdup("-");
}
