/*
 * $Id$
 *
 * The collection widget provides an area for displaying a collection of
 * objects (such as files). It allows the user to choose a selection of
 * them and provides signals to allow popping up menus, detecting double-clicks
 * etc.
 *
 * Thomas Leonard, <tal197@ecs.soton.ac.uk>
 */


#ifndef __COLLECTION_H__
#define __COLLECTION_H__

#include <gdk/gdk.h>
#include <gtk/gtkwidget.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define COLLECTION(obj) GTK_CHECK_CAST((obj), collection_get_type(), Collection)
#define COLLECTION_CLASS(klass) GTK_CHECK_CLASS_CAST((klass), \
					collection_get_type(), CollectionClass)
#define IS_COLLECTION(obj) GTK_CHECK_TYPE((obj), collection_get_type())

/* If the display gets mucked up then remember to fix it next time we get the
 * chance.
 */
enum
{
	PAINT_NORMAL,		/* Just redraw what we need to */
	PAINT_OVERWRITE,	/* Draw everything */
	PAINT_CLEAR,		/* Blank everything, then redraw */
};


typedef struct _Collection       Collection;
typedef struct _CollectionClass  CollectionClass;
typedef struct _CollectionItem   CollectionItem;
typedef void (*CollectionDrawFunc)(GtkWidget *widget,
			     	  CollectionItem *item,
			     	  GdkRectangle *area);
typedef gboolean (*CollectionTestFunc)( Collection *collection,
					int point_x, int point_y,
			       		CollectionItem *item,
			       		int width, int height);
typedef void (*CollectionTargetFunc)(Collection *collection,
					gint item,
					gpointer user_data);

struct _CollectionItem
{
	gpointer	data;
	gboolean	selected;
};

struct _Collection
{
	GtkWidget 	parent_widget;
	gboolean	panel;

	GtkAdjustment	*vadj;
	guint		last_scroll;	/* Current/previous scroll value */

	CollectionDrawFunc draw_item;
	CollectionTestFunc test_point;
	int		paint_level;	/* Complete redraw on next paint? */
	
	gboolean	lasso_box;	/* Is the box drawn? */
	int		drag_box_x[2];	/* Index 0 is the fixed corner */
	int		drag_box_y[2];
	GdkGC		*xor_gc;
	int		buttons_pressed;	/* Number of buttons down */
	gboolean	may_drag;	/* Tried to drag since first press? */

	CollectionItem	*items;
	gint		cursor_item;	/* -1 if not shown */
	gint		wink_item;	/* -1 if not active */
	gint		wink_timeout;
	guint		columns;
	guint		number_of_items;
	guint		item_width, item_height;

	guint		number_selected;
	int		item_clicked;	/* For collection_single_click */

	guint		array_size;

	CollectionTargetFunc target_cb;
	gpointer	target_data;
};

struct _CollectionClass
{
	GtkWidgetClass 	parent_class;

	void 		(*open_item)(Collection *collection,
				     CollectionItem *item,
				     guint	item_number);
	void 		(*drag_selection)(Collection 		*collection,
				     	  GdkEventMotion	*motion_event,
				     	  guint			items_selected);
	void 		(*show_menu)(Collection 	*collection,
				     GdkEventButton	*button_event,
				     guint		items_selected);
	void 		(*gain_selection)(Collection 	*collection,
				     guint		time);
	void 		(*lose_selection)(Collection 	*collection,
				     guint		time);
};

guint   collection_get_type   		(void);
GtkWidget *collection_new          	(GtkAdjustment *vadj);
void    collection_clear           	(Collection *collection);
gint    collection_insert          	(Collection *collection, gpointer data);
void    collection_remove          	(Collection *collection, gint item);
void    collection_unselect_item	(Collection *collection, gint item);
void    collection_select_item		(Collection *collection, gint item);
void 	collection_toggle_item		(Collection *collection, gint item);
void 	collection_select_all		(Collection *collection);
void 	collection_clear_selection	(Collection *collection);
void	collection_draw_item		(Collection *collection, gint item,
					 gboolean blank);
void 	collection_set_functions	(Collection *collection,
					 CollectionDrawFunc draw_item,
					 CollectionTestFunc test_point);
void 	collection_set_item_size	(Collection *collection,
					 int width, int height);
void 	collection_qsort		(Collection *collection,
					 int (*compar)(const void *,
						       const void *));
int 	collection_find_item		(Collection *collection,
					 gpointer data,
					 int (*compar)(const void *,
						       const void *));
void 	collection_set_panel		(Collection *collection,
					 gboolean panel);
int 	collection_get_item		(Collection *collection, int x, int y);
void 	collection_set_cursor_item	(Collection *collection, gint item);
void 	collection_wink_item		(Collection *collection, gint item);
void 	collection_delete_if		(Collection *collection,
			  		 gboolean (*test)(gpointer item,
						          gpointer data),
			  		 gpointer data);
void	collection_target		(Collection *collection,
					 CollectionTargetFunc callback,
					 gpointer user_data);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __COLLECTION_H__ */
