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

/* usericons.c - handle user-defined icons. Diego Zamboni, Feb 7, 2001. */

#include "config.h"

#include <gtk/gtk.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fnmatch.h>

#include "global.h"
#include "dir.h"
#include "gui_support.h"
#include "choices.h"
#include "pixmaps.h"
#include "run.h"
#include "dnd.h"
#include "support.h"
#include "usericons.h"

static GList *glob_icons = NULL;

/* Static prototypes */
static char *process_globicons_line(guchar *line);
static GlobIcon *get_globicon_struct(guchar *path);
static void free_globicon(GlobIcon *gi, gpointer user_data);
static void get_path_set_icon(GtkWidget *dialog);
static gboolean set_icon_path(guchar *path, guchar *icon);
static void show_icon_help(gpointer data);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Read glob-pattern -> icon mappings from "globicons" in Choices */
void read_globicons()
{
	static time_t last_read = (time_t) 0;
	struct stat info;
	guchar *path;
	
	path = choices_find_path_load("globicons", PROJECT);
	if (!path)
		return;	/* Nothing to load */

	if (mc_stat(path, &info) == -1)
		goto out;

	if (info.st_mtime <= last_read)
		goto out;  /* File hasn't been modified since we last read it */

	if (glob_icons)
	{
		g_list_foreach(glob_icons, (GFunc) free_globicon, NULL);
		g_list_free(glob_icons);
		glob_icons = NULL;
	}

	parse_file(path, process_globicons_line);

	last_read = time(NULL);   /* Update time stamp */
out:
	g_free(path);
}

/* Set an item's image field according to the globicons patterns if
 * it matches one of them and the file exists, if item is not NULL.
 */
void check_globicon(guchar *path, DirItem *item)
{
	GlobIcon *gi;

	g_return_if_fail(item && !item->image);
	
	gi = get_globicon_struct(path);
	if (gi)
		item->image = g_fscache_lookup(thumb_cache, gi->iconpath);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void free_globicon(GlobIcon *gi, gpointer user_data)
{
	g_return_if_fail(gi != NULL);

	g_free(gi->pattern);
	g_free(gi->iconpath);
	g_free(gi);
}

/* Write globicons file */
static void write_globicons(void)
{
	guchar *save = NULL;
	GString *tmp = NULL;
	FILE *file = NULL;
	GList *next;
	guchar *save_new = NULL;

	save = choices_find_path_save("globicons", "ROX-Filer", TRUE);

	if (!save)
		return;

	save_new = g_strconcat(save, ".new", NULL);
	file = fopen(save_new, "wb");
	if (!file)
		goto err;

	tmp = g_string_new(NULL);
	for (next = g_list_last(glob_icons); next; next = next->prev)
	{
		GlobIcon *gi = (GlobIcon *) next->data;
		
		g_string_sprintf(tmp, "%s\t%s\n", gi->pattern, gi->iconpath);

		if (fwrite(tmp->str, 1, tmp->len, file) < tmp->len)
			goto err;
	}

	if (fclose(file))
	{
		file = NULL;
		goto err;
	}

	file = NULL;

	if (rename(save_new, save))
		goto err;
	goto out;
 err:
	delayed_error(_("Error saving globicons"), g_strerror(errno));
 out:
	if (file)
		fclose(file);
	if (tmp)
		g_string_free(tmp, TRUE);
	g_free(save_new);
	g_free(save);
}

/* Process a globicon line. Format:
   	glob-pattern	icon-path
   Example:
       /home/<*>/Mail   /usr/local/icons/mailbox.xpm
         (<*> represents a single asterisk, enclosed in brackets to not break
         the C comment).
*/
static char *process_globicons_line(guchar *line)
{
	guchar *pattern, *iconpath;
	GlobIcon *gi;
	
	pattern = strtok(line, " \t");
	/* We ignore empty lines, but they are no cause for a message */
	if (pattern == NULL)
		return NULL;
	
	iconpath = strtok(NULL, " \t");
	
	/* If there is no icon, then we worry */
	g_return_val_if_fail(iconpath != NULL,
			"Invalid line in globicons: no icon specified");

	gi = g_new(GlobIcon, 1);
	gi->pattern = g_strdup(pattern);
	gi->iconpath = g_strdup(iconpath);

	/* Prepend so that later patterns override earlier ones when we loop
	 * through the list.
	 */
	glob_icons = g_list_prepend(glob_icons, gi);

	return NULL;
}

/* If there is a globicon entry that matches the given path, return
 * a pointer to the GlobIcon structure, otherwise return NULL.
 * The returned pointer should not be freed because it is part of
 * the glob_icons list.
 */
static GlobIcon *get_globicon_struct(guchar *path)
{
	GList *list;
	
	for (list = glob_icons; list; list = list->next)
	{
		GlobIcon *gi = (GlobIcon *) list->data;

		if (fnmatch(gi->pattern, path, FNM_PATHNAME) == 0)
			return gi;
	}

	/* If we get here, there is no corresponding globicon */
	return NULL;
}

/* Add a globicon entry to the list. If another one with the same
 * path exists, it is replaced. Otherwise, the new entry is
 * added to the top of the list (so that it takes precedence over
 * other entries).
 */
static void add_globicon(guchar *path, guchar *icon)
{
	GList *list;
	GlobIcon *gi;
	
	for (list = glob_icons; list; list = list->next)
	{
		gi = (GlobIcon *) list->data;
		
		if (strcmp(gi->pattern, path) == 0)
		{
			g_free(gi->iconpath);
			gi->iconpath = g_strdup(icon);
			goto out;
		}
	}

	gi = g_new(GlobIcon, 1);
	gi->pattern = g_strdup(path);
	gi->iconpath = g_strdup(icon);

	/* Prepend so that later patterns override earlier ones when we loop
	 * through the list.
	 */
	glob_icons = g_list_prepend(glob_icons, gi);

out:
	/* Rewrite the globicons file */
	write_globicons();

	/* Make sure any visible icons for the file are updated */
	examine(path);
}

/* Remove the globicon for a certain path from the list. If the path
 * has no associated globicon, the list is not modified.
 */
static void delete_globicon(guchar *path)
{
	GlobIcon *gi;
	
	/* XXX: What happens if the user tries to unset /home/fred/Mail
	 * and there is a rule for /home/<*>/Mail ?
	 */
	gi = get_globicon_struct(path);
	if (!gi)
		return;	/* Not in the list */

	glob_icons = g_list_remove(glob_icons, gi);
	free_globicon(gi, NULL);
	write_globicons();
	examine(path);
}

/* Called when a URI list is dropped onto the box in the Set Icon
 * dialog. Make that the default icon.
 */
static void drag_icon_dropped(GtkWidget	 	*frame,
		       	      GdkDragContext    *context,
		       	      gint              x,
		       	      gint              y,
		       	      GtkSelectionData  *selection_data,
		       	      guint             info,
		       	      guint32           time,
		       	      GtkWidget	 	*dialog)
{
	GSList	*uris;
	guchar	*icon = NULL;
	guchar  *path = NULL;

	if (!selection_data->data)
		return;

	uris = uri_list_to_gslist(selection_data->data);

	if (g_slist_length(uris) == 1)
		icon = get_local_path((guchar *) uris->data);
	g_slist_free(uris);

	if (!icon)
	{
		delayed_error(PROJECT,
			_("You should drop a single local icon file "
			"onto the drop box - that icon will be "
			"used for this file from now on."));
		return;
	}

	path = gtk_object_get_data(GTK_OBJECT(dialog), "pathname");

	if (!set_icon_path(path, icon))
		return;

	destroy_on_idle(dialog);
}

/* Called if the user clicks on the OK button on the Set Icon dialog */
static void get_path_set_icon(GtkWidget *dialog)
{
	GtkEntry *entry;
	guchar	*icon, *path;

	entry = gtk_object_get_data(GTK_OBJECT(dialog), "icon_path");
	path = gtk_object_get_data(GTK_OBJECT(dialog), "pathname");
	g_return_if_fail(entry != NULL && path != NULL);

	icon = gtk_entry_get_text(entry);

	if (!set_icon_path(path, icon))
		return;

	destroy_on_idle(dialog);
}

/* Called if the user clicks on the "Remove custom icon" button */
static void remove_icon(GtkWidget *dialog)
{
	guchar *path;

	g_return_if_fail(dialog != NULL);

	path = gtk_object_get_data(GTK_OBJECT(dialog), "pathname");
	g_return_if_fail(path != NULL);

	delete_globicon(path);

	destroy_on_idle(dialog);
}

/* Add a globicon mapping for the given file to the given icon path */
static gboolean set_icon_path(guchar *filepath, guchar *iconpath)
{
	struct stat icon;

	/* Check if file exists */
	if (!mc_stat(iconpath, &icon) == 0) {
		delayed_error(PROJECT,
		    _("The pathname you gave does not exist. "
		      "The icon has not been changed."));
		return FALSE;
	}

	/* Check if we can load the image */
	/* We can't check for bad image files without some modifications to the
	 * public interface of pixmap.c. Is it worth it? If it's not a valid
	 * image we get the bad_image icon anyway. - Diego Zamboni
	 */

	/* Add the globicon mapping and update visible icons */
	add_globicon(filepath, iconpath);

	return TRUE;
}

/* Display a dialog box allowing the user to set the icon for
 * a file or directory.
 */
void icon_set_handler_dialog(DirItem *item, guchar *path)
{
	guchar		*tmp;
	GtkWidget	*dialog, *vbox, *frame, *hbox, *entry, *label, *button;
	GtkTargetEntry 	targets[] = {
		{"text/uri-list", 0, TARGET_URI_LIST},
	};
	GlobIcon	*gi;
	
	gi = get_globicon_struct(path);

	g_return_if_fail(item != NULL && path != NULL);

	dialog = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_object_set_data_full(GTK_OBJECT(dialog),
				 "pathname",
				 strdup(path),
				 g_free);

	gtk_window_set_title(GTK_WINDOW(dialog), _("Set icon"));
	gtk_container_set_border_width(GTK_CONTAINER(dialog), 10);

	vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(dialog), vbox);

	tmp = g_strconcat(_("Path: "), path, NULL);
	gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new(tmp), FALSE, TRUE, 0);
	g_free(tmp);

	frame = gtk_frame_new(NULL);
	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 4);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 4);

	gtk_drag_dest_set(frame, GTK_DEST_DEFAULT_ALL,
			targets, sizeof(targets) / sizeof(*targets),
			GDK_ACTION_COPY);
	gtk_signal_connect(GTK_OBJECT(frame), "drag_data_received",
			GTK_SIGNAL_FUNC(drag_icon_dropped), dialog);

	label = gtk_label_new(_("Drop an icon file here"));
	gtk_misc_set_padding(GTK_MISC(label), 10, 20);
	gtk_container_add(GTK_CONTAINER(frame), label);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 4);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_hseparator_new(), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("OR")),
						FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_hseparator_new(), TRUE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	label = gtk_label_new(_("Enter the path of an icon file:")),
	gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 4);

	gtk_box_pack_start(GTK_BOX(hbox),
			new_help_button(show_icon_help, NULL), FALSE, TRUE, 0);

	entry = gtk_entry_new();
	/* Set the current icon as the default text if there is one */
	if (gi && gi->iconpath)
		gtk_entry_set_text(GTK_ENTRY(entry), gi->iconpath);

	gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, TRUE, 0);
	gtk_widget_grab_focus(entry);
	gtk_object_set_data(GTK_OBJECT(dialog), "icon_path", entry);
	gtk_signal_connect_object(GTK_OBJECT(entry), "activate",
			GTK_SIGNAL_FUNC(get_path_set_icon),
			GTK_OBJECT(dialog));

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 4);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_hseparator_new(), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("OR")),
						FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_hseparator_new(), TRUE, TRUE, 0);

	hbox = gtk_hbox_new(TRUE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	button = gtk_button_new_with_label(_("Remove custom icon"));
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
				  GTK_SIGNAL_FUNC(remove_icon),
				  GTK_OBJECT(dialog));

	hbox = gtk_hbox_new(TRUE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	button = gtk_button_new_with_label(_("OK"));
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_window_set_default(GTK_WINDOW(dialog), button);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(get_path_set_icon),
			GTK_OBJECT(dialog));
	
	button = gtk_button_new_with_label(_("Cancel"));
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_destroy),
			GTK_OBJECT(dialog));

	gtk_widget_show_all(dialog);
}

static void show_icon_help(gpointer data)
{
	report_error(PROJECT,
		_("Enter the full path of a file that contains a valid "
		  "image to be used as the icon for this file or directory."));
}
