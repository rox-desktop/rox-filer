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
#include <libxml/parser.h>
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
#include "support.h"

#define DELETE_ICON 1

/* Used to index the 'radios' array... */
#define SET_MEDIA 2
#define SET_TYPE 1
#define SET_PATH 0		/* Store in globicons */
#define SET_COPY 3		/* Create .DirIcon */

static GHashTable *glob_icons = NULL; /* Pathname -> Icon pathname */

/* Static prototypes */
static const char *process_globicons_line(gchar *line);
static gboolean free_globicon(gpointer key, gpointer value, gpointer data);
static void get_path_set_icon(GtkWidget *dialog);
static void show_icon_help(gpointer data);
static void write_globicons(void);
static void show_current_dirs_menu(GtkWidget *button, gpointer data);
static void add_globicon(const gchar *path, const gchar *icon);
static void drag_icon_dropped(GtkWidget	 	*frame,
		       	      GdkDragContext    *context,
		       	      gint              x,
		       	      gint              y,
		       	      GtkSelectionData  *selection_data,
		       	      guint             info,
		       	      guint32           time,
		       	      GtkWidget	 	*dialog);
static gboolean set_icon_for_type(MIME_type *type, const gchar *iconpath,
				  gboolean just_media);
static void delete_globicon(const gchar *path);

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
			gchar *path, *icon_path;

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

			icon_path = xmlNodeGetContent(icon);
			path = expand_path(match);
			g_hash_table_insert(glob_icons, path, icon_path);
			g_free(match);
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
void check_globicon(const guchar *path, DirItem *item)
{
	gchar *gi;

	g_return_if_fail(item && !item->image);

	gi = g_hash_table_lookup(glob_icons, path);
	if (gi)
		item->image = g_fscache_lookup(pixmap_cache, gi);
}

gboolean create_diricon(const guchar *filepath, const guchar *iconpath)
{
	MaskedPixmap *pic;
	gchar	*icon_path;

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

	icon_path = make_path(filepath, ".DirIcon")->str;
	gdk_pixbuf_save(pic->huge_pixbuf, icon_path,
			"png", NULL,
			"tEXt::Software", PROJECT,
			NULL);
	g_object_unref(pic);

	dir_check_this(filepath);

	return TRUE;
}

/* Add a globicon mapping for the given file to the given icon path */
gboolean set_icon_path(const guchar *filepath, const guchar *iconpath)
{
	struct stat icon;
	MaskedPixmap *pic;

	/* Check if file exists */
	if (mc_stat(iconpath, &icon))
	{
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
	g_object_unref(pic);

	/* Add the globicon mapping and update visible icons */
	add_globicon(filepath, iconpath);

	return TRUE;
}

static void dialog_response(GtkWidget *dialog, gint response, gpointer data)
{
	if (response == GTK_RESPONSE_OK)
		get_path_set_icon(dialog);
	else if (response == GTK_RESPONSE_CANCEL)
		gtk_widget_destroy(dialog);
	else if (response == DELETE_ICON)
	{
		const gchar *path;
		gchar *icon_path;

		path = g_object_get_data(G_OBJECT(dialog), "pathname");
		g_return_if_fail(path != NULL);

		delete_globicon(path);

		icon_path = make_path(path, ".DirIcon")->str;
		if (access(icon_path, F_OK) == 0)
		{
			GList *list;

			list = g_list_prepend(NULL, icon_path);
			action_delete(list);
			g_list_free(list);
		}

		gtk_widget_destroy(dialog);
	}
}

/* Display a dialog box allowing the user to set the icon for
 * a file or directory.
 */
void icon_set_handler_dialog(DirItem *item, const guchar *path)
{
	struct stat	info;
	guchar		*tmp;
	GtkDialog	*dialog;
	GtkWidget	*frame, *hbox, *vbox2;
	GtkWidget	*entry, *label, *button, *align, *icon;
	GtkWidget	**radio;
	GtkRadioButton	*group;
	GtkTargetEntry 	targets[] = {
		{"text/uri-list", 0, TARGET_URI_LIST},
	};
	char	*gi;
	
	g_return_if_fail(item != NULL && path != NULL);

	gi = g_hash_table_lookup(glob_icons, path);

	dialog = GTK_DIALOG(gtk_dialog_new());
	gtk_dialog_set_has_separator(dialog, FALSE);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);
	g_object_set_data_full(G_OBJECT(dialog), "pathname",
				 strdup(path), g_free);

	gtk_window_set_title(GTK_WINDOW(dialog), _("Set icon"));

	radio = g_new(GtkWidget *, 4);

	tmp = g_strdup_printf(_("Set icon for all `%s/<anything>'"),
			item->mime_type->media_type);
	radio[SET_MEDIA] = gtk_radio_button_new_with_label(NULL, tmp);
	gtk_box_pack_start(GTK_BOX(dialog->vbox),
			radio[SET_MEDIA], FALSE, TRUE, 0);
	g_free(tmp);
	group = GTK_RADIO_BUTTON(radio[SET_MEDIA]);

	tmp = g_strdup_printf(_("Only for the type `%s/%s'"),
			item->mime_type->media_type,
			item->mime_type->subtype);
	radio[SET_TYPE] = gtk_radio_button_new_with_label_from_widget(group,
									tmp);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), radio[SET_TYPE],
			FALSE, TRUE, 0);
	g_free(tmp);

	tmp = g_strdup_printf(_("Only for the file `%s'"), path);
	radio[SET_PATH] = gtk_radio_button_new_with_label_from_widget(group,
									tmp);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), radio[SET_PATH],
			FALSE, TRUE, 0);
	gtk_tooltips_set_tip(tooltips, radio[SET_PATH],
			_("Add the file and image filenames to your "
			"personal list. The setting will be lost if the image "
			"or the file is moved."), NULL);
	g_free(tmp);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio[SET_PATH]), TRUE);

	/* If it's a directory, offer to create a .DirIcon */
	if (mc_stat(path, &info) == 0 && S_ISDIR(info.st_mode))
	{
		radio[3] = gtk_radio_button_new_with_label_from_widget(
					group, _("Copy image into directory"));
		gtk_box_pack_start(GTK_BOX(dialog->vbox), radio[SET_COPY],
					FALSE, TRUE, 0);
		gtk_tooltips_set_tip(tooltips, radio[SET_COPY],
			_("Copy the image inside the directory, as "
			"a hidden file called '.DirIcon'. "
			"All users will than see the "
			"icon, and you can move the directory around safely. "
			"This is usually the best option if you can write to "
			"the directory."), NULL);

		if (access(path, W_OK) == 0)
			gtk_toggle_button_set_active(
					GTK_TOGGLE_BUTTON(radio[SET_COPY]),
					TRUE);
	}
	else
		radio[SET_COPY] = NULL;

	g_object_set_data_full(G_OBJECT(dialog), "radios", radio, g_free);
	g_object_set_data(G_OBJECT(dialog), "mime_type", item->mime_type);

	frame = gtk_frame_new(NULL);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), frame, TRUE, TRUE, 4);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 4);

	gtk_drag_dest_set(frame, GTK_DEST_DEFAULT_ALL,
			targets, sizeof(targets) / sizeof(*targets),
			GDK_ACTION_COPY);
	g_signal_connect(frame, "drag_data_received",
			G_CALLBACK(drag_icon_dropped), dialog);

	vbox2 = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), vbox2);

	label = gtk_label_new(_("Drop an icon file here"));
	gtk_misc_set_padding(GTK_MISC(label), 10, 10);
	gtk_box_pack_start(GTK_BOX(vbox2), label, TRUE, TRUE, 0);
	align = gtk_alignment_new(1, 1, 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox2), align, FALSE, TRUE, 0);
	button = gtk_button_new();
	gtk_container_add(GTK_CONTAINER(align), button);
	icon = gtk_image_new_from_pixmap(im_dirs->pixmap, im_dirs->mask);
	gtk_container_add(GTK_CONTAINER(button), icon);
	gtk_tooltips_set_tip(tooltips, button,
			_("Menu of directories previously used for icons"),
			NULL);
	g_signal_connect(button, "clicked",
			G_CALLBACK(show_current_dirs_menu), NULL);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, FALSE, TRUE, 4);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_hseparator_new(), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("OR")),
						FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_hseparator_new(), TRUE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, FALSE, TRUE, 0);

	label = gtk_label_new(_("Enter the path of an icon file:")),
	gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 4);

	gtk_box_pack_start(GTK_BOX(hbox),
			new_help_button(show_icon_help, NULL), FALSE, TRUE, 0);

	entry = gtk_entry_new();
	/* Set the current icon as the default text if there is one */
	if (gi)
		gtk_entry_set_text(GTK_ENTRY(entry), gi);

	gtk_box_pack_start(GTK_BOX(dialog->vbox), entry, FALSE, TRUE, 0);
	gtk_widget_grab_focus(entry);
	g_object_set_data(G_OBJECT(dialog), "icon_path", entry);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

	button = button_new_mixed(GTK_STOCK_DELETE, _("_Remove"));
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_dialog_add_action_widget(dialog, button, DELETE_ICON);
	gtk_dialog_add_buttons(dialog,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OK, GTK_RESPONSE_OK,
			NULL);
	g_signal_connect(dialog, "response", G_CALLBACK(dialog_response), NULL);
	gtk_dialog_set_default_response(dialog, GTK_RESPONSE_OK);

	gtk_widget_show_all(GTK_WIDGET(dialog));
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
static const char *process_globicons_line(gchar *line)
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
static void add_globicon(const gchar *path, const gchar *icon)
{
	g_hash_table_insert(glob_icons, g_strdup(path), g_strdup(icon));

	/* Rewrite the globicons file */
	write_globicons();

	/* Make sure any visible icons for the file are updated */
	examine(path);
}

/* Remove the globicon for a certain path */
static void delete_globicon(const gchar *path)
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
static void do_set_icon(GtkWidget *dialog, const gchar *icon)
{
	guchar  *path = NULL;
	GtkToggleButton **radio;

	path = g_object_get_data(G_OBJECT(dialog), "pathname");
	g_return_if_fail(path != NULL);

	radio = g_object_get_data(G_OBJECT(dialog), "radios");
	g_return_if_fail(radio != NULL);

	if (gtk_toggle_button_get_active(radio[0]))
	{
		if (!set_icon_path(path, icon))
			return;
	}
	else if (radio[SET_COPY] &&
			gtk_toggle_button_get_active(radio[SET_COPY]))
	{
		if (!create_diricon(path, icon))
			return;
	}
	else
	{
		gboolean just_media;
		MIME_type *type;
		
		type = g_object_get_data(G_OBJECT(dialog), "mime_type");
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
	const gchar *icon;

	entry = g_object_get_data(G_OBJECT(dialog), "icon_path");
	g_return_if_fail(entry != NULL);

	icon = gtk_entry_get_text(entry);

	do_set_icon(dialog, icon);
}

static void show_icon_help(gpointer data)
{
	info_message(
		_("Enter the full path of a file that contains a valid "
		  "image to be used as the icon for this file or directory."));
}

/* Set the icon for the given MIME type.  We copy the file. */
static gboolean set_icon_for_type(MIME_type *type, const gchar *iconpath,
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
	paths = g_list_append(NULL, (gchar *) iconpath);

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
	const char *dir;

	dir = gtk_label_get_text(GTK_LABEL(GTK_BIN(item)->child));
	filer = filer_opendir(dir, NULL);
	if (filer)
		display_set_thumbs(filer, TRUE);
}

static void add_dir_to_menu(gpointer key, gpointer value, gpointer data)
{
	GtkMenuShell *menu = (GtkMenuShell *) data;
	GtkWidget *item;
	
	item = gtk_menu_item_new_with_label(key);
	gtk_widget_set_accel_path(item, NULL, NULL);	/* XXX */
	g_signal_connect(item, "activate",
			G_CALLBACK(open_icon_dir), NULL);
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
