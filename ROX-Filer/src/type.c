/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2005, the ROX-Filer team.
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
#include <sys/types.h>
#include <fcntl.h>

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
#include "action.h"		/* (for action_chmod) */
#include "xml.h"
#include "dropbox.h"

#define TYPE_NS "http://www.freedesktop.org/standards/shared-mime-info"
enum {SET_MEDIA, SET_TYPE};

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
	{"display_door_colour", "#ff00ff"},
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
static void options_changed(void);
static char *get_action_save_path(GtkWidget *dialog);
static MIME_type *get_mime_type(const gchar *type_name, gboolean can_create);
static gboolean remove_handler_with_confirm(const guchar *path);
static void set_icon_theme(void);
static GList *build_icon_theme(Option *option, xmlNode *node, guchar *label);

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
MIME_type *inode_mountpoint;
MIME_type *inode_pipe;
MIME_type *inode_socket;
MIME_type *inode_block_dev;
MIME_type *inode_char_dev;
MIME_type *application_executable;
MIME_type *inode_unknown;
MIME_type *inode_door;

static Option o_display_colour_types;
static Option o_icon_theme;

static GtkIconTheme *icon_theme = NULL;

void type_init(void)
{
	int	    i;

	icon_theme = gtk_icon_theme_new();
	
	extension_hash = g_hash_table_new(g_str_hash, g_str_equal);
	type_hash = g_hash_table_new(g_str_hash, g_str_equal);

	text_plain = get_mime_type("text/plain", TRUE);
	inode_directory = get_mime_type("inode/directory", TRUE);
	inode_mountpoint = get_mime_type("inode/mount-point", TRUE);
	inode_pipe = get_mime_type("inode/fifo", TRUE);
	inode_socket = get_mime_type("inode/socket", TRUE);
	inode_block_dev = get_mime_type("inode/blockdevice", TRUE);
	inode_char_dev = get_mime_type("inode/chardevice", TRUE);
	application_executable = get_mime_type("application/x-executable", TRUE);
	inode_unknown = get_mime_type("inode/unknown", TRUE);
	inode_door = get_mime_type("inode/door", TRUE);

	load_mime_types();

	option_add_string(&o_icon_theme, "icon_theme", "ROX");
	option_add_int(&o_display_colour_types, "display_colour_types", TRUE);
	option_register_widget("icon-theme-chooser", build_icon_theme);
	
	for (i = 0; i < NUM_TYPE_COLOURS; i++)
		option_add_string(&o_type_colours[i],
				  opt_type_colours[i][0],
				  opt_type_colours[i][1]);
	alloc_type_colours();

	set_icon_theme();

	option_add_notify(options_changed);
}

/* Read-load all the glob patterns.
 * Note: calls filer_update_all.
 */
void reread_mime_files(void)
{
	gtk_icon_theme_rescan_if_needed(icon_theme);
	load_mime_types();
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
	mtype->comment = NULL;

	g_hash_table_insert(type_hash, g_strdup(type_name), mtype);

	return mtype;
}

/* NULL if we don't know / don't support contents checking */
MIME_type *mime_type_from_contents(const char *path)
{
	MIME_type *type = NULL;
#ifdef HAVE_GNOME_VFS
	char *type_name;

	type_name = gnome_vfs_get_mime_type(path);
	if (type_name)
	{
		type = get_mime_type(type_name, TRUE);
		free(type_name);
	}
#endif
	return type;
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
		case TYPE_DOOR:
			return _("Door");
	}
	
	return _("Unknown");
}

static void append_names(gpointer key, gpointer value, gpointer udata)
{
	GList **list = (GList **) udata;

	*list = g_list_prepend(*list, key);
}

/* Return list of all mime type names. Caller must free the list
 * but NOT the strings it contains (which are never freed).
 */
GList *mime_type_name_list(void)
{
	GList *list = NULL;

	g_hash_table_foreach(type_hash, append_names, &list);
	list = g_list_sort(list, (GCompareFunc) strcmp);

	return list;
}

/*			MIME-type guessing 			*/

/* Get the type of this file - stats the file and uses that if
 * possible. For regular or missing files, uses the pathname.
 */
MIME_type *type_get_type(const guchar *path)
{
	DirItem		*item;
	MIME_type	*type = NULL;

	item = diritem_new("");
	diritem_restat(path, item, NULL);
	if (item->base_type != TYPE_ERROR)
		type = item->mime_type;
	diritem_free(item);

	if (type)
		return type;

	type = type_from_path(path);

	if (!type)
		return text_plain;

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

MIME_type *mime_type_lookup(const char *type)
{
	return get_mime_type(type, TRUE);
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

	if (info.st_mode & S_IWOTH)
	{
		gchar *choices_dir;
		GList *paths;

		report_error(_("Executable '%s' is world-writeable! Refusing "
			"to run. Please change the permissions now (this "
			"problem may have been caused by a bug in earlier "
			"versions of the filer).\n\n"
			"Having (non-symlink) run actions world-writeable "
			"means that other people who use your computer can "
			"replace your run actions with malicious versions.\n\n"
			"If you trust everyone who could write to these files "
			"then you needn't worry. Otherwise, you should check, "
			"or even just delete, all the existing run actions."),
			open);
		choices_dir = g_path_get_dirname(open);
		paths = g_list_append(NULL, choices_dir);
		action_chmod(paths, TRUE, _("go-w (Fix security problem)"));
		g_free(choices_dir);
		g_list_free(paths);
		g_free(open);
		return TRUE;
	}

	if (S_ISDIR(info.st_mode))
		argv[0] = g_strconcat(open, "/AppRun", NULL);
	else
		argv[0] = open;

	retval = rox_spawn(home_dir, (const gchar **) argv) != 0;

	if (argv[0] != open)
		g_free(argv[0]);

	g_free(open);
	
	return retval;
}

/* Return the image for this type, loading it if needed.
 * Places to check are: (eg type="text_plain", base="text")
 * 1. <Choices>/MIME-icons/base_subtype
 * 2. Icon theme 'mime-base:subtype'
 * 3. Icon theme 'mime-base'
 * 4. Unknown type icon.
 *
 * Note: You must g_object_unref() the image afterwards.
 */
MaskedPixmap *type_to_icon(MIME_type *type)
{
	GdkPixbuf *full;
	char	*type_name, *path;
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

	type_name = g_strconcat(type->media_type, "_", type->subtype,
				".png", NULL);
	path = choices_find_path_load(type_name, "MIME-icons");
	g_free(type_name);
	if (path)
	{
		type->image = g_fscache_lookup(pixmap_cache, path);
		g_free(path);
	}

	if (type->image)
		goto out;

	type_name = g_strconcat("mime-", type->media_type, ":",
				type->subtype, NULL);
	full = gtk_icon_theme_load_icon(icon_theme, type_name, HUGE_HEIGHT,
						0, NULL);
	g_free(type_name);
	if (!full)
	{
		/* Ugly hack... try for a GNOME icon */
		type_name = g_strconcat("gnome-mime-", type->media_type,
				"-", type->subtype, NULL);
		full = gtk_icon_theme_load_icon(icon_theme,
						type_name,
						HUGE_HEIGHT, 0, NULL);
		g_free(type_name);
	}
	if (!full)
	{
		/* Try for a media type */
		type_name = g_strconcat("mime-", type->media_type, NULL);
		full = gtk_icon_theme_load_icon(icon_theme,
						type_name,
						HUGE_HEIGHT, 0, NULL);
		g_free(type_name);
	}
	if (full)
	{
		type->image = masked_pixmap_new(full);
		g_object_unref(full);
	}

out:
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

static void show_shell_help(gpointer data)
{
	info_message(_("Enter a shell command which will load \"$@\" into "
			"a suitable program. Eg:\n\n"
			"gimp \"$@\""));
}

/* Called if the user clicks on the OK button. Returns FALSE if an error
 * was displayed instead of performing the action.
 */
static gboolean set_shell_action(GtkWidget *dialog)
{
	GtkEntry *entry;
	const guchar *command;
	gchar	*tmp, *path;
	int	error = 0, len;
	int	fd;

	entry = g_object_get_data(G_OBJECT(dialog), "shell_command");

	g_return_val_if_fail(entry != NULL, FALSE);

	command = gtk_entry_get_text(entry);
	
	if (!strchr(command, '$'))
	{
		show_shell_help(NULL);
		return FALSE;
	}

	path = get_action_save_path(dialog);
	if (!path)
		return FALSE;
		
	tmp = g_strdup_printf("#! /bin/sh\nexec %s\n", command);
	len = strlen(tmp);
	
	fd = open(path, O_CREAT | O_WRONLY, 0755);
	if (fd == -1)
		error = errno;
	else
	{
		FILE *file;

		file = fdopen(fd, "w");
		if (file)
		{
			if (fwrite(tmp, 1, len, file) < len)
				error = errno;
			if (fclose(file) && error == 0)
				error = errno;
		}
		else
			error = errno;
	}

	if (error)
		report_error(g_strerror(error));

	g_free(tmp);
	g_free(path);

	gtk_widget_destroy(dialog);

	return TRUE;
}

static void set_action_response(GtkWidget *dialog, gint response, gpointer data)
{
	if (response == GTK_RESPONSE_OK)
		if (!set_shell_action(dialog))
			return;
	gtk_widget_destroy(dialog);
}

/* Return the path of the file in choices that handles this type and
 * radio setting.
 * NULL if nothing is defined for it.
 */
static guchar *handler_for_radios(GObject *dialog)
{
	Radios	*radios;
	MIME_type *type;

	radios = g_object_get_data(G_OBJECT(dialog), "rox-radios");
	type = g_object_get_data(G_OBJECT(dialog), "mime_type");
	
	g_return_val_if_fail(radios != NULL, NULL);
	g_return_val_if_fail(type != NULL, NULL);
	
	switch (radios_get_value(radios))
	{
		case SET_MEDIA:
			return choices_find_path_load(type->media_type,
							 "MIME-types");
		case SET_TYPE:
		{
			gchar *tmp, *handler;
			tmp = g_strconcat(type->media_type, "_",
					  type->subtype, NULL);
			handler = choices_find_path_load(tmp, "MIME-types");
			g_free(tmp);
			return handler;
		}
		default:
			g_warning("Bad type");
			return NULL;
	}
}

static void run_action_update(gpointer data)
{
	guchar *handler;
	DropBox *drop_box;
	GObject *dialog = G_OBJECT(data);

	drop_box = g_object_get_data(dialog, "rox-dropbox");

	g_return_if_fail(drop_box != NULL);

	handler = handler_for_radios(dialog);

	if (handler)
	{
		char *old = handler;

		handler = readlink_dup(old);
		if (handler)
			g_free(old);
		else
			handler = old;
	}

	drop_box_set_path(DROP_BOX(drop_box), handler);
	g_free(handler);
}

static void clear_run_action(GtkWidget *drop_box, GtkWidget *dialog)
{
	guchar *handler;

	handler = handler_for_radios(G_OBJECT(dialog));

	if (handler)
		remove_handler_with_confirm(handler);

	run_action_update(dialog);
}

/* Called when a URI list is dropped onto the box in the Set Run Action
 * dialog. Make sure it's an application, and make that the default
 * handler.
 */
static void drag_app_dropped(GtkWidget	*drop_box,
			     const guchar *app,
			     GtkWidget	*dialog)
{
	DirItem	*item;

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

			g_free(path);
		}
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

			dir = g_path_get_dirname(handler);
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
		const guchar *tmp;
		uid_t dir_uid = info.st_uid;

		tmp = make_path(handler, "AppRun");

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
	GtkDialog	*dialog;
	GtkWidget	*frame, *entry, *label, *button;
	GtkWidget	*hbox;
	Radios		*radios;

	g_return_if_fail(type != NULL);

	dialog = GTK_DIALOG(gtk_dialog_new());
	gtk_dialog_set_has_separator(dialog, FALSE);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);

	g_object_set_data(G_OBJECT(dialog), "mime_type", type);

	gtk_window_set_title(GTK_WINDOW(dialog), _("Set run action"));

	radios = radios_new(run_action_update, dialog);
	g_object_set_data(G_OBJECT(dialog), "rox-radios", radios);

	radios_add(radios,
			_("If a handler for the specific type isn't set up, "
			  "use this as the default."), SET_MEDIA,
			_("Set default for all `%s/<anything>'"),
			type->media_type);
	
	radios_add(radios,
			_("Use this application for all files with this MIME "
			  "type."), SET_TYPE,
			_("Only for the type `%s' (%s/%s)"),
			mime_type_comment(type),
			type->media_type, type->subtype);

	radios_set_value(radios, SET_TYPE);

	frame = drop_box_new(_("Drop a suitable application here"));

	g_object_set_data(G_OBJECT(dialog), "rox-dropbox", frame);
	
	radios_pack(radios, GTK_BOX(dialog->vbox));
	gtk_box_pack_start(GTK_BOX(dialog->vbox), frame, TRUE, TRUE, 0);

	g_signal_connect(frame, "path_dropped",
			G_CALLBACK(drag_app_dropped), dialog);
	g_signal_connect(frame, "clear",
			G_CALLBACK(clear_run_action), dialog);

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
		gtk_entry_set_text(GTK_ENTRY(entry), " \"$@\"");
		gtk_editable_set_position(GTK_EDITABLE(entry), 0);
	}

	gtk_dialog_add_button(dialog, GTK_STOCK_CLOSE, GTK_RESPONSE_CANCEL);

	button = button_new_mixed(GTK_STOCK_OK, _("_Use Command"));
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_dialog_add_action_widget(dialog, button, GTK_RESPONSE_OK);

	hbox = gtk_hbox_new(TRUE, 4);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, FALSE, TRUE, 0);

	gtk_dialog_set_default_response(dialog, GTK_RESPONSE_OK);
	
	g_signal_connect(dialog, "response",
			G_CALLBACK(set_action_response), NULL);

	gtk_widget_show_all(GTK_WIDGET(dialog));
}

/* path is an entry in Choices. If it's a symlink or a very small executable
 * then just get rid of it, otherwise confirm first. It it doesn't exist,
 * do nothing.
 *
 * FALSE on error (abort operation).
 */
static gboolean remove_handler_with_confirm(const guchar *path)
{
	struct stat info;

	if (lstat(path, &info) == 0)
	{
		/* A binding already exists... */
		if (S_ISREG(info.st_mode) && info.st_size > 256)
		{
			if (!confirm(_("A run action already exists and is "
				      "quite a big program - are you sure "
				      "you want to delete it?"),
				    GTK_STOCK_DELETE, NULL))
			{
				return FALSE;
			}
		}
		
		if (unlink(path))
		{
			report_error(_("Can't remove %s: %s"),
				path, g_strerror(errno));
			return FALSE;
		}
	}

	return TRUE;
}

/* The user wants to set a new default action for files of this type (or just
 * clear the action). Removes the current binding if possible and returns the
 * path to save the new one to. NULL means cancel. g_free() the result.
 */
static char *get_action_save_path(GtkWidget *dialog)
{
	guchar		*path = NULL;
	guchar 		*type_name = NULL;
	MIME_type	*type;
	Radios		*radios;

	g_return_val_if_fail(dialog != NULL, NULL);

	type = g_object_get_data(G_OBJECT(dialog), "mime_type");
	radios = g_object_get_data(G_OBJECT(dialog), "rox-radios");

	g_return_val_if_fail(radios != NULL && type != NULL, NULL);

	if (radios_get_value(radios) == SET_MEDIA)
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

	if (!remove_handler_with_confirm(path))
		null_g_free(&path);
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
	        case TYPE_DOOR:
	                return inode_door;
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
	else if (S_ISDOOR(st_mode))
		return TYPE_DOOR;

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

static void expire_timer(gpointer key, gpointer value, gpointer data)
{
	MIME_type *type = value;

	type->image_time = 0;
}

static void options_changed(void)
{
	alloc_type_colours();
	if (o_icon_theme.has_changed)
	{
		set_icon_theme();
		g_hash_table_foreach(type_hash, expire_timer, NULL);
		full_refresh();
	}
}

/* Return a pointer to a (static) colour for this item. If colouring is
 * off, returns normal.
 */
GdkColor *type_get_colour(DirItem *item, GdkColor *normal)
{
	int type = item->base_type;

	if (!o_display_colour_types.int_value)
		return normal;

	if (item->flags & ITEM_FLAG_EXEC_FILE &&
	    item->mime_type == application_executable)
		type = TYPE_EXEC;
	else if (item->flags & ITEM_FLAG_APPDIR)
		type = TYPE_APPDIR;

	g_return_val_if_fail(type >= 0 && type < NUM_TYPE_COLOURS, normal);

	return &type_colours[type];
}

/* Process the 'Patterns' value */
static void add_pattern(MIME_type *type, const char *pattern, GHashTable *globs)
{
	if (pattern[0] == '*' && pattern[1] == '.' &&
	    strpbrk(pattern + 2, "*?[") == NULL)
	{
		g_hash_table_insert(extension_hash,
				    g_strdup(pattern + 2),
				    type);
	}
	else if (strpbrk(pattern, "*?[") == NULL)
		g_hash_table_insert(literal_hash, g_strdup(pattern), type);
	else
		g_hash_table_insert(globs, g_strdup(pattern), type);
}

/* Load and parse this file. literal_hash and extension_hash are updated
 * directly. Other patterns are added to 'globs'.
 */
static void import_file(const gchar *file, GHashTable *globs)
{
	MIME_type *type = NULL;
	GError *error = NULL;
	gchar  *data, *line;

	if (access(file, F_OK) != 0)
		return;		/* Doesn't exist. No problem. */

	if (!g_file_get_contents(file, &data, NULL, &error))
	{
		delayed_error(_("Error loading MIME database:\n%s"),
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

		if (*line != '#')
		{
			const gchar *colon;
			gchar *name;

			colon = strchr(line, ':');
			if (!colon)
			{
				delayed_error(_("File '%s' corrupted!"), file);
				break;
			}

			name = g_strndup(line, colon - line);
			type = get_mime_type(name, TRUE);
			g_free(name);
			if (!type)
				g_warning("Invalid type in '%s'", file);

			add_pattern(type, colon + 1, globs);
		}

		line = nl + 1;
	}

	g_free(data);
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

static char **get_xdg_data_dirs(int *n_dirs)
{
	const char *env;
	char **dirs;
	int i, n;

	env = getenv("XDG_DATA_DIRS");
	if (!env)
		env = "/usr/local/share/:/usr/share/";
	dirs = g_strsplit(env, ":", 0);
	g_return_val_if_fail(dirs != NULL, NULL);
	for (n = 0; dirs[n]; n++)
		;
	for (i = n; i > 0; i--)
		dirs[i] = dirs[i - 1];
	env = getenv("XDG_DATA_HOME");
	if (env)
		dirs[0] = g_strdup(env);
	else
		dirs[0] = g_build_filename(g_get_home_dir(), ".local",
						"share", NULL);
	*n_dirs = n + 1;
	return dirs;
}

/* Clear all currently stored information and re-read everything.
 * Note: calls filer_update_all.
 */
static void load_mime_types(void)
{
	GHashTable *globs;
	char **dirs;
	int n_dirs;
	int i;

	dirs = get_xdg_data_dirs(&n_dirs);
	g_return_if_fail(dirs != NULL);

	{
		struct stat info;
		if (lstat(make_path(home_dir, ".mime"), &info) == 0 &&
		    S_ISDIR(info.st_mode))
		{
			delayed_error(_("The ~/.mime directory has moved. "
				"It should now be ~/.local/share/mime. You "
				"should move it there (and make a symlink "
				"from ~/.mime to it for older applications)."));
		}
	}

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

	for (i = n_dirs - 1; i >= 0; i--)
	{
		char *path;
		path = g_build_filename(dirs[i], "mime", "globs", NULL);
		import_file(path, globs);
		g_free(path);
		g_free(dirs[i]);
	}
	g_free(dirs);

	/* Turn the globs hash into a pointer array */
	g_hash_table_foreach(globs, add_to_glob_patterns, NULL);
	g_hash_table_destroy(globs);
	globs = NULL;

	if (glob_patterns->len)
		g_ptr_array_sort(glob_patterns, sort_by_strlen);

	if (g_hash_table_size(extension_hash) == 0)
	{
		delayed_error(_("The standard MIME type database "
			"(version 0.9 or later) was not found. "
			"The filer will probably not show the correct "
			"types for different files. You should download and "
			"install the 'shared-mime-info-0.9' package from "
			"here:\n"
		"http://www.freedesktop.org/software/shared-mime-info\n\n"
		"If you have already installed this package, check that the "
		"permissions allow the files to be read (check "
		"/usr/local/share/mime/globs or /usr/share/mime/globs)."));
	}

	filer_update_all();
}

/* Try to fill in 'type->comment' from this document */
static void get_comment(MIME_type *type, const guchar *path)
{
	xmlNode *node;
	XMLwrapper *doc;
	
	doc = xml_cache_load(path);
	if (!doc)
		return;

	node = xml_get_section(doc, TYPE_NS, "comment");

	if (node)
	{
		char *val;
		g_return_if_fail(type->comment == NULL);
		val= xmlNodeListGetString(node->doc, node->xmlChildrenNode, 1);
		type->comment = g_strdup(val);
		xmlFree(val);
	}

	g_object_unref(doc);
}

/* Fill in the comment field for this MIME type */
static void find_comment(MIME_type *type)
{
	char **dirs;
	int i, n_dirs;

	if (type->comment)
	{
		g_free(type->comment);
		type->comment = NULL;
	}

	dirs = get_xdg_data_dirs(&n_dirs);
	g_return_if_fail(dirs != NULL);

	for (i = 0; i < n_dirs; i++)
	{
		guchar *path;
		
		path = g_strdup_printf("%s/mime/%s/%s.xml", dirs[i],
				type->media_type, type->subtype);
		get_comment(type, path);
		g_free(path);
		if (type->comment)
			break;
	}

	if (!type->comment)
		type->comment = g_strdup_printf("%s/%s", type->media_type,
						type->subtype);

	for (i = 0; i < n_dirs; i++)
		g_free(dirs[i]);
	g_free(dirs);
}

const char *mime_type_comment(MIME_type *type)
{
	if (!type->comment)
		find_comment(type);

	return type->comment;
}

static void set_icon_theme(void)
{
	GtkIconInfo *info;
	char *icon_home;
	const char *theme_name = o_icon_theme.value;

	if (!theme_name || !*theme_name)
		theme_name = "ROX";

	while (1)
	{
		gtk_icon_theme_set_custom_theme(icon_theme, theme_name);
		info = gtk_icon_theme_lookup_icon(icon_theme,
				"mime-application:postscript",
				ICON_HEIGHT, 0);
		if (!info)
		{
			info = gtk_icon_theme_lookup_icon(icon_theme,
					"gnome-mime-application-postscript",
					ICON_HEIGHT, 0);
		}
		if (info)
		{
			gtk_icon_info_free(info);
			return;
		}

		if (strcmp(theme_name, "ROX") == 0)
			break;

		delayed_error(_("Icon theme '%s' does not contain MIME icons. "
				"Using ROX default theme instead."),
				theme_name);
		
		theme_name = "ROX";
	}

	icon_home = g_build_filename(home_dir, ".icons", NULL);
	if (!file_exists(icon_home))
		mkdir(icon_home, 0755);
	g_free(icon_home);	

	icon_home = g_build_filename(home_dir, ".icons", "ROX", NULL);
	if (symlink(make_path(app_dir, "ROX"), icon_home))
		delayed_error(_("Failed to create symlink '%s':\n%s\n\n"
		"(this may mean that the ROX theme already exists there, but "
		"the 'mime-application:postscript' icon couldn't be loaded for "
		"some reason)"), icon_home, g_strerror(errno));
	g_free(icon_home);

	gtk_icon_theme_rescan_if_needed(icon_theme);
}

static guchar *read_theme(Option *option)
{
	GtkOptionMenu *om = GTK_OPTION_MENU(option->widget);
	GtkLabel *item;

	item = GTK_LABEL(GTK_BIN(om)->child);

	g_return_val_if_fail(item != NULL, g_strdup("ROX"));

	return g_strdup(gtk_label_get_text(item));
}

static void update_theme(Option *option)
{
	GtkOptionMenu *om = GTK_OPTION_MENU(option->widget);
	GtkWidget *menu;
	GList *kids, *next;
	int i = 0;

	menu = gtk_option_menu_get_menu(om);

	kids = gtk_container_get_children(GTK_CONTAINER(menu));
	for (next = kids; next; next = next->next, i++)
	{
		GtkLabel *item = GTK_LABEL(GTK_BIN(next->data)->child);
		const gchar *label;

		/* The label actually moves from the menu!! */
		if (!item)
			item = GTK_LABEL(GTK_BIN(om)->child);

		label = gtk_label_get_text(item);

		g_return_if_fail(label != NULL);

		if (strcmp(label, option->value) == 0)
			break;
	}
	g_list_free(kids);
	
	if (next)
		gtk_option_menu_set_history(om, i);
	else
		g_warning("Theme '%s' not found", option->value);
}

static void add_themes_from_dir(GPtrArray *names, const char *dir)
{
	GPtrArray *list;
	int i;

	if (access(dir, F_OK) != 0)
		return;

	list = list_dir(dir);
	g_return_if_fail(list != NULL);

	for (i = 0; i < list->len; i++)
	{
		char *index_path;

		index_path = g_build_filename(dir, list->pdata[i],
						"index.theme", NULL);
		
		if (access(index_path, F_OK) == 0)
			g_ptr_array_add(names, list->pdata[i]);
		else
			g_free(list->pdata[i]);

		g_free(index_path);
	}

	g_ptr_array_free(list, TRUE);
}

static GList *build_icon_theme(Option *option, xmlNode *node, guchar *label)
{
	GtkWidget *button, *menu, *hbox;
	GPtrArray *names;
	gchar **theme_dirs = NULL;
	gint n_dirs = 0;
	int i;

	g_return_val_if_fail(option != NULL, NULL);
	g_return_val_if_fail(label != NULL, NULL);

	hbox = gtk_hbox_new(FALSE, 4);

	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_(label)),
				FALSE, TRUE, 0);

	button = gtk_option_menu_new();
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);

	menu = gtk_menu_new();
	gtk_option_menu_set_menu(GTK_OPTION_MENU(button), menu);

	gtk_icon_theme_get_search_path(icon_theme, &theme_dirs, &n_dirs);
	names = g_ptr_array_new();
	for (i = 0; i < n_dirs; i++)
		add_themes_from_dir(names, theme_dirs[i]);
	g_strfreev(theme_dirs);

	g_ptr_array_sort(names, strcmp2);

	for (i = 0; i < names->len; i++)
	{
		GtkWidget *item;
		char *name = names->pdata[i];

		item = gtk_menu_item_new_with_label(name);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		gtk_widget_show_all(item);

		g_free(name);
	}

	g_ptr_array_free(names, TRUE);

	option->update_widget = update_theme;
	option->read_widget = read_theme;
	option->widget = button;

	gtk_signal_connect_object(GTK_OBJECT(button), "changed",
			GTK_SIGNAL_FUNC(option_check_widget),
			(GtkObject *) option);

	return g_list_append(NULL, hbox);
}
