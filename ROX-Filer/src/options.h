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

/* Prototypes */

void options_init(void);

void option_add_int(guchar *key, int value, OptionChanged *changed);
int option_get_int(guchar *key);

void option_add_string(guchar *key, guchar *value, OptionChanged *changed);
guchar *option_get_static_string(guchar *key);

void option_add_void(gchar *key, OptionChanged *changed);

void option_add_notify(OptionNotify *callback);
void option_add_saver(OptionNotify *callback);

void options_load(void);
void options_show(void);

void option_set_save(guchar *key, gboolean save);

#endif /* _OPTIONS_H */
