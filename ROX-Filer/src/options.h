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

struct _Option {
	OptionUI	*ui;		/* NULL => No UI yet */
	guchar		*value;
	long		int_value;
	OptionChanged	*changed_cb;
	gboolean	save;		/* Save to options file */
	gboolean	has_changed;
};

struct _OptionUI {
	GtkWidget	*widget;
	void		(*update_widget)(OptionUI *ui, guchar *value);
	guchar *	(*read_widget)(OptionUI *ui);
};

/* Prototypes */

void options_init(void);
void option_register_widget(char *name, OptionBuildFn builder);

void option_add_int(Option *option, guchar *key,
		    int value, OptionChanged *changed);
void option_add_string(guchar *key, guchar *value, OptionChanged *changed);
guchar *option_get_static_string(guchar *key);

void options_notify(void);
void option_add_notify(OptionNotify *callback);
void option_add_saver(OptionNotify *callback);

void options_show(void);

#endif /* _OPTIONS_H */
