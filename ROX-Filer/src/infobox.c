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

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include <gtk/gtk.h>

#include "global.h"

#include "support.h"
#include "gui_support.h"
#include "dir.h"
#include "type.h"
#include "infobox.h"

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
static GtkWidget *make_clist(guchar *path);
static GtkWidget *make_file_says(guchar *path);
static void file_info_destroyed(GtkWidget *widget, FileStatus *fs);
static void add_file_output(FileStatus *fs,
			    gint source, GdkInputCondition condition);
static guchar *pretty_type(DirItem *file, guchar *path);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Create and display a new info box showing details about this item */
void infobox_new(guchar *path)
{
	GtkWidget	*window, *vbox, *list, *file, *button;

	g_return_if_fail(path != NULL);

	window = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_MOUSE);
	gtk_container_set_border_width(GTK_CONTAINER(window), 4);
	gtk_window_set_title(GTK_WINDOW(window), path);

	vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	list = make_clist(path);
	gtk_box_pack_start(GTK_BOX(vbox), list, TRUE, TRUE, 0);

	file = make_file_says(path);
	gtk_box_pack_start(GTK_BOX(vbox), file, TRUE, TRUE, 0);

	button = gtk_button_new_with_label(_("OK"));
	GTK_WIDGET_SET_FLAGS(GTK_WIDGET(button), GTK_CAN_DEFAULT);
	gtk_window_set_default(GTK_WINDOW(window), button);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_destroy),
			GTK_OBJECT(window));
	gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);

	gtk_widget_show_all(window);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Create the CList with the file's details */
static GtkWidget *make_clist(guchar *path)
{
	GtkCList	*table;
	GString		*gstring;
	struct stat	info;
	char		*data[] = {NULL, NULL, NULL};
	DirItem		item;

	g_return_val_if_fail(path[0] == '/', NULL);
	
	table = GTK_CLIST(gtk_clist_new(2));
	GTK_WIDGET_UNSET_FLAGS(GTK_WIDGET(table), GTK_CAN_FOCUS);
	gtk_clist_set_column_auto_resize(table, 0, TRUE);
	gtk_clist_set_column_auto_resize(table, 1, TRUE);
	gtk_clist_set_column_justification(table, 0, GTK_JUSTIFY_RIGHT);
	
	data[0] = _("Name:");
	data[1] = strrchr(path, '/') + 1;
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
		g_string_sprintf(gstring, "%s (%ld %s)",
				format_size((unsigned long) info.st_size),
				(unsigned long) info.st_size, _("bytes"));
	}
	else
	{
		g_string_assign(gstring, 
				format_size((unsigned long) info.st_size));
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
	
	dir_stat(path, &item, FALSE);
	data[0] = _("Type:");
	data[1] = pretty_type(&item, path);
	gtk_clist_append(table, data);
	g_free(data[1]);
	dir_item_clear(&item);

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

	g_return_val_if_fail(path[0] == '/', NULL);
	
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
			chdir(g_strndup(path, tmp - path));
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
			delayed_error(_("ROX-Filer: file(1) says..."),
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
		char	p[MAXPATHLEN + 1];
		int	got;
		got = readlink(path, p, MAXPATHLEN);
		if (got > 0 && got <= MAXPATHLEN)
		{
			p[got] = '\0';
			return g_strconcat(_("Symbolic link to "), p, NULL);
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
