/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@users.sourceforge.net>.
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

/* This code is used to communicate between two copies of the filer:
 * If the filer is run and the same version of the filer is already
 * running on the same machine then the new copy simply asks the old
 * one deal with it and quits.
 */

#include "config.h"

#include <gdk/gdkx.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <gtk/gtk.h>
#include <gtk/gtkinvisible.h>

#include "global.h"

#include "main.h"
#include "support.h"
#include "gui_support.h"
#include "run.h"
#include "remote.h"

static GdkAtom filer_atom;	/* _ROX_FILER_VERSION_HOST */
static GdkAtom ipc_atom;	/* _ROX_FILER_OPEN */

/* Static prototypes */
static GdkWindow *get_existing_ipc_window();
static gboolean get_ipc_property(GdkWindow *window, Window *r_xid);
static gboolean ipc_prop_changed(GtkWidget *window,
				 GdkEventProperty *event,
				 gpointer data);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/


/* Try to get an already-running filer to handle things (only if
 * new_copy is FALSE); TRUE if we succeed.
 * Create and IPC widget so that future filers can contact us.
 *
 * See main() for a description of 'to_open'.
 */
gboolean remote_init(GString *to_open, gboolean new_copy)
{
	guchar	*unique_id;
	GdkWindowPrivate	*window;
	GdkWindow		*existing_ipc_window;
	GtkWidget		*ipc_window;

	unique_id = g_strdup_printf("_ROX_FILER_%d_%s_%s",
				(int) euid, VERSION, our_host_name());
	filer_atom = gdk_atom_intern(unique_id, FALSE);
	g_free(unique_id);

	ipc_atom = gdk_atom_intern("_ROX_FILER_OPEN", FALSE);

	existing_ipc_window = new_copy ? NULL : get_existing_ipc_window();
	if (existing_ipc_window)
	{
		gdk_property_change(existing_ipc_window, ipc_atom, XA_STRING, 8,
			GDK_PROP_MODE_APPEND, to_open->str, to_open->len);
		return TRUE;
	}

	/* Note: possible race-condition here if two filers both
	 * start at the same time and one isn't already running.
	 * Oh well.
	 */

	ipc_window = gtk_invisible_new();
	gtk_widget_realize(ipc_window);
			
	window = (GdkWindowPrivate *) ipc_window->window;

	/* Make the IPC window contain a property pointing to
	 * itself - this can then be used to check that it really
	 * is an IPC window.
	 */
	gdk_property_change(ipc_window->window, filer_atom,
			XA_WINDOW, 32, GDK_PROP_MODE_REPLACE,
			(guchar *) &window->xwindow, 1);

	/* Get notified when the IPC property is changed */
	gtk_widget_add_events(ipc_window, GDK_PROPERTY_CHANGE_MASK);
	gtk_signal_connect(GTK_OBJECT(ipc_window), "property-notify-event",
			GTK_SIGNAL_FUNC(ipc_prop_changed), NULL);

	/* Make the root window contain a pointer to the IPC window */
	gdk_property_change(GDK_ROOT_PARENT(), filer_atom,
			XA_WINDOW, 32, GDK_PROP_MODE_REPLACE,
			(guchar *) &window->xwindow, 1);

	return FALSE;
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Get the remote IPC window of the already-running filer if there
 * is one.
 */
static GdkWindow *get_existing_ipc_window()
{
	Window		xid, xid_confirm;
	GdkWindow	*window;

	if (!get_ipc_property(GDK_ROOT_PARENT(), &xid))
		return NULL;

	window = gdk_window_foreign_new(xid);
	if (!window)
		return NULL;

	if (!get_ipc_property(window, &xid_confirm) || xid_confirm != xid)
		return NULL;

	return window;
}

/* Returns the 'rox_atom' property of 'window' */
static gboolean get_ipc_property(GdkWindow *window, Window *r_xid)
{
	guchar		*data;
	gint		format, length;
	gboolean	retval = FALSE;
	
	if (gdk_property_get(window, filer_atom, XA_WINDOW, 0, 4,
			FALSE, NULL, &format, &length, &data) && data)
	{
		if (format == 32 && length == 4)
		{
			retval = TRUE;
			*r_xid = *((Window *) data);
		}
		g_free(data);
	}

	return retval;
}


static gboolean ipc_prop_changed(GtkWidget *window,
				 GdkEventProperty *event,
				 gpointer data)
{
	if (event->atom == ipc_atom)
	{
		gint	grab_len = 4096;
		gint	length;
		guchar	*data;

		while (1)
		{

			if (!(gdk_property_get(window->window, ipc_atom,
					XA_STRING, 0, grab_len,
					TRUE, NULL, NULL,
					&length, &data) && data))
				return TRUE;	/* Error? */

			if (length >= grab_len)
			{
				/* Didn't get all of it - try again */
				grab_len <<= 1;
				g_free(data);
				continue;
			}

			data = g_realloc(data, length + 1);
			data[length] = '\0';

			run_list(data);

			g_free(data);
			break;
		}

		return TRUE;
	}

	return FALSE;
}

