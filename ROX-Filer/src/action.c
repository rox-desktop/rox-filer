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

/* action.c - code for handling the filer action windows.
 * These routines generally fork() and talk to us via pipes.
 */

/* Allow use of GtkText widget */
#define GTK_ENABLE_BROKEN

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/param.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <utime.h>

#include "global.h"

#include "action.h"
#include "string.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "display.h"
#include "main.h"
#include "options.h"
#include "modechange.h"
#include "find.h"
#include "dir.h"
#include "icon.h"
#include "mount.h"

/* Parent->Child messages are one character each:
 *
 * Y/N 		Yes/No button clicked
 * F		Force deletion of non-writeable items
 * Q		Quiet toggled
 * E		Entry text changed
 */

#define SENSITIVE_YESNO(gui_side, state)	\
	do {				\
		gtk_widget_set_sensitive((gui_side)->yes, state);	\
		gtk_widget_set_sensitive((gui_side)->no, state);	\
		if ((gui_side)->entry)					\
			gtk_widget_set_sensitive((gui_side)->entry, state);\
	} while (0)

typedef struct _GUIside GUIside;
typedef void ActionChild(gpointer data);
typedef gboolean ForDirCB(char *path, char *dest_path);

struct _GUIside
{
	int 		from_child;	/* File descriptor */
	FILE		*to_child;
	int 		input_tag;	/* gdk_input_add() */
	GtkWidget 	*vbox, *log, *window, *dir, *log_hbox;
	GtkWidget	*quiet, *yes, *no, *quiet_flag;
	int		child;		/* Process ID */
	int		errors;
	gboolean	show_info;	/* For Disk Usage */

	GtkWidget	*entry;		/* May be NULL */
	guchar		**default_string; /* Changed when the entry changes */
	void		(*entry_string_func)(GtkWidget *widget, guchar *string);
	
	char		*next_dir;	/* NULL => no timer active */
	gint		next_timer;

	/* Used by Find */
	FilerWindow	*preview;
	GtkWidget	*results;
};

/* These don't need to be in a structure because we fork() before
 * using them again.
 */
static gboolean mount_open_dir = FALSE;
static int 	from_parent = 0;
static FILE	*to_parent = NULL;
static gboolean	quiet = FALSE;
static GString  *message = NULL;
static char     *action_dest = NULL;
static char     *action_leaf = NULL;
static gboolean (*action_do_func)(char *source, char *dest);
static double	size_tally;		/* For Disk Usage */
static unsigned long dir_counter;	/* For Disk Usage */
static unsigned long file_counter;	/* For Disk Usage */

static struct mode_change *mode_change = NULL;	/* For Permissions */
static FindCondition *find_condition = NULL;	/* For Find */

/* Only used by child */
static gboolean o_force = FALSE;
static gboolean o_brief = FALSE;
static gboolean o_recurse = FALSE;

/* Whenever the text in these boxes is changed we store a copy of the new
 * string to be used as the default next time.
 */
static guchar	*last_chmod_string = NULL;
static guchar	*last_find_string = NULL;

/* Set to one of the above before forking. This may change over a call to
 * reply(). It is reset to NULL once the text is parsed.
 */
static guchar	*new_entry_string = NULL;

/* Static prototypes */
static gboolean send();
static gboolean send_error();
static gboolean send_dir(char *dir);
static gboolean read_exact(int source, char *buffer, ssize_t len);
static void do_mount(guchar *path, gboolean mount);
static GtkWidget *add_toggle(GUIside *gui_side, guchar *label, guchar *code,
			gboolean active);
static gboolean reply(int fd, gboolean ignore_quiet);
static gboolean remove_pinned_ok(GList *paths);

/*			SUPPORT				*/

static void preview_closed(GtkWidget *window, GUIside *gui_side)
{
	gui_side->preview = NULL;
}

static void select_row_callback(GtkWidget *widget,
                             gint row, gint column,
                             GdkEventButton *event,
                             GUIside *gui_side)
{
	char		*leaf, *dir;

	gtk_clist_get_text(GTK_CLIST(gui_side->results), row, 0, &leaf);
	gtk_clist_get_text(GTK_CLIST(gui_side->results), row, 1, &dir);

	gtk_clist_unselect_row(GTK_CLIST(gui_side->results), row, column);

	if (gui_side->preview)
	{
		if (strcmp(gui_side->preview->path, dir) == 0)
			display_set_autoselect(gui_side->preview, leaf);
		else
			filer_change_to(gui_side->preview, dir, leaf);
		return;
	}

	gui_side->preview = filer_opendir(dir, NULL);
	if (gui_side->preview)
	{
		display_set_autoselect(gui_side->preview, leaf);
		gtk_signal_connect(GTK_OBJECT(gui_side->preview->window),
				"destroy",
				GTK_SIGNAL_FUNC(preview_closed), gui_side);
	}
}


/* This is called whenever the user edits the entry box (if any) - send the
 * new string.
 */
static void entry_changed(GtkEditable *entry, GUIside *gui_side)
{
	guchar	*text;

	g_return_if_fail(gui_side->default_string != NULL);

	text = gtk_editable_get_chars(entry, 0, -1);

	if (gui_side->entry_string_func)
		gui_side->entry_string_func(GTK_WIDGET(gui_side->entry), text);

	g_free(*(gui_side->default_string));
	*(gui_side->default_string) = text;	/* Gets text's ref */

	if (!gui_side->to_child)
		return;

	fputc('E', gui_side->to_child);
	fputs(text, gui_side->to_child);
	fputc('\n', gui_side->to_child);
	fflush(gui_side->to_child);
}

void show_condition_help(gpointer data)
{
	static GtkWidget *help = NULL;

	if (!help)
	{
		GtkWidget *text, *vbox, *button, *hbox, *frame;
		
		help = gtk_window_new(GTK_WINDOW_DIALOG);
		gtk_container_set_border_width(GTK_CONTAINER(help), 10);
		gtk_window_set_title(GTK_WINDOW(help),
				_("Find expression reference"));

		vbox = gtk_vbox_new(FALSE, 0);
		gtk_container_add(GTK_CONTAINER(help), vbox);

		frame = gtk_frame_new(_("Quick Start"));
		text = gtk_label_new(
_("Just put the name of the file you're looking for in single quotes:\n"
"'index.html'	(to find a file called 'index.html')"));
		gtk_misc_set_padding(GTK_MISC(text), 4, 4);
		gtk_label_set_justify(GTK_LABEL(text), GTK_JUSTIFY_LEFT);
		gtk_container_add(GTK_CONTAINER(frame), text);
		gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 4);

		frame = gtk_frame_new(_("Examples"));
		text = gtk_label_new(
_("'*.htm', '*.html'      (finds HTML files)\n"
"IsDir 'lib'            (finds directories called 'lib')\n"
"IsReg 'core'           (finds a regular file called 'core')\n"
"! (IsDir, IsReg)       (is neither a directory nor a regular file)\n"
"mtime after 1 day ago and size > 1Mb   (big, and recently modified)\n"
"'CVS' prune, isreg                     (a regular file not in CVS)\n"
"IsReg system(grep -q fred \"%\")         (contains the word 'fred')"));
		gtk_widget_set_style(text, fixed_style);
		gtk_misc_set_padding(GTK_MISC(text), 4, 4);
		gtk_label_set_justify(GTK_LABEL(text), GTK_JUSTIFY_LEFT);
		gtk_container_add(GTK_CONTAINER(frame), text);
		gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 4);

		frame = gtk_frame_new(_("Simple Tests"));
		text = gtk_label_new(
_("IsReg, IsLink, IsDir, IsChar, IsBlock, IsDev, IsPipe, IsSocket (types)\n"
"IsSUID, IsSGID, IsSticky, IsReadable, IsWriteable, IsExecutable (permissions)"
"\n"
"IsEmpty, IsMine\n"
"\n"
"A pattern in single quotes is a shell-style wildcard pattern to match. If it\n"
"contains a slash then the match is against the full path; otherwise it is \n"
"against the leafname only."));
		gtk_misc_set_padding(GTK_MISC(text), 4, 4);
		gtk_label_set_justify(GTK_LABEL(text), GTK_JUSTIFY_LEFT);
		gtk_container_add(GTK_CONTAINER(frame), text);
		gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 4);

		frame = gtk_frame_new(_("Comparisons"));
		text = gtk_label_new(
_("<, <=, =, !=, >, >=, After, Before (compare two values)\n"
"5 bytes, 1Kb, 2Mb, 3Gb (file sizes)\n"
"2 secs|mins|hours|days|weeks|years  ago|hence (times)\n"
"atime, ctime, mtime, now, size, inode, nlinks, uid, gid, blocks (values)"));
		gtk_misc_set_padding(GTK_MISC(text), 4, 4);
		gtk_label_set_justify(GTK_LABEL(text), GTK_JUSTIFY_LEFT);
		gtk_container_add(GTK_CONTAINER(frame), text);
		gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 4);

		frame = gtk_frame_new(_("Specials"));
		text = gtk_label_new(
_("system(command) (true if 'command' returns with a zero exit status; a % \n"
"in 'command' is replaced with the path of the current file)\n"
"prune (false, and prevents searching the contents of a directory).")
);
		gtk_misc_set_padding(GTK_MISC(text), 4, 4);
		gtk_label_set_justify(GTK_LABEL(text), GTK_JUSTIFY_LEFT);
		gtk_container_add(GTK_CONTAINER(frame), text);
		gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 4);

		hbox = gtk_hbox_new(FALSE, 20);
		gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

		text = gtk_label_new(
			_("See the ROX-Filer manual for full details."));
		gtk_box_pack_start(GTK_BOX(hbox), text, TRUE, TRUE, 0);
		button = gtk_button_new_with_label(_("Close"));
		gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
		gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_hide), GTK_OBJECT(help));

		gtk_signal_connect_object(GTK_OBJECT(help), "delete_event",
			GTK_SIGNAL_FUNC(gtk_widget_hide), GTK_OBJECT(help));
	}

	if (GTK_WIDGET_VISIBLE(help))
		gtk_widget_hide(help);
	gtk_widget_show_all(help);
}

static void show_chmod_help(gpointer data)
{
	static GtkWidget *help = NULL;

	if (!help)
	{
		GtkWidget *text, *vbox, *button, *hbox, *sep;
		
		help = gtk_window_new(GTK_WINDOW_DIALOG);
		gtk_container_set_border_width(GTK_CONTAINER(help), 10);
		gtk_window_set_title(GTK_WINDOW(help),
				_("Permissions command reference"));

		vbox = gtk_vbox_new(FALSE, 0);
		gtk_container_add(GTK_CONTAINER(help), vbox);

		text = gtk_label_new(
	_("Normally, you can just select a command from the menu (click \n"
	"on the arrow beside the command box). Sometimes, you need more...\n"
	"\n"
	"The format of a command is:\n"
	"CHANGE, CHANGE, ...\n"
	"Each CHANGE is:\n"
	"WHO HOW PERMISSIONS\n"
	"WHO is some combination of u, g and o which determines whether to\n"
	"change the permissions for the User (owner), Group or Others.\n"
	"HOW is +, - or = to add, remove or set exactly the permissions.\n"
	"PERMISSIONS is some combination of the letters 'rwxXstugo'\n\n"

	"Bracketed text and spaces are ignored.\n\n"

	"Examples:\n"
	"u+rw 	(the file owner gains read and write permission)\n"
	"g=u	(the group permissions are set to be the same as the user's)\n"
	"o=u-w	(others get the same permissions as the owner, but without "
	"write permission)\n"
	"a+x	(everyone gets execute/access permission - same as 'ugo+x')\n"
	"a+X	(directories become accessable by everyone; files which were\n"
	"executable by anyone become executable by everyone)\n"
	"u+rw, go+r	(two commands at once!)\n"
	"u+s	(set the SetUID bit - often has no effect on script files)\n"
	"755	(set the permissions directly)\n"
	
	"\nSee the chmod(1) man page for full details."));
		gtk_label_set_justify(GTK_LABEL(text), GTK_JUSTIFY_LEFT);
		gtk_box_pack_start(GTK_BOX(vbox), text, TRUE, TRUE, 0);

		hbox = gtk_hbox_new(FALSE, 20);
		gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

		sep = gtk_hseparator_new();
		gtk_box_pack_start(GTK_BOX(hbox), sep, TRUE, TRUE, 0);
		button = gtk_button_new_with_label(_("Close"));
		gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
		gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_hide), GTK_OBJECT(help));

		gtk_signal_connect_object(GTK_OBJECT(help), "delete_event",
			GTK_SIGNAL_FUNC(gtk_widget_hide), GTK_OBJECT(help));
	}

	if (GTK_WIDGET_VISIBLE(help))
		gtk_widget_hide(help);
	gtk_widget_show_all(help);
}

static gboolean display_dir(gpointer data)
{
	GUIside	*gui_side = (GUIside *) data;

	gtk_label_set_text(GTK_LABEL(gui_side->dir), gui_side->next_dir + 1);
	g_free(gui_side->next_dir);
	gui_side->next_dir = NULL;
	
	return FALSE;
}

static void add_to_results(GUIside *gui_side, gchar *path)
{
	gchar	*row[] = {"Leaf", "Dir"};
	gchar	*slash;
	int	len;

	slash = strrchr(path, '/');
	g_return_if_fail(slash != NULL);

	len = slash - path;
	row[1] = g_strndup(path, MAX(len, 1));
	row[0] = slash + 1;

	gtk_clist_append(GTK_CLIST(gui_side->results), row);

	g_free(row[1]);
}

/* Called when the child sends us a message */
static void message_from_child(gpointer 	 data,
			       gint     	 source, 
			       GdkInputCondition condition)
{
	char buf[5];
	GUIside	*gui_side = (GUIside *) data;
	GtkWidget *log = gui_side->log;

	if (read_exact(source, buf, 4))
	{
		ssize_t message_len;
		char	*buffer;

		buf[4] = '\0';
		message_len = strtol(buf, NULL, 16);
		buffer = g_malloc(message_len + 1);
		if (message_len > 0 && read_exact(source, buffer, message_len))
		{
			buffer[message_len] = '\0';
			if (*buffer == '?')
			{
				/* Ask a question */
				SENSITIVE_YESNO(gui_side, TRUE);
				gtk_window_set_focus(
					GTK_WINDOW(gui_side->window),
					gui_side->entry ? gui_side->entry
							: gui_side->yes);
			}
			else if (*buffer == '+')
			{
				/* Update/rescan this item */
				refresh_dirs(buffer + 1);
				g_free(buffer);
				return;
			}
			else if (*buffer == '=')
			{
				/* Add to search results */
				add_to_results(gui_side, buffer + 1);
				g_free(buffer);
				return;
			}
			else if (*buffer == '#')
			{
				/* Clear search results area */
				gtk_clist_clear(GTK_CLIST(gui_side->results));
				g_free(buffer);
				return;
			}

			else if (*buffer == 'm' || *buffer == 'M')
			{
				/* Mount / major changes to this path */
				if (*buffer == 'M')
					mount_update(TRUE);
				filer_check_mounted(buffer + 1);
				g_free(buffer);
				return;
			}
			else if (*buffer == '/')
			{
				/* Update the current object display */
				if (gui_side->next_dir)
					g_free(gui_side->next_dir);
				else
					gui_side->next_timer =
						gtk_timeout_add(500,
								display_dir,
								gui_side);
				gui_side->next_dir = buffer;
				return;
			}
			else if (*buffer == 'o')
			{
				/* Open a filer window */
				filer_opendir(buffer + 1, NULL);
				g_free(buffer);
				return;
			}
			else if (*buffer == '!')
				gui_side->errors++;

			gtk_text_insert(GTK_TEXT(log),
					NULL,
					*buffer == '!' ? &red : NULL,
					NULL,
					buffer + 1, message_len - 1);
			g_free(buffer);
			return;
		}
		g_printerr("Child died in the middle of a message.\n");
	}

	/* The child is dead */
	gui_side->child = 0;

	fclose(gui_side->to_child);
	gui_side->to_child = NULL;
	close(gui_side->from_child);
	gdk_input_remove(gui_side->input_tag);
	gtk_widget_set_sensitive(gui_side->quiet, FALSE);

	if (gui_side->errors)
	{
		guchar *report;

		if (gui_side->errors == 1)
			report = g_strdup(_("There was one error.\n"));
		else
			report = g_strdup_printf(_("There were %d errors.\n"),
							gui_side->errors);

		gtk_text_insert(GTK_TEXT(log), NULL, &red, NULL,
				report, -1);

		g_free(report);
	}
	else if (gui_side->show_info == FALSE)
		gtk_widget_destroy(gui_side->window);
}

/* Scans src_dir, updating dest_path whenever cb returns TRUE */
static void for_dir_contents(ForDirCB *cb, char *src_dir, char *dest_path)
{
	DIR	*d;
	struct dirent *ent;
	GList  *list = NULL, *next;

	d = mc_opendir(src_dir);
	if (!d)
	{
		/* Message displayed is "ERROR reading 'path': message" */
		g_string_sprintf(message, "!%s '%s': %s\n",
				_("ERROR reading"),
				src_dir, g_strerror(errno));
		send();
		return;
	}

	send_dir(src_dir);

	while ((ent = mc_readdir(d)))
	{
		if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0'
			|| (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
			continue;
		list = g_list_append(list, g_strdup(make_path(src_dir,
						       ent->d_name)->str));
	}
	mc_closedir(d);

	if (!list)
		return;

	next = list;

	while (next)
	{
		if (cb((char *) next->data, dest_path))
		{
			g_string_sprintf(message, "+%s", dest_path);
			send();
		}

		g_free(next->data);
		next = next->next;
	}
	g_list_free(list);
	return;
}

/* Read this many bytes into the buffer. TRUE on success. */
static gboolean read_exact(int source, char *buffer, ssize_t len)
{
	while (len > 0)
	{
		ssize_t got;
		got = read(source, buffer, len);
		if (got < 1)
			return FALSE;
		len -= got;
		buffer += got;
	}
	return TRUE;
}

/* Send 'message' to our parent process. TRUE on success. */
static gboolean send(void)
{
	char len_buffer[5];
	ssize_t len;

	g_return_val_if_fail(message->len < 0xffff, FALSE);

	sprintf(len_buffer, "%04x", message->len);
	fwrite(len_buffer, 1, 4, to_parent);
	len = fwrite(message->str, 1, message->len, to_parent);
	fflush(to_parent);
	return len == message->len;
}

/* Set the directory indicator at the top of the window */
static gboolean send_dir(char *dir)
{
	g_string_sprintf(message, "/%s", dir);
	return send();
}

static gboolean send_error(void)
{
	g_string_sprintf(message, "!%s: %s\n", _("ERROR"), g_strerror(errno));
	return send();
}

static void quiet_clicked(GtkWidget *button, GUIside *gui_side)
{
	GtkToggleButton *quiet_flag = GTK_TOGGLE_BUTTON(gui_side->quiet_flag);

	if (!gui_side->to_child)
		return;

	gtk_widget_set_sensitive(gui_side->quiet, FALSE);

	if (gtk_toggle_button_get_active(quiet_flag))
		return;		/* (shouldn't happen) */

	gtk_toggle_button_set_active(quiet_flag, TRUE);

	if (GTK_WIDGET_SENSITIVE(gui_side->yes))
		gtk_button_clicked(GTK_BUTTON(gui_side->yes));
}

/* Send 'Quiet' if possible, 'Yes' otherwise */
static void find_return_pressed(GtkWidget *button, GUIside *gui_side)
{
	if (GTK_WIDGET_SENSITIVE(gui_side->quiet))
		gtk_button_clicked(GTK_BUTTON(gui_side->quiet));
	else if (GTK_WIDGET_SENSITIVE(gui_side->yes))
		gtk_button_clicked(GTK_BUTTON(gui_side->yes));
	else
		gdk_beep();
}

static void button_reply(GtkWidget *button, GUIside *gui_side)
{
	char *text;

	if (!gui_side->to_child)
		return;

	text = gtk_object_get_data(GTK_OBJECT(button), "send-code");
	g_return_if_fail(text != NULL);
	fputc(*text, gui_side->to_child);
	fflush(gui_side->to_child);

	if (*text == 'Y' || *text == 'N')
		SENSITIVE_YESNO(gui_side, FALSE);
	if (*text == 'Q')
	{
		gtk_widget_set_sensitive(gui_side->quiet,
			!gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(gui_side->quiet_flag)));
	}
}

static void read_new_entry_text(void)
{
	int	len;
	char	c;
	GString	*new;

	new = g_string_new(NULL);

	for (;;)
	{
		len = read(from_parent, &c, 1);
		if (len != 1)
		{
			fprintf(stderr, "read() error: %s\n",
					g_strerror(errno));
			_exit(1);	/* Parent died? */
		}

		if (c == '\n')
			break;
		g_string_append_c(new, c);
	}

	g_free(new_entry_string);
	new_entry_string = new->str;
	g_string_free(new, FALSE);
}

static void process_flag(char flag)
{
	switch (flag)
	{
		case 'Q':
			quiet = !quiet;
			break;
		case 'F':
			o_force = !o_force;
			break;
		case 'R':
			o_recurse = !o_recurse;
			break;
		case 'B':
			o_brief = !o_brief;
			break;
		case 'E':
			read_new_entry_text();
			break;
		default:
			g_string_sprintf(message,
					"!ERROR: Bad message '%c'\n", flag);
			send();
			break;
	}
}

/* If the parent has sent any flag toggles, read them */
static void check_flags(void)
{
	fd_set 	set;
	int	got;
	char	retval;
	struct timeval tv;

	FD_ZERO(&set);

	while (1)
	{
		FD_SET(from_parent, &set);
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		got = select(from_parent + 1, &set, NULL, NULL, &tv);

		if (got == -1)
			g_error("select() failed: %s\n", g_strerror(errno));
		else if (!got)
			return;

		got = read(from_parent, &retval, 1);
		if (got != 1)
			g_error("read() error: %s\n", g_strerror(errno));

		process_flag(retval);
	}
}

/* Read until the user sends a reply. If ignore_quiet is TRUE then
 * the user MUST click Yes or No, else treat quiet on as Yes.
 * If the user needs prompting then does send().
 */
static gboolean reply(int fd, gboolean ignore_quiet)
{
	ssize_t len;
	char retval;

	if (quiet && !ignore_quiet)
		return TRUE;

	send();

	while (1)
	{
		len = read(fd, &retval, 1);
		if (len != 1)
		{
			fprintf(stderr, "read() error: %s\n",
					g_strerror(errno));
			_exit(1);	/* Parent died? */
		}

		switch (retval)
		{
			case 'Y':
				g_string_sprintf(message, "' %s\n", _("Yes"));
				send();
				return TRUE;
			case 'N':
				g_string_sprintf(message, "' %s\n", _("No"));
				send();
				return FALSE;
			default:
				process_flag(retval);
				break;
		}
	}
}

static void destroy_action_window(GtkWidget *widget, gpointer data)
{
	GUIside	*gui_side = (GUIside *) data;

	if (gui_side->child)
	{
		kill(gui_side->child, SIGTERM);
		fclose(gui_side->to_child);
		close(gui_side->from_child);
		gdk_input_remove(gui_side->input_tag);
	}

	if (gui_side->next_dir)
	{
		gtk_timeout_remove(gui_side->next_timer);
		g_free(gui_side->next_dir);
	}

	if (gui_side->preview)
	{
		gtk_signal_disconnect_by_data(
				GTK_OBJECT(gui_side->preview->window),
				gui_side);
		gui_side->preview = NULL;
	}
	
	g_free(gui_side);
	
	if (--number_of_windows < 1)
		gtk_main_quit();
}

/* Create two pipes, fork() a child and return a pointer to a GUIside struct
 * (NULL on failure). The child calls func().
 *
 * If autoq then automatically selects 'Quiet'.
 */
static GUIside *start_action_with_options(gpointer data, ActionChild *func,
					  gboolean autoq,
					  int force, int brief, int recurse)
{
	int		filedes[4];	/* 0 and 2 are for reading */
	GUIside		*gui_side;
	int		child;
	GtkWidget	*vbox, *button, *scrollbar, *actions;
	struct sigaction act;

	if (pipe(filedes))
	{
		report_error("pipe: %s", g_strerror(errno));
		return NULL;
	}

	if (pipe(filedes + 2))
	{
		close(filedes[0]);
		close(filedes[1]);
		report_error("pipe: %s", g_strerror(errno));
		return NULL;
	}

	o_force = force;
	o_brief = brief;
	o_recurse = recurse;

	child = fork();
	switch (child)
	{
		case -1:
			report_error("fork: %s", g_strerror(errno));
			return NULL;
		case 0:
			/* We are the child */

			quiet = autoq;

			/* Reset the SIGCHLD handler */
			act.sa_handler = SIG_DFL;
			sigemptyset(&act.sa_mask);
			act.sa_flags = 0;
			sigaction(SIGCHLD, &act, NULL);

			message = g_string_new(NULL);
			close(filedes[0]);
			close(filedes[3]);
			to_parent = fdopen(filedes[1], "wb");
			from_parent = filedes[2];
			func(data);
			send_dir("");
			_exit(0);
	}

	/* We are the parent */
	close(filedes[1]);
	close(filedes[2]);
	gui_side = g_malloc(sizeof(GUIside));
	gui_side->from_child = filedes[0];
	gui_side->to_child = fdopen(filedes[3], "wb");
	gui_side->log = NULL;
	gui_side->child = child;
	gui_side->errors = 0;
	gui_side->show_info = FALSE;
	gui_side->preview = NULL;
	gui_side->results = NULL;
	gui_side->entry = NULL;
	gui_side->default_string = NULL;
	gui_side->entry_string_func = NULL;

	gui_side->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(gui_side->window), 2);
	gtk_window_set_default_size(GTK_WINDOW(gui_side->window), 450, 200);
	gtk_signal_connect(GTK_OBJECT(gui_side->window), "destroy",
			GTK_SIGNAL_FUNC(destroy_action_window), gui_side);

	gui_side->vbox = vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(gui_side->window), vbox);

	gui_side->dir = gtk_label_new(_("<dir>"));
	gtk_widget_set_usize(gui_side->dir, 8, -1);
	gui_side->next_dir = NULL;
	gtk_misc_set_alignment(GTK_MISC(gui_side->dir), 0.5, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), gui_side->dir, FALSE, TRUE, 0);

	gui_side->log_hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_side->log_hbox, TRUE, TRUE, 4);

	gui_side->log = gtk_text_new(NULL, NULL);
	gtk_widget_set_usize(gui_side->log, 400, 100);
	gtk_box_pack_start(GTK_BOX(gui_side->log_hbox),
				gui_side->log, TRUE, TRUE, 0);
	scrollbar = gtk_vscrollbar_new(GTK_TEXT(gui_side->log)->vadj);
	gtk_box_pack_start(GTK_BOX(gui_side->log_hbox),
				scrollbar, FALSE, TRUE, 0);

	actions = gtk_hbox_new(TRUE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), actions, FALSE, TRUE, 0);

	gui_side->quiet = button = gtk_button_new_with_label(_("Quiet"));
	gtk_box_pack_start(GTK_BOX(actions), button, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(quiet_clicked), gui_side);
	gui_side->yes = button = gtk_button_new_with_label(_("Yes"));
	gtk_object_set_data(GTK_OBJECT(button), "send-code", "Y");
	gtk_box_pack_start(GTK_BOX(actions), button, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(button_reply), gui_side);
	gui_side->no = button = gtk_button_new_with_label(_("No"));
	gtk_object_set_data(GTK_OBJECT(button), "send-code", "N");
	gtk_box_pack_start(GTK_BOX(actions), button, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(button_reply), gui_side);
	SENSITIVE_YESNO(gui_side, FALSE);

	button = gtk_button_new_with_label(_("Abort"));
	gtk_box_pack_start(GTK_BOX(actions), button, TRUE, TRUE, 0);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_destroy),
			GTK_OBJECT(gui_side->window));

	gui_side->input_tag = gdk_input_add(gui_side->from_child,
						GDK_INPUT_READ,
						message_from_child,
						gui_side);
	gui_side->quiet_flag = add_toggle(gui_side,
				_("Quiet - don't confirm every operation"),
				"Q", autoq);
	gtk_widget_set_sensitive(gui_side->quiet, !autoq);

	return gui_side;
}

static GUIside *start_action(gpointer data, ActionChild *func, gboolean autoq)
{
	return start_action_with_options(data, func, autoq,
					 option_get_int("action_force"),
					 option_get_int("action_brief"),
					 option_get_int("action_recurse"));
}

/* 			ACTIONS ON ONE ITEM 			*/

/* These may call themselves recursively, or ask questions, etc.
 * TRUE iff the directory containing dest_path needs to be rescanned.
 */

/* Updates the global size_tally, file_counter and dir_counter */
static gboolean do_usage(char *src_path, char *unused)
{
	struct 		stat info;

	check_flags();

	if (mc_lstat(src_path, &info))
	{
		g_string_sprintf(message, "'%s:\n", src_path);
		send();
		send_error();
		return FALSE;
	}

	if (S_ISREG(info.st_mode) || S_ISLNK(info.st_mode))
	{
	        file_counter++;
		size_tally += info.st_size;
	}
	else if (S_ISDIR(info.st_mode))
	{
	        dir_counter++;
		g_string_sprintf(message, _("?Count contents of %s?"),
				src_path);
		if (reply(from_parent, FALSE))
		{
			char *safe_path;
			safe_path = g_strdup(src_path);
			for_dir_contents(do_usage, safe_path, safe_path);
			g_free(safe_path);
		}
	}
	else
		file_counter++;

	return FALSE;
}

/* dest_path is the dir containing src_path */
static gboolean do_delete(char *src_path, char *dest_path)
{
	struct stat 	info;
	gboolean	write_prot;

	check_flags();

	if (mc_lstat(src_path, &info))
	{
		send_error();
		return FALSE;
	}

	write_prot = S_ISLNK(info.st_mode) ? FALSE
					   : access(src_path, W_OK) != 0;
	if (write_prot || !quiet)
	{
		g_string_sprintf(message, _("?Delete %s'%s'?"),
				write_prot ? _("WRITE-PROTECTED ") : " ",
				src_path);
		if (!reply(from_parent, write_prot && !o_force))
			return FALSE;
	}
	else if (!o_brief)
	{
		g_string_sprintf(message, _("'Deleting '%s'\n"), src_path);
		send();
	}

	if (S_ISDIR(info.st_mode))
	{
		char *safe_path;
		safe_path = g_strdup(src_path);
		for_dir_contents(do_delete, safe_path, safe_path);
		if (rmdir(safe_path))
		{
			g_free(safe_path);
			send_error();
			return FALSE;
		}
		g_string_sprintf(message, _("'Directory '%s' deleted\n"),
				safe_path);
		send();
		g_string_sprintf(message, "m%s", safe_path);
		send();
		g_free(safe_path);
	}
	else if (unlink(src_path))
	{
		send_error();
		return FALSE;
	}

	return TRUE;
}

/* path is the item to check. If is is a directory then we may recurse
 * (unless prune is used).
 */
static gboolean do_find(char *path, char *dummy)
{
	FindInfo	info;
	char		*slash;

	check_flags();

	if (!quiet)
	{
		g_string_sprintf(message, _("?Check '%s'?"), path);
		if (!reply(from_parent, FALSE))
			return FALSE;
	}

	for (;;)
	{
		if (new_entry_string)
		{
			if (find_condition)
				find_condition_free(find_condition);
			find_condition = find_compile(new_entry_string);
			g_free(new_entry_string);
			new_entry_string = NULL;
		}

		if (find_condition)
			break;

		g_string_assign(message, _("!Invalid find condition - "
						"change it and try again\n"));
		send();
		g_string_sprintf(message, _("?Check '%s'?"), path);
		if (!reply(from_parent, TRUE))
			return FALSE;
	}

	if (mc_lstat(path, &info.stats))
	{
		send_error();
		g_string_sprintf(message, _("'(while checking '%s')\n"), path);
		send();
		return FALSE;
	}

	info.fullpath = path;
	time(&info.now);	/* XXX: Not for each check! */

	slash = strrchr(path, '/');
	info.leaf = slash ? slash + 1 : path;
	info.prune = FALSE;
	if (find_test_condition(find_condition, &info))
	{
		g_string_sprintf(message, "=%s", path);
		send();
	}

	if (S_ISDIR(info.stats.st_mode) && !info.prune)
	{
		char *safe_path;
		safe_path = g_strdup(path);
		for_dir_contents(do_find, safe_path, safe_path);
		g_free(safe_path);
	}

	return FALSE;
}

/* Like mode_compile(), but ignores spaces and bracketed bits */
struct mode_change *nice_mode_compile(const char *mode_string,
				      unsigned int masked_ops)
{
	GString			*new;
	int			brackets = 0;
	struct mode_change	*retval = NULL;

	new = g_string_new(NULL);

	for (; *mode_string; mode_string++)
	{
		if (*mode_string == '(')
			brackets++;
		if (*mode_string == ')')
		{
			brackets--;
			if (brackets < 0)
				break;
			continue;
		}

		if (brackets == 0 && *mode_string != ' ')
			g_string_append_c(new, *mode_string);
	}

	if (brackets == 0)
		retval = mode_compile(new->str, masked_ops);
	g_string_free(new, TRUE);
	return retval;
}

static gboolean do_chmod(char *path, char *dummy)
{
	struct stat 	info;
	mode_t		new_mode;

	check_flags();

	if (mc_lstat(path, &info))
	{
		send_error();
		return FALSE;
	}
	if (S_ISLNK(info.st_mode))
		return FALSE;

	if (!quiet)
	{
		g_string_sprintf(message,
				_("?Change permissions of '%s'?"), path);
		if (!reply(from_parent, FALSE))
			return FALSE;
	}
	else if (!o_brief)
	{
		g_string_sprintf(message,
				_("'Changing permissions of '%s'\n"),
				path);
		send();
	}

	for (;;)
	{
		if (new_entry_string)
		{
			if (mode_change)
				mode_free(mode_change);
			mode_change = nice_mode_compile(new_entry_string,
							MODE_MASK_ALL);
			g_free(new_entry_string);
			new_entry_string = NULL;
		}

		if (mode_change)
			break;

		g_string_assign(message,
			_("!Invalid mode command - change it and try again\n"));
		send();
		g_string_sprintf(message,
				_("?Change permissions of '%s'?"), path);
		if (!reply(from_parent, TRUE))
			return FALSE;
	}

	if (mc_lstat(path, &info))
	{
		send_error();
		return FALSE;
	}
	if (S_ISLNK(info.st_mode))
		return FALSE;

	new_mode = mode_adjust(info.st_mode, mode_change);
	if (chmod(path, new_mode))
	{
		send_error();
		return FALSE;
	}

	if (o_recurse && S_ISDIR(info.st_mode))
	{
		guchar *safe_path;
		safe_path = g_strdup(path);
		for_dir_contents(do_chmod, safe_path, safe_path);
		g_free(safe_path);
	}

	return TRUE;
}

/* We want to copy 'object' into directory 'dir'. If 'action_leaf'
 * is set then that is the new leafname, otherwise the leafname stays
 * the same.
 */
static char *make_dest_path(char *object, char *dir)
{
	char *leaf;

	if (action_leaf)
		leaf = action_leaf;
	else
	{
		leaf = strrchr(object, '/');
		if (!leaf)
			leaf = object;		/* Error? */
		else
			leaf++;
	}

	return make_path(dir, leaf)->str;
}

/* If action_leaf is not NULL it specifies the new leaf name */
static gboolean do_copy2(char *path, char *dest)
{
	char		*dest_path;
	struct stat 	info;
	struct stat 	dest_info;
	gboolean	retval = TRUE;

	check_flags();

	dest_path = make_dest_path(path, dest);

	if (mc_lstat(path, &info))
	{
		send_error();
		return FALSE;
	}

	if (mc_lstat(dest_path, &dest_info) == 0)
	{
		int		err;
		gboolean	merge;

		merge = S_ISDIR(info.st_mode) && S_ISDIR(dest_info.st_mode);

		g_string_sprintf(message, _("?'%s' already exists - %s?"),
				dest_path,
				merge ? _("merge contents") : _("overwrite"));
		
		if (!reply(from_parent, TRUE))
			return FALSE;

		if (!merge)
		{
			if (S_ISDIR(dest_info.st_mode))
				err = rmdir(dest_path);
			else
				err = unlink(dest_path);

			if (err)
			{
				send_error();
				if (errno != ENOENT)
					return FALSE;
				g_string_sprintf(message,
						_("'Trying copy anyway...\n"));
				send();
			}
		}
	}
	else if (!quiet)
	{
		g_string_sprintf(message,
				_("?Copy %s as %s?"), path, dest_path);
		if (!reply(from_parent, FALSE))
			return FALSE;
	}
	else
	{
		g_string_sprintf(message, _("'Copying %s as %s\n"), path,
				dest_path);
		send();
	}

	if (S_ISDIR(info.st_mode))
	{
		mode_t	mode = info.st_mode;
		char *safe_path, *safe_dest;
		struct stat 	dest_info;
		gboolean	exists;

		/* (we will do the update ourselves now, rather than
		 * afterwards)
		 */
		retval = FALSE;
		
		safe_path = g_strdup(path);
		safe_dest = g_strdup(dest_path);

		exists = !mc_lstat(dest_path, &dest_info);

		if (exists && !S_ISDIR(dest_info.st_mode))
		{
			g_string_sprintf(message,
				_("!ERROR: Destination already exists, "
					"but is not a directory\n"));
		}
		else if (exists == FALSE && mkdir(dest_path, 0700 | mode))
			send_error();
		else
		{
			if (!exists)
			{
				/* (just been created then) */
				g_string_sprintf(message, "+%s", dest);
				send();
			}

			action_leaf = NULL;
			for_dir_contents(do_copy2, safe_path, safe_dest);
			/* Note: dest_path now invalid... */

			if (!exists)
			{
				struct utimbuf utb;

				/* We may have created the directory with
				 * more permissions than the source so that
				 * we could write to it... change it back now.
				 */
				if (chmod(safe_dest, mode))
					send_error();

				/* Also, try to preserve the timestamps */
				utb.actime = info.st_atime;
				utb.modtime = info.st_mtime;

				utime(safe_dest, &utb);
			}
		}

		g_free(safe_path);
		g_free(safe_dest);
	}
	else if (S_ISLNK(info.st_mode))
	{
		char	*target;

		/* Not all versions of cp(1) can make symlinks,
		 * so we special-case it.
		 */

		target = readlink_dup(path);
		if (target)
		{
			if (symlink(target, dest_path))
			{
				send_error();
				retval = FALSE;
			}

			g_free(target);
		}
		else
		{
			send_error();
			retval = FALSE;
		}
	}
	else
	{
		guchar	*error;

		error = copy_file(path, dest_path);

		if (error)
		{
			g_string_sprintf(message, _("!ERROR: %s\n"), error);
			send();
			retval = FALSE;
		}
	}

	return retval;
}

/* If action_leaf is not NULL it specifies the new leaf name */
static gboolean do_move2(char *path, char *dest)
{
	char		*dest_path;
	gboolean	retval = TRUE;
	char		*argv[] = {"mv", "-f", NULL, NULL, NULL};
	struct stat	info2;
	gboolean	is_dir;

	check_flags();

	dest_path = make_dest_path(path, dest);

	is_dir = mc_lstat(path, &info2) == 0 && S_ISDIR(info2.st_mode);

	if (access(dest_path, F_OK) == 0)
	{
		struct stat	info;
		int		err;

		g_string_sprintf(message,
				_("?'%s' already exists - overwrite?"),
				dest_path);
		if (!reply(from_parent, TRUE))
			return FALSE;

		if (mc_lstat(dest_path, &info))
		{
			send_error();
			return FALSE;
		}

		if (S_ISDIR(info.st_mode))
			err = rmdir(dest_path);
		else
			err = unlink(dest_path);

		if (err)
		{
			send_error();
			if (errno != ENOENT)
				return FALSE;
			g_string_sprintf(message,
					_("'Trying move anyway...\n"));
			send();
		}
	}
	else if (!quiet)
	{
		g_string_sprintf(message,
				_("?Move %s as %s?"), path, dest_path);
		if (!reply(from_parent, FALSE))
			return FALSE;
	}
	else
	{
		g_string_sprintf(message, _("'Moving %s as %s\n"), path,
				dest_path);
		send();
	}

	argv[2] = path;
	argv[3] = dest_path;

	if (fork_exec_wait(argv) == 0)
	{
		char	*leaf;

		leaf = strrchr(path, '/');
		if (!leaf)
			leaf = path;		/* Error? */
		else
			leaf++;

		g_string_sprintf(message, "+%s", path);
		g_string_truncate(message, leaf - path + 1);
		send();
		if (is_dir) {
			g_string_sprintf(message, "m%s", path);
			send();
		}
	}
	else
	{
		g_string_sprintf(message,
				_("!ERROR: Failed to move %s as %s\n"),
				path, dest_path);
		send();
		retval = FALSE;
	}

	return retval;
}

/* Copy path to dest.
 * Check that path not copied into itself.
 */
static gboolean do_copy(char *path, char *dest)
{
	if (is_sub_dir(make_dest_path(path, dest), path))
	{
		g_string_sprintf(message,
			_("!ERROR: Can't copy object into itself\n"));
		send();
		return FALSE;
	}
	return do_copy2(path, dest);
}

/* Move path to dest.
 * Check that path not moved into itself.
 */
static gboolean do_move(char *path, char *dest)
{
	if (is_sub_dir(make_dest_path(path, dest), path))
	{
		g_string_sprintf(message,
			_("!ERROR: Can't move/rename object into itself\n"));
		send();
		return FALSE;
	}
	return do_move2(path, dest);
}

static gboolean do_link(char *path, char *dest)
{
	char		*dest_path;

	check_flags();

	dest_path = make_dest_path(path, dest);

	if (quiet)
	{
		g_string_sprintf(message, _("'Linking %s as %s\n"), path,
				dest_path);
		send();
	}
	else
	{
		g_string_sprintf(message,
				_("?Link %s as %s?"), path, dest_path);
		if (!reply(from_parent, FALSE))
			return FALSE;
	}

	if (symlink(path, dest_path))
	{
		send_error();
		return FALSE;
	}

	return TRUE;
}

/* Mount/umount this item (depending on 'mount') */
static void do_mount(guchar *path, gboolean mount)
{
	char		*argv[3] = {NULL, NULL, NULL};

	check_flags();

	argv[0] = mount ? "mount" : "umount";
	argv[1] = path;

	if (quiet)
	{
		g_string_sprintf(message,
				mount ? _("'Mounting %s\n")
				      : _("'Unmounting %s\n"),
				path);
		send();
	}
	else
	{
		g_string_sprintf(message,
				mount ? _("?Mount %s?\n")
				      : _("?Unmount %s?\n"),
				path);
		if (!reply(from_parent, FALSE))
			return;
	}

	if (fork_exec_wait(argv) == 0)
	{
		g_string_sprintf(message, "M%s", path);
		send();
		if (mount && mount_open_dir)
		{
			g_string_sprintf(message, "o%s", path);
			send();
		}
	}
	else
	{
		g_string_sprintf(message, mount ?
			_("!ERROR: Mount failed\n") :
			_("!ERROR: Unmount failed\n"));
		send();
	}
}

/*			CHILD MAIN LOOPS			*/

/* After forking, the child calls one of these functions */

/* We use a double for total size in order to count beyond 4Gb */
static void usage_cb(gpointer data)
{
	GList *paths = (GList *) data;
	double	total_size = 0;
	gchar	*tmp;

	dir_counter = file_counter = 0;

	for (; paths; paths = paths->next)
	{
		guchar	*path = (guchar *) paths->data;

		send_dir(path);

		size_tally = 0;
		
		do_usage(path, NULL);

		g_string_sprintf(message, "'%s: %s\n",
				g_basename(path),
				format_double_size(size_tally));
		send();
		total_size += size_tally;
	}

	g_string_sprintf(message, _("'\nTotal: %s ("),
			 format_double_size(total_size));
	
	if (file_counter)
	{
		tmp = g_strdup_printf("%ld %s%s",
				file_counter,
				file_counter == 1 ? _("file") : _("files"),
				dir_counter ? ", " : ")\n");
		g_string_append(message, tmp);
		g_free(tmp);
	}

	if (file_counter == 0 && dir_counter == 0)
		g_string_append(message, _("no directories)\n"));
	else if (dir_counter)
	{
		tmp = g_strdup_printf("%ld %s)\n",
				dir_counter,
				dir_counter == 1 ? _("directory")
						 : _("directories"));
		g_string_append(message, tmp);
		g_free(tmp);
	}
	
	send();
}

#ifdef DO_MOUNT_POINTS
static void mount_cb(gpointer data)
{
	GList 		*paths = (GList *) data;
	gboolean	mount_points = FALSE;

	for (; paths; paths = paths->next)
	{
		guchar	*path = (guchar *) paths->data;

		if (mount_is_mounted(path))
			do_mount(path, FALSE);	/* Unmount */
		else if (g_hash_table_lookup(fstab_mounts, path))
			do_mount(path, TRUE);	/* Mount */
		else
			continue;

		mount_points = TRUE;
	}

	g_string_sprintf(message,
			 mount_points ? _("'\nDone\n")
			 	      : _("!No mount points selected!\n"));
	send();
}
#endif

static guchar *dirname(guchar *path)
{
	guchar	*slash;

	slash = strrchr(path, '/');
	g_return_val_if_fail(slash != NULL, g_strdup(path));

	if (slash != path)
		return g_strndup(path, slash - path);
	return g_strdup("/");
}

static void delete_cb(gpointer data)
{
	GList	*paths = (GList *) data;

	while (paths)
	{
		guchar	*path = (guchar *) paths->data;
		guchar	*dir;
		
		dir = dirname(path);
		send_dir(dir);

		if (do_delete(path, dir))
		{
			g_string_sprintf(message, "+%s", dir);
			send();
		}

		g_free(dir);
		paths = paths->next;
	}
	
	g_string_sprintf(message, _("'\nDone\n"));
	send();
}

static void find_cb(gpointer data)
{
	GList *all_paths = (GList *) data;
	GList *paths;

	while (1)
	{
		for (paths = all_paths; paths; paths = paths->next)
		{
			guchar	*path = (guchar *) paths->data;

			send_dir(path);

			do_find(path, NULL);
		}

		g_string_assign(message, _("?Another search?"));
		if (!reply(from_parent, TRUE))
			break;
		g_string_assign(message, "#");
		send();
	}
	
	g_string_sprintf(message, _("'\nDone\n"));
	send();
}

static void chmod_cb(gpointer data)
{
	GList *paths = (GList *) data;

	for (; paths; paths = paths->next)
	{
		guchar	*path = (guchar *) paths->data;
		struct stat info;

		send_dir(path);

		if (mc_stat(path, &info) != 0)
			send_error();
		else if (S_ISLNK(info.st_mode))
		{
			g_string_sprintf(message,
					_("!'%s' is a symbolic link\n"),
					g_basename(path));
			send();
		}
		else if (do_chmod(path, NULL))
		{
			g_string_sprintf(message, "+%s", path); /* XXX */
			send();
		}
	}
	
	g_string_sprintf(message, _("'\nDone\n"));
	send();
}

static void list_cb(gpointer data)
{
	GList	*paths = (GList *) data;

	while (paths)
	{
		send_dir((char *) paths->data);

		if (action_do_func((char *) paths->data, action_dest))
		{
			g_string_sprintf(message, "+%s", action_dest);
			send();
		}

		paths = paths->next;
	}

	g_string_sprintf(message, _("'\nDone\n"));
	send();
}

static GtkWidget *add_toggle(GUIside *gui_side, guchar *label, guchar *code,
			     gboolean active)
{
	GtkWidget	*check;

	check = gtk_check_button_new_with_label(label);
	gtk_object_set_data(GTK_OBJECT(check), "send-code", code);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), active);
	gtk_signal_connect(GTK_OBJECT(check), "clicked",
			GTK_SIGNAL_FUNC(button_reply), gui_side);
	gtk_box_pack_start(GTK_BOX(gui_side->vbox), check, FALSE, TRUE, 0);

	return check;
}


/*			EXTERNAL INTERFACE			*/

void action_find(GList *paths)
{
	GUIside		*gui_side;
	GtkWidget	*hbox, *label, *scroller;
	gchar		*titles[2];
	
	titles[0] = _("Name");
	titles[1] = _("Directory");

	if (!paths)
	{
		report_error(_("You need to select some items "
				"to search through"));
		return;
	}

	if (!last_find_string)
		last_find_string = g_strdup("'core'");

	new_entry_string = last_find_string;
	gui_side = start_action(paths, find_cb, FALSE);
	if (!gui_side)
		return;

	gui_side->show_info = TRUE;
	gui_side->entry_string_func = set_find_string_colour;

	scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
			GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_box_pack_start(GTK_BOX(gui_side->vbox), scroller, TRUE, TRUE, 4);
	gui_side->results = gtk_clist_new_with_titles(
			sizeof(titles) / sizeof(*titles), titles);
	gtk_clist_column_titles_passive(GTK_CLIST(gui_side->results));
	gtk_widget_set_usize(gui_side->results, 100, 100);
	gtk_clist_set_column_width(GTK_CLIST(gui_side->results), 0, 100);
	gtk_clist_set_selection_mode(GTK_CLIST(gui_side->results),
					GTK_SELECTION_SINGLE);
	gtk_container_add(GTK_CONTAINER(scroller), gui_side->results);
	gtk_box_set_child_packing(GTK_BOX(gui_side->vbox),
			  gui_side->log_hbox, FALSE, TRUE, 4, GTK_PACK_START);
	gtk_signal_connect(GTK_OBJECT(gui_side->results), "select_row",
			GTK_SIGNAL_FUNC(select_row_callback), gui_side);

	hbox = gtk_hbox_new(FALSE, 0);
	label = gtk_label_new(_("Expression:"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 4);
	gui_side->default_string = &last_find_string;
	gui_side->entry = gtk_entry_new();
	gtk_widget_set_style(gui_side->entry, fixed_style);
	gtk_entry_set_text(GTK_ENTRY(gui_side->entry), last_find_string);
	set_find_string_colour(gui_side->entry, last_find_string);
	gtk_editable_select_region(GTK_EDITABLE(gui_side->entry), 0, -1);
	gtk_widget_set_sensitive(gui_side->entry, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), gui_side->entry, TRUE, TRUE, 4);
	gtk_signal_connect(GTK_OBJECT(gui_side->entry), "changed",
			GTK_SIGNAL_FUNC(entry_changed), gui_side);
	gtk_box_pack_start(GTK_BOX(gui_side->vbox), hbox, FALSE, TRUE, 4);
	gtk_box_pack_start(GTK_BOX(hbox),
				new_help_button(show_condition_help, NULL),
				FALSE, TRUE, 4);

	gtk_window_set_title(GTK_WINDOW(gui_side->window), _("Find"));
	gtk_window_set_focus(GTK_WINDOW(gui_side->window), gui_side->entry);
	gtk_signal_connect(GTK_OBJECT(gui_side->entry), "activate",
			GTK_SIGNAL_FUNC(find_return_pressed), gui_side);
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

/* Count disk space used by selected items */
void action_usage(GList *paths)
{
	GUIside		*gui_side;

	if (!paths)
	{
		report_error(_("You need to select some items to count"));
		return;
	}

	gui_side = start_action(paths, usage_cb, TRUE);
	if (!gui_side)
		return;

	gui_side->show_info = TRUE;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), _("Disk Usage"));
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

/* Mount/unmount listed items (paths).
 * Free the list after this function returns.
 * If open_dir is TRUE and the dir is successfully mounted, open it.
 * quiet can be -1 for default.
 */
void action_mount(GList	*paths, gboolean open_dir, int quiet)
{
#ifdef DO_MOUNT_POINTS
	GUIside		*gui_side;

	if (quiet == -1)
	 	quiet = option_get_int("action_mount");

	mount_open_dir = open_dir;
	gui_side = start_action(paths, mount_cb, quiet);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window),
					_("Mount / Unmount"));
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
#else
	report_error(
		_("ROX-Filer does not yet support mount points on your "
			"system. Sorry."));
#endif /* DO_MOUNT_POINTS */
}

/* Deletes all selected items in the window */
void action_delete(GList *paths)
{
	GUIside		*gui_side;

	if (!remove_pinned_ok(paths))
		return;

	gui_side = start_action(paths, delete_cb,
					option_get_int("action_delete"));
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), _("Delete"));
	add_toggle(gui_side,
		_("Force - don't confirm deletion of non-writeable items"),
		"F", option_get_int("action_force"));
	add_toggle(gui_side,
		_("Brief - only log directories being deleted"),
		"B", option_get_int("action_brief"));

	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

/* Change the permissions of the selected items */
void action_chmod(GList *paths)
{
	GUIside		*gui_side;
	GtkWidget	*hbox, *label, *combo;
	static GList	*presets = NULL;

	if (!paths)
	{
		report_error(_("You need to select the items "
				"whose permissions you want to change"));
		return;
	}

	if (!presets)
	{
		presets = g_list_append(presets,
				_("a+x (Make executable/searchable)"));
		presets = g_list_append(presets,
				_("a-x (Make non-executable/non-searchable)"));
		presets = g_list_append(presets,
				_("u+rw (Give owner read+write)"));
		presets = g_list_append(presets,
				_("go-rwx (Private - owner access only)"));
		presets = g_list_append(presets,
				_("go=u-w (Public access, not write)"));
	}

	if (!last_chmod_string)
		last_chmod_string = g_strdup((guchar *) presets->data);
	new_entry_string = last_chmod_string;
	gui_side = start_action(paths, chmod_cb, FALSE);
	if (!gui_side)
		return;

	add_toggle(gui_side,
		_("Brief - don't list processed files"),
		"B", option_get_int("action_brief"));
	add_toggle(gui_side,
		_("Recurse - also change contents of subdirectories"),
		"R", option_get_int("action_recurse"));

	hbox = gtk_hbox_new(FALSE, 0);
	label = gtk_label_new(_("Command:"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 4);
	gui_side->default_string = &last_chmod_string;
	
	combo = gtk_combo_new();
	gtk_combo_disable_activate(GTK_COMBO(combo));
	gtk_combo_set_use_arrows_always(GTK_COMBO(combo), TRUE);
	gtk_combo_set_popdown_strings(GTK_COMBO(combo), presets);
	
	gui_side->entry = GTK_COMBO(combo)->entry;
	gtk_entry_set_text(GTK_ENTRY(gui_side->entry), last_chmod_string);
	gtk_editable_select_region(GTK_EDITABLE(gui_side->entry), 0, -1);
	gtk_widget_set_sensitive(gui_side->entry, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), combo, TRUE, TRUE, 4);
	gtk_signal_connect(GTK_OBJECT(gui_side->entry), "changed",
			GTK_SIGNAL_FUNC(entry_changed), gui_side);
	gtk_box_pack_start(GTK_BOX(gui_side->vbox), hbox, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox),
				new_help_button(show_chmod_help, NULL),
				FALSE, TRUE, 4);
	
	gtk_window_set_focus(GTK_WINDOW(gui_side->window), gui_side->entry);
	gtk_window_set_title(GTK_WINDOW(gui_side->window), _("Permissions"));
	gtk_signal_connect_object(GTK_OBJECT(gui_side->entry), "activate",
			GTK_SIGNAL_FUNC(gtk_button_clicked),
			GTK_OBJECT(gui_side->yes));

	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

/* If leaf is NULL then the copy has the same name as the original.
 * quiet can be -1 for default.
 */
void action_copy(GList *paths, char *dest, char *leaf, int quiet)
{
	GUIside		*gui_side;

	if (quiet == -1)
		quiet = option_get_int("action_copy");

	action_dest = dest;
	action_leaf = leaf;
	action_do_func = do_copy;
	gui_side = start_action(paths, list_cb, quiet);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), _("Copy"));
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

/* If leaf is NULL then the file is not renamed.
 * quiet can be -1 for default.
 */
void action_move(GList *paths, char *dest, char *leaf, int quiet)
{
	GUIside		*gui_side;

	if (quiet == -1)
		quiet = option_get_int("action_move");

	action_dest = dest;
	action_leaf = leaf;
	action_do_func = do_move;
	gui_side = start_action(paths, list_cb, quiet);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), _("Move"));
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

/* If leaf is NULL then the link will have the same name */
/* XXX: No quiet option here? */
void action_link(GList *paths, char *dest, char *leaf)
{
	GUIside		*gui_side;

	action_dest = dest;
	action_leaf = leaf;
	action_do_func = do_link;
	gui_side = start_action(paths, list_cb, option_get_int("action_link"));
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), _("Link"));
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

void action_init(void)
{
	option_add_int("action_copy", 1, NULL);
	option_add_int("action_move", 1, NULL);
	option_add_int("action_link", 1, NULL);
	option_add_int("action_delete", 0, NULL);
	option_add_int("action_mount", 1, NULL);
	option_add_int("action_force", o_force, NULL);
	option_add_int("action_brief", o_brief, NULL);
	option_add_int("action_recurse", o_recurse, NULL);
}

#define MAX_ASK 4

/* Check to see if any of the selected items (or their children) are
 * on the pinboard or panel. If so, ask for confirmation.
 *
 * TRUE if it's OK to lose them.
 */
static gboolean remove_pinned_ok(GList *paths)
{
	GList		*ask = NULL, *next;
	GString		*message;
	int		i, ask_n = 0;
	gboolean	retval;

	while (paths)
	{
		guchar	*path = (guchar *) paths->data;
		
		if (icons_require(path))
		{
			if (++ask_n > MAX_ASK)
				break;
			ask = g_list_append(ask, path);
		}

		paths = paths->next;
	}

	if (!ask)
		return TRUE;

	if (ask_n > MAX_ASK)
	{
		message = g_string_new(_("Deleting items such as "));
		ask_n--;
	}
	else if (ask_n == 1)
		message = g_string_new(_("Deleting the item "));
	else
		message = g_string_new(_("Deleting the items "));

	i = 0;
	for (next = ask; next; next = next->next)
	{
		guchar	*path = (guchar *) next->data;
		guchar	*leaf;

		leaf = strrchr(path, '/');
		if (leaf)
			leaf++;
		else
			leaf = path;
		
		g_string_append_c(message, '`');
		g_string_append(message, leaf);
		g_string_append_c(message, '\'');
		i++;
		if (i == ask_n - 1 && i > 0)
			g_string_append(message, _(" and "));
		else if (i < ask_n)
			g_string_append(message, ", ");
	}

	g_list_free(ask);

	if (ask_n == 1)
		message = g_string_append(message,
				_(" will affect some items on the pinboard "
				  "or panel - really delete it?"));
	else
	{
		if (ask_n > MAX_ASK)
			message = g_string_append_c(message, ',');
		message = g_string_append(message,
				_(" will affect some items on the pinboard "
					"or panel - really delete them?"));
	}
	
	retval = get_choice(PROJECT, message->str,
				2, _("Cancel"), _("OK")) == 1;

	g_string_free(message, TRUE);
	
	return retval;
}

void set_find_string_colour(GtkWidget *widget, guchar *string)
{
	static GtkStyle *error_style = NULL;
	FindCondition *cond;

	if (!error_style)
	{
		error_style = gtk_style_copy(fixed_style);
		error_style->fg[GTK_STATE_NORMAL] = red;
	}

	cond = find_compile(string);
	gtk_widget_set_style(widget, cond ? fixed_style : error_style);

	if (cond)
		find_condition_free(cond);
}
