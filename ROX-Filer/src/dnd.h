/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _DND_H
#define _DND_H

#include <gtk/gtk.h>

enum
{
	TARGET_RAW,
	TARGET_URI_LIST,
	TARGET_XDS,
	TARGET_STRING,
};

typedef enum {
	MOTION_NONE,		/* Ignoring motion events - click to start! */
	MOTION_REPOSITION,	/* Motion events move current_grab_icon */
	MOTION_READY_FOR_DND,	/* Moving much will start dnd */
	MOTION_DISABLED,	/* Release all buttons to go to MOTION_NONE */
} MotionType;
extern MotionType motion_state;
extern gint drag_start_x, drag_start_y;
extern gint motion_buttons_pressed;

extern gboolean o_no_hostnames;
extern gboolean o_spring_open;
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
		   DirItem		*item);
void drag_data_get(GtkWidget      	*widget,
		   GdkDragContext     	*context,
		   GtkSelectionData   	*selection_data,
		   guint               	info,
		   guint32             	time,
		   gpointer	       	data);
void make_drop_target(GtkWidget *widget, GtkDestDefaults defaults);
void drag_set_dest(FilerWindow *filer_window);
void drag_set_pinboard_dest(GtkWidget *widget);
void dnd_init();
GtkWidget *create_dnd_options();
gboolean provides(GdkDragContext *context, GdkAtom target);

void dnd_spring_load(GdkDragContext *context);
void dnd_spring_abort(void);
GSList *uri_list_to_gslist(char *uri_list);
guchar *dnd_motion_item(GdkDragContext *context, DirItem **item_p);

gboolean dnd_motion_press(GtkWidget *widget, GdkEventButton *event);
void dnd_motion_start(MotionType motion);
gboolean dnd_motion_release(GdkEventButton *event);
void dnd_motion_disable(void);
void dnd_motion_ungrab(void);
gboolean dnd_motion_moved(GdkEventMotion *event);
void dnd_motion_grab_pointer(void);

#endif /* _DND_H */
