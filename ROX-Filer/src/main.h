/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _MAIN_H
#define _MAIN_H

extern int number_of_windows;
extern int to_error_log;	/* Send messages here to log them */
extern gboolean override_redirect;

/* Prototypes */
int main(int argc, char **argv);

#endif /* _MAIN_H */
