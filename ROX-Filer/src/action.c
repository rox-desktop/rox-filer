/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 1999, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
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
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <dirent.h>
#include <sys/time.h>

#include "action.h"
#include "string.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "main.h"
#include "options.h"

/* Options bits */
static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();
static char *action_auto_quiet(char *data);

static OptionsSection options =
{
	"Action window options",
	create_options,
	update_options,
	set_options,
	save_options
};

static gboolean o_auto_copy = TRUE;
static gboolean o_auto_move = TRUE;
static gboolean o_auto_link = TRUE;
static gboolean o_auto_delete = FALSE;
static gboolean o_auto_mount = TRUE;

static GtkWidget *w_auto_copy = NULL;
static GtkWidget *w_auto_move = NULL;
static GtkWidget *w_auto_link = NULL;
static GtkWidget *w_auto_delete = NULL;
static GtkWidget *w_auto_mount = NULL;

static GdkColor red = {0, 0xffff, 0, 0};

/* Parent->Child messages are one character each:
 *
 * Q/Y/N 	Quiet/Yes/No button clicked
 * F		Force deletion of non-writeable items
 */

#define SENSITIVE_YESNO(gui_side, state)	\
	do {				\
		gtk_widget_set_sensitive((gui_side)->yes, state);	\
		gtk_widget_set_sensitive((gui_side)->no, state);	\
	} while (0)

#define ON(flag) ((flag) ? "on" : "off")

typedef struct _GUIside GUIside;
typedef void ActionChild(gpointer data);
typedef gboolean ForDirCB(char *path, char *dest_path);

struct _GUIside
{
	int 		from_child;	/* File descriptor */
	FILE		*to_child;
	int 		input_tag;	/* gdk_input_add() */
	GtkWidget 	*vbox, *log, *window, *dir;
	GtkWidget	*quiet, *yes, *no;
	int		child;		/* Process ID */
	int		errors;
	gboolean	show_info;	/* For Disk Usage */
	
	char		*next_dir;	/* NULL => no timer active */
	gint		next_timer;
};

/* These don't need to be in a structure because we fork() before
 * using them again.
 */
static int 	from_parent = 0;
static FILE	*to_parent = NULL;
static gboolean	quiet = FALSE;
static GString  *message = NULL;
static char     *action_dest = NULL;
static gboolean (*action_do_func)(char *source, char *dest);
static size_t	size_tally;	/* For Disk Usage */
static DirItem 	*mount_item;

static gboolean o_force = FALSE;

/* Static prototypes */
static gboolean send();
static gboolean send_error();
static gboolean send_dir(char *dir);
static gboolean read_exact(int source, char *buffer, ssize_t len);
static void do_mount(FilerWindow *filer_window, DirItem *item);
static void add_toggle(GUIside *gui_side, guchar *label, guchar *code);
static gboolean reply(int fd, gboolean ignore_quiet);

/*			OPTIONS 				*/

/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options()
{
	GtkWidget	*vbox, *hbox, *label;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	label = gtk_label_new("Auto-start (Quiet) these actions:");
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);

	hbox = gtk_hbox_new(TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	w_auto_copy = gtk_check_button_new_with_label("Copy");
	gtk_box_pack_start(GTK_BOX(hbox), w_auto_copy, FALSE, TRUE, 0);
	w_auto_move = gtk_check_button_new_with_label("Move");
	gtk_box_pack_start(GTK_BOX(hbox), w_auto_move, FALSE, TRUE, 0);
	w_auto_link = gtk_check_button_new_with_label("Link");
	gtk_box_pack_start(GTK_BOX(hbox), w_auto_link, FALSE, TRUE, 0);
	w_auto_delete = gtk_check_button_new_with_label("Delete");
	gtk_box_pack_start(GTK_BOX(hbox), w_auto_delete, FALSE, TRUE, 0);
	w_auto_mount = gtk_check_button_new_with_label("Mount");
	gtk_box_pack_start(GTK_BOX(hbox), w_auto_mount, FALSE, TRUE, 0);

	return vbox;
}

/* Reflect current state by changing the widgets in the options box */
static void update_options()
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_auto_copy),
			o_auto_copy);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_auto_move),
			o_auto_move);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_auto_link),
			o_auto_link);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_auto_delete),
			o_auto_delete);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w_auto_mount),
			o_auto_mount);
}

/* Set current values by reading the states of the widgets in the options box */
static void set_options()
{
	o_auto_copy = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(w_auto_copy));
	o_auto_move = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(w_auto_move));
	o_auto_link = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(w_auto_link));
	o_auto_delete = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(w_auto_delete));
	o_auto_mount = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(w_auto_mount));
}

static void save_options()
{
	guchar	str[] = "cmldt";

	if (o_auto_copy)
		str[0] = 'C';
	if (o_auto_move)
		str[1] = 'M';
	if (o_auto_link)
		str[2] = 'L';
	if (o_auto_delete)
		str[3] = 'D';
	if (o_auto_mount)
		str[4] = 'T';

	option_write("action_auto_quiet", str);
}

static char *action_auto_quiet(char *data)
{
	while (*data)
	{
		char	c = *data++;
		gboolean state;

		state = isupper(c);

		switch (tolower(c))
		{
			case 'c':
				o_auto_copy = state;
				break;
			case 'm':
				o_auto_move = state;
				break;
			case 'l':
				o_auto_link = state;
				break;
			case 'd':
				o_auto_delete = state;
				break;
			case 't':
				o_auto_mount = state;
				break;
			default:
				return "Unknown flag";
		}
	}
	
	return NULL;
}

/*			SUPPORT				*/

/* TRUE iff `sub' is (or would be) an object inside the directory `parent',
 * (or the two are the same directory)
 */
static gboolean is_sub_dir(char *sub, char *parent)
{
	int 		parent_len;
	guchar		*real_sub, *real_parent;
	gboolean	retval;

	real_sub = pathdup(sub);
	real_parent = pathdup(parent);

	parent_len = strlen(real_parent);
	if (strncmp(real_parent, real_sub, parent_len))
		retval = FALSE;
	else
	{
		/* real_sub is at least as long as real_parent and all
		 * characters upto real_parent's length match.
		 */

		retval = real_sub[parent_len] == 0
			|| real_sub[parent_len] == '/';
	}

	g_free(real_sub);
	g_free(real_parent);

	return retval;
}

static gboolean display_dir(gpointer data)
{
	GUIside	*gui_side = (GUIside *) data;

	gtk_label_set_text(GTK_LABEL(gui_side->dir), gui_side->next_dir + 1);
	g_free(gui_side->next_dir);
	gui_side->next_dir = NULL;
	
	return FALSE;
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
				SENSITIVE_YESNO(gui_side, TRUE);
			else if (*buffer == '+')
			{
				refresh_dirs(buffer + 1);
				g_free(buffer);
				return;
			}
			else if (*buffer == 'm')
			{
				filer_check_mounted(buffer + 1);
				g_free(buffer);
				return;
			}
			else if (*buffer == '/')
			{
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
		g_print("Child died in the middle of a message.\n");
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
		GString *report;
		report = g_string_new(NULL);
		g_string_sprintf(report, "There %s %d error%s.\n",
				gui_side->errors == 1 ? "was" : "were",
				gui_side->errors,
				gui_side->errors == 1 ? "" : "s");
		gtk_text_insert(GTK_TEXT(log), NULL, &red, NULL,
				report->str, report->len);

		g_string_free(report, TRUE);
	}
	else if (gui_side->show_info == FALSE)
		gtk_widget_destroy(gui_side->window);
}

/* Scans src_dir, updating dest_path whenever cb returns TRUE */
static void for_dir_contents(ForDirCB *cb, char *src_dir, char *dest_path)
{
	DIR	*d;
	struct dirent *ent;
	GSList  *list = NULL, *next;

	d = opendir(src_dir);
	if (!d)
	{
		send_error();
		return;
	}

	send_dir(src_dir);

	while ((ent = readdir(d)))
	{
		if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0'
			|| (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
			continue;
		list = g_slist_append(list, g_strdup(make_path(src_dir,
						       ent->d_name)->str));
	}
	closedir(d);

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
	g_slist_free(list);
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
static gboolean send()
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

static gboolean send_error()
{
	g_string_sprintf(message, "!ERROR: %s\n", g_strerror(errno));
	return send();
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

	if (*text == 'Y' || *text == 'N' || *text == 'Q')
		SENSITIVE_YESNO(gui_side, FALSE);
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
	gboolean asked = FALSE;

	while (ignore_quiet || !quiet)
	{
		if (!asked)
		{
			send();
			asked = TRUE;
		}

		len = read(fd, &retval, 1);
		if (len != 1)
		{
			fprintf(stderr, "read() error: %s\n",
					g_strerror(errno));
			_exit(1);	/* Parent died? */
		}

		switch (retval)
		{
			case 'Q':
				quiet = !quiet;
				if (ignore_quiet)
				{
					g_string_assign(message, "?");
					send();
				}
				break;
			case 'Y':
				g_string_assign(message, "' Yes\n");
				send();
				return TRUE;
			case 'N':
				g_string_assign(message, "' No\n");
				send();
				return FALSE;
			default:
				process_flag(retval);
				break;
		}
	}

	if (asked)
	{
		g_string_assign(message, "' Quiet\n");
		send();
	}
	return TRUE;
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
	g_free(gui_side);
	
	if (--number_of_windows < 1)
		gtk_main_quit();
}

/* Create two pipes, fork() a child and return a pointer to a GUIside struct
 * (NULL on failure). The child calls func().
 *
 * If autoq then automatically selects 'Quiet'.
 */
static GUIside *start_action(gpointer data, ActionChild *func, gboolean autoq)
{
	int		filedes[4];	/* 0 and 2 are for reading */
	GUIside		*gui_side;
	int		child;
	GtkWidget	*vbox, *button, *hbox, *scrollbar, *actions;
	struct sigaction act;

	if (pipe(filedes))
	{
		report_error("ROX-Filer", g_strerror(errno));
		return NULL;
	}

	if (pipe(filedes + 2))
	{
		close(filedes[0]);
		close(filedes[1]);
		report_error("ROX-Filer", g_strerror(errno));
		return NULL;
	}

	child = fork();
	switch (child)
	{
		case -1:
			report_error("ROX-Filer", g_strerror(errno));
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

	gui_side->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(gui_side->window), 2);
	gtk_window_set_default_size(GTK_WINDOW(gui_side->window), 450, 200);
	gtk_signal_connect(GTK_OBJECT(gui_side->window), "destroy",
			GTK_SIGNAL_FUNC(destroy_action_window), gui_side);

	gui_side->vbox = vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(gui_side->window), vbox);

	gui_side->dir = gtk_label_new("<dir>");
	gui_side->next_dir = NULL;
	gtk_misc_set_alignment(GTK_MISC(gui_side->dir), 0.5, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), gui_side->dir, FALSE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

	gui_side->log = gtk_text_new(NULL, NULL);
	gtk_widget_set_usize(gui_side->log, 400, 100);
	gtk_box_pack_start(GTK_BOX(hbox), gui_side->log, TRUE, TRUE, 0);
	scrollbar = gtk_vscrollbar_new(GTK_TEXT(gui_side->log)->vadj);
	gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, TRUE, 0);

	actions = gtk_hbox_new(TRUE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), actions, FALSE, TRUE, 0);

	gui_side->quiet = button = gtk_toggle_button_new_with_label("Quiet");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), autoq);
	gtk_object_set_data(GTK_OBJECT(button), "send-code", "Q");
	gtk_box_pack_start(GTK_BOX(actions), button, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			button_reply, gui_side);
	gui_side->yes = button = gtk_button_new_with_label("Yes");
	gtk_object_set_data(GTK_OBJECT(button), "send-code", "Y");
	gtk_box_pack_start(GTK_BOX(actions), button, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			button_reply, gui_side);
	gui_side->no = button = gtk_button_new_with_label("No");
	gtk_object_set_data(GTK_OBJECT(button), "send-code", "N");
	gtk_box_pack_start(GTK_BOX(actions), button, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			button_reply, gui_side);
	SENSITIVE_YESNO(gui_side, FALSE);

	button = gtk_button_new_with_label("Abort");
	gtk_box_pack_start(GTK_BOX(actions), button, TRUE, TRUE, 0);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			gtk_widget_destroy, GTK_OBJECT(gui_side->window));

	gui_side->input_tag = gdk_input_add(gui_side->from_child,
						GDK_INPUT_READ,
						message_from_child,
						gui_side);

	return gui_side;
}

/* 			ACTIONS ON ONE ITEM 			*/

/* These may call themselves recursively, or ask questions, etc.
 * TRUE iff the directory containing dest_path needs to be rescanned.
 */

/* dest_path is the dir containing src_path.
 * Updates the global size_tally.
 */
static gboolean do_usage(char *src_path, char *dest_path)
{
	struct 		stat info;

	check_flags();

	if (lstat(src_path, &info))
	{
		g_string_sprintf(message, "'%s:\n", src_path);
		send();
		send_error();
		return FALSE;
	}

	if (S_ISREG(info.st_mode) || S_ISLNK(info.st_mode))
		size_tally += info.st_size;
	else if (S_ISDIR(info.st_mode))
	{
		g_string_sprintf(message, "?Count contents of %s?",
				src_path);
		if (reply(from_parent, FALSE))
		{
			char *safe_path;
			safe_path = g_strdup(src_path);
			for_dir_contents(do_usage, safe_path, safe_path);
			g_free(safe_path);
		}
	}

	return FALSE;
}

/* dest_path is the dir containing src_path */
static gboolean do_delete(char *src_path, char *dest_path)
{
	struct 		stat info;
	gboolean	write_prot;

	check_flags();

	if (lstat(src_path, &info))
	{
		send_error();
		return FALSE;
	}

	write_prot = S_ISLNK(info.st_mode) ? FALSE
					   : access(src_path, W_OK) != 0;
	if (write_prot || !quiet)
	{
		g_string_sprintf(message, "?Delete %s'%s'?",
				write_prot ? "WRITE-PROTECTED " : " ",
				src_path);
		if (!reply(from_parent, write_prot && !o_force))
			return FALSE;
	}
	else
	{
		g_string_sprintf(message, "'Removing '%s'\n", src_path);
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
		g_string_assign(message, "'Directory deleted\n");
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

static gboolean do_copy2(char *path, char *dest)
{
	char		*dest_path;
	char		*leaf;
	struct stat 	info;
	struct stat 	dest_info;
	gboolean	retval = TRUE;

	check_flags();

	leaf = strrchr(path, '/');
	if (!leaf)
		leaf = path;		/* Error? */
	else
		leaf++;

	dest_path = make_path(dest, leaf)->str;

	if (lstat(path, &info))
	{
		send_error();
		return FALSE;
	}

	if (lstat(dest_path, &dest_info) == 0)
	{
		int		err;
		gboolean	merge;

		merge = S_ISDIR(info.st_mode) && S_ISDIR(dest_info.st_mode);

		g_string_sprintf(message, "?'%s' already exists - %s?",
				dest_path,
				merge ? "merge contents" : "overwrite");
		
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
						"'Trying copy anyway...\n");
				send();
			}
		}
	}
	else if (!quiet)
	{
		g_string_sprintf(message, "?Copy %s as %s?", path, dest_path);
		if (!reply(from_parent, FALSE))
			return FALSE;
	}
	else
	{
		g_string_sprintf(message, "'Copying %s as %s\n", path,
				dest_path);
		send();
	}

	if (S_ISDIR(info.st_mode))
	{
		char *safe_path, *safe_dest;
		struct stat 	dest_info;
		gboolean	exists;

		/* (we will do the update ourselves now, rather than
		 * afterwards)
		 */
		retval = FALSE;
		
		safe_path = g_strdup(path);
		safe_dest = g_strdup(dest_path);

		exists = !lstat(dest_path, &dest_info);

		if (exists && !S_ISDIR(dest_info.st_mode))
		{
			g_string_sprintf(message,
					"!ERROR: Destination already exists, "
					"but is not a directory\n");
		}
		else if (exists == FALSE && mkdir(dest_path, info.st_mode))
			send_error();
		else
		{
			if (!exists)
			{
				/* (just been created then) */
				g_string_sprintf(message, "+%s", dest);
				send();
			}
			
			for_dir_contents(do_copy2, safe_path, safe_dest);
			/* Note: dest_path now invalid... */
		}

		g_free(safe_path);
		g_free(safe_dest);
	}
	else if (S_ISLNK(info.st_mode))
	{
		char	target[MAXPATHLEN + 1];
		int	count;

		/* Not all versions of cp(1) can make symlinks,
		 * so we special-case it.
		 */

		count = readlink(path, target, sizeof(target) - 1);
		if (count < 0)
		{
			send_error();
			retval = FALSE;
		}
		else
		{
			target[count] = '\0';
			if (symlink(target, dest_path))
			{
				send_error();
				retval = FALSE;
			}
		}
	}
	else
	{
		char	*argv[] = {"cp", "-pRf", NULL, NULL, NULL};

		argv[2] = path;
		argv[3] = dest_path;

		if (fork_exec_wait(argv))
		{
			g_string_sprintf(message, "!ERROR: Copy failed\n");
			send();
			retval = FALSE;
		}
	}

	return retval;
}

/* Copy path to dest.
 * Check that path not copied into itself.
 */
static gboolean do_copy(char *path, char *dest)
{
	if (is_sub_dir(dest, path))
	{
		g_string_sprintf(message,
				"!ERROR: Can't copy directory into itself\n");
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
	char		*dest_path;
	char		*leaf;
	gboolean	retval = TRUE;
	char		*argv[] = {"mv", "-f", NULL, NULL, NULL};

	if (is_sub_dir(dest, path))
	{
		g_string_sprintf(message,
				"!ERROR: Can't move directory into itself\n");
		send();
		return FALSE;
	}

	check_flags();

	leaf = strrchr(path, '/');
	if (!leaf)
		leaf = path;		/* Error? */
	else
		leaf++;

	dest_path = make_path(dest, leaf)->str;

	if (access(dest_path, F_OK) == 0)
	{
		struct stat	info;
		int		err;

		g_string_sprintf(message, "?'%s' already exists - overwrite?",
				dest_path);
		if (!reply(from_parent, TRUE))
			return FALSE;

		if (lstat(dest_path, &info))
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
					"'Trying move anyway...\n");
			send();
		}
	}
	else if (!quiet)
	{
		g_string_sprintf(message, "?Move %s as %s?", path, dest_path);
		if (!reply(from_parent, FALSE))
			return FALSE;
	}
	else
	{
		g_string_sprintf(message, "'Moving %s as %s\n", path,
				dest_path);
		send();
	}

	argv[2] = path;
	argv[3] = dest;

	if (fork_exec_wait(argv) == 0)
	{
		g_string_sprintf(message, "+%s", path);
		g_string_truncate(message, leaf - path);
		send();
	}
	else
	{
		g_string_sprintf(message, "!ERROR: Failed to move %s as %s\n",
				path, dest_path);
		retval = FALSE;
	}
	send();

	return retval;
}

static gboolean do_link(char *path, char *dest)
{
	char		*dest_path;
	char		*leaf;

	check_flags();

	leaf = strrchr(path, '/');
	if (!leaf)
		leaf = path;		/* Error? */
	else
		leaf++;

	dest_path = make_path(dest, leaf)->str;

	if (quiet)
	{
		g_string_sprintf(message, "'Linking %s as %s\n", path,
				dest_path);
		send();
	}
	else
	{
		g_string_sprintf(message, "?Link %s as %s?", path, dest_path);
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

/* Mount/umount this item */
static void do_mount(FilerWindow *filer_window, DirItem *item)
{
	char		*argv[3] = {NULL, NULL, NULL};

	check_flags();

	argv[0] = item->flags & ITEM_FLAG_MOUNTED ? "umount" : "mount";
	argv[1] = make_path(filer_window->path, item->leafname)->str;

	if (quiet)
	{
		g_string_sprintf(message, "'%sing %s\n", argv[0], argv[1]);
		send();
	}
	else
	{
		g_string_sprintf(message, "?%s %s?", argv[0], argv[1]);
		if (!reply(from_parent, FALSE))
			return;
	}

	if (fork_exec_wait(argv) == 0)
	{
		g_string_sprintf(message, "+%s", filer_window->path);
		send();
		g_string_sprintf(message, "m%s", argv[1]);
		send();
	}
	else
	{
		g_string_sprintf(message, "!ERROR: %s failed\n", argv[0]);
		send();
	}
}

/*			CHILD MAIN LOOPS			*/

/* After forking, the child calls one of these functions */

static void usage_cb(gpointer data)
{
	FilerWindow *filer_window = (FilerWindow *) data;
	Collection *collection = filer_window->collection;
	DirItem   *item;
	int	left = collection->number_selected;
	int	i = -1;
	off_t	total_size = 0;

	send_dir(filer_window->path);

	while (left > 0)
	{
		i++;
		if (!collection->items[i].selected)
			continue;
		item = (DirItem *) collection->items[i].data;
		size_tally = 0;
		do_usage(make_path(filer_window->path,
					item->leafname)->str,
					filer_window->path);
		g_string_sprintf(message, "'%s: %s\n",
				item->leafname,
				format_size((unsigned long) size_tally));
		send();
		total_size += size_tally;
		left--;
	}
	
	g_string_sprintf(message, "'\nTotal: %s\n",
			format_size((unsigned long) total_size));
	send();
}

#ifdef DO_MOUNT_POINTS
static void mount_cb(gpointer data)
{
	FilerWindow 	*filer_window = (FilerWindow *) data;
	Collection	*collection = filer_window->collection;
	DirItem   	*item;
	int		i;
	gboolean	mount_points = FALSE;

	send_dir(filer_window->path);

	if (mount_item)
		do_mount(filer_window, mount_item);
	else
	{
		for (i = 0; i < collection->number_of_items; i++)
		{
			if (!collection->items[i].selected)
				continue;
			item = (DirItem *) collection->items[i].data;
			if (!(item->flags & ITEM_FLAG_MOUNT_POINT))
				continue;
			mount_points = TRUE;

			do_mount(filer_window, item);
		}
	
		if (!mount_points)
		{
			g_string_sprintf(message,
					"!No mount points selected!\n");
			send();
		}
	}

	g_string_sprintf(message, "'\nDone\n");
	send();
}
#endif

static void delete_cb(gpointer data)
{
	FilerWindow *filer_window = (FilerWindow *) data;
	Collection *collection = filer_window->collection;
	DirItem   *item;
	int	left = collection->number_selected;
	int	i = -1;

	send_dir(filer_window->path);

	while (left > 0)
	{
		i++;
		if (!collection->items[i].selected)
			continue;
		item = (DirItem *) collection->items[i].data;
		if (do_delete(make_path(filer_window->path,
						item->leafname)->str,
						filer_window->path))
		{
			g_string_sprintf(message, "+%s", filer_window->path);
			send();
		}
		left--;
	}
	
	g_string_sprintf(message, "'\nDone\n");
	send();
}

static void list_cb(gpointer data)
{
	GSList	*paths = (GSList *) data;

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

	g_string_sprintf(message, "'\nDone\n");
	send();
}

static void add_toggle(GUIside *gui_side, guchar *label, guchar *code)
{
	GtkWidget	*check;

	check = gtk_check_button_new_with_label(label);
	gtk_object_set_data(GTK_OBJECT(check), "send-code", code);
	gtk_signal_connect(GTK_OBJECT(check), "clicked",
			button_reply, gui_side);
	gtk_box_pack_start(GTK_BOX(gui_side->vbox), check, FALSE, TRUE, 0);
}


/*			EXTERNAL INTERFACE			*/

/* Count disk space used by selected items */
void action_usage(FilerWindow *filer_window)
{
	GUIside		*gui_side;
	Collection 	*collection;

	collection = filer_window->collection;

	if (collection->number_selected < 1)
	{
		report_error("ROX-Filer", "You need to select some items "
				"to count");
		return;
	}

	gui_side = start_action(filer_window, usage_cb, TRUE);
	if (!gui_side)
		return;

	gui_side->show_info = TRUE;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), "Disk Usage");
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

/* Mount/unmount 'item', or all selected mount points if NULL. */
void action_mount(FilerWindow *filer_window, DirItem *item)
{
#ifdef DO_MOUNT_POINTS
	GUIside		*gui_side;
	Collection 	*collection;

	collection = filer_window->collection;

	if (item == NULL && collection->number_selected < 1)
	{
		report_error("ROX-Filer", "You need to select some items "
				"to mount or unmount");
		return;
	}

	mount_item = item;
	gui_side = start_action(filer_window, mount_cb, o_auto_mount);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), "Mount / Unmount");
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
#else
	report_error("ROX-Filer",
			"ROX-Filer does not yet support mount points on your "
			"system. Sorry.");
#endif /* DO_MOUNT_POINTS */
}

/* Deletes all selected items in the window */
void action_delete(FilerWindow *filer_window)
{
	GUIside		*gui_side;
	Collection 	*collection;

	collection = filer_window->collection;

	if (collection->number_selected < 1)
	{
		report_error("ROX-Filer", "You need to select some items "
				"to delete");
		return;
	}

	gui_side = start_action(filer_window, delete_cb, o_auto_delete);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), "Delete");
	add_toggle(gui_side,
		"Don't confirm deletion of non-writeable items", "F");
	
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

void action_copy(GSList *paths, char *dest)
{
	GUIside		*gui_side;

	action_dest = dest;
	action_do_func = do_copy;
	gui_side = start_action(paths, list_cb, o_auto_copy);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), "Copy");
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

void action_move(GSList *paths, char *dest)
{
	GUIside		*gui_side;

	action_dest = dest;
	action_do_func = do_move;
	gui_side = start_action(paths, list_cb, o_auto_move);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), "Move");
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

void action_link(GSList *paths, char *dest)
{
	GUIside		*gui_side;

	action_dest = dest;
	action_do_func = do_link;
	gui_side = start_action(paths, list_cb, o_auto_link);
	if (!gui_side)
		return;

	gtk_window_set_title(GTK_WINDOW(gui_side->window), "Link");
	number_of_windows++;
	gtk_widget_show_all(gui_side->window);
}

void action_init(void)
{
	options_sections = g_slist_prepend(options_sections, &options);
	option_register("action_auto_quiet", action_auto_quiet);
}
