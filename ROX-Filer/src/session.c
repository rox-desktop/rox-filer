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

/* session.c - XSMP client support */

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <X11/SM/SMlib.h>
#include <pwd.h>
#include "global.h"
#include "filer.h"
#include "main.h"
#include "pinboard.h"
#include "panel.h"
#include "sc.h"

void save_state(SmClient *client)
{
	FilerWindow *filer_window;
	Panel *panel;
	Pinboard *pinboard = current_pinboard;
	GList *list;
	GPtrArray *restart_cmd = g_ptr_array_new();
	SmPropValue *program;
	gchar *types[] = { "-t", "-b", "-l", "-r" };
	gint i, nvals;
	
	sc_get_prop_value(client, SmProgram, &program, &nvals);
	g_ptr_array_add(restart_cmd, program->value);
	
	g_ptr_array_add(restart_cmd, "-c");
	g_ptr_array_add(restart_cmd, client->id);
	
	for(list = all_filer_windows; list != NULL; list = g_list_next(list))
	{
		filer_window = (FilerWindow *)list->data;
		gdk_window_set_role(filer_window->window->window, filer_window->path);
		g_ptr_array_add(restart_cmd, "-d");
		g_ptr_array_add(restart_cmd, filer_window->path);
	}
	
	for(i = 0; i < PANEL_NUMBER_OF_SIDES; i++)
	{
		panel = current_panel[i];
		if(!panel)
			continue;
		g_ptr_array_add(restart_cmd, types[panel->side]);
		g_ptr_array_add(restart_cmd, panel->name);
	}
	
	if(pinboard)
	{
		g_ptr_array_add(restart_cmd, "-p");
		g_ptr_array_add(restart_cmd, pinboard->name);
	}
	
	sc_set_list_of_array_prop(client, SmRestartCommand, 
				(gchar **)restart_cmd->pdata, restart_cmd->len);

	g_ptr_array_free(restart_cmd, TRUE);
}

/* Callbacks for various SM messages */

gboolean save_yourself(SmClient *client)
{
	save_state(client);
	return TRUE;
}

void shutdown_cancelled(SmClient *client)
{
}

void save_complete(SmClient *client)
{
}

void die(SmClient *client)
{
	gtk_main_quit();
}

void session_init(gchar *client_id)
{
	SmClient *client;
	struct passwd *pw;
	gchar *bin_path;
	gchar *clone_cmd[] = { bin_path, "-n" };

	if (!sc_session_up())
		return;

	pw = getpwuid(euid);
	bin_path = g_strconcat(app_dir, "/AppRun", NULL);

	client = sc_new(client_id);
	
	if (!sc_connect(client))
	{
		sc_destroy(client);
		return;
	}
	
	sc_set_array_prop(client, SmProgram, bin_path);
	sc_set_array_prop(client, SmUserID, pw->pw_name);
	sc_set_list_of_array_prop(client, SmCloneCommand, clone_cmd, 2);
	sc_set_card_prop(client, SmRestartStyleHint, SmRestartIfRunning);
	
	client->save_yourself_fn = &save_yourself;
	client->shutdown_cancelled_fn = &shutdown_cancelled;
	client->save_complete_fn = &save_complete;
	client->die_fn = &die;
}
