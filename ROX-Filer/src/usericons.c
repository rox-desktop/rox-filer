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

/* usericons.c - handle user-defined icons. Diego Zamboni, Feb 7, 2001. */

#include "config.h"

#include <gtk/gtk.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fnmatch.h>
#include <parser.h>
#include <time.h>

#include "global.h"

#include "fscache.h"
#include "diritem.h"
#include "dir.h"
#include "gui_support.h"
#include "choices.h"
#include "pixmaps.h"
#include "type.h"
#include "run.h"
#include "dnd.h"
#include "support.h"
#include "usericons.h"
#include "main.h"
#include "menu.h"
#include "filer.h"
#include "action.h"
#include "display.h"

static GHashTable *glob_icons = NULL; /* Pathname -> Icon pathname */

/* Static prototypes */
static char *process_globicons_line(guchar *line);
static gboolean free_globicon(gpointer key, gpointer value, gpointer data);
static void get_path_set_icon(GtkWidget *dialog);
static void show_icon_help(gpointer data);
static void write_globicons(void);
static void show_current_dirs_menu(GtkWidget *button, gpointer data);
static void add_globicon(guchar *path, guchar *icon);
static void drag_icon_dropped(GtkWidget	 	*frame,
		       	      GdkDragContext    *context,
		       	      gint              x,
		       	      gint              y,
		       	      GtkSelectionData  *selection_data,
		       	      guint             info,
		       	      guint32           time,
		       	      GtkWidget	 	*dialog);
static void remove_icon(GtkWidget *dialog);
static gboolean set_icon_for_type(const MIME_type *type, gchar *iconpath,
				  gboolean just_media);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Read glob-pattern -> icon mappings from "globicons" in Choices */
void read_globicons()
{
	static time_t last_read = (time_t) 0;
	struct stat info;
	guchar *path;
	xmlDocPtr doc;

	if (!glob_icons)
		glob_icons = g_hash_table_new(g_str_hash, g_str_equal);

	path = choices_find_path_load("globicons", PROJECT);
	if (!path)
		return;	/* Nothing to load */

	if (mc_stat(path, &info) == -1)
		goto out;

	if (info.st_mtime <= last_read)
		goto out;  /* File hasn't been modified since we last read it */

	g_hash_table_foreach_remove(glob_icons, free_globicon, NULL);

	doc = xmlParseFile(path);
	if (doc)
	{
		xmlNodePtr node, icon, root;
		char	   *match;
		
		root = xmlDocGetRootElement(doc);
		
		/* Handle the new XML file format */
		for (node = root->xmlChildrenNode; node; node = node->next)
		{
			gchar *path;

			if (node->type != XML_ELEMENT_NODE)
				continue;
			if (strcmp(node->name, "rule") != 0)
				continue;
			icon = get_subnode(node, NULL, "icon");
			if (!icon)
				continue;
			match = xmlGetProp(node, "match");
			if (!match)
				continue;

			path = icon_convert_path(match);
			g_free(match);

			g_hash_table_insert(glob_icons, path,
					xmlNodeGetContent(icon));
		}

		xmlFreeDoc(doc);
	}
	else
	{
		/* Handle the old non-XML format */
		parse_file(path, process_globicons_line);
		if (g_hash_table_size(glob_icons))
			write_globicons();	/* Upgrade to new format */
	}

	last_read = time(NULL);   /* Update time stamp */
out:
	g_free(path);
}

/* Set an item's image field according to the globicons patterns if
 * it matches one of them and the file exists.
 */
void check_globicon(guchar *path, DirItem *item)
{
	gchar *gi;

	g_return_if_fail(item && !item->image);

	gi = g_hash_table_lookup(glob_icons, path);
	if (gi)
		item->image = g_fscache_lookup(pixmap_cache, gi);
}

/* Add a globicon mapping for the given file to the given icon path */
gboolean set_icon_path(guchar *filepath, guchar *iconpath)
{
	struct stat icon;
	MaskedPixmap *pic;

	/* Check if file exists */
	if (!mc_stat(iconpath, &icon) == 0) {
		delayed_error(_("The pathname you gave does not exist. "
			      	    "The icon has not been changed."));
		return FALSE;
	}

	/* Check if we can load the image, warn the user if not. */
	pic = g_fscache_lookup(pixmap_cache, iconpath);
	if (!pic)
	{
		delayed_error(
			_("Unable to load image file -- maybe it's not in a "
			  "format I understand, or maybe the permissions are "
			  "wrong?\n"
			  "The icon has not been changed."));
		return FALSE;
	}
	g_fscache_data_unref(pixmap_cache, pic);

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
	GtkWidget	*dialog, *vbox, *frame, *hbox, *vbox2;
	GtkWidget	*entry, *label, *button, *align, *icon;
	GtkWidget	**radio;
	GtkTargetEntry 	targets[] = {
		{"text/uri-list", 0, TARGET_URI_LIST},
	};
	char	*gi;
	
	g_return_if_fail(item != NULL && path != NULL);

	gi = g_hash_table_lookup(glob_icons, path);

	dialog = gtk_window_new(GTK_WINDOW_DIALOG);
#ifdef GTK2
	gtk_window_set_type_hint(GTK_WINDOW(dialog),
			GDK_WINDOW_TYPE_HINT_DIALOG);
#endif
	gtk_object_set_data_full(GTK_OBJECT(dialog),
				 "pathname",
				 strdup(path),
				 g_free);

	gtk_window_set_title(GTK_WINDOW(dialog), _("Set icon"));
	gtk_container_set_border_width(GTK_CONTAINER(dialog), 10);

	vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(dialog), vbox);

	radio = g_new(GtkWidget *, 3);

	tmp = g_strdup_printf(_("Set icon for all `%s/<anything>'"),
			item->mime_type->media_type);
	radio[2] = gtk_radio_button_new_with_label(NULL, tmp);
	gtk_box_pack_start(GTK_BOX(vbox), radio[2], FALSE, TRUE, 0);
	g_free(tmp);

	tmp = g_strdup_printf(_("Only for the type `%s/%s'"),
			item->mime_type->media_type,
			item->mime_type->subtype);
	radio[1] = gtk_radio_button_new_with_label_from_widget(
					GTK_RADIO_BUTTON(radio[2]), tmp);
	gtk_box_pack_start(GTK_BOX(vbox), radio[1], FALSE, TRUE, 0);
	g_free(tmp);

	tmp = g_strdup_printf(_("Only for the file `%s'"), path);
	radio[0] = gtk_radio_button_new_with_label_from_widget(
					GTK_RADIO_BUTTON(radio[2]), tmp);
	gtk_box_pack_start(GTK_BOX(vbox), radio[0], FALSE, TRUE, 0);
	g_free(tmp);

	gtk_object_set_data_full(GTK_OBJECT(dialog),
				 "radios", radio, g_free);
	gtk_object_set_data(GTK_OBJECT(dialog),
				 "mime_type", item->mime_type);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio[0]), TRUE);

	frame = gtk_frame_new(NULL);
	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 4);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 4);

	gtk_drag_dest_set(frame, GTK_DEST_DEFAULT_ALL,
			targets, sizeof(targets) / sizeof(*targets),
			GDK_ACTION_COPY);
	gtk_signal_connect(GTK_OBJECT(frame), "drag_data_received",
			GTK_SIGNAL_FUNC(drag_icon_dropped), dialog);

	vbox2 = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), vbox2);

	label = gtk_label_new(_("Drop an icon file here"));
	gtk_misc_set_padding(GTK_MISC(label), 10, 10);
	gtk_box_pack_start(GTK_BOX(vbox2), label, TRUE, TRUE, 0);
	align = gtk_alignment_new(1, 1, 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox2), align, FALSE, TRUE, 0);
	button = gtk_button_new();
	gtk_container_add(GTK_CONTAINER(align), button);
	icon = gtk_pixmap_new(im_dirs->pixmap, im_dirs->mask);
	gtk_container_add(GTK_CONTAINER(button), icon);
	gtk_tooltips_set_tip(tooltips, button,
			_("Menu of directories previously used for icons"),
			NULL);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(show_current_dirs_menu), NULL);

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
	if (gi)
		gtk_entry_set_text(GTK_ENTRY(entry), gi);

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


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static gboolean free_globicon(gpointer key, gpointer value, gpointer data)
{
	g_free(key);
	g_free(value);

	return TRUE;		/* For g_hash_table_foreach_remove() */
}

static void write_globicon(gpointer key, gpointer value, gpointer data)
{
	xmlNodePtr doc = (xmlNodePtr) data;
	xmlNodePtr tree;

	tree = xmlNewTextChild(doc, NULL, "rule", NULL);
	xmlSetProp(tree, "match", key);
	xmlNewChild(tree, NULL, "icon", value);
}

/* Write globicons file */
static void write_globicons(void)
{
	gchar *save = NULL, *save_new = NULL;
	xmlDocPtr doc = NULL;

	save = choices_find_path_save("globicons", PROJECT, TRUE);

	if (!save)
		return;		/* Saving is disabled */

	save_new = g_strconcat(save, ".new", NULL);

	doc = xmlNewDoc("1.0");
	xmlDocSetRootElement(doc,
		             xmlNewDocNode(doc, NULL, "special-files", NULL));

	g_hash_table_foreach(glob_icons, write_globicon,
			     xmlDocGetRootElement(doc));

	if (save_xml_file(doc, save_new) || rename(save_new, save))
		delayed_error(_("Error saving %s: %s"),
				save, g_strerror(errno));

	g_free(save_new);
	g_free(save);

	if (doc)
		xmlFreeDoc(doc);
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
	
	pattern = strtok(line, " \t");
	/* We ignore empty lines, but they are no cause for a message */
	if (pattern == NULL)
		return NULL;
	
	iconpath = strtok(NULL, " \t");
	
	/* If there is no icon, then we worry */
	g_return_val_if_fail(iconpath != NULL,
			"Invalid line in globicons: no icon specified");

	g_hash_table_insert(glob_icons, g_strdup(pattern), g_strdup(iconpath));

	return NULL;
}

/* Add a globicon entry to the list. If another one with the same
 * path exists, it is replaced. Otherwise, the new entry is
 * added to the top of the list (so that it takes precedence over
 * other entries).
 */
static void add_globicon(guchar *path, guchar *icon)
{
	g_hash_table_insert(glob_icons, g_strdup(path), g_strdup(icon));

	/* Rewrite the globicons file */
	write_globicons();

	/* Make sure any visible icons for the file are updated */
	examine(path);
}

/* Remove the globicon for a certain path */
static void delete_globicon(guchar *path)
{
	gpointer key, value;

	if (!g_hash_table_lookup_extended(glob_icons, path, &key, &value))
		return;

	g_hash_table_remove(glob_icons, path);

	g_free(key);
	g_free(value);

	write_globicons();
	examine(path);
}

/* Set the icon for this dialog's file to 'icon' */
static void do_set_icon(GtkWidget *dialog, gchar *icon)
{
	guchar  *path = NULL;
	GtkToggleButton **radio;

	path = gtk_object_get_data(GTK_OBJECT(dialog), "pathname");
	g_return_if_fail(path != NULL);

	radio = gtk_object_get_data(GTK_OBJECT(dialog), "radios");
	g_return_if_fail(radio != NULL);

	if (gtk_toggle_button_get_active(radio[0]))
	{
		if (!set_icon_path(path, icon))
			return;
	}
	else
	{
		gboolean just_media;
		MIME_type *type;
		
		type = gtk_object_get_data(GTK_OBJECT(dialog), "mime_type");
		just_media = gtk_toggle_button_get_active(radio[2]);

		if (!set_icon_for_type(type, icon, just_media))
			return;
	}

	destroy_on_idle(dialog);
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
	GList	*uris, *next;
	guchar	*icon = NULL;

	if (!selection_data->data)
		return;

	uris = uri_list_to_glist(selection_data->data);

	if (g_list_length(uris) == 1)
		icon = g_strdup(get_local_path((guchar *) uris->data));

	for (next = uris; next; next = next->next)
		g_free(next->data);
	g_list_free(uris);

	if (!icon)
	{
		delayed_error(_("You should drop a single local icon file "
				    "onto the drop box - that icon will be "
				    "used for this file from now on."));
		return;
	}

	do_set_icon(dialog, icon);

	g_free(icon);
}

/* Called if the user clicks on the OK button on the Set Icon dialog */
static void get_path_set_icon(GtkWidget *dialog)
{
	GtkEntry *entry;
	guchar	*icon;

	entry = gtk_object_get_data(GTK_OBJECT(dialog), "icon_path");
	g_return_if_fail(entry != NULL);

	icon = gtk_entry_get_text(entry);

	do_set_icon(dialog, icon);
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

static void show_icon_help(gpointer data)
{
	report_error(
		_("Enter the full path of a file that contains a valid "
		  "image to be used as the icon for this file or directory."));
}

/* Set the icon for the given MIME type.  We copy the file. */
static gboolean set_icon_for_type(const MIME_type *type, gchar *iconpath,
				  gboolean just_media)
{
	gchar *target;
	gchar *leaf;
	gchar *dir;
	GList *paths;

	/* XXX: Should convert to XPM format... */

	if (just_media)
		leaf = g_strconcat(type->media_type, ".xpm", NULL);
	else
		leaf = g_strconcat(type->media_type, "_", type->subtype,
								".xpm", NULL);

	target = choices_find_path_save(leaf, "MIME-icons", TRUE);
	if (!target)
	{
		delayed_error(_("Setting icon disabled by CHOICESPATH"));
		g_free(leaf);
		return FALSE;
	}

	dir = g_dirname(target);
	paths = g_list_append(NULL, iconpath);

	action_copy(paths, dir, leaf, -1);

	g_free(leaf);
	g_free(dir);
	g_free(target);
	g_list_free(paths);

	return TRUE;
}

static void get_dir(gpointer key, gpointer value, gpointer data)
{
	GHashTable *names = (GHashTable *) data;
	gchar *dir;
	
	dir = g_dirname(value);	/* Freed in add_dir_to_menu */
	if (dir)
	{
		g_hash_table_insert(names, dir, NULL);
	}
}

static void open_icon_dir(GtkMenuItem *item, gpointer data)
{
	FilerWindow *filer;
	char *dir;

	gtk_label_get(GTK_LABEL(GTK_BIN(item)->child), &dir);
	filer = filer_opendir(dir, NULL);
	if (filer)
		display_set_thumbs(filer, TRUE);
}

static void add_dir_to_menu(gpointer key, gpointer value, gpointer data)
{
	GtkMenuShell *menu = (GtkMenuShell *) data;
	GtkWidget *item;
	
	item = gtk_menu_item_new_with_label(key);
#ifdef GTK2
	gtk_widget_set_accel_path(item, NULL, NULL);
#else
	gtk_widget_lock_accelerators(item);
#endif
	gtk_signal_connect(GTK_OBJECT(item), "activate",
			GTK_SIGNAL_FUNC(open_icon_dir), NULL);
	g_free(key);
	gtk_menu_shell_append(menu, item);
}

static void show_current_dirs_menu(GtkWidget *button, gpointer data)
{
	GHashTable *names;
	GtkWidget *menu;

	names = g_hash_table_new(g_str_hash, g_str_equal);

	g_hash_table_foreach(glob_icons, get_dir, names);
	if (g_hash_table_size(glob_icons) == 0)
	{
		/* TODO: Include MIME-icons? */
		delayed_error(_("You have not yet set any special icons; "
			"therefore, I have no directories to show you"));
		return;
	}
	
	menu = gtk_menu_new();

	g_hash_table_foreach(names, add_dir_to_menu, menu);

	g_hash_table_destroy(names);

	show_popup_menu(menu, gtk_get_current_event(), 0);
}
