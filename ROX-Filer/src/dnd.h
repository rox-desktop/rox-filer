/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _DND_H
#define _DND_H

#include <gtk/gtk.h>
#include "collection.h"
#include "filer.h"

extern gboolean o_no_hostnames;
extern char *drop_dest_prog;
extern char *drop_dest_dir;
extern GdkAtom XdndDirectSave0;
extern GdkAtom text_uri_list;
extern GdkAtom _rox_run_action;
extern GdkAtom application_octet_stream;

void drag_selection(GtkWidget *widget, GdkEventMotion *event, guchar *uri_list);
void drag_one_item(GtkWidget		*widget,
		   GdkEventMotion	*event,
		   guchar		*full_path,
		   DirItem		*item,
		   gboolean		set_run_action);
void drag_data_get(GtkWidget      	*widget,
		   GdkDragContext     	*context,
		   GtkSelectionData   	*selection_data,
		   guint               	info,
		   guint32             	time,
		   gpointer	       	data);
void make_drop_target(GtkWidget *widget);
void drag_set_dest(FilerWindow *filer_window);
void drag_set_pinboard_dest(GtkWidget *widget);
void dnd_init();
GtkWidget *create_dnd_options();
gboolean provides(GdkDragContext *context, GdkAtom target);

void dnd_spring_load(GdkDragContext *context);
void dnd_spring_abort(void);

#endif /* _DND_H */
