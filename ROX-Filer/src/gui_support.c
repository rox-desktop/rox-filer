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

#include <glib.h>
#include <gtk/gtk.h>

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

	va_start(ap, number_of_buttons);

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
