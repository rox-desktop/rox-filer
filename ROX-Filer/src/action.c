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

/* action.c - code for handling the filer action windows.
 * These routines generally fork() and talk to us via pipes.
 */

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
#include "abox.h"
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
 * W		neWer toggled
 */

typedef struct _GUIside GUIside;
typedef void ActionChild(gpointer data);
typedef void ForDirCB(const char *path, const char *dest_path);

struct _GUIside
{
	ABox		*abox;		/* The action window widget */

	int 		from_child;	/* File descriptor */
	FILE		*to_child;
	int 		input_tag;	/* gdk_input_add() */
	pid_t		child;		/* Process ID */
	int		errors;		/* Number of errors so far */
	gboolean	show_info;	/* For Disk Usage */

	guchar		**default_string; /* Changed when the entry changes */
	void		(*entry_string_func)(GtkWidget *widget,
					     const guchar *string);
};

/* These don't need to be in a structure because we fork() before
 * using them again.
 */
static gboolean mount_open_dir = FALSE;
static int 	from_parent = 0;
static FILE	*to_parent = NULL;
static gboolean	quiet = FALSE;
static GString  *message = NULL;
static const char *action_dest = NULL;
static const char *action_leaf = NULL;
static void (*action_do_func)(const char *source, const char *dest);
static double	size_tally;		/* For Disk Usage */
static unsigned long dir_counter;	/* For Disk Usage */
static unsigned long file_counter;	/* For Disk Usage */

static struct mode_change *mode_change = NULL;	/* For Permissions */
static FindCondition *find_condition = NULL;	/* For Find */

/* Only used by child */
static gboolean o_force = FALSE;
static gboolean o_brief = FALSE;
static gboolean o_recurse = FALSE;
static gboolean o_newer = FALSE;

static Option o_action_copy, o_action_move, o_action_link;
static Option o_action_delete, o_action_mount;
static Option o_action_force, o_action_brief, o_action_recurse;
static Option o_action_newer;

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
static gboolean send_dir(const char *dir);
static gboolean read_exact(int source, char *buffer, ssize_t len);
static void do_mount(guchar *path, gboolean mount);
static gboolean reply(int fd, gboolean ignore_quiet);
static gboolean remove_pinned_ok(GList *paths);

/*			SUPPORT				*/


/* This is called whenever the user edits the entry box (if any) - send the
 * new string.
 */
static void entry_changed(GtkEditable *entry, GUIside *gui_side)
{
	guchar	*text;

	g_return_if_fail(gui_side->default_string != NULL);

	text = gtk_editable_get_chars(entry, 0, -1);

	if (gui_side->entry_string_func)
		gui_side->entry_string_func(GTK_WIDGET(entry), text);

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
		
		help = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_type_hint(GTK_WINDOW(help),
				    GDK_WINDOW_TYPE_HINT_DIALOG);
		gtk_container_set_border_width(GTK_CONTAINER(help), 10);
		gtk_window_set_title(GTK_WINDOW(help),
				_("Find expression reference"));

		vbox = gtk_vbox_new(FALSE, 0);
		gtk_container_add(GTK_CONTAINER(help), vbox);

		frame = gtk_frame_new(_("Quick Start"));
		text = gtk_label_new(
_("Just put the name of the file you're looking for in single quotes:\n"
"'index.html' (to find a file called 'index.html')"));
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
		gtk_widget_set_name(text, "fixed-style");
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
		button = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
		gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
		g_signal_connect_swapped(button, "clicked",
			G_CALLBACK(gtk_widget_hide), help);

		g_signal_connect_swapped(help, "delete_event",
			G_CALLBACK(gtk_widget_hide), help);
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
		
		help = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_type_hint(GTK_WINDOW(help),
				    GDK_WINDOW_TYPE_HINT_DIALOG);
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
		button = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
		gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
		g_signal_connect_swapped(button, "clicked",
			G_CALLBACK(gtk_widget_hide), help);

		g_signal_connect_swapped(help, "delete_event",
			G_CALLBACK(gtk_widget_hide), help);
	}

	if (GTK_WIDGET_VISIBLE(help))
		gtk_widget_hide(help);
	gtk_widget_show_all(help);
}

static void process_message(GUIside *gui_side, const gchar *buffer)
{
	ABox *abox = gui_side->abox;

	if (*buffer == '?')
		abox_ask(abox, buffer + 1);
	else if (*buffer == 's')
		dir_check_this(buffer + 1);	/* Update this item */
	else if (*buffer == '=')
		abox_add_filename(abox, buffer + 1);
	else if (*buffer == '#')
		abox_clear_results(abox);
	else if (*buffer == 'm' || *buffer == 'M')
	{
		/* Mount / major changes to this path */
		if (*buffer == 'M')
			mount_update(TRUE);
		filer_check_mounted(buffer + 1);
	}
	else if (*buffer == '/')
		abox_set_current_object(abox, buffer + 1);
	else if (*buffer == 'o')
		filer_opendir(buffer + 1, NULL);
	else if (*buffer == '!')
	{
		gui_side->errors++;
		abox_log(abox, buffer + 1, "error");
	}
	else
		abox_log(abox, buffer + 1, NULL);
}

/* Called when the child sends us a message */
static void message_from_child(gpointer 	  data,
			        gint     	  source, 
			        GdkInputCondition condition)
{
	char buf[5];
	GUIside	*gui_side = (GUIside *) data;
	ABox	*abox = gui_side->abox;
	GtkTextBuffer *text_buffer;

	text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(abox->log));

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
			process_message(gui_side, buffer);
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
	g_source_remove(gui_side->input_tag);
	abox_cancel_ask(gui_side->abox);

	if (gui_side->errors)
	{
		guchar *report;

		if (gui_side->errors == 1)
			report = g_strdup(_("There was one error.\n"));
		else
			report = g_strdup_printf(_("There were %d errors.\n"),
							gui_side->errors);

		gtk_text_buffer_insert_at_cursor(text_buffer, report, -1);

		g_free(report);
	}
	else if (gui_side->show_info == FALSE)
		gtk_widget_destroy(GTK_WIDGET(gui_side->abox));
}

/* Scans src_dir, calling cb(item, dest_path) for each item */
static void for_dir_contents(ForDirCB *cb,
			     const char *src_dir,
			     const char *dest_path)
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
		cb((char *) next->data, dest_path);

		g_free(next->data);
		next = next->next;
	}
	g_list_free(list);
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
static gboolean send_dir(const char *dir)
{
	g_string_sprintf(message, "/%s", dir);
	return send();
}

static gboolean send_error(void)
{
	g_string_sprintf(message, "!%s: %s\n", _("ERROR"), g_strerror(errno));
	return send();
}

static void response(GtkDialog *dialog, gint response, GUIside *gui_side)
{
	gchar code;

	if (!gui_side->to_child)
		return;

	if (response == GTK_RESPONSE_YES)
		code = 'Y';
	else if (response == GTK_RESPONSE_NO)
		code = 'N';
	else
		return;

	fputc(code, gui_side->to_child);
	fflush(gui_side->to_child);
}

static void flag_toggled(ABox *abox, gint flag, GUIside *gui_side)
{
	if (!gui_side->to_child)
		return;

	fputc(flag, gui_side->to_child);
	fflush(gui_side->to_child);
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
	        case 'W':
		        o_newer = !o_newer;
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
		g_source_remove(gui_side->input_tag);
	}

	g_free(gui_side);
	
	one_less_window();
}

/* Create two pipes, fork() a child and return a pointer to a GUIside struct
 * (NULL on failure). The child calls func().
 */
static GUIside *start_action(GtkWidget *abox, ActionChild *func, gpointer data,
			      int force, int brief, int recurse, int newer)
{
	gboolean	autoq;
	int		filedes[4];	/* 0 and 2 are for reading */
	GUIside		*gui_side;
	pid_t		child;
	struct sigaction act;

	if (pipe(filedes))
	{
		report_error("pipe: %s", g_strerror(errno));
		gtk_widget_destroy(abox);
		return NULL;
	}

	if (pipe(filedes + 2))
	{
		close(filedes[0]);
		close(filedes[1]);
		report_error("pipe: %s", g_strerror(errno));
		gtk_widget_destroy(abox);
		return NULL;
	}

	autoq = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(ABOX(abox)->quiet));

	o_force = force;
	o_brief = brief;
	o_recurse = recurse;
	o_newer = newer;

	child = fork();
	switch (child)
	{
		case -1:
			report_error("fork: %s", g_strerror(errno));
			gtk_widget_destroy(abox);
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
	gui_side->child = child;
	gui_side->errors = 0;
	gui_side->show_info = FALSE;
	gui_side->default_string = NULL;
	gui_side->entry_string_func = NULL;

	gui_side->abox = ABOX(abox);
	g_signal_connect(abox, "destroy",
			G_CALLBACK(destroy_action_window), gui_side);

	g_signal_connect(abox, "response", G_CALLBACK(response), gui_side);
	g_signal_connect(abox, "flag_toggled",
			 G_CALLBACK(flag_toggled), gui_side);

	gui_side->input_tag = gtk_input_add_full(gui_side->from_child,
						GDK_INPUT_READ,
						message_from_child,
						NULL, gui_side, NULL);

	return gui_side;
}

/* 			ACTIONS ON ONE ITEM 			*/

/* These may call themselves recursively, or ask questions, etc */

/* Updates the global size_tally, file_counter and dir_counter */
static void do_usage(const char *src_path, const char *unused)
{
	struct 		stat info;

	check_flags();

	if (mc_lstat(src_path, &info))
	{
		g_string_sprintf(message, "'%s:\n", src_path);
		send();
		send_error();
	}
	else if (S_ISREG(info.st_mode) || S_ISLNK(info.st_mode))
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
}

/* dest_path is the dir containing src_path */
static void do_delete(const char *src_path, const char *unused)
{
	struct stat 	info;
	gboolean	write_prot;
	char		*safe_path;

	check_flags();

	if (mc_lstat(src_path, &info))
	{
		send_error();
		return;
	}

	write_prot = S_ISLNK(info.st_mode) ? FALSE
					   : access(src_path, W_OK) != 0;
	if (write_prot || !quiet)
	{
		g_string_sprintf(message, _("?Delete %s'%s'?"),
				write_prot ? _("WRITE-PROTECTED ") : "",
				src_path);
		if (!reply(from_parent, write_prot && !o_force))
			return;
	}
	else if (!o_brief)
	{
		g_string_sprintf(message, _("'Deleting '%s'\n"), src_path);
		send();
	}

	safe_path = g_strdup(src_path);

	if (S_ISDIR(info.st_mode))
	{
		for_dir_contents(do_delete, safe_path, safe_path);
		if (rmdir(safe_path))
		{
			g_free(safe_path);
			send_error();
			return;
		}
		g_string_sprintf(message, _("'Directory '%s' deleted\n"),
				safe_path);
		send();
		g_string_sprintf(message, "m%s", safe_path);
		send();
	}
	else if (unlink(src_path))
		send_error();
	else
	{
		g_string_sprintf(message, "s%s", safe_path);
		send();
		if (strcmp(g_basename(safe_path), ".DirIcon") == 0)
		{
			gchar *dir;
			dir = g_dirname(safe_path);
			g_string_sprintf(message, "s%s", dir);
			g_free(dir);
			send();
		}
	}

	g_free(safe_path);
}

/* path is the item to check. If is is a directory then we may recurse
 * (unless prune is used).
 */
static void do_find(const char *path, const char *unused)
{
	FindInfo	info;

	check_flags();

	if (!quiet)
	{
		g_string_sprintf(message, _("?Check '%s'?"), path);
		if (!reply(from_parent, FALSE))
			return;
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
			return;
	}

	if (mc_lstat(path, &info.stats))
	{
		send_error();
		g_string_sprintf(message, _("'(while checking '%s')\n"), path);
		send();
		return;
	}

	info.fullpath = path;
	time(&info.now);	/* XXX: Not for each check! */

	info.leaf = g_basename(path);
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

static void do_chmod(const char *path, const char *unused)
{
	struct stat 	info;
	mode_t		new_mode;

	check_flags();

	if (mc_lstat(path, &info))
	{
		send_error();
		return;
	}
	if (S_ISLNK(info.st_mode))
		return;

	if (!quiet)
	{
		g_string_sprintf(message,
				_("?Change permissions of '%s'?"), path);
		if (!reply(from_parent, FALSE))
			return;
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
			return;
	}

	if (mc_lstat(path, &info))
	{
		send_error();
		return;
	}
	if (S_ISLNK(info.st_mode))
		return;

	new_mode = mode_adjust(info.st_mode, mode_change);
	if (chmod(path, new_mode))
	{
		send_error();
		return;
	}

	g_string_sprintf(message, "s%s", path);
	send();

	if (S_ISDIR(info.st_mode))
	{
		g_string_sprintf(message, "m%s", path);
		send();

		if (o_recurse)
		{
			guchar *safe_path;
			safe_path = g_strdup(path);
			for_dir_contents(do_chmod, safe_path, safe_path);
			g_free(safe_path);
		}
	}
}

/* We want to copy 'object' into directory 'dir'. If 'action_leaf'
 * is set then that is the new leafname, otherwise the leafname stays
 * the same.
 */
static char *make_dest_path(const char *object, const char *dir)
{
	const char *leaf;

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
static void do_copy2(const char *path, const char *dest)
{
	char		*dest_path;
	struct stat 	info;
	struct stat 	dest_info;

	check_flags();

	dest_path = make_dest_path(path, dest);

	if (mc_lstat(path, &info))
	{
		send_error();
		return;
	}

	if (mc_lstat(dest_path, &dest_info) == 0)
	{
		int		err;
		gboolean	merge;

		merge = S_ISDIR(info.st_mode) && S_ISDIR(dest_info.st_mode);

		if (!merge && o_newer && info.st_mtime > dest_info.st_mtime)
		{
		  /* Newer; keep going */
		}
		else
		{
			g_string_sprintf(message,
				_("?'%s' already exists - %s?"), dest_path,
				merge ? _("merge contents") : _("overwrite"));

			if (!reply(from_parent, TRUE))
				return;
		}

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
					return;
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
			return;
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

		safe_path = g_strdup(path);
		safe_dest = g_strdup(dest_path);

		exists = !mc_lstat(dest_path, &dest_info);

		if (exists && !S_ISDIR(dest_info.st_mode))
		{
			g_string_sprintf(message,
				_("!ERROR: Destination already exists, "
					"but is not a directory\n"));
			send();
		}
		else if (exists == FALSE && mkdir(dest_path, 0700 | mode))
			send_error();
		else
		{
			if (!exists)
			{
				/* (just been created then) */
				g_string_sprintf(message, "s%s", dest_path);
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
				{
					/* Some filesystems don't support
					 * SetGID and SetUID bits. Ignore
					 * these errors.
					 */
					if (errno != EPERM)
						send_error();
				}

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
				send_error();
			else
			{
				g_string_sprintf(message, "s%s", dest_path);
				send();
			}

			g_free(target);
		}
		else
			send_error();
	}
	else
	{
		guchar	*error;

		error = copy_file(path, dest_path);

		if (error)
		{
			g_string_sprintf(message, _("!%s\nFailed to copy '%s'"),
					error, path);
			g_free(error);
			send();
		}
		else
		{
			g_string_sprintf(message, "s%s", dest_path);
			send();
		}
	}
}

/* If action_leaf is not NULL it specifies the new leaf name */
static void do_move2(const char *path, const char *dest)
{
	char		*dest_path;
	const char	*argv[] = {"mv", "-f", NULL, NULL, NULL};
	struct stat	info2;
	gboolean	is_dir;
	char            *err;

	check_flags();

	dest_path = make_dest_path(path, dest);

	is_dir = mc_lstat(path, &info2) == 0 && S_ISDIR(info2.st_mode);

	if (access(dest_path, F_OK) == 0)
	{
		struct stat	info;
		int		err;

		if (mc_lstat(dest_path, &info))
		{
			send_error();
			return;
		}

		if (!is_dir && o_newer && info2.st_mtime > info.st_mtime)
		{
			/* Newer; keep going */
		}
		else
		{
			g_string_sprintf(message,
					_("?'%s' already exists - overwrite?"),
					dest_path);
			if (!reply(from_parent, TRUE))
				return;
		}

		if (S_ISDIR(info.st_mode))
			err = rmdir(dest_path);
		else
			err = unlink(dest_path);

		if (err)
		{
			send_error();
			if (errno != ENOENT)
				return;
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
			return;
	}
	else
	{
		g_string_sprintf(message, _("'Moving %s as %s\n"), path,
				dest_path);
		send();
	}

	argv[2] = path;
	argv[3] = dest_path;

	err = fork_exec_wait(argv);
	if (err)
	{
		g_string_sprintf(message,
				_("!%s\nFailed to move %s as %s\n"),
				err, path, dest_path);
		send();

		g_free(err);
	}
	else
	{
		g_string_sprintf(message, "s%s", dest_path);
		send();

		if (is_dir)
			g_string_sprintf(message, "m%s", path);
		else
			g_string_sprintf(message, "s%s", path);

		send();
	}
}

/* Copy path to dest.
 * Check that path not copied into itself.
 */
static void do_copy(const char *path, const char *dest)
{
	if (is_sub_dir(make_dest_path(path, dest), path))
	{
		g_string_sprintf(message,
			_("!ERROR: Can't copy object into itself\n"));
		send();
	}
	else
		do_copy2(path, dest);
}

/* Move path to dest.
 * Check that path not moved into itself.
 */
static void do_move(const char *path, const char *dest)
{
	if (is_sub_dir(make_dest_path(path, dest), path))
	{
		g_string_sprintf(message,
			_("!ERROR: Can't move/rename object into itself\n"));
		send();
	}
	else
		do_move2(path, dest);
}

static void do_link(const char *path, const char *dest)
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
			return;
	}

	if (symlink(path, dest_path))
		send_error();
	else
	{
		g_string_sprintf(message, "s%s", dest_path);
		send();
	}
}

/* Mount/umount this item (depending on 'mount') */
static void do_mount(guchar *path, gboolean mount)
{
	const char *argv[3] = {NULL, NULL, NULL};
	char *err;

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
				mount ? _("?Mount %s?")
				      : _("?Unmount %s?"),
				path);
		if (!reply(from_parent, FALSE))
			return;
	}

	err = fork_exec_wait(argv);
	if (err)
	{
		g_string_sprintf(message, mount ?
			_("!%s\nMount failed\n") :
			_("!%s\nUnmount failed\n"), err);
		send();
		g_free(err);
	}
	else
	{
		g_string_sprintf(message, "M%s", path);
		send();
		if (mount && mount_open_dir)
		{
			g_string_sprintf(message, "o%s", path);
			send();
		}
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

		if (mount_is_mounted(path, NULL, NULL))
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

/* (use g_dirname() instead?) */
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

		do_delete(path, dir);

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
		else
			do_chmod(path, NULL);
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

		action_do_func((char *) paths->data, action_dest);

		paths = paths->next;
	}

	g_string_sprintf(message, _("'\nDone\n"));
	send();
}

/*			EXTERNAL INTERFACE			*/

void action_find(GList *paths)
{
	GUIside		*gui_side;
	GtkWidget	*abox;

	if (!paths)
	{
		report_error(_("You need to select some items "
				"to search through"));
		return;
	}

	if (!last_find_string)
		last_find_string = g_strdup("'core'");

	new_entry_string = last_find_string;

	abox = abox_new(_("Find"), FALSE);
	gui_side = start_action(abox, find_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_newer.int_value);
	if (!gui_side)
		return;

	abox_add_results(ABOX(abox));

	gui_side->default_string = &last_find_string;
	abox_add_entry(ABOX(abox), last_find_string,
				new_help_button(show_condition_help, NULL));
	g_signal_connect(ABOX(abox)->entry, "changed",
			G_CALLBACK(entry_changed), gui_side);
	set_find_string_colour(ABOX(abox)->entry, last_find_string);

	gui_side->show_info = TRUE;
	gui_side->entry_string_func = set_find_string_colour;

	number_of_windows++;
	gtk_widget_show_all(abox);
}

/* Count disk space used by selected items */
void action_usage(GList *paths)
{
	GUIside *gui_side;
	GtkWidget *abox;

	if (!paths)
	{
		report_error(_("You need to select some items to count"));
		return;
	}

	abox = abox_new(_("Disk Usage"), TRUE);
	
	gui_side = start_action(abox, usage_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_newer.int_value);
	if (!gui_side)
		return;

	gui_side->show_info = TRUE;

	number_of_windows++;

	gtk_widget_show(abox);
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
	GtkWidget	*abox;

	if (quiet == -1)
	 	quiet = o_action_mount.int_value;

	mount_open_dir = open_dir;

	abox = abox_new(_("Mount / Unmount"), quiet);
	gui_side = start_action(abox, mount_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_newer.int_value);
	if (!gui_side)
		return;

	number_of_windows++;
	gtk_widget_show(abox);
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
	GtkWidget	*abox;

	if (!remove_pinned_ok(paths))
		return;

	abox = abox_new(_("Delete"), o_action_delete.int_value);
	gui_side = start_action(abox, delete_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_newer.int_value);
	if (!gui_side)
		return;

	abox_add_flag(ABOX(abox),
		_("Force"), _("Don't confirm deletion of non-writeable items"),
		'F', o_action_force.int_value);
	abox_add_flag(ABOX(abox),
		_("Brief"), _("Only log directories being deleted"),
		'B', o_action_brief.int_value);

	number_of_windows++;
	gtk_widget_show(abox);
}

/* Change the permissions of the selected items */
void action_chmod(GList *paths)
{
	GtkWidget	*abox;
	GUIside		*gui_side;
	static GList	*presets = NULL;

	if (!paths)
	{
		report_error(_("You need to select the items "
				"whose permissions you want to change"));
		return;
	}

	if (!presets)
	{
		presets = g_list_append(presets, (gchar *)
				_("a+x (Make executable/searchable)"));
		presets = g_list_append(presets, (gchar *)
				_("a-x (Make non-executable/non-searchable)"));
		presets = g_list_append(presets, (gchar *)
				_("u+rw (Give owner read+write)"));
		presets = g_list_append(presets, (gchar *)
				_("go-rwx (Private - owner access only)"));
		presets = g_list_append(presets, (gchar *)
				_("go=u-w (Public access, not write)"));
	}

	if (!last_chmod_string)
		last_chmod_string = g_strdup((guchar *) presets->data);
	new_entry_string = last_chmod_string;

	abox = abox_new(_("Permissions"), FALSE);
	gui_side = start_action(abox, chmod_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_newer.int_value);
	if (!gui_side)
		return;

	abox_add_flag(ABOX(abox),
		_("Brief"), _("Don't list processed files"),
		'B', o_action_brief.int_value);
	abox_add_flag(ABOX(abox),
		_("Recurse"), _("Also change contents of subdirectories"),
		'R', o_action_recurse.int_value);

	gui_side->default_string = &last_chmod_string;
	abox_add_combo(ABOX(abox), presets, last_chmod_string,
				new_help_button(show_chmod_help, NULL));

	g_signal_connect(ABOX(abox)->entry, "changed",
			G_CALLBACK(entry_changed), gui_side);
#if 0
	g_signal_connect_swapped(gui_side->entry, "activate",
			G_CALLBACK(gtk_button_clicked),
			gui_side->yes);
#endif

	number_of_windows++;
	gtk_widget_show(abox);
}

/* If leaf is NULL then the copy has the same name as the original.
 * quiet can be -1 for default.
 */
void action_copy(GList *paths, const char *dest, const char *leaf, int quiet)
{
	GUIside		*gui_side;
	GtkWidget	*abox;

	if (quiet == -1)
		quiet = o_action_copy.int_value;

	action_dest = dest;
	action_leaf = leaf;
	action_do_func = do_copy;

	abox = abox_new(_("Copy"), quiet);
	gui_side = start_action(abox, list_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_newer.int_value);
	if (!gui_side)
		return;

	abox_add_flag(ABOX(abox),
		   _("Newer"),
		   _("Only over-write if source is newer than destination."),
		   'W', o_action_newer.int_value);

	number_of_windows++;
	gtk_widget_show(abox);
}

/* If leaf is NULL then the file is not renamed.
 * quiet can be -1 for default.
 */
void action_move(GList *paths, const char *dest, const char *leaf, int quiet)
{
	GUIside		*gui_side;
	GtkWidget	*abox;

	if (quiet == -1)
		quiet = o_action_move.int_value;

	action_dest = dest;
	action_leaf = leaf;
	action_do_func = do_move;

	abox = abox_new(_("Move"), quiet);
	gui_side = start_action(abox, list_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_newer.int_value);
	if (!gui_side)
		return;

	abox_add_flag(ABOX(abox),
		   _("Newer"),
		   _("Only over-write if source is newer than destination."),
		   'W', o_action_newer.int_value);
	number_of_windows++;
	gtk_widget_show(abox);
}

/* If leaf is NULL then the link will have the same name */
/* XXX: No quiet option here? */
void action_link(GList *paths, const char *dest, const char *leaf)
{
	GtkWidget	*abox;
	GUIside		*gui_side;

	action_dest = dest;
	action_leaf = leaf;
	action_do_func = do_link;

	abox = abox_new(_("Link"), o_action_link.int_value);
	gui_side = start_action(abox, list_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_newer.int_value);
	if (!gui_side)
		return;

	number_of_windows++;
	gtk_widget_show(abox);
}

void action_init(void)
{
	option_add_int(&o_action_copy, "action_copy", 1);
	option_add_int(&o_action_move, "action_move", 1);
	option_add_int(&o_action_link, "action_link", 1);
	option_add_int(&o_action_delete, "action_delete", 0);
	option_add_int(&o_action_mount, "action_mount", 1);
	option_add_int(&o_action_force, "action_force", FALSE);
	option_add_int(&o_action_brief, "action_brief", FALSE);
	option_add_int(&o_action_recurse, "action_recurse", FALSE);
	option_add_int(&o_action_newer, "action_newer", FALSE);
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
				2, _("Cancel"), _("Delete")) == 1;

	g_string_free(message, TRUE);
	
	return retval;
}

void set_find_string_colour(GtkWidget *widget, const guchar *string)
{
	FindCondition *cond;

	cond = find_compile(string);
	entry_set_error(widget, !cond);

	if (cond)
		find_condition_free(cond);
}
