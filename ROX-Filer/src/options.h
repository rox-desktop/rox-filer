/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _OPTIONS_H
#define _OPTIONS_H

#include <gtk/gtk.h>

#define OPTION_TIP(widget, tip)	\
	gtk_tooltips_set_tip(option_tooltips, widget, tip, NULL)

typedef char *OptionFunc(char *value);

typedef struct _OptionsSection OptionsSection;

struct _OptionsSection
{
	char 	*name;
	GtkWidget *(*create)(void);	/* Create widgets */
	void 	(*update)(void);	/* Update widgets */
	void 	(*set)(void);		/* Read values from widgets */
	void 	(*save)(void);		/* Save values to Choices */
};

extern GSList *options_sections;
extern GtkTooltips *option_tooltips;

/* Prototypes */

void options_init(void);
void option_register(guchar *key, OptionFunc *func);
void options_load(void);
void options_show(void);
void option_write(guchar *name, guchar *value);

#endif /* _OPTIONS_H */
