/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
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

#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <sys/param.h>

#include "stdlib.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "menu.h"
#include "main.h"
#include "action.h"

/* Static prototypes */
static void write_data(gpointer data, gint fd, GdkInputCondition cond);
static gboolean follow_symlink(char *full_path, FilerWindow *filer_window);
static gboolean open_file(guchar *path, MIME_type *type);

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
void run_app(char *path)
{
	GString	*apprun;
	char	*argv[] = {NULL, NULL};

	apprun = g_string_new(path);
	argv[0] = g_string_append(apprun, "/AppRun")->str;

	if (!spawn_full(argv, home_dir))
		report_error(PROJECT, "Failed to fork() child process");
	
	g_string_free(apprun, TRUE);
}

/* Execute this program, passing all the URIs in the list as arguments.
 * URIs that are files on the local machine will be passed as simple
 * pathnames. The uri_list should be freed after this function returns.
 */
void run_with_files(char *path, GSList *uri_list)
{
	char		**argv;
	int		argc = 0;
	struct stat 	info;

	if (stat(path, &info))
	{
		delayed_error(PROJECT, _("Program not found - deleted?"));
		return;
	}

	argv = g_malloc(sizeof(char *) * (g_slist_length(uri_list) + 2));

	if (S_ISDIR(info.st_mode))
		argv[argc++] = make_path(path, "AppRun")->str;
	else
		argv[argc++] = path;
	
	while (uri_list)
	{
		char *uri = (char *) uri_list->data;
		char *local;

		local = get_local_path(uri);
		if (local)
			argv[argc++] = local;
		else
			argv[argc++] = uri;
		uri_list = uri_list->next;
	}
	
	argv[argc++] = NULL;

	if (!spawn_full(argv, home_dir))
		delayed_error(PROJECT, _("Failed to fork() child process"));
}

/* Run the program as '<path> -', piping the data to it via stdin.
 * You can g_free() the data as soon as this returns.
 */
void run_with_data(char *path, gpointer data, gulong length)
{
	char		*argv[] = {NULL, "-", NULL};
	struct stat 	info;
	int		fds[2];
	PipedData	*pd;

	if (stat(path, &info))
	{
		delayed_error(PROJECT, _("Program not found - deleted?"));
		return;
	}

	if (S_ISDIR(info.st_mode))
		argv[0] = make_path(path, "AppRun")->str;
	else
		argv[0] = path;
	
	if (pipe(fds))
	{
		delayed_error("pipe() failed", g_strerror(errno));
		return;
	}
	close_on_exec(fds[1], TRUE);
	close_on_exec(fds[0], TRUE);

	switch (fork())
	{
		case -1:
			delayed_error("fork() failed", g_strerror(errno));
			close(fds[1]);
			break;
		case 0:
			/* We are the child */
			chdir(home_dir);
			dup2(to_error_log, STDERR_FILENO);
			close_on_exec(STDERR_FILENO, FALSE);
			if (dup2(fds[0], 0) == -1)
				g_warning("dup2() failed: %s\n",
						g_strerror(errno));
			else
			{
				close_on_exec(0, FALSE);
				if (execv(argv[0], argv))
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
			pd->tag = gdk_input_add(fds[1], GDK_INPUT_WRITE,
							write_data, pd);
			break;
	}

	close(fds[0]);
}

/* Load a file, open a directory or run an application. Or, if 'edit' is set:
 * edit a file, open an application, follow a symlink or mount a device.
 *
 * filer_window is the window to use for displaying a directory.
 * NULL will always use a new directory when needed.
 *
 * Returns TRUE on success.
 */
gboolean run_diritem(guchar *full_path,
		     DirItem *item,
		     FilerWindow *filer_window,
		     gboolean edit)
{
	if (filer_window && filer_window->panel_type)
		filer_window = NULL;
		
	if (item->flags & ITEM_FLAG_SYMLINK && edit)
		return follow_symlink(full_path, filer_window);

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

				paths = g_list_prepend(NULL, full_path);
				action_mount(paths);
				g_list_free(paths);
				if (item->flags & ITEM_FLAG_MOUNTED)
					return TRUE;
			}

			if (filer_window)
				filer_change_to(filer_window, full_path, NULL);
			else
				filer_opendir(full_path, PANEL_NO);
			return TRUE;
		case TYPE_FILE:
			if ((item->flags & ITEM_FLAG_EXEC_FILE) && !edit)
			{
				char	*argv[] = {NULL, NULL};
				guchar	*dir = filer_window ? filer_window->path
							    : NULL;

				argv[0] = full_path;

				if (spawn_full(argv, dir))
					return TRUE;
				else
				{
					report_error(PROJECT,
						_("Failed to fork() child"));
					return FALSE;
				}
			}

			return open_file(full_path, edit ? &text_plain
						  : item->mime_type);
		default:
			report_error("open_item",
					"I don't know how to open that");
			return FALSE;
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
			delayed_error(_("ROX-Filer - Sending data to program"),
					g_strerror(errno));
			goto finish;
		}

		pd->sent += sent;
	}

finish:
	gdk_input_remove(pd->tag);
	g_free(pd->data);
	g_free(pd);
	close(fd);
}

/* Follow the link 'full_path' and display it in filer_window, or a
 * new window if that is NULL.
 */
static gboolean follow_symlink(char *full_path, FilerWindow *filer_window)
{
	char	*real, *slash;
	char	*new_dir;
	char	path[MAXPATHLEN + 1];
	int	got;

	got = readlink(full_path, path, MAXPATHLEN);
	if (got < 0)
	{
		delayed_error(PROJECT, g_strerror(errno));
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
		g_free(tmp);
	}
	else
		real = pathdup(path);

	slash = strrchr(real, '/');
	if (!slash)
	{
		g_free(real);
		delayed_error(PROJECT,
			_("Broken symlink (or you don't have permission "
			  "to follow it)."));
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
		
		new = filer_opendir(new_dir, PANEL_NO);
		display_set_autoselect(new, slash + 1);
	}

	g_free(real);

	return TRUE;
}

/* Load this file into an appropriate editor */
static gboolean open_file(guchar *path, MIME_type *type)
{
	GString		*message;

	g_return_val_if_fail(type != NULL, FALSE);

	if (type_open(path, type))
		return TRUE;

	message = g_string_new(NULL);
	g_string_sprintf(message,
		_("No run action specified for files of this type (%s/%s) - "
		"you can set a run action using by choosing `Set Run Action' "
		"from the Window menu"),
		type->media_type,
		type->subtype);
	report_error(PROJECT, message->str);
	g_string_free(message, TRUE);

	return FALSE;
}
