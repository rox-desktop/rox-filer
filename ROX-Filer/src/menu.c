/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* menu.c - code for handling the popup menu */

#include <gtk/gtk.h>

#include "filer.h"
#include "support.h"

#define C_ "<control>"

GtkAccelGroup	*filer_keys;


/* Static prototypes */
static void position_menu(GtkMenu *menu, gint *x, gint *y, gpointer data);
static void open_parent(gpointer data, guint action, GtkWidget *widget);

static GtkWidget	*filer_menu;		/* The popup filer menu */
static FilerWindow	*menu_owner;

static GtkItemFactoryEntry filer_menu_def[] = {
{"/_Display",		    NULL,	NULL, 0, "<Branch>"},
{"/Display/_Large Icons",   NULL,   	NULL, 0, "<RadioItem>"},
{"/Display/_Small Icons",   NULL,   	NULL, 0, "/Display/Large Icons"},
{"/Display/_Full Info",	    NULL,   	NULL, 0, "/Display/Large Icons"},
{"/Display/Separator",	    NULL,   	NULL, 0, "<Separator>"},
{"/Display/Sort by _Name",  NULL,   	NULL, 0, "<RadioItem>"},
{"/Display/Sort by _Type",  NULL,   	NULL, 0, "/Display/Sort by Name"},
{"/Display/Sort by _Date",  NULL,   	NULL, 0, "/Display/Sort by Name"},
{"/Display/Sort by Si_ze",  NULL,   	NULL, 0, "/Display/Sort by Name"},
{"/Display/Separator",	    NULL,   	NULL, 0, "<Separator>"},
{"/Display/Show _Hidden",   C_"H", 	NULL, 0, "<ToggleItem>"},
{"/Display/Refresh",	    C_"L", 	NULL, 0, NULL},
{"/File",		    NULL,   	NULL, 0, "<Branch>"},
{"/File/_Copy",		    NULL,   	NULL, 0, NULL},
{"/File/_Rename",	    NULL,   	NULL, 0, NULL},
{"/File/_Help",		    "F1",   	NULL, 0, NULL},
{"/File/_Info",		    NULL,   	NULL, 0, NULL},
{"/File/Separator",	    NULL,   	NULL, 0, "<Separator>"},
{"/File/_Delete",	    C_"X", 	NULL, 0, NULL},
{"/File/Disk _Usage",	    C_"U", 	NULL, 0, NULL},
{"/File/_Permissions",	    NULL,   	NULL, 0, NULL},
{"/File/_Stamp",    	    NULL,   	NULL, 0, NULL},
{"/File/_Find",		    NULL,   	NULL, 0, NULL},
{"/_Select All",	    C_"A",   	NULL, 0, NULL},
{"/_Clear Selection",	    C_"Z",   	NULL, 0, NULL},
{"/_Options",		    NULL,   	NULL, 0, "<Branch>"},
{"/Options/_Confirm",	    NULL,   	NULL, 0, "<ToggleItem>"},
{"/Options/_Verbose",	    NULL,   	NULL, 0, "<ToggleItem>"},
{"/Options/_Force",	    NULL,   	NULL, 0, "<ToggleItem>"},
{"/Options/_Newer",	    NULL,   	NULL, 0, "<ToggleItem>"},
{"/_New directory",	    NULL,   	NULL, 0, NULL},
{"/_Open parent",	    NULL,   	open_parent, 0, NULL},
};


void menu_init()
{
    GtkItemFactory  	*item_factory;

    filer_keys = gtk_accel_group_new();
    item_factory = gtk_item_factory_new(GTK_TYPE_MENU, "<popup>", filer_keys);

    gtk_item_factory_create_items(item_factory,
			sizeof(filer_menu_def) / sizeof(*filer_menu_def),
			filer_menu_def,
			NULL);

    filer_menu = gtk_item_factory_get_widget(item_factory, "<popup>");
}

static void position_menu(GtkMenu *menu, gint *x, gint *y, gpointer data)
{
    GtkRequisition requisition;

    gtk_widget_size_request(GTK_WIDGET(menu), &requisition);

    *x -= requisition.width >> 2;
    *y -= requisition.height >> 2;
}

void show_filer_menu(FilerWindow *filer_window, GdkEventButton *event)
{
	menu_owner = filer_window;
	
	gtk_menu_popup(GTK_MENU(filer_menu), NULL, NULL, position_menu,
			NULL, event->button, event->time);
}

static void open_parent(gpointer data, guint action, GtkWidget *widget)
{
	filer_opendir(make_path(menu_owner->path, "/..")->str);
}
