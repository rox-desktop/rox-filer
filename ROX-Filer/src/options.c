/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2001, the ROX-Filer team.
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
 *   above.
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

/* The various widget types */
typedef enum {
	OPTION_MENU,
	OPTION_ITEM,
	OPTION_RADIO_GROUP,
	OPTION_TOGGLE,
	OPTION_ENTRY,
	OPTION_SLIDER,
	OPTION_COLOUR,
	OPTION_TOOLS,
	OPTION_BUTTON,
} OptionType;

struct _Option {
	GtkWidget	*widget;	/* NULL => No widget yet */
	OptionType	widget_type;
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
static GtkWidget *build_frame(void);
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
	if (window)
		gtk_widget_destroy(window);

	build_options_window();

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

void option_add_void(gchar *key, OptionChanged *changed)
{
	Option	*option;

	option = new_option(key, changed);
	option->save = FALSE;
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

static GtkColorSelectionDialog *current_csel_box = NULL;

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

/* These are used during parsing... */
static guchar *section_name = NULL;
static xmlDocPtr options_doc = NULL;

#define DATA(node) (xmlNodeListGetString(options_doc, node->xmlChildrenNode, 1))

static void may_add_tip(GtkWidget *widget, xmlNode *element)
{
	guchar	*data, *tip;

	data = DATA(element);
	if (!data)
		return;

	tip = g_strstrip(g_strdup(data));
	g_free(data);
	if (*tip)
		OPTION_TIP(widget, _(tip));
	g_free(tip);
}

static int get_int(xmlNode *node, guchar *attr)
{
	guchar *txt;
	int	retval;

	txt = xmlGetProp(node, attr);
	if (!txt)
		return 0;

	retval = atoi(txt);
	g_free(txt);

	return retval;
}

static GtkWidget *build_radio(xmlNode *radio, GtkWidget *box, GtkWidget *prev)
{
	GtkWidget	*button;
	GtkRadioButton	*prev_button = (GtkRadioButton *) prev;
	guchar		*label;

	label = xmlGetProp(radio, "label");

	button = gtk_radio_button_new_with_label(
			prev_button ? gtk_radio_button_group(prev_button)
				    : NULL,
			_(label));
	g_free(label);

	gtk_box_pack_start(GTK_BOX(box), button, FALSE, TRUE, 0);
	may_add_tip(button, radio);

	gtk_object_set_data(GTK_OBJECT(button), "value",
			xmlGetProp(radio, "value"));

	return button;
}

static void build_menu_item(xmlNode *node, GtkWidget *option_menu)
{
	GtkWidget	*item, *menu;
	guchar		*label;

	g_return_if_fail(strcmp(node->name, "item") == 0);

	label = xmlGetProp(node, "label");
	item = gtk_menu_item_new_with_label(_(label));
	g_free(label);

	menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(option_menu));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_widget_show_all(menu);

	gtk_object_set_data(GTK_OBJECT(item),
				"value", xmlGetProp(node, "value"));
}

static void show_notice(GtkObject *button)
{
	guchar *text;

	text = gtk_object_get_data(button, "notice_text");
	g_return_if_fail(text != NULL);

	report_error(_("Notice"), _(text));
}

static void button_click(Option *option)
{
	if (option->changed_cb)
		option->changed_cb(NULL);
}

static void build_widget(xmlNode *widget, GtkWidget *box)
{
	const char *name = widget->name;
	guchar	*oname;
	Option	*option;
	guchar	*label;

	if (strcmp(name, "label") == 0)
	{
		GtkWidget *label;
		guchar *text;

		text = DATA(widget);
		label = gtk_label_new(_(text));
		g_free(text);

		gtk_misc_set_alignment(GTK_MISC(label), 0, 1);
		gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
		gtk_box_pack_start(GTK_BOX(box), label, FALSE, TRUE, 0);
		return;
	}
	else if (strcmp(name, "spacer") == 0)
	{
		GtkWidget *eb;

		eb = gtk_event_box_new();
		gtk_widget_set_usize(eb, 12, 12);
		gtk_box_pack_start(GTK_BOX(box), eb, FALSE, TRUE, 0);
		return;
	}

	label = xmlGetProp(widget, "label");

	if (strcmp(name, "notice") == 0)
	{
		GtkWidget *button, *align;
		guchar *text;

		align = gtk_alignment_new(0.1, 0, 0.1, 0);
		gtk_box_pack_start(GTK_BOX(box), align, FALSE, TRUE, 0);
		button = gtk_button_new_with_label(_(label));
		gtk_container_add(GTK_CONTAINER(align), button);

		text = DATA(widget);

		gtk_object_set_data_full(GTK_OBJECT(button),
					"notice_text", text, g_free);
		gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(show_notice), GTK_OBJECT(button));


		g_free(label);
		return;
	}

	if (strcmp(name, "hbox") == 0 || strcmp(name, "vbox") == 0)
	{
		GtkWidget *nbox;
		xmlNode	  *hw;

		if (name[0] == 'h')
			nbox = gtk_hbox_new(FALSE, 4);
		else
			nbox = gtk_vbox_new(FALSE, 4);

		if (label)
			gtk_box_pack_start(GTK_BOX(nbox),
				gtk_label_new(_(label)), FALSE, TRUE, 4);
		gtk_box_pack_start(GTK_BOX(box), nbox, FALSE, TRUE, 0);

		for (hw = widget->xmlChildrenNode; hw; hw = hw->next)
		{
			if (hw->type == XML_ELEMENT_NODE)
				build_widget(hw, nbox);
		}

		g_free(label);
		return;
	}

	oname = xmlGetProp(widget, "name");
	g_return_if_fail(oname != NULL);

	{
		guchar	*tmp;

		tmp = g_strconcat(section_name, "_", oname, NULL);
		g_free(oname);
		option = g_hash_table_lookup(option_hash, tmp);

		if (!option)
		{
			g_warning("No Option for '%s'!\n", tmp);
			g_free(tmp);
			return;
		}

		g_free(tmp);
	}

	if (strcmp(name, "button") == 0)
	{
		GtkWidget *button, *align;

		align = gtk_alignment_new(0.1, 0, 0.1, 0);
		gtk_box_pack_start(GTK_BOX(box), align, FALSE, TRUE, 0);
		button = gtk_button_new_with_label(_(label));
		gtk_container_add(GTK_CONTAINER(align), button);

		option->widget_type = OPTION_BUTTON;
		option->widget = button;

		gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
				GTK_SIGNAL_FUNC(button_click), option);

	}
	else if (strcmp(name, "toggle") == 0)
	{
		GtkWidget	*toggle;

		toggle = gtk_check_button_new_with_label(_(label));
		
		gtk_box_pack_start(GTK_BOX(box), toggle, FALSE, TRUE, 0);
		may_add_tip(toggle, widget);

		option->widget_type = OPTION_TOGGLE;
		option->widget = toggle;
	}
	else if (strcmp(name, "slider") == 0)
	{
		GtkAdjustment *adj;
		GtkWidget *hbox, *slide;
		int	min, max;
		int	fixed;
		int	showvalue;

		min = get_int(widget, "min");
		max = get_int(widget, "max");
		fixed = get_int(widget, "fixed");
		showvalue = get_int(widget, "showvalue");

		adj = GTK_ADJUSTMENT(gtk_adjustment_new(0,
					min, max, 1, 10, 0));

		hbox = gtk_hbox_new(FALSE, 4);
		gtk_box_pack_start(GTK_BOX(hbox),
				gtk_label_new(_(label)),
				FALSE, TRUE, 0);

		slide = gtk_hscale_new(adj);

		if (fixed)
			gtk_widget_set_usize(slide, adj->upper, 24);
		if (showvalue)
		{
			gtk_scale_set_draw_value(GTK_SCALE(slide), TRUE);
			gtk_scale_set_value_pos(GTK_SCALE(slide),
						GTK_POS_LEFT);
			gtk_scale_set_digits(GTK_SCALE(slide), 0);
		}
		else 
			gtk_scale_set_draw_value(GTK_SCALE(slide), FALSE);
		GTK_WIDGET_UNSET_FLAGS(slide, GTK_CAN_FOCUS);

		may_add_tip(slide, widget);
		
		gtk_box_pack_start(GTK_BOX(hbox), slide, !fixed, TRUE, 0);

		gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, TRUE, 0);

		option->widget_type = OPTION_SLIDER;
		option->widget = slide;
	}
	else if (strcmp(name, "entry") == 0)
	{
		GtkWidget	*hbox;
		GtkWidget	*entry;

		hbox = gtk_hbox_new(FALSE, 4);

		gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_(label)),
					FALSE, TRUE, 0);

		entry = gtk_entry_new();
		gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
		may_add_tip(entry, widget);

		gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, TRUE, 0);

		option->widget_type = OPTION_ENTRY;
		option->widget = entry;
	}
	else if (strcmp(name, "radio-group") == 0)
	{
		GtkWidget	*button = NULL;
		xmlNode		*rn;

		for (rn = widget->xmlChildrenNode; rn; rn = rn->next)
		{
			if (rn->type == XML_ELEMENT_NODE)
				button = build_radio(rn, box, button);
		}

		option->widget_type = OPTION_RADIO_GROUP;
		option->widget = button;
	}
	else if (strcmp(name, "colour") == 0)
	{
		GtkWidget	*hbox, *da, *button;
		int		lpos;
		
		/* lpos gives the position for the label 
		 * 0: label comes before the button
		 * non-zero: label comes after the button
		 */
		lpos = get_int(widget, "lpos");

		hbox = gtk_hbox_new(FALSE, 4);
		if (lpos == 0)
			gtk_box_pack_start(GTK_BOX(hbox),
				gtk_label_new(_(label)),
				FALSE, TRUE, 0);

		button = gtk_button_new();
		da = gtk_drawing_area_new();
		gtk_drawing_area_size(GTK_DRAWING_AREA(da), 64, 12);
		gtk_container_add(GTK_CONTAINER(button), da);
		gtk_signal_connect(GTK_OBJECT(button), "clicked",
				GTK_SIGNAL_FUNC(open_coloursel), button);

		may_add_tip(button, widget);
		
		gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
		if (lpos)
			gtk_box_pack_start(GTK_BOX(hbox),
				gtk_label_new(_(label)),
				FALSE, TRUE, 0);

		gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, TRUE, 0);

		option->widget_type = OPTION_COLOUR;
		option->widget = button;
	}
	else if (strcmp(name, "menu") == 0)
	{
		GtkWidget	*hbox, *om, *option_menu;
		xmlNode		*item;
		GtkWidget	*menu;
		GList		*list, *kids;
		int		min_w = 4, min_h = 4;

		hbox = gtk_hbox_new(FALSE, 4);
		gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, TRUE, 0);

		gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_(label)),
				FALSE, TRUE, 0);

		option_menu = gtk_option_menu_new();
		gtk_box_pack_start(GTK_BOX(hbox), option_menu, FALSE, TRUE, 0);

		om = gtk_menu_new();
		gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu), om);

		for (item = widget->xmlChildrenNode; item; item = item->next)
		{
			if (item->type == XML_ELEMENT_NODE)
				build_menu_item(item, option_menu);
		}

		menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(option_menu));
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

		gtk_widget_set_usize(option_menu,
				min_w + 50,	/* Else widget doesn't work! */
				min_h + 4);

		option->widget_type = OPTION_MENU;
		option->widget = option_menu;
	}
	else if (strcmp(name, "tool-options") == 0)
	{
		int		i = 0;
		GtkWidget	*hbox, *tool;

		hbox = gtk_hbox_new(FALSE, 0);

		while ((tool = toolbar_tool_option(i++)))
			gtk_box_pack_start(GTK_BOX(hbox), tool, FALSE, TRUE, 0);

		gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, TRUE, 0);

		option->widget_type = OPTION_TOOLS;
		option->widget = hbox;
	}
	else
		g_warning("Unknown option type '%s'\n", name);

	g_free(label);
}


static void build_sections(xmlNode *options, GtkWidget *sections_box)
{
	xmlNode	*section = options->xmlChildrenNode;

	g_return_if_fail(strcmp(options->name, "options") == 0);

	for (; section; section = section->next)
	{
		guchar 		*title;
		GtkWidget   	*page, *scrolled_area;
		xmlNode		*widget;

		if (section->type != XML_ELEMENT_NODE)
			continue;

		title = xmlGetProp(section, "title");
		section_name = xmlGetProp(section, "name");
		page = gtk_vbox_new(FALSE, 0);
		gtk_container_set_border_width(GTK_CONTAINER(page), 4);

		scrolled_area = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(
					GTK_SCROLLED_WINDOW(scrolled_area),
					GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

		gtk_scrolled_window_add_with_viewport(
					GTK_SCROLLED_WINDOW(scrolled_area),
					page);

		gtk_notebook_append_page(GTK_NOTEBOOK(sections_box),
					scrolled_area,
					gtk_label_new(_(title)));

		widget = section->xmlChildrenNode;
		for (; widget; widget = widget->next)
		{
			if (widget->type == XML_ELEMENT_NODE)
				build_widget(widget, page);
		}

		g_free(title);
		g_free(section_name);
		section_name = NULL;
	}
}

/* Parse ROX-Filer/Options.xml to create the options window.
 * Sets the global 'window' variable.
 */
static void build_options_window(void)
{
	GtkWidget *sections_box;
	xmlDocPtr options_doc;

	sections_box = build_frame();
	
	options_doc = xmlParseFile(make_path(app_dir, "Options.xml")->str);
	if (!options_doc)
	{
		report_rox_error("Internal error: Options.xml unreadable");
		return;
	}

	build_sections(xmlDocGetRootElement(options_doc), sections_box);

	xmlFreeDoc(options_doc);
	options_doc = NULL;
}

static void null_widget(gpointer key, gpointer value, gpointer data)
{
	Option	*option = (Option *) value;

	option->widget = NULL;
}

static void options_destroyed(GtkWidget *widget, gpointer data)
{
	if (widget == window)
	{
		window = NULL;

		g_hash_table_foreach(option_hash, null_widget, NULL);
	}
}

/* Creates the window and adds the various buttons to it.
 * Returns the vbox to add sections to and sets the global
 * 'window'.
 */
static GtkWidget *build_frame(void)
{
	GtkWidget	*sections_box;
	GtkWidget	*tl_vbox;
	GtkWidget	*label;
	GtkWidget	*actions, *button;
	char		*string, *save_path;

	window = gtk_window_new(GTK_WINDOW_DIALOG);

	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_window_set_title(GTK_WINDOW(window), _("ROX-Filer options"));
	gtk_signal_connect(GTK_OBJECT(window), "destroy",
			GTK_SIGNAL_FUNC(options_destroyed), NULL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 4);
	gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);

	tl_vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(window), tl_vbox);

	sections_box = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(sections_box), TRUE);
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(sections_box), GTK_POS_LEFT);
	gtk_box_pack_start(GTK_BOX(tl_vbox), sections_box, TRUE, TRUE, 0);
	
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
		GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(window));

	return sections_box;
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
		case OPTION_TOGGLE:
			new = g_strdup_printf("%d",
				gtk_toggle_button_get_active(
					GTK_TOGGLE_BUTTON(widget)));
			break;
		case OPTION_ENTRY:
			new = gtk_editable_get_chars(GTK_EDITABLE(widget),
					0, -1);
			break;
		case OPTION_SLIDER:
			new = g_strdup_printf("%f",
				gtk_range_get_adjustment(
					GTK_RANGE(widget))->value);
			break;
		case OPTION_RADIO_GROUP:
			new = radio_group_get_value(GTK_RADIO_BUTTON(widget));
			break;
		case OPTION_MENU:
			new = g_strdup(option_menu_get(
						GTK_OPTION_MENU(widget)));
			break;
		case OPTION_COLOUR:
			style = GTK_BIN(widget)->child->style;

			new = g_strdup_printf("#%04x%04x%04x",
					style->bg[GTK_STATE_NORMAL].red,
					style->bg[GTK_STATE_NORMAL].green,
					style->bg[GTK_STATE_NORMAL].blue);
			break;
		case OPTION_TOOLS:
			new = tools_to_list(widget);
			break;
		default:
			g_printerr("[ unknown widget for change '%s' ]\n",
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
		delayed_rox_error(_("Could not save options: %s"),
				  g_strerror(errno));

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
		        delayed_rox_error(_("Could not save options: %s"),
				          _("Choices saving is disabled by "
					  "CHOICESPATH variable"));
			return;
		}
		
		file = fopen(path, "wb");
		g_free(path);
		
		g_hash_table_foreach(option_hash, save_cb, file);

		if (fclose(file) == EOF)
			delayed_rox_error(_("Could not save options: %s"),
					  g_strerror(errno));

		for (next = saver_callbacks; next; next = next->next)
		{
			OptionNotify *cb = (OptionNotify *) next->data;
			cb();
		}
	}

	if (button != BUTTON_APPLY)
		gtk_widget_destroy(window);
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
		case OPTION_TOGGLE:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),
					atoi(option->value));
			break;
		case OPTION_ENTRY:
			gtk_entry_set_text(GTK_ENTRY(widget), option->value);
			break;
		case OPTION_RADIO_GROUP:
			radio_group_set_value(GTK_RADIO_BUTTON(widget),
						option->value);
			break;
		case OPTION_SLIDER:
			gtk_adjustment_set_value(
				gtk_range_get_adjustment(GTK_RANGE(widget)),
				atoi(option->value));
			break;
		case OPTION_MENU:
			option_menu_set(GTK_OPTION_MENU(widget), option->value);
			break;
		case OPTION_COLOUR:
			gdk_color_parse(option->value, &colour);
			button_patch_set_colour(widget, &colour);
			break;
		case OPTION_TOOLS:
			disable_tools(widget, option->value);
			break;
	        case OPTION_BUTTON:
	                break;
		default:
			g_printerr("Unknown widget for update '%s'\n",
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
