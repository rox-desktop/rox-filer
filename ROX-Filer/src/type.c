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

/* type.c - code for dealing with filetypes */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/param.h>

#ifdef HAVE_REGEX_H
# include <regex.h>
#endif

#include "global.h"

#include "string.h"
#include "main.h"
#include "pixmaps.h"
#include "run.h"
#include "gui_support.h"
#include "choices.h"
#include "type.h"
#include "support.h"
#include "dir.h"
#include "dnd.h"

#if defined(HAVE_REGEX_H) && \
		defined(HAVE_RE_COMPILE_PATTERN) && defined(HAVE_RE_SEARCH)
# define USE_REGEX 1
#else
# undef USE_REGEX
#endif

#ifdef USE_REGEX
/* A regexp rule, which maps matching filenames to a MIME-types */
typedef struct pattern {
  MIME_type *type;
  struct re_pattern_buffer buffer;
} Pattern;
#endif

/* Static prototypes */
static char *import_extensions(guchar *line);
static void import_for_dir(guchar *path);
char *get_action_save_path(GtkWidget *dialog);

/* Maps extensions to MIME_types (eg 'png'-> MIME_type *).
 * Extensions may contain dots; 'tar.gz' matches '*.tar.gz', etc.
 * The hash table is consulted from each dot in the string in turn
 * (First .ps.gz, then try .gz)
 */
static GHashTable *extension_hash = NULL;
static char *current_type = NULL;	/* (used while reading file) */
#ifdef USE_REGEX
static GList *patterns = NULL;		/* [(regexp -> MIME type)] */
#endif

/* Most things on Unix are text files, so this is the default type */
MIME_type text_plain 		= {"text", "plain", NULL};

MIME_type special_directory 	= {"special", "directory", NULL};
MIME_type special_pipe 		= {"special", "pipe", NULL};
MIME_type special_socket 	= {"special", "socket", NULL};
MIME_type special_block_dev 	= {"special", "block-device", NULL};
MIME_type special_char_dev 	= {"special", "char-device", NULL};
MIME_type special_exec 		= {"special", "executable", NULL};
MIME_type special_unknown 	= {"special", "unknown", NULL};

void type_init()
{
	int		i;
	GPtrArray	*list;
	
#ifdef HAVE_RE_SET_SYNTAX
	re_set_syntax(RE_SYNTAX_POSIX_EGREP);
#endif

	extension_hash = g_hash_table_new(g_str_hash, g_str_equal);

	current_type = NULL;

	list = choices_list_dirs("MIME-info");
	for (i = 0; i < list->len; i++)
		import_for_dir((gchar *) g_ptr_array_index(list, i));
	choices_free_list(list);
}

/* Parse every file in 'dir' */
static void import_for_dir(guchar *path)
{
	DIR		*dir;
	struct dirent	*item;

	dir = opendir(path);
	if (!dir)
		return;

	while ((item = readdir(dir)))
	{
		guchar	*file;
		struct stat info;
		
		if (item->d_name[0] == '.')
			continue;

		current_type = NULL;
		file = make_path(path, item->d_name)->str;

		if (stat(file, &info) == 0 && S_ISREG(info.st_mode))
			parse_file(file, import_extensions);
	}

	closedir(dir);
}

/* Add one entry to the extension_hash table */
static void add_ext(char *type_name, char *ext)
{
	MIME_type *new;
	char	  *slash;

	slash = strchr(type_name, '/');
	g_return_if_fail(slash != NULL);	/* XXX: Report nicely */
			
	new = g_new(MIME_type, 1);
	new->media_type = g_strndup(type_name, slash - type_name);
	new->subtype = g_strdup(slash + 1);
	new->image = NULL;

	g_hash_table_insert(extension_hash, g_strdup(ext), new);
}

static void add_regex(char *type_name, char *reg)
{
#ifdef USE_REGEX
	MIME_type *new;
	char	  *slash;
	Pattern	  *pattern;

	slash = strchr(type_name, '/');
	g_return_if_fail(slash != NULL);	/* XXX: Report nicely */

	pattern = g_new(Pattern, 1);
	pattern->buffer.buffer = NULL;
	pattern->buffer.allocated = 0;
	pattern->buffer.fastmap = NULL;
	pattern->buffer.translate = NULL;

	if (re_compile_pattern(reg, strlen(reg), &pattern->buffer))
	{
		regfree(&pattern->buffer);
		g_free(pattern);
		return;
	}
	
	new = g_new(MIME_type, 1);
	new->media_type = g_strndup(type_name, slash - type_name);
	new->subtype = g_strdup(slash + 1);
	new->image = NULL;

	pattern->type = new;

	patterns = g_list_prepend(patterns, pattern);
#endif
}

/* Parse one line from the file and add entries to extension_hash */
static char *import_extensions(guchar *line)
{

	if (*line == '\0' || *line == '#')
		return NULL;		/* Comment */

	if (isspace(*line))
	{
		if (!current_type)
			return _("Missing MIME-type");
		while (*line && isspace(*line))
			line++;

		if (strncmp(line, "ext:", 4) == 0)
		{
			char *ext;
			line += 4;

			for (;;)
			{
				while (*line && isspace(*line))
					line++;
				if (*line == '\0')
					break;
				ext = line;
				while (*line && !isspace(*line))
					line++;
				if (*line)
					*line++ = '\0';
				add_ext(current_type, ext);
			}
		}
		else if (strncmp(line, "regex:", 6) == 0)
		{
		        line += 6;

			while (*line && isspace(*line))
				line++;
			if (*line)
				add_regex(current_type, line);
		}
		/* else ignore */
	}
	else
	{
		char		*type = line;
		while (*line && *line != ':' && !isspace(*line))
			line++;
		if (*line)
			*line++ = '\0';
		while (*line && isspace(*line))
			line++;
		if (*line)
			return _("Trailing chars after MIME-type");
		current_type = g_strdup(type);
	}
	return NULL;
}

char *basetype_name(DirItem *item)
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
MIME_type *type_get_type(guchar *path)
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
			type = &special_exec;
		else
			type = mime_type_from_base_type(base);
	}

	return type;
}

/* Returns a pointer to the MIME-type.
 * NULL if we can't think of anything.
 */
MIME_type *type_from_path(char *path)
{
	char	*ext, *dot, *lower, *leafname;
#ifdef USE_REGEX
	GList *patt;
	int len;
#endif

	leafname = strrchr(path, '/');
	if (!leafname)
		leafname = path;
	else
		leafname++;
	ext = leafname;

	while ((dot = strchr(ext, '.')))
	{
		MIME_type *type;

		ext = dot + 1;

		type = g_hash_table_lookup(extension_hash, ext);

		if (type)
			return type;

		lower = g_strdup(ext);
		g_strdown(lower);
		type = g_hash_table_lookup(extension_hash, lower);
		g_free(lower);

		if (type)
			return type;
	}

#ifdef USE_REGEX
	len = strlen(leafname);

	for (patt = patterns; patt; patt = patt->next)
	{
		Pattern *pattern = (Pattern *) patt->data;

		if (re_search(&pattern->buffer,
					leafname, len, 0, len, NULL) >= 0)
			return pattern->type;
	}
#endif

	return NULL;
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

gboolean type_open(char *path, MIME_type *type)
{
	char	*argv[] = {NULL, NULL, NULL};
	char	*open;
	gboolean	retval = TRUE;
	struct stat	info;

	argv[1] = path;

	open = handler_for(type);
	if (!open)
		return FALSE;

	if (stat(open, &info))
	{
		report_rox_error("stat(%s): %s", open, g_strerror(errno));
		g_free(open);
		return FALSE;
	}

	if (S_ISDIR(info.st_mode))
		argv[0] = g_strconcat(open, "/AppRun", NULL);
	else
		argv[0] = open;

	if (!spawn_full(argv, home_dir))
	{
		report_rox_error(_("Failed to fork() child process"));
		retval = FALSE;
	}

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
 * Note: You must pixmap_unref() the image afterwards.
 */
MaskedPixmap *type_to_icon(MIME_type *type)
{
	char	*path;
	char	*type_name;
	time_t	now;

	if (type == NULL)
	{
		pixmap_ref(im_unknown);
		return im_unknown;
	}

	now = time(NULL);
	/* Already got an image? */
	if (type->image)
	{
		/* Yes - don't recheck too often */
		if (abs(now - type->image_time) < 2)
		{
			pixmap_ref(type->image);
			return type->image;
		}
		pixmap_unref(type->image);
		type->image = NULL;
	}

	type_name = g_strconcat(type->media_type, "_",
				type->subtype, ".xpm", NULL);
	path = choices_find_path_load(type_name, "MIME-icons");
	if (!path)
	{
		strcpy(type_name + strlen(type->media_type), ".xpm");
		path = choices_find_path_load(type_name, "MIME-icons");
	}
	
	g_free(type_name);

	if (path)
	{
		type->image = g_fscache_lookup(pixmap_cache, path);
		g_free(path);
	}

	if (!type->image)
		type->image = im_unknown;

	type->image_time = now;
	
	pixmap_ref(type->image);
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
	report_rox_error(_("Enter a shell command which will load \"$1\" into "
			"a suitable program. Eg:\n\n"
			"gimp \"$1\""));
}

/* Called if the user clicks on the OK button */
static void set_shell_action(GtkWidget *dialog)
{
	GtkEntry *entry;
	GtkToggleButton *for_all;
	guchar	*command, *path, *tmp;
	int	error = 0, len;
	FILE	*file;

	entry = gtk_object_get_data(GTK_OBJECT(dialog), "shell_command");
	for_all = gtk_object_get_data(GTK_OBJECT(dialog), "set_for_all");
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
		report_rox_error(g_strerror(errno));

	g_free(tmp);
	g_free(path);

	gtk_widget_destroy(dialog);
}

/* Called when a URI list is dropped onto the box in the Set Run Action
 * dialog. Make sure it's an application, and make that the default
 * handler.
 */
void drag_app_dropped(GtkWidget		*frame,
		      GdkDragContext    *context,
		      gint              x,
		      gint              y,
		      GtkSelectionData  *selection_data,
		      guint             info,
		      guint32           time,
		      GtkWidget		*dialog)
{
	GList	*uris;
	guchar	*app = NULL;
	DirItem	item;

	if (!selection_data->data)
		return; 		/* Timeout? */

	uris = uri_list_to_glist(selection_data->data);

	if (g_list_length(uris) == 1)
		app = get_local_path((guchar *) uris->data);
	g_list_free(uris);

	if (!app)
	{
		delayed_rox_error(
			_("You should drop a single (local) application "
			"onto the drop box - that application will be "
			"used to load files of this type in future"));
		return;
	}

	diritem_stat(app, &item, FALSE);
	if (item.flags & (ITEM_FLAG_APPDIR | ITEM_FLAG_EXEC_FILE))
	{
		guchar	*path;

		path = get_action_save_path(dialog);

		if (path)
		{
			if (symlink(app, path))
				delayed_rox_error("symlink: %s",
						g_strerror(errno));
			else
				destroy_on_idle(dialog);
		}

		g_free(path);
	}
	else
		delayed_rox_error(
			_("This is not a program! Give me an application "
			"instead!"));

	diritem_clear(&item);
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

/* Display a dialog box allowing the user to set the default run action
 * for this type.
 */
void type_set_handler_dialog(MIME_type *type)
{
	guchar		*tmp;
	GtkWidget	*dialog, *vbox, *frame, *hbox, *entry, *label, *button;
	GtkWidget	*radio;
	GtkTargetEntry 	targets[] = {
		{"text/uri-list", 0, TARGET_URI_LIST},
	};

	g_return_if_fail(type != NULL);

	dialog = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);
	gtk_object_set_data(GTK_OBJECT(dialog), "mime_type", type);

	gtk_window_set_title(GTK_WINDOW(dialog), _("Set run action"));
	gtk_container_set_border_width(GTK_CONTAINER(dialog), 10);

	vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(dialog), vbox);

	tmp = g_strdup_printf(_("Set default for all `%s/<anything>'"),
				type->media_type);
	radio = gtk_radio_button_new_with_label(NULL, tmp);
	g_free(tmp);
	gtk_object_set_data(GTK_OBJECT(dialog), "set_for_all", radio);

	tmp = g_strdup_printf(_("Only for the type `%s/%s'"), type->media_type,
			type->subtype);
	gtk_box_pack_start(GTK_BOX(vbox), radio, FALSE, TRUE, 0);
	radio = gtk_radio_button_new_with_label(
			gtk_radio_button_group(GTK_RADIO_BUTTON(radio)),
			tmp);
	g_free(tmp);
	gtk_box_pack_start(GTK_BOX(vbox), radio, FALSE, TRUE, 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), TRUE);

	frame = gtk_frame_new(NULL);
	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 4);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 4);

	gtk_drag_dest_set(frame, GTK_DEST_DEFAULT_ALL,
			targets, sizeof(targets) / sizeof(*targets),
			GDK_ACTION_COPY);
	gtk_signal_connect(GTK_OBJECT(frame), "drag_data_received",
			GTK_SIGNAL_FUNC(drag_app_dropped), dialog);

	label = gtk_label_new(_("Drop a suitable\napplication here"));
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

	label = gtk_label_new(_("Enter a shell command:")),
	gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 4);

	gtk_box_pack_start(GTK_BOX(hbox),
			new_help_button(show_shell_help, NULL), FALSE, TRUE, 0);

	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, TRUE, 0);
	gtk_widget_grab_focus(entry);
	gtk_object_set_data(GTK_OBJECT(dialog), "shell_command", entry);
	gtk_signal_connect_object(GTK_OBJECT(entry), "activate",
			GTK_SIGNAL_FUNC(set_shell_action), GTK_OBJECT(dialog));

	hbox = gtk_hbox_new(TRUE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	/* If possible, fill in the entry box with the current command */
	tmp = get_current_command(type);
	if (tmp)
	{
		gtk_entry_set_text(GTK_ENTRY(entry), tmp);
		gtk_entry_set_position(GTK_ENTRY(entry), -1);
		g_free(tmp);
	}
	else
	{
		gtk_entry_set_text(GTK_ENTRY(entry), " \"$1\"");
		gtk_entry_set_position(GTK_ENTRY(entry), 0);
	}

	button = gtk_button_new_with_label(_("OK"));
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_window_set_default(GTK_WINDOW(dialog), button);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(set_shell_action), GTK_OBJECT(dialog));
	
	button = gtk_button_new_with_label(_("Cancel"));
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_destroy),
			GTK_OBJECT(dialog));

	gtk_widget_show_all(dialog);
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
	type = gtk_object_get_data(GTK_OBJECT(dialog), "mime_type");
	for_all = gtk_object_get_data(GTK_OBJECT(dialog), "set_for_all");
	g_return_val_if_fail(for_all != NULL && type != NULL, NULL);

	if (gtk_toggle_button_get_active(for_all))
		type_name = g_strdup(type->media_type);
	else
		type_name = g_strconcat(type->media_type, "_",
				type->subtype, NULL);

	path = choices_find_path_save("", PROJECT, FALSE);
	if (!path)
	{
		report_rox_error(
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
				"delete it?"), 2, "Delete", "Cancel") != 0)
			{
				g_free(path);
				path = NULL;
				goto out;
			}
		}
		
		if (unlink(path))
		{
			report_rox_error(_("Can't remove %s: %s"),
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
			return &text_plain;
		case TYPE_DIRECTORY:
			return &special_directory;
		case TYPE_PIPE:
			return &special_pipe;
		case TYPE_SOCKET:
			return &special_socket;
		case TYPE_BLOCK_DEVICE:
			return &special_block_dev;
		case TYPE_CHAR_DEVICE:
			return &special_char_dev;
	}
	return &special_unknown;
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

	return TYPE_UNKNOWN;
}

/* Returns TRUE is this is something that is run by looking up its type
 * in MIME-types and, hence, can have its run action set.
 */
gboolean can_set_run_action(DirItem *item)
{
	g_return_val_if_fail(item != NULL, FALSE);

	return item->base_type == TYPE_FILE &&
		!(item->mime_type == &special_exec);
}

