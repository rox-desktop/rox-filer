/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _OPTIONS_H
#define _OPTIONS_H

typedef char *OptionFunc(char *value);

typedef struct _OptionsSection OptionsSection;

#include "filer.h"

struct _OptionsSection
{
	char 	*name;
	GtkWidget *(*create)(void);	/* Create widgets */
	void 	(*update)(void);	/* Update widgets */
	void 	(*set)(void);		/* Read values from widgets */
	void 	(*save)(void);		/* Save values to Choices */
};

extern GSList *options_sections;

/* Prototypes */

void options_init(void);
void option_register(char *key, OptionFunc *func);
void options_load(void);
void options_show(FilerWindow *filer_window);
void option_write(char *name, char *value);

#endif /* _OPTIONS_H */
