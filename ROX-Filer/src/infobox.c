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
static GtkWidget *make_about(guchar *path, XMLwrapper *ai);
static GtkWidget *make_file_says(guchar *path);
static void add_file_output(FileStatus *fs,
			    gint source, GdkInputCondition condition);
static const gchar *pretty_type(DirItem *file, const guchar *path);
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
	GObject		*owindow;

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
	gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(window)->vbox),
				    details);

	owindow = G_OBJECT(window);
	g_object_set_data(owindow, "details", details);
	g_object_set_data_full(owindow, "path", path, g_free);

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
	gtk_box_pack_start_defaults(GTK_BOX(vbox), details);
	gtk_widget_show_all(details);
}

static void add_frame(GtkBox *vbox, GtkWidget *list)
{
	GtkWidget	*frame;

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(frame), list);
	gtk_box_pack_start_defaults(vbox, frame);
}

/* Create the VBox widget that contains the details.
 * Note that 'path' must not be freed until the vbox is destroyed.
 */
static GtkWidget *make_vbox(guchar *path)
{
	DirItem		*item;
	GtkBox		*vbox;
	XMLwrapper	*ai;
	xmlNode 	*about = NULL;
	gchar		*help_dir;

	g_return_val_if_fail(path[0] == '/', NULL);
	
	item = diritem_new(g_basename(path));
	diritem_restat(path, item, NULL);

	ai = appinfo_get(path, item);
	if (ai)
		about = xml_get_section(ai, NULL, "About");

	vbox = GTK_BOX(gtk_vbox_new(FALSE, 4));

	add_frame(vbox, make_details(path, item));

	help_dir = g_strconcat(path, "/Help", NULL);

	if (access(help_dir, F_OK) == 0)
	{
		GtkWidget *button, *align;

		align = gtk_alignment_new(0.5, 0.5, 0, 0);
		
		button = button_new_mixed(GTK_STOCK_JUMP_TO,
				_("Show _Help Files"));
		gtk_box_pack_start(vbox, align, FALSE, TRUE, 0);
		gtk_container_add(GTK_CONTAINER(align), button);
		g_signal_connect_swapped(button, "clicked", 
				G_CALLBACK(show_help_clicked),
				path);
	}
	g_free(help_dir);

	if (about)
		add_frame(vbox, make_about(path, ai));
	else
		gtk_box_pack_start_defaults(vbox, make_file_says(path));

	if (ai)
		g_object_unref(ai);

	diritem_free(item);

	return (GtkWidget *) vbox;
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
	gchar		*u8 = NULL;

	if (!g_utf8_validate(data, -1, NULL))
		u8 = to_utf8(data);

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, label, 1, u8 ? u8 : data, -1);

	g_free(u8);
}

static void add_row_and_free(GtkListStore *store,
			     const gchar *label, gchar *data)
{
	add_row(store, label, data);
	g_free(data);
}

/* Create an empty list view, ready to place some data in */
static void make_list(GtkListStore **list_store, GtkWidget **list_view)
{
	GtkListStore	*store;
	GtkTreeView	*view;
	GtkCellRenderer *cell_renderer;

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	view = GTK_TREE_VIEW(
			gtk_tree_view_new_with_model(GTK_TREE_MODEL(store)));
	g_object_unref(G_OBJECT(store));
	gtk_tree_view_set_headers_visible(view, FALSE);

	cell_renderer = gtk_cell_renderer_text_new();
	g_object_set(G_OBJECT(cell_renderer), "xalign", 1.0, NULL);
	gtk_tree_view_insert_column_with_attributes(view,
			0, NULL, cell_renderer, "text", 0, NULL);

	cell_renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(view,
			1, NULL, cell_renderer, "text", 1, NULL);

	g_signal_connect(view, "cursor_changed",
			G_CALLBACK(set_selection), NULL);

	*list_store = store;
	*list_view = (GtkWidget *) view;
}
	
/* Create the TreeView widget with the file's details */
static GtkWidget *make_details(guchar *path, DirItem *item)
{
	GtkListStore	*store;
	GtkWidget	*view;
	gchar		*tmp, *tmp2;

	make_list(&store, &view);
	
	add_row(store, _("Name:"), item->leafname);

	if (item->base_type == TYPE_ERROR)
	{
		add_row(store, _("Error:"), g_strerror(item->lstat_errno));
		return view;
	}

	tmp = g_path_get_dirname(path);
	tmp2 = pathdup(tmp);
	if (strcmp(tmp, tmp2) != 0)
		add_row(store, _("Real directory:"), tmp2);
	g_free(tmp);
	g_free(tmp2);

	add_row_and_free(store, _("Owner, Group:"),
			 g_strdup_printf("%s, %s",
					 user_name(item->uid),
					 group_name(item->gid)));

	add_row_and_free(store, _("Size:"),
			 item->size >= PRETTY_SIZE_LIMIT
			 ? g_strdup_printf("%s (%" SIZE_FMT " %s)",
					   format_size(item->size),
					   item->size, _("bytes"))
			 : g_strdup(format_size(item->size)));

	add_row_and_free(store, _("Change time:"), pretty_time(&item->ctime));
	
	add_row_and_free(store, _("Modify time:"), pretty_time(&item->mtime));

	add_row_and_free(store, _("Access time:"), pretty_time(&item->atime));

	add_row(store, _("Permissions:"), pretty_permissions(item->mode));

	add_row(store, _("Type:"), pretty_type(item, path));

	if (item->base_type != TYPE_DIRECTORY)
		add_row_and_free(store, _("Run action:"),
				 describe_current_command(item->mime_type));

	return view;
}
	
/* Create the TreeView widget with the application's details */
static GtkWidget *make_about(guchar *path, XMLwrapper *ai)
{
	GtkListStore	*store;
	GtkWidget	*view;
	xmlNode 	*prop;
	gchar		*tmp;
	xmlNode		*about, *about_trans;
	GHashTable	*translate;

	g_return_val_if_fail(ai != NULL, NULL);

	about_trans = xml_get_section(ai, NULL, "About");

	about = xmlDocGetRootElement(ai->doc)->xmlChildrenNode;
	for (; about; about = about->next)
	{
		if (about->type != XML_ELEMENT_NODE)
			continue;
		if (about->ns == NULL && strcmp(about->name, "About") == 0)
			break;
	}

	g_return_val_if_fail(about != NULL, NULL);
	
	make_list(&store, &view);

	/* Add each field in about to the list, but overriding each element
	 * with about_trans if a translation is supplied.
	 */
	translate = g_hash_table_new_full(g_str_hash, g_str_equal,
					  NULL, g_free);
	if (about_trans != about)
	{
		xmlNode *p;
		for (p = about_trans->xmlChildrenNode; p; p = p->next)
		{
			if (p->type != XML_ELEMENT_NODE)
				continue;
			tmp = xmlNodeListGetString(p->doc,
						   p->xmlChildrenNode, 1);
			if (tmp)
				g_hash_table_insert(translate,
						(char *) p->name, tmp);
		}

	}

	for (prop = about->xmlChildrenNode; prop; prop = prop->next)
	{
		if (prop->type == XML_ELEMENT_NODE)
		{
			char *l;
			char *trans;

			trans = g_hash_table_lookup(translate, prop->name);
			
			l = g_strconcat((char *) prop->name, ":", NULL);
			if (trans)
				tmp = g_strdup(trans);
			else
				tmp = xmlNodeListGetString(prop->doc,
						prop->xmlChildrenNode, 1);
			if (!tmp)
				tmp = g_strdup("-");
			add_row_and_free(store, l, tmp);
			g_free(l);
		}
	}

	g_hash_table_destroy(translate);

	return view;
}

static GtkWidget *make_file_says(guchar *path)
{
	GtkWidget	*frame;
	GtkWidget	*w_file_label;
	GtkLabel	*l_file_label;
	int		file_data[2];
	char 		*argv[] = {"file", "-b", NULL, NULL};
	FileStatus 	*fs = NULL;
	guchar 		*tmp;

	frame = gtk_frame_new(_("file(1) says..."));
	w_file_label = gtk_label_new(_("<nothing yet>"));
	l_file_label = GTK_LABEL(w_file_label);
	gtk_misc_set_padding(GTK_MISC(w_file_label), 4, 4);
	gtk_label_set_line_wrap(l_file_label, TRUE);
	gtk_container_add(GTK_CONTAINER(frame), w_file_label);
	
	if (pipe(file_data))
	{
		tmp = g_strdup_printf("pipe(): %s", g_strerror(errno));
		gtk_label_set_text(l_file_label, tmp);
		g_free(tmp);
		return frame;
	}

	switch (fork())
	{
		case -1:
			tmp = g_strdup_printf("pipe(): %s", g_strerror(errno));
			gtk_label_set_text(l_file_label, tmp);
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
			chdir(g_path_get_dirname(path));
#endif
			if (execvp(argv[0], argv))
				fprintf(stderr, "execvp() error: %s\n",
						g_strerror(errno));
			_exit(0);
		default:
			/* We are the parent */
			close(file_data[1]);
			fs = g_new(FileStatus, 1);
			fs->label = l_file_label;
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
	
	str = to_utf8(fs->text);
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

/* Don't g_free() the result */
static const gchar *pretty_type(DirItem *file, const guchar *path)
{
	static gchar *text = NULL;

	null_g_free(&text);

	if (file->flags & ITEM_FLAG_SYMLINK)
	{
		char	*target;

		target = readlink_dup(path);
		if (target)
		{
			ensure_utf8(&target);

			text = g_strdup_printf(_("Symbolic link to %s"),
					       target);
			g_free(target);
			return text;
		}

		return _("Symbolic link");
	}

	if (file->flags & ITEM_FLAG_APPDIR)
		return _("ROX application");

	if (file->flags & ITEM_FLAG_MOUNT_POINT)
	{
		MountPoint *mp;
		const gchar *mounted;

		mounted = mount_is_mounted(path, NULL, NULL)
			  ? _("mounted") : _("unmounted");

		mp = g_hash_table_lookup(fstab_mounts, path);
		if (mp)
			text = g_strdup_printf(_("Mount point for %s (%s)"),
					       mp->name, mounted);
		else
			text = g_strdup_printf(_("Mount point (%s)"), mounted);
		return text;
	}

	if (file->mime_type)
	{
		text = g_strconcat(file->mime_type->media_type, "/",
					  file->mime_type->subtype, NULL);
		return text;
	}

	return "-";
}
