/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GTK+ Team and others 1997-1999.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/. 
 */

#ifndef __GTK_SAVEBOX_H__
#define __GTK_SAVEBOX_H__


#include <gdk/gdk.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkselection.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* This is for the 'info' value of the GtkTargetList.
 * It's for the XdndDirectSave0 target - ignore requests for this target
 * because they're handled internally by the widget. Don't use this
 * value for anything else!
 */
#define GTK_TARGET_XDS 0x584453

#define GTK_TYPE_SAVEBOX		(gtk_savebox_get_type ())

#define GTK_SAVEBOX(obj)		\
	(GTK_CHECK_CAST ((obj), GTK_TYPE_SAVEBOX, GtkSavebox))

#define GTK_SAVEBOX_CLASS(klass)	\
	(GTK_CHECK_CLASS_CAST ((klass), GTK_TYPE_SAVEBOX, GtkSaveboxClass))

#define GTK_IS_SAVEBOX(obj)	(GTK_CHECK_TYPE ((obj), GTK_TYPE_SAVEBOX))

#define GTK_IS_SAVEBOX_CLASS(klass)	\
	(GTK_CHECK_CLASS_TYPE ((klass), GTK_TYPE_SAVEBOX))


typedef struct _GtkSavebox        GtkSavebox;
typedef struct _GtkSaveboxClass   GtkSaveboxClass;
typedef struct _GtkSaveboxButton  GtkSaveboxButton;

enum {
  GTK_XDS_SAVED,		/* Done the save - no problem */
  GTK_XDS_SAVE_ERROR,		/* Error during save - reported */
  GTK_XDS_NO_HANDLER,		/* Used internally (sanity checking) */
};

struct _GtkSavebox
{
  GtkWindow window;

  GtkWidget *drag_box;		/* Event box - contains pixmap, or NULL */
  GtkWidget *icon;		/* The pixmap widget */
  GtkWidget *entry;		/* Where the pathname goes */
  GtkWidget *vbox;		/* Append extra buttons here */

  GtkTargetList *targets;	/* Formats that we can save in */
  gboolean  using_xds;		/* Have we sent XDS reply 'S' or 'F' yet? */
  gboolean  data_sent;		/* Did we send any data at all this drag? */
};

struct _GtkSaveboxClass
{
  GtkWindowClass parent_class;

  gint (*save_to_file)	(GtkSavebox *savebox, guchar *pathname);
  void (*saved_to_uri)	(GtkSavebox *savebox, guchar *uri);
  void (*save_done)	(GtkSavebox *savebox);
};


GtkType    gtk_savebox_get_type 	(void);
GtkWidget* gtk_savebox_new		(void);
void	   gtk_savebox_set_icon		(GtkSavebox *savebox,
					 GdkPixmap *pixmap, GdkPixmap *mask);
void	   gtk_savebox_set_pathname	(GtkSavebox *savebox, gchar *pathname);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GTK_SAVEBOX_H__ */
