/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2003, the ROX-Filer team.
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
static void open_mountpoint(const guchar *full_path, DirItem *item,
			    FilerWindow *filer_window, FilerWindow *src_window,
			    gboolean edit);

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

void run_app_with_arg(const char *path, const char *arg)
{
	GString	*apprun;
	const char *argv[] = {NULL, NULL, NULL};

	apprun = g_string_new(path);
	argv[0] = g_string_append(apprun, "/AppRun")->str;
	argv[1] = arg;

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
	int		argc = 0, i;
	struct stat 	info;

	if (stat(path, &info))
	{
		delayed_error(_("Program %s not found - deleted?"), path);
		return;
	}

	argv = g_malloc(sizeof(char *) * (g_list_length(uri_list) + 2));

	if (S_ISDIR(info.st_mode))
		argv[argc++] = make_path(path, "AppRun");
	else
		argv[argc++] = path;
	
	while (uri_list)
	{
		const char *uri = uri_list->data;
		char *local;

		local = get_local_path(uri);
		if (local) 
			argv[argc++] = local;
		else
			argv[argc++] = g_strdup(uri);
		uri_list = uri_list->next;
	}
	
	argv[argc++] = NULL;

	rox_spawn(home_dir, argv);

	for (i = 1; i < argc; i++)
		g_free((gchar *) argv[i]);
	g_free(argv);
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
		argv[0] = make_path(path, "AppRun");
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
			pd->tag = gdk_input_add_full(fds[1], GDK_INPUT_WRITE,
						write_data, pd, NULL);
			break;
	}

	close(fds[0]);
}

/* Load a file, open a directory or run an application. Or, if 'edit' is set:
 * edit a file, open an application, follow a symlink or mount a device.
 *
 * arg is a string to append to the command when running apps or executables,
 * or NULL.   Ignored for non-executables
 * filer_window is the window to use for displaying a directory.
 * NULL will always use a new directory when needed.
 * src_window is the window to copy options from, or NULL.
 *
 * Returns TRUE on success.
 */
gboolean run_diritem_with_arg(const guchar *full_path,
		     DirItem *item,
			      const gchar *arg,
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
				if(arg)
					run_app_with_arg(full_path, arg);
				else
					run_app(full_path);
				return TRUE;
			}

			if (item->flags & ITEM_FLAG_MOUNT_POINT)
			{
				open_mountpoint(full_path, item,
						filer_window, src_window, edit);
			}
			else if (filer_window)
				filer_change_to(filer_window, full_path, NULL);
			else
				filer_opendir(full_path, src_window, NULL);
			return TRUE;
		case TYPE_FILE:
			if (item->mime_type == application_executable && !edit)
			{
				const char *argv[] = {NULL, NULL, NULL};
				guchar	*dir = filer_window
						? filer_window->sym_path
						: NULL;

				argv[0] = full_path;
				argv[1] = arg;

				return rox_spawn(dir, argv) != 0;
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
	run_diritem_with_arg(full_path, item, NULL, filer_window,
			     src_window, edit);
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

/* Open dir/Help, or show a message if missing */
void show_help_files(const char *dir)
{
	const char	*help_dir;

	help_dir = make_path(dir, "Help");

	if (file_exists(help_dir))
		filer_opendir(help_dir, NULL, NULL);
	else
		info_message(
			_("Application:\n"
			"This is an application directory - you can "
			"run it as a program, or open it (hold down "
			"Shift while you open it). Most applications provide "
			"their own help here, but this one doesn't."));
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
		new = filer_opendir("/", NULL, NULL);
		if (new && dir[1])
			display_set_autoselect(new, dir + 1);
	}
	else
	{
		*slash = '\0';
		new = filer_opendir(dir, NULL, NULL);
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
		real = pathdup(make_path(tmp, path));
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
		
		new = filer_opendir(new_dir, src_window, NULL);
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

/* Called like run_diritem, when a mount-point is opened */
static void open_mountpoint(const guchar *full_path, DirItem *item,
			    FilerWindow *filer_window, FilerWindow *src_window,
			    gboolean edit)
{
	gboolean mounted = (item->flags & ITEM_FLAG_MOUNTED) != 0;

	if (mounted == edit)
	{
		GList	*paths;

		paths = g_list_prepend(NULL, (gpointer) full_path);
		action_mount(paths, filer_window == NULL, -1);
		g_list_free(paths);
		if (filer_window && !mounted)
			filer_change_to(filer_window, full_path, NULL);
	}
	else
	{
		if (filer_window)
			filer_change_to(filer_window, full_path, NULL);
		else
			filer_opendir(full_path, src_window, NULL);
	}
}
