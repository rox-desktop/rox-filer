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

/* run.c */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <sys/param.h>

#include "global.h"

#include "run.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "display.h"
#include "main.h"
#include "type.h"
#include "dir.h"
#include "diritem.h"
#include "action.h"
#include "icon.h"

/* Static prototypes */
static void write_data(gpointer data, gint fd, GdkInputCondition cond);
static gboolean follow_symlink(const char *full_path,
			       FilerWindow *filer_window,
			       FilerWindow *src_window);
static gboolean open_file(const guchar *path, MIME_type *type);
static void dir_show_help(DirItem *item, const char *path);

typedef struct _PipedData PipedData;

struct _PipedData
{
	guchar		*data;
	gint		tag;
	gulong		sent;
	gulong		length;
};


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/


/* An application has been double-clicked (or run in some other way) */
void run_app(const char *path)
{
	GString	*apprun;
	const char *argv[] = {NULL, NULL};

	apprun = g_string_new(path);
	argv[0] = g_string_append(apprun, "/AppRun")->str;

	rox_spawn(home_dir, argv);
	
	g_string_free(apprun, TRUE);
}

/* Execute this program, passing all the URIs in the list as arguments.
 * URIs that are files on the local machine will be passed as simple
 * pathnames. The uri_list should be freed after this function returns.
 */
void run_with_files(const char *path, GList *uri_list)
{
	const char	**argv;
	int		argc = 0;
	struct stat 	info;

	if (stat(path, &info))
	{
		delayed_error(_("Program %s not found - deleted?"), path);
		return;
	}

	argv = g_malloc(sizeof(char *) * (g_list_length(uri_list) + 2));

	if (S_ISDIR(info.st_mode))
		argv[argc++] = make_path(path, "AppRun")->str;
	else
		argv[argc++] = path;
	
	while (uri_list)
	{
		const char *uri = uri_list->data;
		const char *local;

		local = get_local_path(uri);
		if (local)
			argv[argc++] = local;
		else
			argv[argc++] = uri;
		uri_list = uri_list->next;
	}
	
	argv[argc++] = NULL;

	rox_spawn(home_dir, argv);
}

/* Run the program as '<path> -', piping the data to it via stdin.
 * You can g_free() the data as soon as this returns.
 */
void run_with_data(const char *path, gpointer data, gulong length)
{
	const char	*argv[] = {NULL, "-", NULL};
	struct stat 	info;
	int		fds[2];
	PipedData	*pd;

	if (stat(path, &info))
	{
		delayed_error(_("Program %s not found - deleted?"), path);
		return;
	}

	if (S_ISDIR(info.st_mode))
		argv[0] = make_path(path, "AppRun")->str;
	else
		argv[0] = path;
	
	if (pipe(fds))
	{
		delayed_error("pipe: %s", g_strerror(errno));
		return;
	}
	close_on_exec(fds[1], TRUE);
	close_on_exec(fds[0], TRUE);

	switch (fork())
	{
		case -1:
			delayed_error("fork: %s", g_strerror(errno));
			close(fds[1]);
			break;
		case 0:
			/* We are the child */
			chdir(home_dir);
			if (dup2(fds[0], 0) == -1)
				g_warning("dup2() failed: %s\n",
						g_strerror(errno));
			else
			{
				close_on_exec(0, FALSE);
				if (execv(argv[0], (char **) argv))
					g_warning("execv(%s) failed: %s\n",
						argv[0], g_strerror(errno));
			}
			_exit(1);
		default:
			/* We are the parent */
			set_blocking(fds[1], FALSE);
			pd = g_new(PipedData, 1);
			pd->data = g_malloc(length);
			memcpy(pd->data, data, length);
			pd->length = length;
			pd->sent = 0;
			pd->tag = gtk_input_add_full(fds[1], GDK_INPUT_WRITE,
						write_data, NULL, pd, NULL);
			break;
	}

	close(fds[0]);
}

/* Load a file, open a directory or run an application. Or, if 'edit' is set:
 * edit a file, open an application, follow a symlink or mount a device.
 *
 * filer_window is the window to use for displaying a directory.
 * NULL will always use a new directory when needed.
 * src_window is the window to copy options from, or NULL.
 *
 * Returns TRUE on success.
 */
gboolean run_diritem(const guchar *full_path,
		     DirItem *item,
		     FilerWindow *filer_window,
		     FilerWindow *src_window,
		     gboolean edit)
{
	if (item->flags & ITEM_FLAG_SYMLINK && edit)
		return follow_symlink(full_path, filer_window, src_window);

	switch (item->base_type)
	{
		case TYPE_DIRECTORY:
			if (item->flags & ITEM_FLAG_APPDIR && !edit)
			{
				run_app(full_path);
				return TRUE;
			}

			if (item->flags & ITEM_FLAG_MOUNT_POINT && edit)
			{
				GList	*paths;

				paths = g_list_prepend(NULL,
							(gpointer) full_path);
				action_mount(paths, filer_window == NULL, -1);
				g_list_free(paths);
				if (item->flags & ITEM_FLAG_MOUNTED ||
						!filer_window)
					return TRUE;
			}

			if (filer_window)
				filer_change_to(filer_window, full_path, NULL);
			else
				filer_opendir(full_path, src_window);
			return TRUE;
		case TYPE_FILE:
			if ((item->mime_type == application_executable) && !edit)
			{
				const char *argv[] = {NULL, NULL};
				guchar	*dir = filer_window
						? filer_window->sym_path
						: NULL;

				argv[0] = full_path;

				return rox_spawn(dir, argv);
			}

			return open_file(full_path, edit ? text_plain
						  : item->mime_type);
		case TYPE_ERROR:
			delayed_error(_("File doesn't exist, or I can't "
					  "access it: %s"), full_path);
			return FALSE;
		default:
		        delayed_error(
				_("I don't know how to open '%s'"), full_path);
			return FALSE;
	}
}

/* Attempt to open this item */
gboolean run_by_path(const guchar *full_path)
{
	gboolean retval;
	DirItem	*item;

	/* XXX: Loads an image - wasteful */
	item = diritem_new("");
	diritem_restat(full_path, item, NULL);
	retval = run_diritem(full_path, item, NULL, NULL, FALSE);
	diritem_free(item);
	
	return retval;
}

void show_item_help(const guchar *path, DirItem *item)
{
	switch (item->base_type)
	{
		case TYPE_FILE:
			if (item->flags & ITEM_FLAG_EXEC_FILE)
				info_message(
				      _("Executable file:\n"
					"This is a file with an eXecute bit "
					"set - it can be run as a program."));
			else
				info_message(
				      _("File:\n"
					"This is a data file. Try using the "
					"Info menu item to find out more..."));
			break;
		case TYPE_DIRECTORY:
			if (item->flags & ITEM_FLAG_MOUNT_POINT)
				info_message(
				_("Mount point:\n"
				"A mount point is a directory which another "
				"filing system can be mounted on. Everything "
				"on the mounted filesystem then appears to be "
				"inside the directory."));
			else
				dir_show_help(item, path);
			break;
		case TYPE_CHAR_DEVICE:
		case TYPE_BLOCK_DEVICE:
			info_message(
				_("Device file:\n"
				"Device files allow you to read from or write "
				"to a device driver as though it was an "
				"ordinary file."));
			break;
		case TYPE_PIPE:
			info_message(
				_("Named pipe:\n"
				"Pipes allow different programs to "
				"communicate. One program writes data to the "
				"pipe while another one reads it out again."));
			break;
		case TYPE_SOCKET:
			info_message(
				_("Socket:\n"
				"Sockets allow processes to communicate."));
			break;
		case TYPE_DOOR:
			info_message(
				_("Door:\n"
				"Doors are a little-used Solaris method for "
				"processes to communicate."));
			break;
		default:
			info_message(
				_("Unknown type:\n"
				"I couldn't find out what kind of file this "
				"is. Maybe it doesn't exist anymore or you "
				"don't have search permission on the directory "
				"it's in?"));
			break;
	}
}

/* Open a directory viewer showing this file, and wink it */
void open_to_show(const guchar *path)
{
	FilerWindow	*new;
	guchar		*dir, *slash;

	g_return_if_fail(path != NULL);

	dir = g_strdup(path);
	slash = strrchr(dir, '/');
	if (slash == dir || !slash)
	{
		/* Item in the root (or root itself!) */
		new = filer_opendir("/", NULL);
		if (new && dir[1])
			display_set_autoselect(new, dir + 1);
	}
	else
	{
		*slash = '\0';
		new = filer_opendir(dir, NULL);
		if (new)
		{
			if (slash[1] == '.')
				display_set_hidden(new, TRUE);
			display_set_autoselect(new, slash + 1);
		}
	}

	g_free(dir);
}

/* Invoked using -x, this indicates that the filesystem has been modified
 * and we should look at this item again.
 */
void examine(const guchar *path)
{
	struct stat info;

	if (mc_stat(path, &info) != 0)
	{
		/* Deleted? Do a paranoid update of everything... */
		filer_check_mounted(path);
	}
	else
	{
		/* Update directory containing this item... */
		dir_check_this(path);

		/* If this is itself a directory then rescan its contents... */
		if (S_ISDIR(info.st_mode))
			refresh_dirs(path);

		/* If it's on the pinboard, update the icon... */
		icons_may_update(path);
	}
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/


static void write_data(gpointer data, gint fd, GdkInputCondition cond)
{
	PipedData *pd = (PipedData *) data;
	
	while (pd->sent < pd->length)
	{
		int	sent;

		sent = write(fd, pd->data + pd->sent, pd->length - pd->sent);

		if (sent < 0)
		{
			if (errno == EAGAIN)
				return;
			delayed_error(_("Could not send data to program: %s"),
					g_strerror(errno));
			goto finish;
		}

		pd->sent += sent;
	}

finish:
	g_source_remove(pd->tag);
	g_free(pd->data);
	g_free(pd);
	close(fd);
}

/* Follow the link 'full_path' and display it in filer_window, or a
 * new window if that is NULL.
 */
static gboolean follow_symlink(const char *full_path,
			       FilerWindow *filer_window,
			       FilerWindow *src_window)
{
	char	*real, *slash;
	char	*new_dir;
	char	path[MAXPATHLEN + 1];
	int	got;

	got = readlink(full_path, path, MAXPATHLEN);
	if (got < 0)
	{
		delayed_error(_("Could not read link: %s"),
				  g_strerror(errno));
		return FALSE;
	}

	g_return_val_if_fail(got <= MAXPATHLEN, FALSE);
	path[got] = '\0';

	/* Make a relative path absolute */
	if (path[0] != '/')
	{
		guchar	*tmp;
		slash = strrchr(full_path, '/');
		g_return_val_if_fail(slash != NULL, FALSE);

		tmp = g_strndup(full_path, slash - full_path);
		real = pathdup(make_path(tmp, path)->str);
		/* NB: full_path may be invalid here... */
		g_free(tmp);
	}
	else
		real = pathdup(path);

	slash = strrchr(real, '/');
	if (!slash)
	{
		g_free(real);
		delayed_error(
			_("Broken symlink (or you don't have permission "
			  "to follow it): %s"), full_path);
		return FALSE;
	}

	*slash = '\0';

	if (*real)
		new_dir = real;
	else
		new_dir = "/";

	if (filer_window)
		filer_change_to(filer_window, new_dir, slash + 1);
	else
	{
		FilerWindow *new;
		
		new = filer_opendir(new_dir, src_window);
		if (new)
			display_set_autoselect(new, slash + 1);
	}

	g_free(real);

	return TRUE;
}

/* Load this file into an appropriate editor */
static gboolean open_file(const guchar *path, MIME_type *type)
{
	g_return_val_if_fail(type != NULL, FALSE);

	if (type_open(path, type))
		return TRUE;

	report_error(
		_("No run action specified for files of this type (%s/%s) - "
		"you can set a run action by choosing `Set Run Action' "
		"from the File menu, or you can just drag the file to an "
		"application"),
		type->media_type,
		type->subtype);

	return FALSE;
}

/* Show the help for a directory - tries to open App/Help, but if
 * that doesn't work then it displays a default message.
 */
static void dir_show_help(DirItem *item, const char *path)
{
	char		*help_dir;
	struct stat 	info;

	help_dir = g_strconcat(path, "/Help", NULL);

	if (mc_stat(help_dir, &info) == 0)
		filer_opendir(help_dir, NULL);
	else if (item->flags & ITEM_FLAG_APPDIR)
		info_message(
			_("Application:\n"
			"This is an application directory - you can "
			"run it as a program, or open it (hold down "
			"Shift while you open it). Most applications provide "
			"their own help here, but this one doesn't."));
	else
		info_message(_("Directory:\n"
				"This is a directory. It contains an index to "
				"other items - open it to see the list."));
}
