/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2002, the ROX-Filer team.
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
 * On startup:
 *
 * - The <Choices>/PROJECT/Options file is read in. Each line
 *   is a name/value pair, and these are stored in the 'loading' hash table.
 *
 * - Each part of the filer then calls option_add_int(), or a related function,
 *   supplying the name for the option and a default value. Once an option is
 *   registered, it is removed from the loading table.
 *   Update callbacks are called if the default value isn't used.
 *
 * - option_register_widget() can be used during initialisation (any time
 *   before the Options box is displayed) to tell the system how to render a
 *   particular type of option.
 *
 * - All notify callbacks are called.
 *
 * When the user opens the Options box:
 *
 * - The Options.xml file is read and used to create the Options dialog box.
 *   Each element in the file has a key corresponding to an option named
 *   above.
 *
 * - For each widget in the box, the current value of the option is used to
 *   set the widget's state.
 *
 * When the user clicks Save/OK/Apply:
 *
 * - The state of each widget is copied to the options values.
 *
 * - The update callbacks are called for those values which have changed.
 *
 * - If Save or OK was clicked then the box is also closed.
 *
 * - If Save was clicked then the options are written to the filesystem and the
 *   saver_callbacks are called.
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

/* Add all option tooltips to this group */
static GtkTooltips *option_tooltips = NULL;
#define OPTION_TIP(widget, tip)	\
	gtk_tooltips_set_tip(option_tooltips, widget, tip, NULL)

/* The Options window. NULL if not yet created. */
static GtkWidget *window = NULL;

enum {BUTTON_SAVE, BUTTON_OK, BUTTON_APPLY};

/* "filer_unique" -> (Option *) */
static GHashTable *option_hash = NULL;

/* A mapping (name -> value) for options which have been loaded by not
 * yet registered. The options in this table cannot be used until
 * option_add_*() is called to move them into option_hash.
 */
static GHashTable *loading = NULL;

/* A mapping (XML name -> OptionBuildFn). When reading the Options.xml
 * file, this table gives the function used to create the widgets.
 */
static GHashTable *widget_builder = NULL;

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
static Option *new_option(guchar *key, OptionChanged *changed, guchar *def);
static void button_patch_set_colour(GtkWidget *button, GdkColor *color);
static void option_add(Option *option, guchar *key, OptionChanged *changed);

static GList *build_toggle(OptionUI *ui, xmlNode *node, guchar *label);
static GList *build_slider(OptionUI *ui, xmlNode *node, guchar *label);
static GList *build_entry(OptionUI *ui, xmlNode *node, guchar *label);
static GList *build_radio_group(OptionUI *ui, xmlNode *node, guchar *label);
static GList *build_colour(OptionUI *ui, xmlNode *node, guchar *label);
static GList *build_menu(OptionUI *ui, xmlNode *node, guchar *label);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void options_init(void)
{
	char	*path;

	loading = g_hash_table_new(g_str_hash, g_str_equal);
	option_hash = g_hash_table_new(g_str_hash, g_str_equal);
	widget_builder = g_hash_table_new(g_str_hash, g_str_equal);

	path = choices_find_path_load("Options", PROJECT);
	if (path)
	{
		/* Load in all the options set in the filer, storing them
		 * temporarily in the loading hash table.
		 * They get moved to option_hash when they're registered.
		 */
		parse_file(path, process_option_line);
		g_free(path);
	}

	option_register_widget("toggle", build_toggle);
	option_register_widget("slider", build_slider);
	option_register_widget("entry", build_entry);
	option_register_widget("radio-group", build_radio_group);
	option_register_widget("colour", build_colour);
	option_register_widget("menu", build_menu);
}

/* When parsing the XML file, process an element named 'name' by
 * calling 'builder(ui, xml_node, label)'.
 * builder returns the new widgets to add to the options box.
 * 'name' should be a static string.
 *
 * Functions to set or get the widget's state can be stored in 'ui'.
 * If the option doesn't have a name attribute in Options.xml then
 * ui will be NULL on entry (this is used for buttons).
 */
void option_register_widget(char *name, OptionBuildFn builder)
{
	g_hash_table_insert(widget_builder, name, builder);
}

/* Call all the notify callbacks. This should happen after any options
 * have their values changed. Set each has_changed before calling.
 */
void options_notify(void)
{
	GList	*next;

	for (next = notify_callbacks; next; next = next->next)
	{
		OptionNotify *cb = (OptionNotify *) next->data;

		cb();
	}
}

void options_show(void)
{
	if (!option_tooltips)
		option_tooltips = gtk_tooltips_new();

	if (g_hash_table_size(loading) != 0)
	{
		g_printerr(PROJECT ": Some options loaded but not used:\n");
		g_hash_table_foreach(loading, (GHFunc) puts, NULL);
	}

	if (window)
		gtk_widget_destroy(window);

	build_options_window();

	update_option_widgets();
	
	gtk_widget_show_all(window);
}

/* Option should contain the default value.
 * It must never be destroyed after being registered (Options are typically
 * statically allocated).
 * The key corresponds to the option's name in Options.xml, and to the key
 * in the saved options file.
 * 'changed' is called whenever the value changes.
 *
 * On exit, the value will have been updated to the loaded value, if
 * different to the default.
 * XXX: Still call changed?
 */
static void option_add(Option *option, guchar *key, OptionChanged *changed)
{
	gpointer okey, value;

	g_return_if_fail(option_hash != NULL);
	g_return_if_fail(g_hash_table_lookup(option_hash, key) == NULL);
	g_return_if_fail(option->value != NULL);
	
	option->save = TRUE;	/* Save by default */
	option->ui = NULL;
	option->changed_cb = changed;
	option->has_changed = FALSE;

	g_hash_table_insert(option_hash, key, option);

	/* Use the value loaded from the file, if any */
	if (g_hash_table_lookup_extended(loading, key, &okey, &value))
	{
		option->has_changed = strcmp(option->value, value) != 0;
			
		g_free(option->value);
		option->value = value;
		option->int_value = atoi(value);
		g_hash_table_remove(loading, key);
		g_free(okey);

		if (changed && option->has_changed)
			changed(option->value);
	}
}

/* Initialise and register a new integer option */
void option_add_int(Option *option, guchar *key,
		    int value, OptionChanged *changed)
{
	option->value = g_strdup_printf("%d", value);
	option->int_value = value;
	option_add(option, key, changed);
}

void option_add_string(guchar *key, guchar *value, OptionChanged *changed)
{
	new_option(key, changed, g_strdup(value));
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

/****************************************************************
 *                      INTERNAL FUNCTIONS                      *
 ****************************************************************/

/* 'def' is placed directly into the new Option. It will be g_free()d
 * automatically.
 */
static Option *new_option(guchar *key, OptionChanged *changed, guchar *def)
{
	Option	*option;
	gpointer okey, value;

	g_return_val_if_fail(option_hash != NULL, NULL);
	g_return_val_if_fail(g_hash_table_lookup(option_hash, key) == NULL,
									NULL);
	
	option = g_new(Option, 1);

	option->save = TRUE;	/* Save by default */
	option->ui = NULL;
	option->changed_cb = changed;

	g_hash_table_insert(option_hash, key, option);

	/* Use the value loaded from the file, if any */
	if (g_hash_table_lookup_extended(loading, key, &okey, &value))
	{
		option->value = value;
		g_hash_table_remove(loading, key);
		g_free(okey);

		if (changed && strcmp(def, option->value) != 0)
			changed(option->value);

		g_free(def);	/* Don't need the default */
	}
	else
		option->value = def;

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

static GtkWidget *build_radio(xmlNode *radio, GtkWidget *prev)
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

	report_error("%s", _(text));
}

static void build_widget(xmlNode *widget, GtkWidget *box)
{
	const char *name = widget->name;
	OptionBuildFn builder;
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

	if (oname)
	{
		option = g_hash_table_lookup(option_hash, oname);

		if (!option)
		{
			g_warning("No Option for '%s'!\n", oname);
			g_free(oname);
			return;
		}

		g_free(oname);
	}
	else
		option = NULL;

	builder = g_hash_table_lookup(widget_builder, name);
	if (builder)
	{
		GList *widgets, *next;

		if (option)
		{
			if (option->ui)
				g_warning("UI for option already exists!");
			else
			{
				option->ui = g_new(OptionUI, 1);
				option->ui->update_widget = NULL;
				option->ui->read_widget = NULL;
			}

			widgets = builder(option->ui, widget, label);
		}
		else
			widgets = builder(NULL, widget, label);

		for (next = widgets; next; next = next->next)
		{
			GtkWidget *w = (GtkWidget *) next->data;
			gtk_box_pack_start(GTK_BOX(box), w, FALSE, TRUE, 0);
		}
		g_list_free(widgets);
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
		report_error("Internal error: Options.xml unreadable");
		return;
	}

	build_sections(xmlDocGetRootElement(options_doc), sections_box);

	xmlFreeDoc(options_doc);
	options_doc = NULL;
}

static void null_ui(gpointer key, gpointer value, gpointer data)
{
	Option	*option = (Option *) value;

	g_return_if_fail(option->ui != NULL);

	g_free(option->ui);
	option->ui = NULL;
}

static void options_destroyed(GtkWidget *widget, gpointer data)
{
	if (widget == window)
	{
		window = NULL;

		g_hash_table_foreach(option_hash, null_ui, NULL);
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
		string = g_strdup_printf(_("Choices will be saved as %s"),
					save_path);
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
	char		*name = line;

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

	if (g_hash_table_lookup(loading, name))
		return "Duplicate option found!";

	g_hash_table_insert(loading, g_strdup(name), g_strdup(g_strstrip(c)));

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
	Option *option = (Option *) value;
	guchar		*new = NULL;

	g_return_if_fail(option != NULL);
	g_return_if_fail(option->ui != NULL);

	if (option->ui->read_widget)
		new = option->ui->read_widget(option->ui);
	else
		return;

	g_return_if_fail(new != NULL);

	option->has_changed = strcmp(option->value, new) != 0;

	if (!option->has_changed)
	{
		g_free(new);
		return;
	}

	g_free(option->value);
	option->value = new;
	option->int_value = atoi(new);

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
		delayed_error(_("Could not save options: %s"),
				  g_strerror(errno));

	g_free(tmp);
}

static void save_options(GtkWidget *widget, gpointer data)
{
	GList		*next;
	int		button = (int) data;

	/* Updates every value, and sets or clears has_changed */
	g_hash_table_foreach(option_hash, may_change_cb, NULL);

	options_notify();

	if (button == BUTTON_SAVE)
	{
		guchar	*path;
		FILE	*file;

		path = choices_find_path_save("Options", PROJECT, TRUE);
		if (!path)
		{
		        delayed_error(_("Could not save options: %s"),
				          _("Choices saving is disabled by "
					  "CHOICESPATH variable"));
			return;
		}
		
		file = fopen(path, "wb");
		g_free(path);
		
		g_hash_table_foreach(option_hash, save_cb, file);

		if (fclose(file) == EOF)
			delayed_error(_("Could not save options: %s"),
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

/* Make the widget reflect the current value of the option */
static void update_cb(gpointer key, gpointer value, gpointer data)
{
	Option *option = (Option *) value;

	g_return_if_fail(option != NULL);
	g_return_if_fail(option->ui != NULL);

	if (option->ui->update_widget)
		option->ui->update_widget(option->ui, option->value);
}

/* Reflect the values in the Option structures by changing the widgets
 * in the Options window.
 */
static void update_option_widgets(void)
{
	g_hash_table_foreach(option_hash, update_cb, NULL);
}

/* Each of the following update the widget to make it show the current
 * value of the option.
 */

static void update_toggle(OptionUI *ui, guchar *value)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->widget),
			atoi(value));
}

static void update_entry(OptionUI *ui, guchar *value)
{
	gtk_entry_set_text(GTK_ENTRY(ui->widget), value);
}

static void update_radio_group(OptionUI *ui, guchar *value)
{
	radio_group_set_value(GTK_RADIO_BUTTON(ui->widget), value);
}

static void update_slider(OptionUI *ui, guchar *value)
{
	gtk_adjustment_set_value(
		gtk_range_get_adjustment(GTK_RANGE(ui->widget)), atoi(value));
}

static void update_menu(OptionUI *ui, guchar *value)
{
	option_menu_set(GTK_OPTION_MENU(ui->widget), value);
}

static void update_colour(OptionUI *ui, guchar *value)
{
	GdkColor colour;

	gdk_color_parse(value, &colour);
	button_patch_set_colour(ui->widget, &colour);
}

/* Each of these read_* calls get the new (string) value of an option
 * from the widget.
 */

static guchar *read_toggle(OptionUI *ui)
{
	return g_strdup_printf("%d",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->widget)));
}

static guchar *read_entry(OptionUI *ui)
{
	return gtk_editable_get_chars(GTK_EDITABLE(ui->widget), 0, -1);
}

static guchar *read_slider(OptionUI *ui)
{
	return g_strdup_printf("%f",
			gtk_range_get_adjustment(GTK_RANGE(ui->widget))->value);
}

static guchar *read_radio_group(OptionUI *ui)
{
	return radio_group_get_value(GTK_RADIO_BUTTON(ui->widget));
}

static guchar *read_menu(OptionUI *ui)
{
	return g_strdup(option_menu_get(GTK_OPTION_MENU(ui->widget)));
}

static guchar *read_colour(OptionUI *ui)
{
	GtkStyle *style = GTK_BIN(ui->widget)->child->style;

	return g_strdup_printf("#%04x%04x%04x",
			style->bg[GTK_STATE_NORMAL].red,
			style->bg[GTK_STATE_NORMAL].green,
			style->bg[GTK_STATE_NORMAL].blue);
}

/* These create new widgets in the options window and set the appropriate
 * callbacks.
 */

static GList *build_toggle(OptionUI *ui, xmlNode *node, guchar *label)
{
	GtkWidget	*toggle;

	g_return_val_if_fail(ui != NULL, NULL);

	toggle = gtk_check_button_new_with_label(_(label));

	may_add_tip(toggle, node);

	ui->update_widget = update_toggle;
	ui->read_widget = read_toggle;
	ui->widget = toggle;

	return g_list_append(NULL, toggle);
}

static GList *build_slider(OptionUI *ui, xmlNode *node, guchar *label)
{
	GtkAdjustment *adj;
	GtkWidget *hbox, *slide;
	int	min, max;
	int	fixed;
	int	showvalue;

	g_return_val_if_fail(ui != NULL, NULL);

	min = get_int(node, "min");
	max = get_int(node, "max");
	fixed = get_int(node, "fixed");
	showvalue = get_int(node, "showvalue");

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

	may_add_tip(slide, node);

	gtk_box_pack_start(GTK_BOX(hbox), slide, !fixed, TRUE, 0);

	ui->update_widget = update_slider;
	ui->read_widget = read_slider;
	ui->widget = slide;

	return g_list_append(NULL, hbox);
}

static GList *build_entry(OptionUI *ui, xmlNode *node, guchar *label)
{
	GtkWidget	*hbox;
	GtkWidget	*entry;

	g_return_val_if_fail(ui != NULL, NULL);

	hbox = gtk_hbox_new(FALSE, 4);

	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_(label)),
				FALSE, TRUE, 0);

	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
	may_add_tip(entry, node);

	ui->update_widget = update_entry;
	ui->read_widget = read_entry;
	ui->widget = entry;

	return g_list_append(NULL, hbox);
}

static GList *build_radio_group(OptionUI *ui, xmlNode *node, guchar *label)
{
	GList		*list = NULL;
	GtkWidget	*button = NULL;
	xmlNode		*rn;

	g_return_val_if_fail(ui != NULL, NULL);

	for (rn = node->xmlChildrenNode; rn; rn = rn->next)
	{
		if (rn->type == XML_ELEMENT_NODE)
		{
			button = build_radio(rn, button);
			list = g_list_append(list, button);
		}
	}

	ui->update_widget = update_radio_group;
	ui->read_widget = read_radio_group;
	ui->widget = button;

	return list;
}

static GList *build_colour(OptionUI *ui, xmlNode *node, guchar *label)
{
	GtkWidget	*hbox, *da, *button;
	int		lpos;
	
	g_return_val_if_fail(ui != NULL, NULL);

	/* lpos gives the position for the label 
	 * 0: label comes before the button
	 * non-zero: label comes after the button
	 */
	lpos = get_int(node, "lpos");

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

	may_add_tip(button, node);
	
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
	if (lpos)
		gtk_box_pack_start(GTK_BOX(hbox),
			gtk_label_new(_(label)),
			FALSE, TRUE, 0);

	ui->update_widget = update_colour;
	ui->read_widget = read_colour;
	ui->widget = button;

	return g_list_append(NULL, hbox);
}

static GList *build_menu(OptionUI *ui, xmlNode *node, guchar *label)
{
	GtkWidget	*hbox, *om, *option_menu;
	xmlNode		*item;
	GtkWidget	*menu;
	GList		*list, *kids;
	int		min_w = 4, min_h = 4;

	g_return_val_if_fail(ui != NULL, NULL);

	hbox = gtk_hbox_new(FALSE, 4);

	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_(label)),
			FALSE, TRUE, 0);

	option_menu = gtk_option_menu_new();
	gtk_box_pack_start(GTK_BOX(hbox), option_menu, FALSE, TRUE, 0);

	om = gtk_menu_new();
	gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu), om);

	for (item = node->xmlChildrenNode; item; item = item->next)
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
			min_w + 50,	/* Else node doesn't work! */
			min_h + 4);

	ui->update_widget = update_menu;
	ui->read_widget = read_menu;
	ui->widget = option_menu;

	return g_list_append(NULL, hbox);
}

static void button_patch_set_colour(GtkWidget *button, GdkColor *colour)
{
	GtkStyle   	*style;
	GtkWidget	*patch;

	patch = GTK_BIN(button)->child;

	style = gtk_style_copy(GTK_WIDGET(patch)->style);
	style->bg[GTK_STATE_NORMAL].red = colour->red;
	style->bg[GTK_STATE_NORMAL].green = colour->green;
	style->bg[GTK_STATE_NORMAL].blue = colour->blue;
	gtk_widget_set_style(patch, style);
	gtk_style_unref(style);

	if (GTK_WIDGET_REALIZED(patch))
		gdk_window_clear(patch->window);
}
