/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* menu.c - code for handling the popup menu */

#include <sys/wait.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "filer.h"
#include "type.h"
#include "support.h"
#include "gui_support.h"
#include "options.h"
#include "choices.h"
#include "newdir.h"

#define C_ "<control>"

#define MENU_MARGIN 32

GtkAccelGroup	*filer_keys;
GtkAccelGroup	*panel_keys;

/* Static prototypes */
static void position_menu(GtkMenu *menu, gint *x, gint *y, gpointer data);
static void menu_closed(GtkWidget *widget);

static void refresh(gpointer data, guint action, GtkWidget *widget);
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

static GtkWidget	*filer_menu;		/* The popup filer menu */
static GtkWidget	*filer_file_item;	/* The File '' menu */
static GtkWidget	*panel_menu;		/* The popup panel menu */
static GtkWidget	*panel_file_item;	/* The File '' menu */

static GtkItemFactoryEntry filer_menu_def[] = {
{"/Display",			NULL,	NULL, 0, "<Branch>"},
{"/Display/Large Icons",   	NULL,  	NULL, 0, "<RadioItem>"},
{"/Display/Small Icons",   	NULL,  	NULL, 0, "/Display/Large Icons"},
{"/Display/Full Info",		NULL,  	NULL, 0, "/Display/Large Icons"},
{"/Display/Separator",		NULL,  	NULL, 0, "<Separator>"},
{"/Display/Sort by Name",	NULL,  	NULL, 0, "<RadioItem>"},
{"/Display/Sort by Type",	NULL,  	NULL, 0, "/Display/Sort by Name"},
{"/Display/Sort by Date",	NULL,  	NULL, 0, "/Display/Sort by Name"},
{"/Display/Sort by Size",	NULL,  	NULL, 0, "/Display/Sort by Name"},
{"/Display/Separator",		NULL,  	NULL, 0, "<Separator>"},
{"/Display/Show Hidden",   	C_"H", 	NULL, 0, "<ToggleItem>"},
{"/Display/Refresh",	   	C_"L", 	refresh, 0,	NULL},
{"/File",			NULL,  	NULL, 0, "<Branch>"},
{"/File/Copy",			NULL,  	NULL, 0, NULL},
{"/File/Rename",		NULL,  	NULL, 0, NULL},
{"/File/Help",		    	"F1",  	NULL, 0, NULL},
{"/File/Info",			NULL,  	NULL, 0, NULL},
{"/File/Mount",	    		C_"M",  mount, 0,	NULL},
{"/File/Separator",		NULL,   NULL, 0, "<Separator>"},
{"/File/Delete",	    	C_"X", 	delete, 0,	NULL},
{"/File/Disk Usage",	    	C_"U", 	NULL, 0, NULL},
{"/File/Permissions",		NULL,   NULL, 0, NULL},
{"/File/Stamp",    		NULL,   NULL, 0, NULL},
{"/File/Find",			NULL,   NULL, 0, NULL},
{"/Select All",	    		C_"A",  select_all, 0, NULL},
{"/Clear Selection",	    	C_"Z",  clear_selection, 0, NULL},
{"/Options...",			NULL,   show_options, 0, NULL},
{"/New directory",		NULL,   new_directory, 0, NULL},
{"/Xterm here",			NULL,  	xterm_here, 0, NULL},
{"/Open parent",		NULL,   open_parent, 0, NULL},
};

static GtkItemFactoryEntry panel_menu_def[] = {
{"/Display",			NULL,	NULL, 0, "<Branch>"},
{"/Display/Large Icons",	NULL,   NULL, 0, "<RadioItem>"},
{"/Display/Small Icons",	NULL,   NULL, 0, "/Display/Large Icons"},
{"/Display/Full Info",		NULL,   NULL, 0, "/Display/Large Icons"},
{"/Display/Separator",		NULL,   NULL, 0, "<Separator>"},
{"/Display/Sort by Name",	NULL,   NULL, 0, "<RadioItem>"},
{"/Display/Sort by Type",	NULL,   NULL, 0, "/Display/Sort by Name"},
{"/Display/Sort by Date",	NULL,   NULL, 0, "/Display/Sort by Name"},
{"/Display/Sort by Size",	NULL,   NULL, 0, "/Display/Sort by Name"},
{"/Display/Separator",		NULL,   NULL, 0, "<Separator>"},
{"/Display/Show Hidden",   	NULL, 	NULL, 0, "<ToggleItem>"},
{"/Display/Refresh",	    	NULL, 	refresh, 0,	NULL},
{"/File",			NULL,	NULL, 	0, "<Branch>"},
{"/File/Delete",		NULL,	NULL, 	0, NULL},
{"/Open as directory",		NULL, 	open_as_dir, 0, NULL},
{"/Close panel",		NULL, 	close_panel, 0, NULL},
};


void menu_init()
{
	GtkItemFactory  	*item_factory;
	char			*menurc;
	GList			*items;

	filer_keys = gtk_accel_group_new();
	item_factory = gtk_item_factory_new(GTK_TYPE_MENU,
					    "<filer>",
					    filer_keys);
	gtk_item_factory_create_items(item_factory,
			sizeof(filer_menu_def) / sizeof(*filer_menu_def),
			filer_menu_def,
			NULL);
	filer_menu = gtk_item_factory_get_widget(item_factory, "<filer>");
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
}
 
/* Save the keybindings... */
void menu_save()
{
	char	*menurc;

	menurc = choices_find_path_save("menus");
	if (menurc)
		gtk_item_factory_dump_rc(menurc, NULL, TRUE);
}

static void position_menu(GtkMenu *menu, gint *x, gint *y, gpointer data)
{
	int		*pos = (int *) data;
	int		swidth, sheight;
	GtkRequisition 	requisition;

	gdk_window_get_size(GDK_ROOT_PARENT(), &swidth, &sheight);
	
	gtk_widget_size_request(GTK_WIDGET(menu), &requisition);

	if (pos[0] == -1)
		*x = swidth - MENU_MARGIN - requisition.width;
	else if (pos[0] == -2)
		*x = MENU_MARGIN;
	else
		*x = pos[0] - (requisition.width >> 2);
		
	if (pos[1] == -1)
		*y = sheight - MENU_MARGIN - requisition.height;
	else if (pos[1] == -2)
		*y = MENU_MARGIN;
	else
		*y = pos[1] - (requisition.height >> 2);

	*x = CLAMP(*x, 0, swidth - requisition.width);
	*y = CLAMP(*y, 0, sheight - requisition.height);
}

void show_filer_menu(FilerWindow *filer_window, GdkEventButton *event,
		     int item)
{
	GString		*buffer;
	GtkWidget	*file_label;
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
		file_label = panel_file_item;
	else
		file_label = filer_file_item;

	buffer = g_string_new(NULL);
	switch (filer_window->collection->number_selected)
	{
		case 0:
			g_string_assign(buffer, "<nothing selected>");
			gtk_widget_set_sensitive(file_label, FALSE);
			break;
		case 1:
			gtk_widget_set_sensitive(file_label, TRUE);
			file_item = selected_item(filer_window->collection);
			g_string_sprintf(buffer, "%s '%s'",
					basetype_name(file_item),
					file_item->leafname);
			break;
		default:
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

static void refresh(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	scan_dir(window_with_focus);
}

static void delete(gpointer data, guint action, GtkWidget *widget)
{
	const char	*start_args[] = {"xterm", "-wf",
				"-e", "rm", "-vir"};
	int		argc = sizeof(start_args) / sizeof(char *);
	char		**argv;
	Collection	*collection;
	int		i;
	FileItem	*item;
	int		child;

	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;

	if (collection->number_selected < 1)
	{
		report_error("ROX-Filer", "Nothing to delete!");
		return;
	}

	argv = g_malloc(sizeof(start_args) +
		sizeof(char *) * (collection->number_selected + 1));
	memcpy(argv, start_args, sizeof(start_args));

	for (i = 0; i < collection->number_of_items; i++)
		if (collection->items[i].selected)
		{
			item = (FileItem *) collection->items[i].data;
			argv[argc++] = g_strdup(make_path(
						window_with_focus->path,
						item->leafname)->str);
		}
	argv[argc] = NULL;

	child = spawn(argv);
	if (child)
		g_hash_table_insert(child_to_filer,
				(gpointer) child, window_with_focus);
	else
		report_error("ROX-Filer", "Failed to fork() child "
					"process");

	for (i = sizeof(start_args) / sizeof(char *); i < argc; i++)
		g_free(argv[i]);
	g_free(argv);
}

static void mount(gpointer data, guint action, GtkWidget *widget)
{
	FileItem	*item;
	int		i;
	Collection	*collection;
	char		*error = NULL;
	
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
	scan_dir(window_with_focus);

	if (error)
		report_error("ROX-Filer", error);
}

static void select_all(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	collection_select_all(window_with_focus->collection);
}

static void clear_selection(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	collection_clear_selection(window_with_focus->collection);
}

static void show_options(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	options_show(window_with_focus);
}

static void new_directory(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	newdir_show(window_with_focus);
}

static void xterm_here(gpointer data, guint action, GtkWidget *widget)
{
	char	*argv[] = {"gnome-terminal", NULL};	/* XXX: Bad default */

	g_return_if_fail(window_with_focus != NULL);

	if (!spawn_in(argv, window_with_focus->path))
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
