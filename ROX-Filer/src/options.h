/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _OPTIONS_H
#define _OPTIONS_H

#include <gtk/gtk.h>

typedef void OptionChanged(guchar *new_value); /* (also connect_object cb) */
typedef void OptionNotify(void);
typedef struct _OptionUI OptionUI;
typedef GList * (*OptionBuildFn)(OptionUI *ui, xmlNode *node, guchar *label);

struct _OptionUI {
	GtkWidget	*widget;
	void		(*update_widget)(OptionUI *ui, guchar *value);
	guchar *	(*read_widget)(OptionUI *ui);
};

/* Prototypes */

void options_init(void);
void option_register_widget(char *name, OptionBuildFn builder);

void option_add_int(guchar *key, int value, OptionChanged *changed);
int option_get_int(guchar *key);

void option_add_string(guchar *key, guchar *value, OptionChanged *changed);
guchar *option_get_static_string(guchar *key);

void options_notify(void);
void option_add_notify(OptionNotify *callback);
void option_add_saver(OptionNotify *callback);

void options_show(void);

#endif /* _OPTIONS_H */
