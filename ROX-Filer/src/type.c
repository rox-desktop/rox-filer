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

/* type.c - code for dealing with filetypes */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/param.h>
#include <fnmatch.h>

#ifdef WITH_GNOMEVFS
# include <libgnomevfs/gnome-vfs.h>
# include <libgnomevfs/gnome-vfs-mime.h>
# include <libgnomevfs/gnome-vfs-mime-handlers.h>
# include <libgnomevfs/gnome-vfs-application-registry.h>
#endif

#include "global.h"

#include "string.h"
#include "fscache.h"
#include "main.h"
#include "pixmaps.h"
#include "run.h"
#include "gui_support.h"
#include "choices.h"
#include "type.h"
#include "support.h"
#include "diritem.h"
#include "dnd.h"
#include "options.h"
#include "filer.h"

/* Colours for file types (same order as base types) */
static gchar *opt_type_colours[][2] = {
	{"display_err_colour",  "#ff0000"},
	{"display_unkn_colour", "#000000"},
	{"display_dir_colour",  "#000080"},
	{"display_pipe_colour", "#444444"},
	{"display_sock_colour", "#ff00ff"},
	{"display_file_colour", "#000000"},
	{"display_cdev_colour", "#000000"},
	{"display_bdev_colour", "#000000"},
	{"display_exec_colour", "#006000"},
	{"display_adir_colour", "#006000"}
};
#define NUM_TYPE_COLOURS\
		(sizeof(opt_type_colours) / sizeof(opt_type_colours[0]))

/* Parsed colours for file types */
static Option o_type_colours[NUM_TYPE_COLOURS];
static GdkColor	type_colours[NUM_TYPE_COLOURS];

/* Static prototypes */
static void load_mime_types(void);
static void alloc_type_colours(void);
char *get_action_save_path(GtkWidget *dialog);
static void edit_mime_types(guchar *unused);
static void reread_mime_files(guchar *unused);
static MIME_type *get_mime_type(const gchar *type_name, gboolean can_create);
static GList *build_type_reread(Option *none, xmlNode *node, guchar *label);
static GList *build_type_edit(Option *none, xmlNode *node, guchar *label);

/* When working out the type for a file, this hash table is checked
 * first...
 */
static GHashTable *literal_hash = NULL;	/* name -> MIME-type */

/* Maps extensions to MIME_types (eg 'png'-> MIME_type *).
 * Extensions may contain dots; 'tar.gz' matches '*.tar.gz', etc.
 * The hash table is consulted from each dot in the string in turn
 * (First .ps.gz, then try .gz)
 */
static GHashTable *extension_hash = NULL;

/* The first pattern in the list which matches is used */
typedef struct pattern {
	gint len;		/* Used for sorting */
	gchar *glob;
	MIME_type *type;
} Pattern;
static GPtrArray *glob_patterns = NULL;	/* [Pattern] */

/* Hash of all allocated MIME types, indexed by "media/subtype".
 * MIME_type structs are never freed; this table prevents memory leaks
 * when rereading the config files.
 */
static GHashTable *type_hash = NULL;

/* Most things on Unix are text files, so this is the default type */
MIME_type *text_plain;
MIME_type *inode_directory;
MIME_type *inode_pipe;
MIME_type *inode_socket;
MIME_type *inode_block_dev;
MIME_type *inode_char_dev;
MIME_type *application_executable;
MIME_type *inode_unknown;

static Option o_display_colour_types;

void type_init(void)
{
	int		i;
	
	extension_hash = g_hash_table_new(g_str_hash, g_str_equal);
	type_hash = g_hash_table_new(g_str_hash, g_str_equal);

	text_plain = get_mime_type("text/plain", TRUE);
	inode_directory = get_mime_type("inode/directory", TRUE);
	inode_pipe = get_mime_type("inode/fifo", TRUE);
	inode_socket = get_mime_type("inode/socket", TRUE);
	inode_block_dev = get_mime_type("inode/blockdevice", TRUE);
	inode_char_dev = get_mime_type("inode/chardevice", TRUE);
	application_executable = get_mime_type("application/x-executable", TRUE);
	inode_unknown = get_mime_type("inode/unknown", TRUE);

	load_mime_types();

	option_register_widget("type-edit", build_type_edit);
	option_register_widget("type-reread", build_type_reread);
	
	option_add_int(&o_display_colour_types, "display_colour_types", TRUE);
	
	for (i = 0; i < NUM_TYPE_COLOURS; i++)
		option_add_string(&o_type_colours[i],
				  opt_type_colours[i][0],
				  opt_type_colours[i][1]);
	alloc_type_colours();

	option_add_notify(alloc_type_colours);
}

/* Returns the MIME_type structure for the given type name. It is looked
 * up in type_hash and returned if found. If not found (and can_create is
 * TRUE) then a new MIME_type is made, added to type_hash and returned.
 * NULL is returned if type_name is not in type_hash and can_create is
 * FALSE, or if type_name does not contain a '/' character.
 */
static MIME_type *get_mime_type(const gchar *type_name, gboolean can_create)
{
        MIME_type *mtype;
	gchar *slash;

	mtype = g_hash_table_lookup(type_hash, type_name);
	if (mtype || !can_create)
		return mtype;

	slash = strchr(type_name, '/');
	g_return_val_if_fail(slash != NULL, NULL);     /* XXX: Report nicely */

	mtype = g_new(MIME_type, 1);
	mtype->media_type = g_strndup(type_name, slash - type_name);
	mtype->subtype = g_strdup(slash + 1);
	mtype->image = NULL;

	g_hash_table_insert(type_hash, g_strdup(type_name), mtype);

	return mtype;
}

const char *basetype_name(DirItem *item)
{
	if (item->flags & ITEM_FLAG_SYMLINK)
		return _("Sym link");
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
		return _("Mount point");
	else if (item->flags & ITEM_FLAG_APPDIR)
		return _("App dir");

	switch (item->base_type)
	{
		case TYPE_FILE:
			return _("File");
		case TYPE_DIRECTORY:
			return _("Dir");
		case TYPE_CHAR_DEVICE:
			return _("Char dev");
		case TYPE_BLOCK_DEVICE:
			return _("Block dev");
		case TYPE_PIPE:
			return _("Pipe");
		case TYPE_SOCKET:
			return _("Socket");
	}
	
	return _("Unknown");
}

/*			MIME-type guessing 			*/

/* Get the type of this file - stats the file and uses that if
 * possible. For regular or missing files, uses the pathname.
 */
MIME_type *type_get_type(const guchar *path)
{
	struct stat	info;
	MIME_type	*type = NULL;
	int		base = TYPE_FILE;
	gboolean	exec = FALSE;

	if (mc_stat(path, &info) == 0)
	{
		base = mode_to_base_type(info.st_mode);
		if (info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
			exec = TRUE;
	}

	if (base == TYPE_FILE)
		type = type_from_path(path);

	if (!type)
	{
		if (base == TYPE_FILE && exec)
			type = application_executable;
		else
			type = mime_type_from_base_type(base);
	}

	return type;
}

/* Returns a pointer to the MIME-type.
 * NULL if we can't think of anything.
 */
MIME_type *type_from_path(const char *path)
{
	const char *ext, *dot, *leafname;
	char *lower;
	MIME_type *type = NULL;
	int	i;

#if 0
# ifdef WITH_GNOMEVFS
	if (o_use_gnomevfs.int_value)
		return get_mime_type(gnome_vfs_mime_type_from_name(path), TRUE);
# endif
#endif

	leafname = g_basename(path);

	type = g_hash_table_lookup(literal_hash, leafname);
	if (type)
		return type;
	lower = g_utf8_strdown(leafname, -1);
	type = g_hash_table_lookup(literal_hash, lower);
	if (type)
		goto out;

	ext = leafname;

	while ((dot = strchr(ext, '.')))
	{
		ext = dot + 1;

		type = g_hash_table_lookup(extension_hash, ext);

		if (type)
			goto out;

		type = g_hash_table_lookup(extension_hash,
					lower + (ext - leafname));
		if (type)
			goto out;
	}

	for (i = 0; i < glob_patterns->len; i++)
	{
		Pattern *p = glob_patterns->pdata[i];

		if (fnmatch(p->glob, leafname, 0) == 0 ||
		    fnmatch(p->glob, lower, 0) == 0)
		{
			type = p->type;
			goto out;
		}
	}

out:
	g_free(lower);

	return type;
}

/* Returns the file/dir in Choices for handling this type.
 * NULL if there isn't one. g_free() the result.
 */
static char *handler_for(MIME_type *type)
{
	char	*type_name;
	char	*open;

	type_name = g_strconcat(type->media_type, "_", type->subtype, NULL);
	open = choices_find_path_load(type_name, "MIME-types");
	g_free(type_name);

	if (!open)
		open = choices_find_path_load(type->media_type, "MIME-types");

	return open;
}

/*			Actions for types 			*/

gboolean type_open(const char *path, MIME_type *type)
{
	gchar *argv[] = {NULL, NULL, NULL};
	char		*open;
	gboolean	retval;
	struct stat	info;

	argv[1] = (char *) path;

	open = handler_for(type);
	if (!open)
		return FALSE;

	if (stat(open, &info))
	{
		report_error("stat(%s): %s", open, g_strerror(errno));
		g_free(open);
		return FALSE;
	}

	if (S_ISDIR(info.st_mode))
		argv[0] = g_strconcat(open, "/AppRun", NULL);
	else
		argv[0] = open;

	retval = rox_spawn(home_dir, (const gchar **) argv);

	if (argv[0] != open)
		g_free(argv[0]);

	g_free(open);
	
	return retval;
}

/* Return the image for this type, loading it if needed.
 * Places to check are: (eg type="text_plain", base="text")
 * 1. Choices:MIME-icons/<type>
 * 2. Choices:MIME-icons/<base>
 * 3. Unknown type icon.
 *
 * Note: You must g_object_unref() the image afterwards.
 */
MaskedPixmap *type_to_icon(MIME_type *type)
{
	char	*path;
	char	*type_name;
	time_t	now;

	if (type == NULL)
	{
		g_object_ref(im_unknown);
		return im_unknown;
	}

	now = time(NULL);
	/* Already got an image? */
	if (type->image)
	{
		/* Yes - don't recheck too often */
		if (abs(now - type->image_time) < 2)
		{
			g_object_ref(type->image);
			return type->image;
		}
		g_object_unref(type->image);
		type->image = NULL;
	}

	type_name = g_strconcat(type->media_type, "_",
				type->subtype, ".png", NULL);
	path = choices_find_path_load(type_name, "MIME-icons");
	if (!path)
	{
		strcpy(type_name + strlen(type->media_type), ".png");
		path = choices_find_path_load(type_name, "MIME-icons");
	}
	
	g_free(type_name);

	if (path)
	{
		type->image = g_fscache_lookup(pixmap_cache, path);
		g_free(path);
	}

	if (!type->image)
	{
		/* One ref from the type structure, one returned */
		type->image = im_unknown;
		g_object_ref(im_unknown);
	}

	type->image_time = now;
	
	g_object_ref(type->image);
	return type->image;
}

GdkAtom type_to_atom(MIME_type *type)
{
	char	*str;
	GdkAtom	retval;
	
	g_return_val_if_fail(type != NULL, GDK_NONE);

	str = g_strconcat(type->media_type, "/", type->subtype, NULL);
	retval = gdk_atom_intern(str, FALSE);
	g_free(str);
	
	return retval;
}

void show_shell_help(gpointer data)
{
	info_message(_("Enter a shell command which will load \"$1\" into "
			"a suitable program. Eg:\n\n"
			"gimp \"$1\""));
}

/* Called if the user clicks on the OK button */
static void set_shell_action(GtkWidget *dialog)
{
	GtkEntry *entry;
	GtkToggleButton *for_all;
	const guchar *command;
	gchar	*tmp, *path;
	int	error = 0, len;
	FILE	*file;

	entry = g_object_get_data(G_OBJECT(dialog), "shell_command");
	for_all = g_object_get_data(G_OBJECT(dialog), "set_for_all");
	g_return_if_fail(entry != NULL);

	command = gtk_entry_get_text(entry);
	
	if (!strchr(command, '$'))
	{
		show_shell_help(NULL);
		return;
	}

	path = get_action_save_path(dialog);
	if (!path)
		return;
		
	tmp = g_strdup_printf("#! /bin/sh\nexec %s\n", command);
	len = strlen(tmp);
	
	file = fopen(path, "wb");
	if (fwrite(tmp, 1, len, file) < len)
		error = errno;
	if (fclose(file) && error == 0)
		error = errno;
	if (chmod(path, 0777))
		error = errno;

	if (error)
		report_error(g_strerror(errno));

	g_free(tmp);
	g_free(path);

	gtk_widget_destroy(dialog);
}

static void set_action_response(GtkWidget *dialog, gint response, gpointer data)
{
	if (response == GTK_RESPONSE_OK)
		set_shell_action(dialog);
	gtk_widget_destroy(dialog);
}

/* Called when a URI list is dropped onto the box in the Set Run Action
 * dialog. Make sure it's an application, and make that the default
 * handler.
 */
static void drag_app_dropped(GtkWidget		*eb,
		      GdkDragContext    *context,
		      gint              x,
		      gint              y,
		      GtkSelectionData  *selection_data,
		      guint             info,
		      guint32           time,
		      GtkWidget		*dialog)
{
	GList	*uris;
	const gchar *app = NULL;
	DirItem	*item;

	if (!selection_data->data)
		return; 		/* Timeout? */

	uris = uri_list_to_glist(selection_data->data);

	if (g_list_length(uris) == 1)
		app = get_local_path((guchar *) uris->data);
	g_list_free(uris);

	if (!app)
	{
		delayed_error(
			_("You should drop a single (local) application "
			"onto the drop box - that application will be "
			"used to load files of this type in future"));
		return;
	}

	item = diritem_new("");
	diritem_restat(app, item, NULL);
	if (item->flags & (ITEM_FLAG_APPDIR | ITEM_FLAG_EXEC_FILE))
	{
		guchar	*path;

		path = get_action_save_path(dialog);

		if (path)
		{
			if (symlink(app, path))
				delayed_error("symlink: %s",
						g_strerror(errno));
			else
				destroy_on_idle(dialog);
		}

		g_free(path);
	}
	else
		delayed_error(
			_("This is not a program! Give me an application "
			"instead!"));

	diritem_free(item);
}

/* Find the current command which is used to run files of this type.
 * Returns NULL on failure. g_free() the result.
 */
static guchar *get_current_command(MIME_type *type)
{
	struct stat	info;
	char *handler, *nl, *data = NULL;
	long len;
	guchar *command = NULL;

	handler = handler_for(type);

	if (!handler)
		return NULL;		/* No current handler */

	if (stat(handler, &info))
		goto out;		/* Can't stat */

	if ((!S_ISREG(info.st_mode)) || info.st_size > 256)
		goto out;		/* Only use small regular files */
	
	if (!load_file(handler, &data, &len))
		goto out;		/* Didn't load OK */

	if (strncmp(data, "#! /bin/sh\nexec ", 16) != 0)
		goto out;		/* Not one of ours */

	nl = strchr(data + 16, '\n');
	if (!nl)
		goto out;		/* No newline! */

	command = g_strndup(data + 16, nl - data - 16);
out:
	g_free(handler);
	g_free(data);
	return command;
}

/* Find the current command which is used to run files of this type,
 * and return a textual description of it.
 * g_free() the result.
 */
gchar *describe_current_command(MIME_type *type)
{
	char *handler;
	char *desc = NULL;
	struct stat info;
	char *target;

	g_return_val_if_fail(type != NULL, NULL);

	if (type == application_executable)
		return g_strdup(_("Execute file"));

	handler = handler_for(type);

	if (!handler)
		return g_strdup(_("No run action defined"));

	target = readlink_dup(handler);
	if (target)
	{
		/* Cope with relative paths (shouldn't normally be needed) */

		if (target[0] == '/')
		{
			g_free(handler);
			handler = target;
		}
		else
		{
			gchar *dir;

			dir = g_dirname(handler);
			g_free(handler);
			handler = g_strconcat(dir, "/", target, NULL);
			g_free(target);
			g_free(dir);
		}
	}

	if (mc_stat(handler, &info) !=0 )
	{
		desc = g_strdup_printf(_("Error in handler %s: %s"), handler,
					g_strerror(errno));
		goto out;
	}

	if (S_ISDIR(info.st_mode))
	{
		gchar *tmp;
		uid_t dir_uid = info.st_uid;

		tmp = make_path(handler, "AppRun")->str;

		if (mc_lstat(tmp, &info) != 0 || info.st_uid != dir_uid
			|| !(info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
			desc = g_strdup_printf(
				_("Invalid application %s (bad AppRun)"),
				handler);
		/* Else, just report handler... */

		goto out;
	}

	/* It's not an application directory, and it's not a symlink... */

	if (access(handler, X_OK) != 0)
	{
		desc = g_strdup_printf(_("Non-executable %s"), handler);
		goto out;
	}

	desc = get_current_command(type);
out:
	if (!desc)
		desc = handler;
	else
		g_free(handler);

	return desc;
}

/* Display a dialog box allowing the user to set the default run action
 * for this type.
 */
void type_set_handler_dialog(MIME_type *type)
{
	guchar		*tmp;
	gchar           *handler;
	GtkDialog	*dialog;
	GtkWidget	*frame, *entry, *label;
	GtkWidget	*radio, *eb, *hbox;
	GtkTargetEntry 	targets[] = {
		{"text/uri-list", 0, TARGET_URI_LIST},
	};

	g_return_if_fail(type != NULL);

	dialog = GTK_DIALOG(gtk_dialog_new());
	gtk_dialog_set_has_separator(dialog, FALSE);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);

	g_object_set_data(G_OBJECT(dialog), "mime_type", type);

	gtk_window_set_title(GTK_WINDOW(dialog), _("Set run action"));

	tmp = g_strdup_printf(_("Set default for all `%s/<anything>'"),
				type->media_type);
	radio = gtk_radio_button_new_with_label(NULL, tmp);
	g_free(tmp);
	g_object_set_data(G_OBJECT(dialog), "set_for_all", radio);

	tmp = g_strdup_printf(_("Only for the type `%s/%s'"), type->media_type,
			type->subtype);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), radio, FALSE, TRUE, 0);
	radio = gtk_radio_button_new_with_label(
			gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio)),
			tmp);
	g_free(tmp);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), radio, FALSE, TRUE, 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), TRUE);

	frame = gtk_frame_new(NULL);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), frame, TRUE, TRUE, 4);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	eb = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(frame), eb);

	gtk_container_set_border_width(GTK_CONTAINER(eb), 4);

	handler = handler_for(type);
	if (handler)
	{
		char *link;

		link = readlink_dup(handler);
		if (link)
		{
			char *msg;

			msg = g_strdup_printf(_("Currently %s"), link);
			gtk_tooltips_set_tip(tooltips, eb, msg, NULL);
			g_free(link);
			g_free(msg);
		}
		g_free(handler);
	}

	gtk_drag_dest_set(eb, GTK_DEST_DEFAULT_ALL,
			targets, sizeof(targets) / sizeof(*targets),
			GDK_ACTION_COPY);
	g_signal_connect(eb, "drag_data_received",
			G_CALLBACK(drag_app_dropped), dialog);

	label = gtk_label_new(_("Drop a suitable\napplication here"));
	gtk_misc_set_padding(GTK_MISC(label), 10, 20);
	gtk_container_add(GTK_CONTAINER(eb), label);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, FALSE, TRUE, 4);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_hseparator_new(), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("OR")),
						FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_hseparator_new(), TRUE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, FALSE, TRUE, 0);

	label = gtk_label_new(_("Enter a shell command:")),
	gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 4);

	gtk_box_pack_start(GTK_BOX(hbox),
			new_help_button(show_shell_help, NULL), FALSE, TRUE, 0);

	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(dialog->vbox), entry, FALSE, TRUE, 0);
	gtk_widget_grab_focus(entry);
	g_object_set_data(G_OBJECT(dialog), "shell_command", entry);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

	/* If possible, fill in the entry box with the current command */
	tmp = get_current_command(type);
	if (tmp)
	{
		gtk_entry_set_text(GTK_ENTRY(entry), tmp);
		gtk_editable_set_position(GTK_EDITABLE(entry), -1);
		g_free(tmp);
	}
	else
	{
		gtk_entry_set_text(GTK_ENTRY(entry), " \"$1\"");
		gtk_editable_set_position(GTK_EDITABLE(entry), 0);
	}

	gtk_dialog_add_buttons(dialog,
			GTK_STOCK_CANCEL, GTK_RESPONSE_DELETE_EVENT,
			GTK_STOCK_OK, GTK_RESPONSE_OK,
			NULL);

	hbox = gtk_hbox_new(TRUE, 4);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, FALSE, TRUE, 0);

	gtk_dialog_set_default_response(dialog, GTK_RESPONSE_OK);
	
	g_signal_connect(dialog, "response",
			G_CALLBACK(set_action_response), NULL);

	gtk_widget_show_all(GTK_WIDGET(dialog));
}

/* The user wants to set a new default action for files of this type.
 * Removes the current binding if possible and returns the path to
 * save the new one to. NULL means cancel. g_free() the result.
 */
char *get_action_save_path(GtkWidget *dialog)
{
	guchar		*path = NULL;
	struct stat 	info;
	guchar 		*type_name = NULL;
	MIME_type	*type;
	GtkToggleButton *for_all;

	g_return_val_if_fail(dialog != NULL, NULL);
	type = g_object_get_data(G_OBJECT(dialog), "mime_type");
	for_all = g_object_get_data(G_OBJECT(dialog), "set_for_all");
	g_return_val_if_fail(for_all != NULL && type != NULL, NULL);

	if (gtk_toggle_button_get_active(for_all))
		type_name = g_strdup(type->media_type);
	else
		type_name = g_strconcat(type->media_type, "_",
				type->subtype, NULL);

	path = choices_find_path_save("", PROJECT, FALSE);
	if (!path)
	{
		report_error(
		_("Choices saving is disabled by CHOICESPATH variable"));
		goto out;
	}
	g_free(path);

	path = choices_find_path_save(type_name, "MIME-types", TRUE);

	if (lstat(path, &info) == 0)
	{
		/* A binding already exists... */
		if (S_ISREG(info.st_mode) && info.st_size > 256)
		{
			if (get_choice(PROJECT,
				_("A run action already exists and is quite "
				"a big program - are you sure you want to "
				"delete it?"), 2, "Cancel", "Delete") != 1)
			{
				g_free(path);
				path = NULL;
				goto out;
			}
		}
		
		if (unlink(path))
		{
			report_error(_("Can't remove %s: %s"),
				path, g_strerror(errno));
			g_free(path);
			path = NULL;
			goto out;
		}
	}

out:
	g_free(type_name);
	return path;
}

MIME_type *mime_type_from_base_type(int base_type)
{
	switch (base_type)
	{
		case TYPE_FILE:
			return text_plain;
		case TYPE_DIRECTORY:
			return inode_directory;
		case TYPE_PIPE:
			return inode_pipe;
		case TYPE_SOCKET:
			return inode_socket;
		case TYPE_BLOCK_DEVICE:
			return inode_block_dev;
		case TYPE_CHAR_DEVICE:
			return inode_char_dev;
	}
	return inode_unknown;
}

/* Takes the st_mode field from stat() and returns the base type.
 * Should not be a symlink.
 */
int mode_to_base_type(int st_mode)
{
	if (S_ISREG(st_mode))
		return TYPE_FILE;
	else if (S_ISDIR(st_mode))
		return TYPE_DIRECTORY;
	else if (S_ISBLK(st_mode))
		return TYPE_BLOCK_DEVICE;
	else if (S_ISCHR(st_mode))
		return TYPE_CHAR_DEVICE;
	else if (S_ISFIFO(st_mode))
		return TYPE_PIPE;
	else if (S_ISSOCK(st_mode))
		return TYPE_SOCKET;

	return TYPE_ERROR;
}

/* Returns TRUE is this is something that is run by looking up its type
 * in MIME-types and, hence, can have its run action set.
 */
gboolean can_set_run_action(DirItem *item)
{
	g_return_val_if_fail(item != NULL, FALSE);

	return item->base_type == TYPE_FILE &&
		!(item->mime_type == application_executable);
}

/* To edit the MIME types, open a filer window for <Choices>/MIME-info */
static void edit_mime_types(guchar *unused)
{
	const gchar *path;
	struct stat info;

	mkdir(make_path(home_dir, ".mime")->str, 0700);
	path = make_path(home_dir, ".mime/mime-info")->str;
	mkdir(path, 0700);
	filer_opendir(path, NULL);

	path = "/usr/local/share/mime/mime-info";
	if (mc_stat(path, &info) == 0)
		filer_opendir(path, NULL);

	path = "/usr/share/mime/mime-info";
	if (mc_stat(path, &info) == 0)
		filer_opendir(path, NULL);
}

static void reread_mime_files(guchar *unused)
{
	load_mime_types();
}

static GList *build_type_reread(Option *none, xmlNode *node, guchar *label)
{
	GtkWidget *button, *align;

	g_return_val_if_fail(none == NULL, NULL);

	align = gtk_alignment_new(0.1, 0, 0.1, 0);
	button = gtk_button_new_with_label(_(label));
	gtk_container_add(GTK_CONTAINER(align), button);

	g_signal_connect_swapped(button, "clicked",
			G_CALLBACK(reread_mime_files), NULL);

	return g_list_append(NULL, align);
}

static GList *build_type_edit(Option *none, xmlNode *node, guchar *label)
{
	GtkWidget *button, *align;

	g_return_val_if_fail(none == NULL, NULL);

	align = gtk_alignment_new(0.1, 0, 0.1, 0);
	button = gtk_button_new_with_label(_(label));
	gtk_container_add(GTK_CONTAINER(align), button);

	g_signal_connect_swapped(button, "clicked",
			G_CALLBACK(edit_mime_types), NULL);

	return g_list_append(NULL, align);
}

/* Parse file type colours and allocate/free them as necessary */
static void alloc_type_colours(void)
{
	gboolean	success[NUM_TYPE_COLOURS];
	int		change_count = 0;	/* No. needing realloc */
	int		i;
	static gboolean	allocated = FALSE;

	/* Parse colours */
	for (i = 0; i < NUM_TYPE_COLOURS; i++)
	{
		GdkColor *c = &type_colours[i];
		gushort r = c->red;
		gushort g = c->green;
		gushort b = c->blue;

		gdk_color_parse(o_type_colours[i].value, &type_colours[i]);

		if (allocated && (c->red != r || c->green != g || c->blue != b))
			change_count++;
	}
	
	/* Free colours if they were previously allocated and
	 * have changed or become unneeded.
	 */
	if (allocated && (change_count || !o_display_colour_types.int_value))
	{
		gdk_colormap_free_colors(gdk_rgb_get_colormap(),
					 type_colours, NUM_TYPE_COLOURS);
		allocated = FALSE;
	}

	/* Allocate colours, unless they are still allocated (=> they didn't
	 * change) or we don't want them anymore.
	 * XXX: what should be done if allocation fails?
	 */
	if (!allocated && o_display_colour_types.int_value)
	{
		gdk_colormap_alloc_colors(gdk_rgb_get_colormap(),
				type_colours, NUM_TYPE_COLOURS,
				FALSE, TRUE, success);
		allocated = TRUE;
	}
}

/* Return a pointer to a (static) colour for this item. If colouring is
 * off, returns normal.
 */
GdkColor *type_get_colour(DirItem *item, GdkColor *normal)
{
	if (!o_display_colour_types.int_value)
		return normal;

	if (item->flags & ITEM_FLAG_EXEC_FILE)
		return &type_colours[8];
	else if (item->flags & ITEM_FLAG_APPDIR)
		return &type_colours[9];
	else
		return &type_colours[item->base_type];
}

/* Process the 'Patterns' value */
static void add_patterns(MIME_type *type, gchar *patterns, GHashTable *globs)
{
	while (1)
	{
		char *semi;

		semi = strchr(patterns, ';');
		if (semi)
			*semi = '\0';
		g_strstrip(patterns);
		if (patterns[0] == '*' && patterns[1] == '.' &&
				strpbrk(patterns + 2, "*?[") == NULL)
		{
			g_hash_table_insert(extension_hash,
					g_strdup(patterns + 2), type);
		}
		else if (strpbrk(patterns, "*?[") == NULL)
			g_hash_table_insert(literal_hash,
					g_strdup(patterns), type);
		else
			g_hash_table_insert(globs, g_strdup(patterns), type);
		if (!semi)
			return;
		patterns = semi + 1;
	}
}

/* Load and parse this file. literal_hash and extension_hash are updated
 * directly. Other patterns are added to 'globs'.
 */
static void import_file(const gchar *file, GHashTable *globs)
{
	MIME_type *type = NULL;
	GError *error = NULL;
	gchar  *data, *line;
	
	if (!g_file_get_contents(file, &data, NULL, &error))
	{
		delayed_error("Error loading MIME-Info database:\n%s",
				error->message);
		g_error_free(error);
		return;
	}

	line = data;

	while (line && *line)
	{
		char *nl;

		nl = strchr(line, '\n');
		if (!nl)
			break;
		*nl = '\0';

		if (*line == '[')
		{
			const gchar *end;

			type = NULL;

			end = strchr(line, ']');
			if (!end)
			{
				delayed_error(_("File '%s' corrupted!"), file);
				break;
			}

			if (strncmp(line + 1, "MIME-Info ", 10) == 0)
			{
				gchar *name;

				line += 11;
				while (*line == ' ' || *line == '\t')
					line++;
				name = g_strndup(line, end - line);
				g_strstrip(name);

				type = get_mime_type(name, TRUE);
				if (!type)
				{
					delayed_error(
						_("Invalid type '%s' in '%s'"),
						name, file);
					break;
				}
				g_free(name);
			}
		}
		else if (type)
		{
			char *eq;
			eq = strchr(line, '=');
			if (eq)
			{
				char *tmp = eq;

				while (tmp > line &&
					(tmp[-1] == ' ' || tmp[-1] == '\t'))
					tmp--;
				*tmp = '\0';

				eq++;
				while (*eq == ' ' || *eq == '\t')
					eq++;

				if (strcmp(line, "Patterns") == 0)
					add_patterns(type, eq, globs);
			}
		}

		line = nl + 1;
	}

	g_free(data);
}

/* Parse every .mimeinfo file in 'dir' */
static void import_for_dir(guchar *path, GHashTable *globs, gboolean *freedesk)
{
	DIR		*dir;
	struct dirent	*item;

	dir = opendir(path);
	if (!dir)
		return;

	while ((item = readdir(dir)))
	{
		gchar   *dot;

		dot = strrchr(item->d_name, '.');
		if (!dot)
			continue;
		if (strcmp(dot + 1, "mimeinfo") != 0)
			continue;

		if (*freedesk == FALSE &&
			!strcmp(item->d_name, "freedesktop-shared.mimeinfo"))
		{
			*freedesk = TRUE;
		}
		
		import_file(make_path(path, item->d_name)->str, globs);
	}

	closedir(dir);
}

static void add_to_glob_patterns(gpointer key, gpointer value, gpointer unused)
{
	Pattern *pattern;

	pattern = g_new(Pattern, 1);
	pattern->glob = g_strdup((gchar *) key);
	pattern->type = (MIME_type *) value;
	pattern->len = strlen(pattern->glob);

	g_ptr_array_add(glob_patterns, pattern);
}

static gint sort_by_strlen(gconstpointer a, gconstpointer b)
{
	const Pattern *pa = *(const Pattern **) a;
	const Pattern *pb = *(const Pattern **) b;

	if (pa->len > pb->len)
		return -1;
	else if (pa->len == pb->len)
		return 0;
	return 1;
}

/* Clear all currently stored information and re-read everything */
static void load_mime_types(void)
{
	gboolean got_freedesk = FALSE;
	GHashTable *globs;
	gchar *tmp;

	if (!glob_patterns)
		glob_patterns = g_ptr_array_new();
	else
	{
		int i;

		for (i = glob_patterns->len - 1; i >= 0; i--)
		{
			Pattern *p = glob_patterns->pdata[i];
			g_free(p->glob);
			g_free(p);
		}
		g_ptr_array_set_size(glob_patterns, 0);
	}

	if (literal_hash)
		g_hash_table_destroy(literal_hash);
	if (extension_hash)
		g_hash_table_destroy(extension_hash);
	literal_hash = g_hash_table_new_full(g_str_hash, g_str_equal,
						g_free, NULL);
	extension_hash = g_hash_table_new_full(g_str_hash, g_str_equal,
						g_free, NULL);
	globs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	import_for_dir("/usr/share/mime/mime-info", globs, &got_freedesk);

	import_for_dir("/usr/local/share/mime/mime-info", globs, &got_freedesk);

	tmp = g_strconcat(home_dir, "/.mime/mime-info", NULL);
	import_for_dir(tmp, globs, &got_freedesk);
	g_free(tmp);

	/* Turn the globs hash into a pointer array */
	g_hash_table_foreach(globs, add_to_glob_patterns, NULL);
	g_hash_table_destroy(globs);

	if (glob_patterns->len)
		g_ptr_array_sort(glob_patterns, sort_by_strlen);

	if (!got_freedesk)
	{
		delayed_error(_("The standard MIME type database was not "
			"found. The filer will probably not show the correct "
			"types for different files. You should download and "
			"install the 'Common types package' from here:\n"
		"http://www.freedesktop.org/standards/shared-mime-info.html"));
	}

	filer_update_all();
}
