/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@users.sourceforge.net>.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* options.c - code for handling user choices */

/* How it works:
 *
 * On startup, each part of the filer calls option_add_int() or a related
 * function, supplying a name for the option and a default value.
 *
 * Once everything has initialised, the options file (if any) is read,
 * which may change some or all of the values. Update callbacks are called
 * if values change from their defaults.
 *
 * All the notify_callbacks are called.
 *
 * When the user opens the Options box:
 * - The Options.xml file is read and used to create the Options dialog box.
 *   Each element in the file has a key corresponding to an option named
 *   above. This only happens the first time the box is opened.
 * - For each widget in the box, the current value of the option is used to
 *   set the widget's state.
 *
 * When the user clicks Save/OK/Apply, the state of each widget is copied
 * to the options values and the update callbacks are called for those values
 * which have changed.
 *
* All the notify_callbacks are called.
 *
 * If Save or OK was clicked then the box is also closed.
 *
 * If Save was clicked then the options are written to the filesystem
 * and the saver_callbacks are called.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include <parser.h>

#include "global.h"

#include "gui_support.h"
#include "choices.h"
#include "options.h"
#include "main.h"
#include "support.h"
#include "toolbar.h"

typedef struct _Option Option;

/* Add all option tooltips to this group */
static GtkTooltips *option_tooltips = NULL;
#define OPTION_TIP(widget, tip)	\
	gtk_tooltips_set_tip(option_tooltips, widget, tip, NULL)

/* The Options window. NULL if not yet created. */
static GtkWidget *window = NULL;

typedef struct _ParseContext ParseContext;

/* What tag we are in.
 * Also used for the widget_type.
 */
typedef enum {
	PARSE_TOPLEVEL,
	PARSE_OPTIONS,
	PARSE_SECTION,
	PARSE_HBOX,
	PARSE_MENU,
	PARSE_ITEM,
	PARSE_RADIO_GROUP,
	PARSE_RADIO,
	PARSE_TOGGLE,
	PARSE_ENTRY,
	PARSE_SLIDER,
	PARSE_COLOUR,
	PARSE_LABEL,
	PARSE_TOOLS,
	PARSE_UNKNOWN,
} ParserState;

struct _Option {
	GtkWidget	*widget;	/* NULL => No widget yet */
	ParserState	widget_type;
	guchar		*value;
	OptionChanged	*changed_cb;
	gboolean	save;		/* Save to options file */
};

enum {BUTTON_SAVE, BUTTON_OK, BUTTON_APPLY};

/* "filer_unique" -> (Option *) */
static GHashTable *option_hash = NULL;

/* List of functions to call after all option values are updated */
static GList *notify_callbacks = NULL;

/* List of functions to call after all options are saved */
static GList *saver_callbacks = NULL;

/* Static prototypes */
static void save_options(GtkWidget *widget, gpointer data);
static char *process_option_line(guchar *line);
static void build_options_window(void);
static GtkWidget *build_frame(ParseContext *context);
static void update_option_widgets(void);
static Option *new_option(guchar *key, OptionChanged *changed);
static guchar *tools_to_list(GtkWidget *hbox);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void options_init(void)
{
	option_tooltips = gtk_tooltips_new();
}

void options_load(void)
{
	GList	*next;
	char	*path;

	path = choices_find_path_load("Options", PROJECT);
	if (!path)
		return;		/* Nothing to load */

	parse_file(path, process_option_line);
	g_free(path);

	for (next = notify_callbacks; next; next = next->next)
	{
		OptionNotify *cb = (OptionNotify *) next->data;

		cb();
	}
}

void options_show(void)
{
	if (!window)
		build_options_window();

	if (GTK_WIDGET_MAPPED(window))
		gtk_widget_hide(window);

	update_option_widgets();
	
	gtk_widget_show_all(window);
}

/* Create a new option and register it */
void option_add_int(guchar *key, int value, OptionChanged *changed)
{
	Option	*option;

	option = new_option(key, changed);
	option->value = g_strdup_printf("%d", value);
}

int option_get_int(guchar *key)
{
	Option	*option;

	option = g_hash_table_lookup(option_hash, key);

	g_return_val_if_fail(option != NULL, -1);

	return atoi(option->value);
}

void option_add_string(guchar *key, guchar *value, OptionChanged *changed)
{
	Option	*option;

	option = new_option(key, changed);
	option->value = g_strdup(value);
}

/* The string returned is only valid until the option value changes.
 * Do not free it!
 */
guchar *option_get_static_string(guchar *key)
{
	Option	*option;

	option = g_hash_table_lookup(option_hash, key);

	g_return_val_if_fail(option != NULL, NULL);

	return option->value;
}

/* Add a callback which will be called after all the options have
 * been set following a Save/OK/Apply.
 * Useful if you need to update if any of several values have changed.
 */
void option_add_notify(OptionNotify *callback)
{
	g_return_if_fail(callback != NULL);

	notify_callbacks = g_list_append(notify_callbacks, callback);
}

/* Call this after all the options have been saved */
void option_add_saver(OptionNotify *callback)
{
	g_return_if_fail(callback != NULL);

	saver_callbacks = g_list_append(saver_callbacks, callback);
}

/* Set whether this option should be saved in the options file */
void option_set_save(guchar *key, gboolean save)
{
	Option	*option;

	option = g_hash_table_lookup(option_hash, key);
	g_return_if_fail(option != NULL);

	option->save = save;
}

/****************************************************************
 *                      INTERNAL FUNCTIONS                      *
 ****************************************************************/

static Option *new_option(guchar *key, OptionChanged *changed)
{
	Option	*option;

	if (!option_hash)
		option_hash = g_hash_table_new(g_str_hash, g_str_equal);
	
	option = g_new(Option, 1);

	option->save = TRUE;	/* Save by default */
	option->widget = NULL;
	option->value = NULL;
	option->changed_cb = changed;
	
	g_hash_table_insert(option_hash, key, option);

	return option;
}

struct _ParseContext {
	GList		*state;		/* First item is current state */
	GtkWidget	*vbox, *hbox, *option_menu;
	GString		*data;
	GtkWidget	*prev_radio;
	GtkAdjustment	*adj;
	guchar		*section, *label, *radio_group, *value;
	gboolean	fixed;
	Option		*option;
};

static ParseContext context;

static CHAR *get_attr(const CHAR **attrs, char *name)
{
	while (attrs && *attrs)
	{
		if (strcmp(*attrs, name) == 0)
			return (guchar *) attrs[1];
		attrs += 2;
	}

	return NULL;
}

static GtkColorSelectionDialog *current_csel_box = NULL;

static void set_to_null(gpointer *data)
{
	*data = NULL;
}

static void get_new_colour(GtkWidget *ok, GtkWidget *button)
{
	GtkWidget	*csel;
	gdouble		c[4];
	GdkColor	colour;

	g_return_if_fail(current_csel_box != NULL);

	csel = current_csel_box->colorsel;

	gtk_color_selection_get_color(GTK_COLOR_SELECTION(csel), c);
	colour.red = c[0] * 0xffff;
	colour.green = c[1] * 0xffff;
	colour.blue = c[2] * 0xffff;

	button_patch_set_colour(button, &colour);
	
	gtk_widget_destroy(GTK_WIDGET(current_csel_box));
}

static void open_coloursel(GtkWidget *ok, GtkWidget *button)
{
	GtkColorSelectionDialog	*csel;
	GtkWidget		*dialog, *patch;
	gdouble			c[4];

	if (current_csel_box)
		gtk_widget_destroy(GTK_WIDGET(current_csel_box));

	dialog = gtk_color_selection_dialog_new(NULL);
	csel = GTK_COLOR_SELECTION_DIALOG(dialog);
	current_csel_box = csel;

	gtk_signal_connect_object(GTK_OBJECT(dialog), "destroy",
			GTK_SIGNAL_FUNC(set_to_null),
			(GtkObject *) &current_csel_box);
	gtk_widget_hide(csel->help_button);
	gtk_signal_connect_object(GTK_OBJECT(csel->cancel_button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_destroy),
			GTK_OBJECT(dialog));
	gtk_signal_connect(GTK_OBJECT(csel->ok_button), "clicked",
			GTK_SIGNAL_FUNC(get_new_colour), button);

	patch = GTK_BIN(button)->child;

	c[0] = ((gdouble) patch->style->bg[GTK_STATE_NORMAL].red) / 0xffff;
	c[1] = ((gdouble) patch->style->bg[GTK_STATE_NORMAL].green) / 0xffff;
	c[2] = ((gdouble) patch->style->bg[GTK_STATE_NORMAL].blue) / 0xffff;
	gtk_color_selection_set_color(GTK_COLOR_SELECTION(csel->colorsel), c);

	gtk_widget_show(dialog);
}

static void startElement(void *user_data, const CHAR *name, const CHAR **attrs)
{
	ParserState old = (ParserState) context.state->data;
	ParserState new = PARSE_UNKNOWN;

	g_string_truncate(context.data, 0);
	
	if (old == PARSE_TOPLEVEL)
	{
		if (strcmp(name, "options") == 0)
			new = PARSE_OPTIONS;
	}
	else if (old == PARSE_OPTIONS)
	{
		if (strcmp(name, "section") == 0)
		{
			GtkWidget	*hbox;

			if (context.section)
				gtk_box_pack_start(GTK_BOX(context.vbox),
							gtk_event_box_new(),
							FALSE, TRUE, 8);
			g_free(context.section);
			context.section = g_strdup(get_attr(attrs, "name"));

			hbox = gtk_hbox_new(FALSE, 4);
			gtk_box_pack_start(GTK_BOX(hbox),
				gtk_hseparator_new(), TRUE, TRUE, 0);
			
			gtk_box_pack_start(GTK_BOX(hbox),
				gtk_label_new(_(get_attr(attrs, "title"))),
				FALSE, TRUE, 0);

			gtk_box_pack_start(GTK_BOX(hbox),
				gtk_hseparator_new(), TRUE, TRUE, 0);

			gtk_box_pack_start(GTK_BOX(context.vbox), hbox,
						FALSE, TRUE, 2);

			new = PARSE_SECTION;
		}
	}
	else if (old == PARSE_SECTION || old == PARSE_HBOX)
	{
		guchar	*oname;

		oname = get_attr(attrs, "name");
		if (oname)
		{
			Option	*option;
			guchar	*tmp;

			tmp = g_strconcat(context.section, "_", oname, NULL);
			option = g_hash_table_lookup(option_hash, tmp);

			if (option)
				context.option = option;
			else
			{
				context.option = NULL;
				g_print("No Option for '%s'!\n", tmp);
			}

			g_free(tmp);
		}
		
		if (strcmp(name, "toggle") == 0)
		{
			new = PARSE_TOGGLE;
			g_free(context.label);
			context.label = g_strdup(get_attr(attrs, "label"));
		}
		else if (strcmp(name, "slider") == 0)
		{
			guchar	*fixed;

			new = PARSE_SLIDER;
			g_free(context.label);
			context.label = g_strdup(get_attr(attrs, "label"));
			context.adj = GTK_ADJUSTMENT(gtk_adjustment_new(0,
				atoi(get_attr(attrs, "min")),
				atoi(get_attr(attrs, "max")),
				1, 10, 0));

			fixed = get_attr(attrs, "fixed");
			context.fixed = fixed ? atoi(fixed) : 0;
		}
		else if (strcmp(name, "label") == 0)
			new = PARSE_LABEL;
		else if (strcmp(name, "hbox") == 0 && !context.hbox)
		{
			guchar	*label;
			
			new = PARSE_HBOX;

			label = get_attr(attrs, "label");
			context.hbox = gtk_hbox_new(FALSE, 4);
			if (label)
				gtk_box_pack_start(GTK_BOX(context.hbox),
					gtk_label_new(_(label)),
					FALSE, TRUE, 4);
			gtk_box_pack_start(GTK_BOX(context.vbox), context.hbox,
					FALSE, TRUE, 0);
		}
		else if (strcmp(name, "radio-group") == 0)
		{
			new = PARSE_RADIO_GROUP;

			g_free(context.radio_group);
			context.radio_group = g_strdup(get_attr(attrs, "name"));
			context.prev_radio = NULL;
		}
		else if (strcmp(name, "colour") == 0)
		{
			g_free(context.label);
			context.label = g_strdup(get_attr(attrs, "label"));

			new = PARSE_COLOUR;
		}
		else if (strcmp(name, "menu") == 0)
		{
			GtkWidget	*hbox, *om;
			GtkWidget	*box = context.hbox;

			if (!box)
				box = context.vbox;

			hbox = gtk_hbox_new(FALSE, 4);
			gtk_box_pack_start(GTK_BOX(box), hbox,
					FALSE, TRUE, 0);

			gtk_box_pack_start(GTK_BOX(hbox),
				gtk_label_new(_(get_attr(attrs, "label"))),
				FALSE, TRUE, 0);

			context.option_menu = gtk_option_menu_new();
			gtk_box_pack_start(GTK_BOX(hbox), context.option_menu,
					FALSE, TRUE, 0);

			om = gtk_menu_new();
			gtk_option_menu_set_menu(
					GTK_OPTION_MENU(context.option_menu),
					om);

			new = PARSE_MENU;
		}
		else if (strcmp(name, "entry") == 0)
		{
			new = PARSE_ENTRY;
			g_free(context.label);
			context.label = g_strdup(get_attr(attrs, "label"));
		}
		else if (strcmp(name, "tool-options") == 0)
			new = PARSE_TOOLS;
	}
	else if (old == PARSE_MENU)
	{
		if (strcmp(name, "item") == 0)
		{
			GtkWidget	*item, *menu;

			item = gtk_menu_item_new_with_label(
					_(get_attr(attrs, "label")));

			menu = gtk_option_menu_get_menu(
					GTK_OPTION_MENU(context.option_menu));
			gtk_menu_append(GTK_MENU(menu), item);
			gtk_widget_show_all(menu);

			gtk_object_set_data(GTK_OBJECT(item), "value",
					g_strdup(get_attr(attrs, "value")));

			new = PARSE_ITEM;
		}
	}
	else if (old == PARSE_RADIO_GROUP)
	{
		if (strcmp(name, "radio") == 0)
		{
			new = PARSE_RADIO;
			g_free(context.label);
			context.label = g_strdup(get_attr(attrs, "label"));

			g_free(context.value);
			context.value = g_strdup(get_attr(attrs, "value"));
		}
	}
	
	if (new == PARSE_UNKNOWN)
		g_warning("Unknown Options.xml tag '%s'\n", name);

	context.state = g_list_prepend(context.state, GINT_TO_POINTER(new));
}

static void may_add_tip(GtkWidget *widget, guchar *tip)
{
	tip = g_strstrip(g_strdup(tip));
	if (*tip)
		OPTION_TIP(widget, _(tip));
	g_free(tip);
}

static void endElement(void *user_data, const CHAR *name)
{
	ParserState old = (ParserState) context.state->data;
	GtkWidget	*box = context.hbox;

	if (!box)
		box = context.vbox;

	if (old == PARSE_TOGGLE)
	{
		GtkWidget	*toggle;

		toggle = gtk_check_button_new_with_label(_(context.label));
		gtk_box_pack_start(GTK_BOX(box), toggle,
				FALSE, TRUE, 0);
		may_add_tip(toggle, context.data->str);

		if (context.option)
		{
			context.option->widget_type = old;
			context.option->widget = toggle;
		}
	}
	else if (old == PARSE_ENTRY)
	{
		GtkWidget	*hbox;
		GtkWidget	*entry;

		hbox = gtk_hbox_new(FALSE, 4);

		gtk_box_pack_start(GTK_BOX(hbox),
				gtk_label_new(_(context.label)),
				FALSE, TRUE, 0);

		entry = gtk_entry_new();
		gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
		may_add_tip(entry, context.data->str);

		gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, TRUE, 0);

		if (context.option)
		{
			context.option->widget_type = old;
			context.option->widget = entry;
		}
	}
	else if (old == PARSE_RADIO)
	{
		GtkWidget	*radio;
		GtkRadioButton	*prev = (GtkRadioButton *) context.prev_radio;

		radio = gtk_radio_button_new_with_label(
				prev ? gtk_radio_button_group(prev)
				     : NULL,
				_(context.label));
		gtk_box_pack_start(GTK_BOX(box), radio, FALSE, TRUE, 0);
		context.prev_radio = radio;
		may_add_tip(radio, context.data->str);

		g_return_if_fail(context.value != NULL);
		gtk_object_set_data(GTK_OBJECT(radio), "value", context.value);
		context.value = NULL;
	}
	else if (old == PARSE_RADIO_GROUP)
	{
		GtkWidget	*prev = (GtkWidget *) context.prev_radio;

		g_return_if_fail(prev != NULL);

		if (context.option)
		{
			context.option->widget_type = old;
			context.option->widget = prev;
		}
	}
	else if (old == PARSE_LABEL)
	{
		GtkWidget	*label;

		label = gtk_label_new(context.data->str);
		gtk_box_pack_start(GTK_BOX(box), label,
				FALSE, TRUE, 0);
	}
	else if (old == PARSE_HBOX)
		context.hbox = NULL;
	else if (old == PARSE_COLOUR)
	{
		GtkWidget	*hbox, *da, *button;

		hbox = gtk_hbox_new(FALSE, 4);
		gtk_box_pack_start(GTK_BOX(hbox),
				gtk_label_new(_(context.label)),
				FALSE, TRUE, 0);

		button = gtk_button_new();
		da = gtk_drawing_area_new();
		gtk_drawing_area_size(GTK_DRAWING_AREA(da), 64, 12);
		gtk_container_add(GTK_CONTAINER(button), da);
		gtk_signal_connect(GTK_OBJECT(button), "clicked",
				GTK_SIGNAL_FUNC(open_coloursel), button);

		may_add_tip(button, context.data->str);
		
		gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);

		gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, TRUE, 0);

		if (context.option)
		{
			context.option->widget_type = old;
			context.option->widget = button;
		}
	}
	else if (old == PARSE_SLIDER)
	{
		GtkWidget	*hbox, *slide;

		hbox = gtk_hbox_new(FALSE, 4);
		gtk_box_pack_start(GTK_BOX(hbox),
				gtk_label_new(_(context.label)),
				FALSE, TRUE, 0);

		slide = gtk_hscale_new(context.adj);

		if (context.fixed)
			gtk_widget_set_usize(slide, context.adj->upper, 24);
		gtk_scale_set_draw_value(GTK_SCALE(slide), FALSE);
		GTK_WIDGET_UNSET_FLAGS(slide, GTK_CAN_FOCUS);

		may_add_tip(slide, context.data->str);
		
		gtk_box_pack_start(GTK_BOX(hbox), slide,
				!context.fixed, TRUE, 0);

		gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, TRUE, 0);

		if (context.option)
		{
			context.option->widget_type = old;
			context.option->widget = slide;
		}
	}
	else if (old == PARSE_MENU)
	{
		GtkWidget	*menu;
		GList		*list, *kids;
		int		min_w = 4, min_h = 4;

		menu = gtk_option_menu_get_menu(
				GTK_OPTION_MENU(context.option_menu));
		list = kids = gtk_container_children(GTK_CONTAINER(menu));

		while (kids)
		{
			GtkWidget	*item = (GtkWidget *) kids->data;
			GtkRequisition	req;

			gtk_widget_size_request(item, &req);
			if (req.width > min_w)
				min_w = req.width;
			if (req.height > min_h)
				min_h = req.height;
			
			kids = kids->next;
		}

		g_list_free(list);

		gtk_widget_set_usize(context.option_menu,
				min_w + 50,	/* Else widget doesn't work! */
				min_h + 4);

		if (context.option)
		{
			context.option->widget_type = old;
			context.option->widget = context.option_menu;
		}
	}
	else if (old == PARSE_TOOLS)
	{
		int		i = 0;
		GtkWidget	*hbox, *tool;

		hbox = gtk_hbox_new(FALSE, 0);

		while ((tool = toolbar_tool_option(i++)))
			gtk_box_pack_start(GTK_BOX(hbox), tool, FALSE, TRUE, 0);

		gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, TRUE, 0);

		if (context.option)
		{
			context.option->widget_type = old;
			context.option->widget = hbox;
		}
	}
	
	context.state = g_list_remove(context.state, GINT_TO_POINTER(old));
	g_string_truncate(context.data, 0);
}

static void characters(void *user_data, const CHAR *ch, int len)
{
	guchar	*tmp;

	tmp = g_strndup(ch, len);
	g_string_append(context.data, tmp);
	g_free(tmp);
}

static xmlEntityPtr getEntity(void *user_data, const CHAR *name) {
	return xmlGetPredefinedEntity(name);
}

/* Parse ROX-Filer/Options.xml to create the options window.
 * Sets the global 'window' variable.
 */
static void build_options_window(void)
{
	xmlSAXHandler 	sax = {
		NULL,	/* internalSubset */
		NULL,	/* isStandalone */
		NULL,	/* hasInternalSubset */
		NULL,	/* hasExternalSubset */
		NULL,	/* resolveEntity */
		getEntity,
		NULL,	/* entityDecl */
		NULL,	/* notationDecl */
		NULL,	/* attributeDecl */
		NULL,	/* elementDecl */
		NULL,	/* unparsedEntityDecl */
		NULL,	/* setDocumentLocator */
		NULL,	/* startDocument */
		NULL,	/* endDocument */
		startElement,
		endElement,
		NULL,	/* reference */
		characters,
		NULL,	/* ignorableWhitespace */
		NULL,	/* processingInstruction */
		NULL,	/* comment */
		NULL,	/* warning */
		NULL,	/* error */
		NULL,	/* fatalError */
	};

	window = build_frame(&context);

	context.state = g_list_prepend(NULL, PARSE_TOPLEVEL);
	context.label = NULL;
	context.section = NULL;
	context.value = NULL;
	context.data = g_string_new(NULL);

	xmlSAXParseFile(&sax, make_path(app_dir, "Options.xml")->str, 0);
	if ((ParserState) context.state->data != PARSE_TOPLEVEL)
		g_warning("Failed to parse Options.xml file!\n");

	g_free(context.section);
	g_free(context.label);
	g_free(context.radio_group);
	g_free(context.value);
	g_string_free(context.data, TRUE);
}

/* Creates the window and adds the various buttons to it.
 * context->vbox becomes the box to add sections to.
 */
static GtkWidget *build_frame(ParseContext *context)
{
	GtkWidget	*window;
	GtkWidget	*tl_vbox, *scrolled_area;
	GtkWidget	*border, *label;
	GtkWidget	*actions, *button;
	char		*string, *save_path;

	window = gtk_window_new(GTK_WINDOW_DIALOG);

	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_window_set_title(GTK_WINDOW(window), _("ROX-Filer options"));
	gtk_signal_connect(GTK_OBJECT(window), "delete_event",
			GTK_SIGNAL_FUNC(hide_dialog_event), window);
	gtk_container_set_border_width(GTK_CONTAINER(window), 4);
	gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);

	tl_vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(window), tl_vbox);

	scrolled_area = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_set_border_width(GTK_CONTAINER(scrolled_area), 4);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_area),
			GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_box_pack_start(GTK_BOX(tl_vbox), scrolled_area, TRUE, TRUE, 0);

	border = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(border), GTK_SHADOW_NONE);
	gtk_container_set_border_width(GTK_CONTAINER(border), 4);
	gtk_scrolled_window_add_with_viewport(
			GTK_SCROLLED_WINDOW(scrolled_area), border);

	context->vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(border), context->vbox);
	
	save_path = choices_find_path_save("...", PROJECT, FALSE);
	if (save_path)
	{
		string = g_strconcat(_("Choices will be saved as "),
					save_path,
					NULL);
		g_free(save_path);
		label = gtk_label_new(string);
		g_free(string);
	}
	else
		label = gtk_label_new(_("Choices saving is disabled by "
					"CHOICESPATH variable"));
	gtk_box_pack_start(GTK_BOX(tl_vbox), label, FALSE, TRUE, 0);

	actions = gtk_hbox_new(TRUE, 16);
	gtk_box_pack_start(GTK_BOX(tl_vbox), actions, FALSE, TRUE, 0);
	
	button = gtk_button_new_with_label(_("Save"));
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
	if (!save_path)
		gtk_widget_set_sensitive(button, FALSE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(save_options), (gpointer) BUTTON_SAVE);
	gtk_widget_grab_default(button);
	gtk_widget_grab_focus(button);

	button = gtk_button_new_with_label(_("OK"));
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(save_options), (gpointer) BUTTON_OK);

	button = gtk_button_new_with_label(_("Apply"));
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(save_options), (gpointer) BUTTON_APPLY);

	button = gtk_button_new_with_label(_("Cancel"));
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_hide), GTK_OBJECT(window));

	return window;
}

/* Process one line from the options file (\0 term'd).
 * Returns NULL on success, or a pointer to an error message.
 * The line is modified.
 */
static char *process_option_line(guchar *line)
{
	guchar 		*eq, *c;
	Option		*option;

	g_return_val_if_fail(option_hash != NULL, "No registered options!");

	eq = strchr(line, '=');
	if (!eq)
		return _("Missing '='");

	c = eq - 1;
	while (c > line && (*c == ' ' || *c == '\t'))
		c--;
	c[1] = '\0';
	c = eq + 1;
	while (*c == ' ' || *c == '\t')
		c++;

	option = g_hash_table_lookup(option_hash, line);
	if (!option)
		return NULL;	/* Unknown option - silently ignore */

	if (strcmp(option->value, line) == 0)
		return NULL;	/* Value unchanged */

	g_free(option->value);
	option->value = g_strdup(g_strstrip(c));
	if (option->changed_cb)
		option->changed_cb(option->value);

	return NULL;
}

/* Given the last radio button in the group, select whichever
 * radio button matches the given value.
 */
static void radio_group_set_value(GtkRadioButton *last, guchar *value)
{	
	GSList	*next;

	next = gtk_radio_button_group(last);
	while (next)
	{
		GtkToggleButton *button = (GtkToggleButton *) next->data;
		guchar	*val;

		val = gtk_object_get_data(GTK_OBJECT(button), "value");
		g_return_if_fail(val != NULL);

		if (strcmp(val, value) == 0)
		{
			gtk_toggle_button_set_active(button, TRUE);
			return;
		}
		
		next = next->next;
	}

	g_warning("Can't find radio button with value %s\n", value);
}

/* Given the last radio button in the group, return a copy of the
 * value for the selected radio item.
 */
static guchar *radio_group_get_value(GtkRadioButton *last)
{
	GSList	*next;

	next = gtk_radio_button_group(last);
	while (next)
	{
		GtkToggleButton *button = (GtkToggleButton *) next->data;

		if (gtk_toggle_button_get_active(button))
		{
			guchar	*val;

			val = gtk_object_get_data(GTK_OBJECT(button), "value");
			g_return_val_if_fail(val != NULL, NULL);

			return g_strdup(val);
		}
		
		next = next->next;
	}

	return NULL;
}

/* Select this item with this value */
static void option_menu_set(GtkOptionMenu *om, guchar *value)
{
	GtkWidget *menu;
	GList	  *list, *next;
	int	  i = 0;

	menu = gtk_option_menu_get_menu(om);
	list = gtk_container_children(GTK_CONTAINER(menu));

	for (next = list; next; next = next->next)
	{
		GtkObject	*item = (GtkObject *) next->data;
		guchar		*data;

		data = gtk_object_get_data(item, "value");
		g_return_if_fail(data != NULL);

		if (strcmp(data, value) == 0)
		{
			gtk_option_menu_set_history(om, i);
			break;
		}
		
		i++;
	}

	g_list_free(list);
}

/* Get the value (static) of the selected item */
static guchar *option_menu_get(GtkOptionMenu *om)
{
	GtkWidget *menu, *item;

	menu = gtk_option_menu_get_menu(om);
	item = gtk_menu_get_active(GTK_MENU(menu));

	return gtk_object_get_data(GTK_OBJECT(item), "value");
}

/* Called for each Option when Save/OK/Apply is clicked.
 * Update value and call the callback if changed.
 */
static void may_change_cb(gpointer key, gpointer value, gpointer data)
{
	Option		*option = (Option *) value;
	guchar		*new = NULL;
	GtkWidget	*widget = option->widget;
	GtkStyle	*style;

	switch (option->widget_type)
	{
		case PARSE_TOGGLE:
			new = g_strdup_printf("%d",
				gtk_toggle_button_get_active(
					GTK_TOGGLE_BUTTON(widget)));
			break;
		case PARSE_ENTRY:
			new = gtk_editable_get_chars(GTK_EDITABLE(widget),
					0, -1);
			break;
		case PARSE_SLIDER:
			new = g_strdup_printf("%f",
				gtk_range_get_adjustment(
					GTK_RANGE(widget))->value);
			break;
		case PARSE_RADIO_GROUP:
			new = radio_group_get_value(GTK_RADIO_BUTTON(widget));
			break;
		case PARSE_MENU:
			new = g_strdup(option_menu_get(
						GTK_OPTION_MENU(widget)));
			break;
		case PARSE_COLOUR:
			style = GTK_BIN(widget)->child->style;

			new = g_strdup_printf("#%04x%04x%04x",
					style->bg[GTK_STATE_NORMAL].red,
					style->bg[GTK_STATE_NORMAL].green,
					style->bg[GTK_STATE_NORMAL].blue);
			break;
		case PARSE_TOOLS:
			new = tools_to_list(widget);
			break;
		default:
			g_print("[ unknown widget for change '%s' ]\n",
					(guchar *) key);
			break;
	}

	g_return_if_fail(new != NULL);

	if (strcmp(option->value, new) == 0)
	{
		g_free(new);
		return;
	}

	g_free(option->value);
	option->value = new;

	if (option->changed_cb)
		option->changed_cb(option->value);
}

static void save_cb(gpointer key, gpointer value, gpointer data)
{
	int	len;
	guchar	*tmp;
	guchar	*name = (guchar *) key;
	Option	*option = (Option *) value;
	FILE	*stream = (FILE *) data;

	if (!option->save)
		return;

	tmp = g_strdup_printf("%s = %s\n", name, option->value);
	len = strlen(tmp);

	if (fwrite(tmp, sizeof(char), len, stream) < len)
		delayed_error(_("Saving options"), g_strerror(errno));

	g_free(tmp);
}

static void save_options(GtkWidget *widget, gpointer data)
{
	GList		*next;
	int		button = (int) data;

	g_hash_table_foreach(option_hash, may_change_cb, NULL);

	for (next = notify_callbacks; next; next = next->next)
	{
		OptionNotify *cb = (OptionNotify *) next->data;

		cb();
	}

	if (button == BUTTON_SAVE)
	{
		guchar	*path;
		FILE	*file;

		path = choices_find_path_save("Options", PROJECT, TRUE);
		if (!path)
		{
			delayed_error(_("Saving options"),
				      _("Choices saving is disabled by "
					"CHOICESPATH variable"));
			return;
		}
		
		file = fopen(path, "wb");
		g_free(path);
		
		g_hash_table_foreach(option_hash, save_cb, file);

		if (fclose(file) == EOF)
			delayed_error(_("Saving options"), g_strerror(errno));

		for (next = saver_callbacks; next; next = next->next)
		{
			OptionNotify *cb = (OptionNotify *) next->data;
			cb();
		}
	}

	if (button != BUTTON_APPLY)
		gtk_widget_hide(window);
}

/* Shade the listed tools; unshade the rest. */
static void disable_tools(GtkWidget *hbox, guchar *list)
{
	GList	*next, *kids;

	kids = gtk_container_children(GTK_CONTAINER(hbox));

	for (next = kids; next; next = next->next)
	{
		GtkWidget	*kid = (GtkWidget *) next->data;
		guchar		*name;

		name = gtk_object_get_data(GTK_OBJECT(kid), "tool_name");

		g_return_if_fail(name != NULL);
		
		gtk_widget_set_sensitive(GTK_BIN(kid)->child,
					 !in_list(name, list));
	}

	g_list_free(kids);
}

/* Return a list of the shaded tools */
static guchar *tools_to_list(GtkWidget *hbox)
{
	GList	*next, *kids;
	GString	*list;
	guchar	*retval;

	list = g_string_new(NULL);

	kids = gtk_container_children(GTK_CONTAINER(hbox));

	for (next = kids; next; next = next->next)
	{
		GtkObject	*kid = (GtkObject *) next->data;
		guchar		*name;

		if (!GTK_WIDGET_SENSITIVE(GTK_BIN(kid)->child))
		{
			name = gtk_object_get_data(kid, "tool_name");
			g_return_val_if_fail(name != NULL, list->str);

			if (list->len)
				g_string_append(list, ", ");
			g_string_append(list, name);
		}
	}

	g_list_free(kids);
	retval = list->str;
	g_string_free(list, FALSE);

	return retval;
}

/* Make the widget reflect the current value of the option */
static void update_cb(gpointer key, gpointer value, gpointer data)
{
	Option		*option = (Option *) value;
	GtkWidget	*widget = option->widget;
	GdkColor	colour;

	g_return_if_fail(option->widget != NULL);

	switch (option->widget_type)
	{
		case PARSE_TOGGLE:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),
					atoi(option->value));
			break;
		case PARSE_ENTRY:
			gtk_entry_set_text(GTK_ENTRY(widget), option->value);
			break;
		case PARSE_RADIO_GROUP:
			radio_group_set_value(GTK_RADIO_BUTTON(widget),
						option->value);
			break;
		case PARSE_SLIDER:
			gtk_adjustment_set_value(
				gtk_range_get_adjustment(GTK_RANGE(widget)),
				atoi(option->value));
			break;
		case PARSE_MENU:
			option_menu_set(GTK_OPTION_MENU(widget), option->value);
			break;
		case PARSE_COLOUR:
			gdk_color_parse(option->value, &colour);
			button_patch_set_colour(widget, &colour);
			break;
		case PARSE_TOOLS:
			disable_tools(widget, option->value);
			break;
		default:
			g_print("Unknown widget for update '%s'\n",
					(guchar *) key);
			break;
	}
}

/* Reflect the values in the Option structures by changing the widgets
 * in the Options window.
 */
static void update_option_widgets(void)
{
	g_hash_table_foreach(option_hash, update_cb, NULL);
}
