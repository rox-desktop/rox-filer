/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* menu.c - code for handling the popup menu */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "apps.h"
#include "action.h"
#include "filer.h"
#include "type.h"
#include "support.h"
#include "gui_support.h"
#include "options.h"
#include "choices.h"
#include "savebox.h"

#define C_ "<control>"

#define MENU_MARGIN 32

GtkAccelGroup	*filer_keys;
GtkAccelGroup	*panel_keys;

/* Options */
static GtkWidget *xterm_here_entry;
static char *xterm_here_value;

/* Static prototypes */
static void position_menu(GtkMenu *menu, gint *x, gint *y, gpointer data);
static void menu_closed(GtkWidget *widget);
static void items_sensitive(GtkWidget *menu, int from, int n, gboolean state);
static char *load_xterm_here(char *data);

static void not_yet(gpointer data, guint action, GtkWidget *widget);

static void hidden(gpointer data, guint action, GtkWidget *widget);
static void refresh(gpointer data, guint action, GtkWidget *widget);

static void copy_item(gpointer data, guint action, GtkWidget *widget);
static void rename_item(gpointer data, guint action, GtkWidget *widget);
static void link_item(gpointer data, guint action, GtkWidget *widget);
static void help(gpointer data, guint action, GtkWidget *widget);
static void show_file_info(gpointer data, guint action, GtkWidget *widget);
static void mount(gpointer data, guint action, GtkWidget *widget);
static void delete(gpointer data, guint action, GtkWidget *widget);

static void select_all(gpointer data, guint action, GtkWidget *widget);
static void clear_selection(gpointer data, guint action, GtkWidget *widget);
static void show_options(gpointer data, guint action, GtkWidget *widget);
static void new_directory(gpointer data, guint action, GtkWidget *widget);
static void xterm_here(gpointer data, guint action, GtkWidget *widget);
static void open_parent(gpointer data, guint action, GtkWidget *widget);

static void open_as_dir(gpointer data, guint action, GtkWidget *widget);
static void close_panel(gpointer data, guint action, GtkWidget *widget);

static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();

static OptionsSection options =
{
	"Menu options",
	create_options,
	update_options,
	set_options,
	save_options
};


static GtkWidget	*filer_menu;		/* The popup filer menu */
static GtkWidget	*filer_file_item;	/* The File '' label */
static GtkWidget	*filer_file_menu;	/* The File '' menu */
static GtkWidget	*panel_menu;		/* The popup panel menu */
static GtkWidget	*panel_file_item;	/* The File '' label */
static GtkWidget	*panel_file_menu;	/* The File '' menu */

static gint		screen_width, screen_height;

static GtkItemFactoryEntry filer_menu_def[] = {
{"/Display",			NULL,	NULL, 0, "<Branch>"},
{"/Display/Large Icons",   	NULL,  	not_yet, 0, "<RadioItem>"},
{"/Display/Small Icons",   	NULL,  	not_yet, 0, "/Display/Large Icons"},
{"/Display/Full Info",		NULL,  	not_yet, 0, "/Display/Large Icons"},
{"/Display/Separator",		NULL,  	not_yet, 0, "<Separator>"},
{"/Display/Sort by Name",	NULL,  	not_yet, 0, "<RadioItem>"},
{"/Display/Sort by Type",	NULL,  	not_yet, 0, "/Display/Sort by Name"},
{"/Display/Sort by Date",	NULL,  	not_yet, 0, "/Display/Sort by Name"},
{"/Display/Sort by Size",	NULL,  	not_yet, 0, "/Display/Sort by Name"},
{"/Display/Sort by Owner",	NULL,  	not_yet, 0, "/Display/Sort by Name"},
{"/Display/Separator",		NULL,  	NULL, 0, "<Separator>"},
{"/Display/Show Hidden",   	C_"H", 	hidden, 0, "<ToggleItem>"},
{"/Display/Refresh",	   	C_"L", 	refresh, 0,	NULL},
{"/File",			NULL,  	NULL, 0, "<Branch>"},
{"/File/Copy...",		NULL,  	copy_item, 0, NULL},
{"/File/Rename...",		NULL,  	rename_item, 0, NULL},
{"/File/Link...",		NULL,  	link_item, 0, NULL},
{"/File/Help",		    	"F1",  	help, 0, NULL},
{"/File/Info",			"I",  	show_file_info, 0, NULL},
{"/File/Separator",		NULL,   NULL, 0, "<Separator>"},
{"/File/Mount",	    		"M",  	mount, 0,	NULL},
{"/File/Delete",	    	C_"X", 	delete, 0,	NULL},
{"/File/Disk Usage",	    	C_"U", 	not_yet, 0, NULL},
{"/File/Permissions",		NULL,   not_yet, 0, NULL},
{"/File/Touch",    		NULL,   not_yet, 0, NULL},
{"/File/Find",			NULL,   not_yet, 0, NULL},
{"/Select All",	    		C_"A",  select_all, 0, NULL},
{"/Clear Selection",	    	C_"Z",  clear_selection, 0, NULL},
{"/Options...",			NULL,   show_options, 0, NULL},
{"/New directory",		NULL,   new_directory, 0, NULL},
{"/Xterm here",			NULL,  	xterm_here, 0, NULL},
{"/Open parent",		NULL,   open_parent, 0, NULL},
};

static GtkItemFactoryEntry panel_menu_def[] = {
{"/Display",			NULL,	NULL, 0, "<Branch>"},
{"/Display/Large Icons",	NULL,   not_yet, 0, "<RadioItem>"},
{"/Display/Small Icons",	NULL,   not_yet, 0, "/Display/Large Icons"},
{"/Display/Full Info",		NULL,   not_yet, 0, "/Display/Large Icons"},
{"/Display/Separator",		NULL,   NULL, 0, "<Separator>"},
{"/Display/Sort by Name",	NULL,   not_yet, 0, "<RadioItem>"},
{"/Display/Sort by Type",	NULL,   not_yet, 0, "/Display/Sort by Name"},
{"/Display/Sort by Date",	NULL,   not_yet, 0, "/Display/Sort by Name"},
{"/Display/Sort by Size",	NULL,   not_yet, 0, "/Display/Sort by Name"},
{"/Display/Sort by Owner",	NULL,  	not_yet, 0, "/Display/Sort by Name"},
{"/Display/Separator",		NULL,   NULL, 0, "<Separator>"},
{"/Display/Show Hidden",   	NULL, 	hidden, 0, "<ToggleItem>"},
{"/Display/Refresh",	    	NULL, 	refresh, 0,	NULL},
{"/File",			NULL,	NULL, 	0, "<Branch>"},
{"/File/Help",		    	NULL,  	help, 0, NULL},
{"/File/Info",			NULL,  	show_file_info, 0, NULL},
{"/File/Delete",		NULL,	delete,	0, NULL},
{"/Open as directory",		NULL, 	open_as_dir, 0, NULL},
{"/Close panel",		NULL, 	close_panel, 0, NULL},
};

void menu_init()
{
	GtkItemFactory  	*item_factory;
	char			*menurc;
	GList			*items;

	/* This call starts returning strange values after a while, so get
	 * the result here during init.
	 */
	gdk_window_get_size(GDK_ROOT_PARENT(), &screen_width, &screen_height);

	filer_keys = gtk_accel_group_new();
	item_factory = gtk_item_factory_new(GTK_TYPE_MENU,
					    "<filer>",
					    filer_keys);
	gtk_item_factory_create_items(item_factory,
			sizeof(filer_menu_def) / sizeof(*filer_menu_def),
			filer_menu_def,
			NULL);
	filer_menu = gtk_item_factory_get_widget(item_factory, "<filer>");
	filer_file_menu = gtk_item_factory_get_widget(item_factory,
			"<filer>/File");
	items = gtk_container_children(GTK_CONTAINER(filer_menu));
	filer_file_item = GTK_BIN(g_list_nth(items, 1)->data)->child;
	g_list_free(items);

	panel_keys = gtk_accel_group_new();
	item_factory = gtk_item_factory_new(GTK_TYPE_MENU,
					    "<panel>",
					    panel_keys);
	gtk_item_factory_create_items(item_factory,
			sizeof(panel_menu_def) / sizeof(*panel_menu_def),
			panel_menu_def,
			NULL);
	panel_menu = gtk_item_factory_get_widget(item_factory, "<panel>");
	panel_file_menu = gtk_item_factory_get_widget(item_factory, 
			"<panel>/File");
	items = gtk_container_children(GTK_CONTAINER(panel_menu));
	panel_file_item = GTK_BIN(g_list_nth(items, 1)->data)->child;
	g_list_free(items);

	menurc = choices_find_path_load("menus");
	if (menurc)
		gtk_item_factory_parse_rc(menurc);

	gtk_signal_connect(GTK_OBJECT(panel_menu), "unmap_event",
			GTK_SIGNAL_FUNC(menu_closed), NULL);
	gtk_signal_connect(GTK_OBJECT(filer_menu), "unmap_event",
			GTK_SIGNAL_FUNC(menu_closed), NULL);

	gtk_accel_group_lock(panel_keys);

	options_sections = g_slist_prepend(options_sections, &options);
	xterm_here_value = g_strdup("xterm");
	option_register("xterm_here", load_xterm_here);
}
 
/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options()
{
	GtkWidget	*table, *label;

	table = gtk_table_new(2, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 4);

	label = gtk_label_new("To set the keyboard short-cuts you simply open "
			"the menu over a filer window, move the pointer over "
			"the item you want to use and press a key. The key "
			"will appear next to the menu item and you can just "
			"press that key without opening the menu in future. "
			"To save the current menu short-cuts for next time, "
			"click the Save button at the bottom of this window.");
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 2, 0, 1);

	label = gtk_label_new("'Xterm here' program:");
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 1, 2);
	xterm_here_entry = gtk_entry_new();
	gtk_table_attach_defaults(GTK_TABLE(table), xterm_here_entry,
			1, 2, 1, 2);

	return table;
}

static char *load_xterm_here(char *data)
{
	g_free(xterm_here_value);
	xterm_here_value = g_strdup(data);
	return NULL;
}

static void update_options()
{
	gtk_entry_set_text(GTK_ENTRY(xterm_here_entry), xterm_here_value);
}

static void set_options()
{
	g_free(xterm_here_value);
	xterm_here_value = g_strdup(gtk_entry_get_text(
				GTK_ENTRY(xterm_here_entry)));
}

static void save_options()
{
	char	*menurc;

	menurc = choices_find_path_save("menus");
	if (menurc)
		gtk_item_factory_dump_rc(menurc, NULL, TRUE);

	option_write("xterm_here", xterm_here_value);
}


static void items_sensitive(GtkWidget *menu, int from, int n, gboolean state)
{
	GList	*items, *item;

	items = gtk_container_children(GTK_CONTAINER(menu));

	item = g_list_nth(items, from);
	while (item && n--)
	{
		gtk_widget_set_sensitive(GTK_BIN(item->data)->child, state);
		item = item->next;
	}

	g_list_free(items);
}

static void position_menu(GtkMenu *menu, gint *x, gint *y, gpointer data)
{
	int		*pos = (int *) data;
	GtkRequisition 	requisition;

	gtk_widget_size_request(GTK_WIDGET(menu), &requisition);

	if (pos[0] == -1)
		*x = screen_width - MENU_MARGIN - requisition.width;
	else if (pos[0] == -2)
		*x = MENU_MARGIN;
	else
		*x = pos[0] - (requisition.width >> 2);
		
	if (pos[1] == -1)
		*y = screen_height - MENU_MARGIN - requisition.height;
	else if (pos[1] == -2)
		*y = MENU_MARGIN;
	else
		*y = pos[1] - (requisition.height >> 2);

	*x = CLAMP(*x, 0, screen_width - requisition.width);
	*y = CLAMP(*y, 0, screen_height - requisition.height);
}

void show_filer_menu(FilerWindow *filer_window, GdkEventButton *event,
		     int item)
{
	GString		*buffer;
	GtkWidget	*file_label, *file_menu;
	FileItem	*file_item;
	int		pos[] = {event->x_root, event->y_root};

	window_with_focus = filer_window;

	if (filer_window->panel)
	{
		switch (filer_window->panel_side)
		{
			case TOP: 	pos[1] = -2; break;
			case BOTTOM: 	pos[1] = -1; break;
			case LEFT: 	pos[0] = -2; break;
			case RIGHT: 	pos[0] = -1; break;
		}
	}

	if (filer_window->panel)
	{
		collection_clear_selection(filer_window->collection);
		panel_set_timeout(NULL, 0);
	}

	if (filer_window->collection->number_selected == 0 && item >= 0)
	{
		collection_select_item(filer_window->collection, item);
		filer_window->temp_item_selected = TRUE;
	}
	else
		filer_window->temp_item_selected = FALSE;

	if (filer_window->panel)
	{
		file_label = panel_file_item;
		file_menu = panel_file_menu;
	}
	else
	{
		file_label = filer_file_item;
		file_menu = filer_file_menu;
	}

	buffer = g_string_new(NULL);
	switch (filer_window->collection->number_selected)
	{
		case 0:
			g_string_assign(buffer, "<nothing selected>");
			items_sensitive(file_menu, 0, 5, FALSE);
			items_sensitive(file_menu, 6, -1, FALSE);
			gtk_widget_set_sensitive(file_label, FALSE);
			break;
		case 1:
			items_sensitive(file_menu, 0, 5, TRUE);
			items_sensitive(file_menu, 6, -1, TRUE);
			gtk_widget_set_sensitive(file_label, TRUE);
			file_item = selected_item(filer_window->collection);
			g_string_sprintf(buffer, "%s '%s'",
					basetype_name(file_item),
					file_item->leafname);
			break;
		default:
			items_sensitive(file_menu, 0, 5, FALSE);
			items_sensitive(file_menu, 6, -1, TRUE);
			gtk_widget_set_sensitive(file_label, TRUE);
			g_string_sprintf(buffer, "%d items",
				filer_window->collection->number_selected);
			break;
	}

	gtk_label_set_text(GTK_LABEL(file_label), buffer->str);

	g_string_free(buffer, TRUE);

	gtk_menu_popup(filer_window->panel ? GTK_MENU(panel_menu)
				           : GTK_MENU(filer_menu),
			NULL, NULL, position_menu,
			(gpointer) pos, event->button, event->time);
}

static void menu_closed(GtkWidget *widget)
{
	if (window_with_focus == NULL)
		return;			/* Close panel item chosen? */

	if (window_with_focus->temp_item_selected)
	{
		collection_clear_selection(window_with_focus->collection);
		window_with_focus->temp_item_selected = FALSE;
	}
}

/* Actions */

/* Fake action to warn when a menu item does nothing */
static void not_yet(gpointer data, guint action, GtkWidget *widget)
{
	delayed_error("ROX-Filer", "Sorry, that feature isn't implemented yet");
}

static void hidden(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->show_hidden = !window_with_focus->show_hidden;
	update_dir(window_with_focus);
}

static void refresh(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	update_dir(window_with_focus);
}

static void delete(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	action_delete(window_with_focus);
}

static gboolean copy_cb(char *initial, char *path)
{
	char	*new_dir, *slash;
	int	len;
	GString	*command;
	gboolean retval = TRUE;

	slash = strrchr(path, '/');
	if (!slash)
	{
		report_error("ROX-Filer", "Missing '/' in new pathname");
		return FALSE;
	}

	if (access(path, F_OK) == 0)
	{
		report_error("ROX-Filer",
				"An item with this name already exists");
		return FALSE;
	}

	len = slash - path;
	new_dir = g_malloc(len + 1);
	memcpy(new_dir, path, len);
	new_dir[len] = '\0';

	command = g_string_new(NULL);
	g_string_sprintf(command, "cp -a %s %s", initial, path);

	if (system(command->str))
	{
		g_string_append(command, " failed!");
		report_error("ROX-Filer", command->str);
		retval = FALSE;
	}

	g_string_free(command, TRUE);

	refresh_dirs(new_dir);
	return retval;
}

static void copy_item(gpointer data, guint action, GtkWidget *widget)
{
	Collection *collection;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected != 1)
		report_error("ROX-Filer", "You must select a single "
				"item to copy");
	else
	{
		FileItem *item = selected_item(collection);

		savebox_show(window_with_focus, "Copy",
				window_with_focus->path, item->leafname,
				item->image, copy_cb);
	}
}

static gboolean rename_cb(char *initial, char *path)
{
	if (rename(initial, path))
	{
		report_error("ROX-Filer: rename()", g_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

static void rename_item(gpointer data, guint action, GtkWidget *widget)
{
	Collection *collection;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected != 1)
		report_error("ROX-Filer", "You must select a single "
				"item to rename");
	else
	{
		FileItem *item = selected_item(collection);

		savebox_show(window_with_focus, "Rename",
				window_with_focus->path, item->leafname,
				item->image, rename_cb);
	}
}

static gboolean link_cb(char *initial, char *path)
{
	if (symlink(initial, path))
	{
		report_error("ROX-Filer: symlink()", g_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

static void link_item(gpointer data, guint action, GtkWidget *widget)
{
	Collection *collection;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected != 1)
		report_error("ROX-Filer", "You must select a single "
				"item to link");
	else
	{
		FileItem *item = selected_item(collection);

		savebox_show(window_with_focus, "Symlink",
				window_with_focus->path, item->leafname,
				item->image, link_cb);
	}
}

static void show_file_info(gpointer data, guint action, GtkWidget *widget)
{
	GtkWidget	*window, *table, *label, *button, *frame;
	GtkWidget	*file_label;
	GString		*gstring;
	char		*string;
	int		file_data[2];
	char		*path;
	char		buffer[20];
	char 		*argv[] = {"file", "-b", NULL, NULL};
	int		got;
	Collection 	*collection;
	FileItem	*file;
	struct stat	info;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected != 1)
	{
		report_error("ROX-Filer", "You must select a single "
				"item before using Info");
		return;
	}
	file = selected_item(collection);
	path = make_path(window_with_focus->path, file->leafname)->str;
	if (lstat(path, &info))
	{
		delayed_error("ROX-Filer", g_strerror(errno));
		return;
	}
	
	gstring = g_string_new(NULL);

	window = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_MOUSE);
	gtk_container_set_border_width(GTK_CONTAINER(window), 4);
	gtk_window_set_title(GTK_WINDOW(window), path);

	table = gtk_table_new(4, 2, FALSE);
	gtk_container_add(GTK_CONTAINER(window), table);
	gtk_table_set_row_spacings(GTK_TABLE(table), 8);
	gtk_table_set_col_spacings(GTK_TABLE(table), 4);
	
	label = gtk_label_new("Owner, group:");
	gtk_misc_set_alignment(GTK_MISC(label), 1, .5);
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_RIGHT);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);
	g_string_sprintf(gstring, "%s, %s", user_name(info.st_uid),
					    group_name(info.st_gid));
	label = gtk_label_new(gstring->str);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 0, 1);
	
	label = gtk_label_new("Permissions:");
	gtk_misc_set_alignment(GTK_MISC(label), 1, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 1, 2);
	g_string_sprintf(gstring, "%o", info.st_mode);
	label = gtk_label_new(gstring->str);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 1, 2);
	
	label = gtk_label_new("MIME type:");
	gtk_misc_set_alignment(GTK_MISC(label), 1, .5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 2, 3);
	if (file->mime_type)
	{
		string = g_strconcat(file->mime_type->media_type, "/",
				file->mime_type->subtype, NULL);
		label = gtk_label_new(string);
		g_free(string);
	}
	else
		label = gtk_label_new("-");
	gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 2, 3);

	frame = gtk_frame_new("file(1) says...");
	gtk_table_attach_defaults(GTK_TABLE(table), frame, 0, 2, 3, 4);
	file_label = gtk_label_new("<nothing yet>");
	gtk_misc_set_padding(GTK_MISC(file_label), 4, 4);
	gtk_label_set_line_wrap(GTK_LABEL(file_label), TRUE);
	gtk_container_add(GTK_CONTAINER(frame), file_label);
	
	button = gtk_button_new_with_label("OK");
	gtk_table_attach(GTK_TABLE(table), button, 0, 2, 4, 5,
			GTK_EXPAND | GTK_FILL | GTK_SHRINK, 0, 40, 4);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			gtk_widget_destroy, GTK_OBJECT(window));

	gtk_widget_show_all(window);
	gdk_flush();

	pipe(file_data);
	switch (fork())
	{
		case -1:
			close(file_data[0]);
			close(file_data[1]);
			gtk_label_set_text(GTK_LABEL(file_label),
					"fork() error");
			g_string_free(gstring, TRUE);
			return;
		case 0:
			close(file_data[0]);
			dup2(file_data[1], STDOUT_FILENO);
			dup2(file_data[1], STDERR_FILENO);
			argv[2] = path;
			if (execvp(argv[0], argv))
				fprintf(stderr, "execvp() error: %s\n",
						g_strerror(errno));
			_exit(0);
	}
	/* We are the parent... */
	close(file_data[1]);
	g_string_truncate(gstring, 0);
	while (gtk_events_pending())
		g_main_iteration(FALSE);
	while ((got = read(file_data[0], buffer, sizeof(buffer) - 1)))
	{
		buffer[got] = '\0';
		g_string_append(gstring, buffer);
	}
	close(file_data[0]);
	gtk_label_set_text(GTK_LABEL(file_label), gstring->str);
	g_string_free(gstring, TRUE);
}

static void help(gpointer data, guint action, GtkWidget *widget)
{
	Collection 	*collection;
	FileItem	*item;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected != 1)
	{
		report_error("ROX-Filer", "You must select a single "
				"item to get help on");
		return;
	}
	item = selected_item(collection);
	switch (item->base_type)
	{
		case TYPE_FILE:
			if (item->flags & ITEM_FLAG_EXEC_FILE)
				report_error("Executable file",
					"This is a file with an eXecute bit "
					"set - it can be run as a program.");
			else
				report_error("File",
					"This is a data file. Try using the "
					"Info menu item to find out more...");
			break;
		case TYPE_DIRECTORY:
			if (item->flags & ITEM_FLAG_APPDIR)
				app_show_help(
					make_path(window_with_focus->path,
						  item->leafname)->str);
			else if (item->flags & ITEM_FLAG_MOUNT_POINT)
				report_error("Mount point",
				"A mount point is a directory which another "
				"filing system can be mounted on. Everything "
				"on the mounted filesystem then appears to be "
				"inside the directory.");
			else
				report_error("Directory",
				"This is a directory. It contains an index to "
				"other items - open it to see the list.");
			break;
		case TYPE_CHAR_DEVICE:
		case TYPE_BLOCK_DEVICE:
			report_error("Device file",
				"Device files allow you to read from or write "
				"to a device driver as though it was an "
				"ordinary file.");
			break;
		case TYPE_PIPE:
			report_error("Named pipe",
				"Pipes allow different programs to "
				"communicate. One program writes data to the "
				"pipe while another one reads it out again.");
			break;
		case TYPE_SOCKET:
			report_error("Socket",
				"Sockets allow processes to communicate.");
			break;
		default:
			report_error("Unknown type", 
				"I couldn't find out what kind of file this "
				"is. Maybe it doesn't exist anymore or you "
				"don't have search permission on the directory "
				"it's in?");
			break;
	}
}

static void mount(gpointer data, guint action, GtkWidget *widget)
{
	FileItem	*item;
	int		i;
	Collection	*collection;
	char		*error = NULL;
	int		count = 0;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	
	for (i = 0; i < collection->number_of_items; i++)
		if (collection->items[i].selected)
		{
			item = (FileItem *) collection->items[i].data;
			if (item->flags & ITEM_FLAG_MOUNT_POINT)
			{
				char	*argv[] = {"mount", NULL, NULL};
				int	child;

				count++;
				if (item->flags & ITEM_FLAG_MOUNTED)
					argv[0] = "umount";
				argv[1] = make_path(window_with_focus->path,
							item->leafname)->str;
				child = spawn(argv);
				if (child)
					waitpid(child, NULL, 0);
				else
					error = "Failed to run mount/umount";
			}
		}
	if (count)
		update_dir(window_with_focus);
	else if (!error)
		error = "You must select some mount points first!";

	if (error)
		report_error("ROX-Filer", error);
}

static void select_all(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	collection_select_all(window_with_focus->collection);
	window_with_focus->temp_item_selected = FALSE;
}

static void clear_selection(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	collection_clear_selection(window_with_focus->collection);
	window_with_focus->temp_item_selected = FALSE;
}

static void show_options(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	options_show(window_with_focus);
}

static gboolean new_directory_cb(char *initial, char *path)
{
	if (mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO))
	{
		report_error("mkdir", g_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

static void new_directory(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	savebox_show(window_with_focus, "Create directory",
			window_with_focus->path, "NewDir",
			default_pixmap + TYPE_DIRECTORY, new_directory_cb);
}

static void xterm_here(gpointer data, guint action, GtkWidget *widget)
{
	char	*argv[] = {xterm_here_value, NULL};

	g_return_if_fail(window_with_focus != NULL);

	if (!spawn_full(argv, window_with_focus->path, 0))
		report_error("ROX-Filer", "Failed to fork() child "
					"process");
}

static void open_parent(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_opendir(make_path(window_with_focus->path, "/..")->str,
			FALSE, BOTTOM);
}

static void open_as_dir(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);
	
	filer_opendir(window_with_focus->path, FALSE, BOTTOM);
}

static void close_panel(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);
	
	gtk_widget_destroy(window_with_focus->window);
}
