/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* gui_support.c - general (GUI) support routines */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <stdarg.h>
#include <errno.h>

#include <glib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <gdk/gdk.h>

#include "main.h"
#include "gui_support.h"

static GdkAtom xa_win_state;
static GdkAtom xa_win_layer;
static GdkAtom xa_win_hints;
static GdkAtom xa_cardinal;

void gui_support_init()
{
	xa_win_state = gdk_atom_intern("_WIN_STATE", FALSE);
	xa_win_layer = gdk_atom_intern("_WIN_LAYER", FALSE);
	xa_win_hints = gdk_atom_intern("_WIN_HINTS", FALSE);
	xa_cardinal  = gdk_atom_intern("CARDINAL", FALSE);
}

static void choice_clicked(GtkWidget *widget, gpointer number)
{
	int	*choice_return;

	choice_return = gtk_object_get_data(GTK_OBJECT(widget),
			"choice_return");

	if (choice_return)
		*choice_return = (int) number;
}

/* Open a modal dialog box showing a message.
 * The user can choose from a selection of buttons at the bottom.
 * Returns -1 if the window is destroyed, or the number of the button
 * if one is clicked (starting from zero).
 */
int get_choice(char *title,
	       char *message,
	       int number_of_buttons, ...)
{
	GtkWidget	*dialog;
	GtkWidget	*vbox, *action_area, *separator;
	GtkWidget	*text, *text_container;
	GtkWidget	*button;
	int		i, retval;
	va_list	ap;
	int		choice_return;

	dialog = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(dialog), vbox);

	action_area = gtk_hbox_new(TRUE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(action_area), 10);
	gtk_box_pack_end(GTK_BOX(vbox), action_area, FALSE, TRUE, 0);

	separator = gtk_hseparator_new ();
	gtk_box_pack_end(GTK_BOX(vbox), separator, FALSE, TRUE, 0);

	text = gtk_label_new(message);
	gtk_label_set_line_wrap(GTK_LABEL(text), TRUE);
	text_container = gtk_event_box_new();
	gtk_container_set_border_width(GTK_CONTAINER(text_container), 32);
	gtk_container_add(GTK_CONTAINER(text_container), text);

	gtk_box_pack_start(GTK_BOX(vbox),
			text_container,
			TRUE, TRUE, 0);

	va_start(ap, number_of_buttons);

	for (i = 0; i < number_of_buttons; i++)
	{
		button = gtk_button_new_with_label(va_arg(ap, char *));
		gtk_object_set_data(GTK_OBJECT(button), "choice_return",
				&choice_return);
		gtk_box_pack_start(GTK_BOX(action_area),
				button,
				TRUE, TRUE, 16);
		gtk_signal_connect(GTK_OBJECT(button), "clicked",
				choice_clicked, (gpointer) i);
	}
	if (!i)
		gtk_widget_grab_focus(button);

	va_end(ap);

	gtk_object_set_data(GTK_OBJECT(dialog), "choice_return",
				&choice_return);
	gtk_signal_connect(GTK_OBJECT(dialog), "destroy", choice_clicked,
			(gpointer) -1);

	choice_return = -2;

	gtk_widget_show_all(dialog);

	while (choice_return == -2)
		g_main_iteration(TRUE);

	retval = choice_return;

	if (retval != -1)
		gtk_widget_destroy(dialog);

	return retval;
}

/* Display a message in a window */
void report_error(char *title, char *message)
{
	g_return_if_fail(message != NULL);

	if (!title)
		title = "Error";

	get_choice(title, message, 1, "OK");
}

void set_cardinal_property(GdkWindow *window, GdkAtom prop, guint32 value)
{
	gdk_property_change(window, prop, xa_cardinal, 32,
				GDK_PROP_MODE_REPLACE, (gchar *) &value, 1);
}

void make_panel_window(GdkWindow *window)
{
	static gboolean need_init = TRUE;
	static GdkAtom	xa_state;

	if (need_init)
	{
		xa_state = gdk_atom_intern("_WIN_STATE", FALSE);
		need_init = FALSE;
	}
	
	gdk_window_set_decorations(window, 0);
	gdk_window_set_functions(window, 0);

	set_cardinal_property(window, xa_state,
			WIN_STATE_STICKY | WIN_STATE_HIDDEN |
			WIN_STATE_FIXED_POSITION | WIN_STATE_ARRANGE_IGNORE);
}

gint hide_dialog_event(GtkWidget *widget, GdkEvent *event, gpointer window)
{
	gtk_widget_hide((GtkWidget *) window);

	return TRUE;
}

static gboolean error_idle_cb(gpointer data)
{
	char	**error = (char **) data;
	
	report_error(error[0], error[1]);
	g_free(error[0]);
	g_free(error[1]);
	error[0] = error[1] = NULL;

	if (--number_of_windows == 0)
		gtk_main_quit();

	return FALSE;
}

/* Display an error next time we are idle */
void delayed_error(char *title, char *error)
{
	static char *delayed_error_data[2] = {NULL, NULL};
	
	g_return_if_fail(error != NULL);
	g_return_if_fail(delayed_error_data[1] == NULL);

	delayed_error_data[0] = g_strdup(title);
	delayed_error_data[1] = g_strdup(error);
	
	gtk_idle_add(error_idle_cb, delayed_error_data);

	number_of_windows++;
}

/* Load the file into memory. Return TRUE on success.
 * Block is zero terminated (but this is not included in the length).
 */
gboolean load_file(char *pathname, char **data_out, long *length_out)
{
	FILE		*file;
	long		length;
	char		*buffer;
	gboolean 	retval = FALSE;

	file = fopen(pathname, "r");

	if (!file)
	{
		delayed_error("Opening file for DND", g_strerror(errno));
		return FALSE;
	}

	fseek(file, 0, SEEK_END);
	length = ftell(file);

	buffer = malloc(length + 1);
	if (buffer)
	{
		fseek(file, 0, SEEK_SET);
		fread(buffer, 1, length, file);

		if (ferror(file))
		{
			delayed_error("Loading file for DND",
						g_strerror(errno));
			g_free(buffer);
		}
		else
		{
			*data_out = buffer;
			*length_out = length;
			buffer[length] = '\0';
			retval = TRUE;
		}
	}
	else
		delayed_error("Loading file for DND",
				"Can't allocate memory for buffer to "
				"transfer this file");

	fclose(file);

	return retval;
}
