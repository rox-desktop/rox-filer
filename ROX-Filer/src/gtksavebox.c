/* GTK - The GIMP Toolkit
 * Copyright (C) 1991-the ROX-Filer team.
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
 * Note: This file is formatted like the Gtk+ sources, as it is/was hoped
 * to include it in Gtk+ at some point.
 */

#include "config.h"

#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "gdk/gdkkeysyms.h"

#include "gtksavebox.h"
#include "gtk/gtkwidget.h"
#include "gtk/gtkalignment.h"
#include "gtk/gtkdnd.h"
#include "gtk/gtkbutton.h"
#include "gtk/gtksignal.h"
#include "gtk/gtkpixmap.h"
#include "gtk/gtkhbox.h"
#include "gtk/gtkeventbox.h"
#include "gtk/gtkentry.h"
#include "gtk/gtkhseparator.h"
#include "gtk/gtkvbox.h"
#include "gtk/gtkdialog.h"
#include "gtk/gtklabel.h"

#include "global.h"
#include "support.h"

/* Signals:
 *
 * gint save_to_file (GtkSavebox *savebox, guchar *pathname) 
 * 	Save the data to disk using this pathname. Return GTK_XDS_SAVED
 * 	on success, or GTK_XDS_SAVE_ERROR on failure (and report the error
 * 	to the user somehow). DO NOT mark the data unmodified or change
 * 	the pathname for the file - this might be a scrap file transfer.
 *
 * void saved_to_uri (GtkSavebox *savebox, guchar *uri)
 *	The data is saved and safe. Mark the file as unmodified and update
 *	the pathname/uri for the file to the one given.
 * 
 * void save_done (GtkSavebox *savebox)
 *	The save operation is over. Close the savebox. This signal is sent
 *	regardless of whether the data is now 'safe', but not if no data
 *	has been sent.
 */

enum
{
	SAVE_TO_FILE,
	SAVED_TO_URI,
	SAVE_DONE,

	LAST_SIGNAL
};

static guint savebox_signals[LAST_SIGNAL] = { 0 };

static GtkWidgetClass *parent_class = NULL;

/* Longest possible XdndDirectSave0 property value */
#define XDS_MAXURILEN 4096

static GdkAtom XdndDirectSave;
static GdkAtom text_plain;
static GdkAtom xa_string;

static void gtk_savebox_class_init (GtkSaveboxClass   *klass);
static void gtk_savebox_init       (GtkSavebox	      *savebox);
static void button_press_over_icon (GtkWidget	      *drag_box,
				    GdkEventButton    *event,
				    GtkSavebox	      *savebox);
static void drag_data_get	   (GtkWidget	      *widget,
				    GdkDragContext    *context,
				    GtkSelectionData  *selection_data,
				    guint	      info,
				    guint32	      time);
static guchar *read_xds_property   (GdkDragContext    *context,
				    gboolean	      delete);
static void write_xds_property	   (GdkDragContext    *context,
				    guchar	      *value);
static void drag_end 		   (GtkWidget 	      *widget,
				    GdkDragContext    *context);
static void do_save		   (GtkWidget	      *widget,
				    GtkSavebox	      *savebox);
static gint delete_event	   (GtkWidget	      *widget,
                                    GdkEventAny	      *event);
static gint key_press_event	   (GtkWidget	      *widget,
				    GdkEventKey	      *event);
static void cancel_clicked	   (GtkWidget	      *widget,
				    GtkSavebox	      *savebox);


GtkType
gtk_savebox_get_type (void)
{
  static GtkType savebox_type = 0;

  if (!savebox_type)
    {
      static const GtkTypeInfo savebox_info =
      {
	"GtkSavebox",
	sizeof (GtkSavebox),
	sizeof (GtkSaveboxClass),
	(GtkClassInitFunc) gtk_savebox_class_init,
	(GtkObjectInitFunc) gtk_savebox_init,
	/* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      savebox_type = gtk_type_unique (GTK_TYPE_WINDOW, &savebox_info);
    }

  return savebox_type;
}

#ifndef GTK_CLASS_TYPE
# define GTK_CLASS_TYPE(c) (c->type)
#endif

static void
gtk_savebox_class_init (GtkSaveboxClass *class)
{
  GtkObjectClass *object_class = (GtkObjectClass *) class;
  GtkWidgetClass *widget_class = (GtkWidgetClass *) class;
  
  XdndDirectSave = gdk_atom_intern ("XdndDirectSave0", FALSE);
  text_plain = gdk_atom_intern ("text/plain", FALSE);
  xa_string = gdk_atom_intern ("STRING", FALSE);

  parent_class = gtk_type_class(gtk_window_get_type());

  class->save_to_file = NULL;
  widget_class->delete_event = delete_event;
  widget_class->key_press_event = key_press_event;

  savebox_signals[SAVE_TO_FILE] = gtk_signal_new ("save_to_file",
					    GTK_RUN_LAST,
					    GTK_CLASS_TYPE(object_class),
					    GTK_SIGNAL_OFFSET (GtkSaveboxClass,
							       save_to_file),
					    gtk_marshal_INT__POINTER,
					    GTK_TYPE_INT, 1,
					    GTK_TYPE_POINTER);

  savebox_signals[SAVED_TO_URI] = gtk_signal_new ("saved_to_uri",
					    GTK_RUN_LAST,
					    GTK_CLASS_TYPE(object_class),
					    GTK_SIGNAL_OFFSET (GtkSaveboxClass,
							       saved_to_uri),
					    gtk_marshal_NONE__POINTER,
					    GTK_TYPE_NONE, 1,
					    GTK_TYPE_POINTER);

  savebox_signals[SAVE_DONE] = gtk_signal_new ("save_done",
					    GTK_RUN_LAST,
					    GTK_CLASS_TYPE(object_class),
					    GTK_SIGNAL_OFFSET (GtkSaveboxClass,
							       save_done),
					    gtk_marshal_NONE__NONE,
					    GTK_TYPE_NONE, 0);

#ifndef GTK2
  gtk_object_class_add_signals (object_class, savebox_signals, LAST_SIGNAL);
#endif
}

static void
gtk_savebox_init (GtkSavebox *savebox)
{
  GtkWidget *hbox, *button, *alignment;
  GtkTargetEntry targets[] = { {"XdndDirectSave0", 0, GTK_TARGET_XDS} };

  savebox->targets = gtk_target_list_new (targets,
					  sizeof (targets) / sizeof (*targets));
  savebox->icon = NULL;

#ifdef GTK2
  gtk_window_set_type_hint (GTK_WINDOW (savebox), GDK_WINDOW_TYPE_HINT_DIALOG);
#else
  GTK_WINDOW (savebox)->type = GTK_WINDOW_DIALOG;
#endif
  gtk_window_set_title (GTK_WINDOW (savebox), _("Save As:"));
  gtk_window_set_position (GTK_WINDOW (savebox), GTK_WIN_POS_MOUSE);
  gtk_window_set_wmclass (GTK_WINDOW (savebox), "savebox", "Savebox");
  gtk_container_set_border_width (GTK_CONTAINER (savebox), 4);

  savebox->vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (savebox), savebox->vbox);

  alignment = gtk_alignment_new (0.5, 0.5, 0, 0);
  gtk_box_pack_start (GTK_BOX (savebox->vbox), alignment, TRUE, TRUE, 0);

  savebox->drag_box = gtk_event_box_new ();
  gtk_container_set_border_width (GTK_CONTAINER (savebox->drag_box), 4);
  gtk_widget_add_events (savebox->drag_box, GDK_BUTTON_PRESS_MASK);
  gtk_signal_connect (GTK_OBJECT (savebox->drag_box), "button_press_event",
		      GTK_SIGNAL_FUNC (button_press_over_icon), savebox);
  gtk_signal_connect (GTK_OBJECT (savebox), "drag_end",
		      GTK_SIGNAL_FUNC (drag_end), savebox);
  gtk_signal_connect (GTK_OBJECT (savebox), "drag_data_get",
		      GTK_SIGNAL_FUNC (drag_data_get), savebox);
  gtk_container_add (GTK_CONTAINER (alignment), savebox->drag_box);

  savebox->entry = gtk_entry_new ();
  gtk_signal_connect (GTK_OBJECT (savebox->entry), "activate",
		      GTK_SIGNAL_FUNC (do_save), savebox);
  gtk_box_pack_start (GTK_BOX (savebox->vbox), savebox->entry, FALSE, TRUE, 4);
  
  hbox = gtk_hbox_new (TRUE, 0);
  gtk_box_pack_end (GTK_BOX (savebox->vbox), hbox, FALSE, TRUE, 0);

  button = gtk_button_new_with_label (_("OK"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (do_save), savebox);
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
  GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
  gtk_widget_grab_default (button);

  button = gtk_button_new_with_label (_("Cancel"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (cancel_clicked), savebox);
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
  GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);

  gtk_widget_show_all (savebox->vbox);

  gtk_widget_grab_focus (savebox->entry);
}

GtkWidget*
gtk_savebox_new (void)
{
  GtkSavebox *savebox;

  savebox = gtk_type_new (gtk_savebox_get_type ());

  return GTK_WIDGET (savebox);
}

void
gtk_savebox_set_icon (GtkSavebox *savebox, GdkPixmap *pixmap, GdkPixmap *mask)
{
  g_return_if_fail (savebox != NULL);
  g_return_if_fail (GTK_IS_SAVEBOX (savebox));
  g_return_if_fail (pixmap != NULL);

  if (savebox->icon)
    gtk_pixmap_set (GTK_PIXMAP (savebox->icon), pixmap, mask);
  else
    {
      savebox->icon = gtk_pixmap_new (pixmap, mask);
      gtk_container_add (GTK_CONTAINER (savebox->drag_box), savebox->icon);
      gtk_widget_show(savebox->icon);
    }
}

void
gtk_savebox_set_pathname (GtkSavebox *savebox, gchar *pathname)
{
  gchar *slash, *dot;
  gint	leaf;
  
  g_return_if_fail (savebox != NULL);
  g_return_if_fail (GTK_IS_SAVEBOX (savebox));
  g_return_if_fail (pathname != NULL);

  gtk_entry_set_text (GTK_ENTRY (savebox->entry), pathname);

  slash = strrchr (pathname, '/');
  
  leaf = slash ? slash - pathname + 1 : 0;
  dot = strchr(pathname + leaf, '.');
  
  /* Gtk+ doesn't seem to scroll the entry properly without this... */
  gtk_widget_realize (savebox->entry);
  gtk_entry_set_position (GTK_ENTRY (savebox->entry), -1);

  gtk_editable_select_region (GTK_EDITABLE (savebox->entry), leaf,
			      dot ? dot - pathname : -1);
}

static void
button_press_over_icon (GtkWidget *drag_box, GdkEventButton *event,
			GtkSavebox *savebox)
{
  GdkDragContext  *context;
  GdkPixmap	  *pixmap, *mask;
  guchar	  *uri = NULL, *leafname;

  g_return_if_fail (savebox != NULL);
  g_return_if_fail (GTK_IS_SAVEBOX (savebox));
  g_return_if_fail (event != NULL);
  g_return_if_fail (savebox->icon != NULL);

  savebox->using_xds = FALSE;
  savebox->data_sent = FALSE;
  context = gtk_drag_begin (GTK_WIDGET (savebox),
			    savebox->targets, GDK_ACTION_COPY,
			    event->button, (GdkEvent *) event);

  uri = gtk_entry_get_text (GTK_ENTRY (savebox->entry));
  if (uri && *uri)
    {
      leafname = strrchr (uri, '/');
      if (leafname)
	leafname++;
      else
	leafname = uri;
    }
  else
    leafname = _("Unnamed");
  
  write_xds_property (context, leafname);

  gtk_pixmap_get (GTK_PIXMAP (savebox->icon), &pixmap, &mask);
  gtk_drag_set_icon_pixmap (context,
			    gtk_widget_get_colormap (savebox->icon),
			    pixmap,
			    mask,
			    event->x, event->y);

}

static void
drag_data_get (GtkWidget	*widget,
	       GdkDragContext   *context,
	       GtkSelectionData *selection_data,
               guint            info,
               guint32          time)
{
  GtkSavebox  *savebox;
  guchar      to_send = 'E';
  guchar      *uri;
  guchar      *pathname;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SAVEBOX (widget));
  g_return_if_fail (context != NULL);
  g_return_if_fail (selection_data != NULL);

  savebox = GTK_SAVEBOX (widget);

  /* We're only concerned with the XDS protocol. Responding to other requests
   * (including application/octet-stream) is the job of the application.
   */
  if (info != GTK_TARGET_XDS)
  {
    /* Assume that the data will be/has been sent */
    savebox->data_sent = TRUE;
    return;
  }

  uri = read_xds_property (context, FALSE);

  if (uri)
  {
    gint result = GTK_XDS_NO_HANDLER;

    pathname = get_local_path (uri);
    if (!pathname)
      to_send = 'F';    /* Not on the local machine */
    else
      {
	gtk_signal_emit (GTK_OBJECT (widget), 
	    savebox_signals[SAVE_TO_FILE],
	    pathname, &result);

	if (result == GTK_XDS_SAVED)
	  {
	    savebox->data_sent = TRUE;
	    to_send = 'S';
	  }
	else if (result != GTK_XDS_SAVE_ERROR)
	  g_warning ("No handler for saving to a file.\n");

	g_free (uri);
      }
  }
  else
  {
    g_warning (_("Remote application wants to use Direct Save, but I can't "
	       "read the XdndDirectSave0 (type text/plain) property.\n"));
  }

  if (to_send != 'E')
    savebox->using_xds = TRUE;
  gtk_selection_data_set (selection_data, xa_string, 8, &to_send, 1);
}

static guchar *
read_xds_property (GdkDragContext *context, gboolean delete)
{
  guchar  *prop_text;
  guint	  length;
  guchar  *retval = NULL;
  
  g_return_val_if_fail (context != NULL, NULL);

  if (gdk_property_get (context->source_window, XdndDirectSave, text_plain,
		       0, XDS_MAXURILEN, delete,
		       NULL, NULL, &length, &prop_text)
	    && prop_text)
  {
    /* Terminate the string */
    retval = g_realloc (prop_text, length + 1);
    retval[length] = '\0';
  }

  return retval;
}

static void
write_xds_property (GdkDragContext *context, guchar *value)
{
  if (value)
    {
      gdk_property_change (context->source_window, XdndDirectSave,
			   text_plain, 8, GDK_PROP_MODE_REPLACE,
			   value, strlen (value));
    }
  else
    gdk_property_delete (context->source_window, XdndDirectSave);
}

static void drag_end (GtkWidget *widget, GdkDragContext *context)
{
  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SAVEBOX (widget));
  g_return_if_fail (context != NULL);

  if (GTK_SAVEBOX (widget)->using_xds)
    {
      guchar  *uri;
      uri = read_xds_property (context, TRUE);

      if (uri)
	{
	  guchar  *path;

	  path = get_local_path (uri);
	  
	  gtk_signal_emit (GTK_OBJECT (widget),
			   savebox_signals[SAVED_TO_URI],
			   path ? path : uri);
	  g_free(uri);
	}
    }
  else
      write_xds_property (context, NULL);

  if (GTK_SAVEBOX (widget)->data_sent)
    gtk_signal_emit (GTK_OBJECT (widget), savebox_signals[SAVE_DONE]);
}

static void cancel_clicked (GtkWidget *widget, GtkSavebox *savebox)
{
  gtk_signal_emit (GTK_OBJECT (savebox), savebox_signals[SAVE_DONE]);
}

/* User has clicked Save or pressed Return... */
static void do_save (GtkWidget *widget, GtkSavebox *savebox)
{
  gint	  result = GTK_XDS_NO_HANDLER;
  guchar  *pathname, *uri;

  g_return_if_fail (savebox != NULL);
  g_return_if_fail (GTK_IS_SAVEBOX (savebox));

  uri = gtk_entry_get_text (GTK_ENTRY (savebox->entry));
  pathname = get_local_path (uri);

  if (!pathname)
    {
      GtkWidget *dialog, *label, *button;

      dialog = gtk_dialog_new ();
      GTK_WINDOW (dialog)->type = GTK_WINDOW_DIALOG;

      label = gtk_label_new (_("Drag the icon to a directory viewer\n"
				  "(or enter a full pathname)"));
      gtk_misc_set_padding (GTK_MISC (label), 8, 32);

      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			  label, TRUE, TRUE, 4);

      button = gtk_button_new_with_label (_("OK"));
      GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
      gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
		  GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT (dialog));
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area),
			  button, TRUE, TRUE, 32);
      gtk_window_set_default (GTK_WINDOW (dialog), button);

      gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);

      gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

      gtk_widget_show_all (dialog);

      return;
    }

  g_return_if_fail (pathname != NULL);

  gtk_signal_emit (GTK_OBJECT (savebox), savebox_signals[SAVE_TO_FILE],
		   pathname, &result);

  if (result == GTK_XDS_SAVED)
    {
      gtk_signal_emit (GTK_OBJECT (savebox), savebox_signals[SAVED_TO_URI],
		       pathname);
      gtk_signal_emit (GTK_OBJECT (savebox), savebox_signals[SAVE_DONE]);
    }
  else if (result == GTK_XDS_NO_HANDLER)
    g_warning ("No handler for saving to a file.\n");
}

static gint
delete_event(GtkWidget *widget, GdkEventAny *event)
{
  g_return_val_if_fail (widget != NULL, FALSE);

  gtk_signal_emit (GTK_OBJECT (widget), savebox_signals[SAVE_DONE]);

  return TRUE;
}

static gint
key_press_event(GtkWidget *widget, GdkEventKey *event)
{
  gint (*parent_handler)(GtkWidget *widget, GdkEventKey *event);

  g_return_val_if_fail (widget != NULL, FALSE);

  if (event->keyval == GDK_Escape)
  {
    gtk_signal_emit (GTK_OBJECT (widget), savebox_signals[SAVE_DONE]);
    return TRUE;
  }

  parent_handler = GTK_WIDGET_CLASS (parent_class)->key_press_event;

  if (parent_handler)
    return parent_handler (widget, event);

  return FALSE;
}
