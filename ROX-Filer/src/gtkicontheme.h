/* GtkIconTheme - a loader for icon themes
 * gtk-icon-loader.h Copyright (C) 2002, 2003 Red Hat, Inc.
 *
 * This was LGPL; it's now GPL, as allowed by the LGPL. It's also very
 * stripped down. GTK 2.4 will have this stuff built-in.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef __GTK_ICON_THEME_H__
#define __GTK_ICON_THEME_H__

#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkscreen.h>

G_BEGIN_DECLS

#define GTK_TYPE_ICON_INFO              (gtk_icon_info_get_type)

#define GTK_TYPE_ICON_THEME             (rox_icon_theme_get_type ())
#define GTK_ICON_THEME(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_ICON_THEME, GtkIconTheme))
#define GTK_ICON_THEME_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_ICON_THEME, GtkIconThemeClass))
#define GTK_IS_ICON_THEME(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_ICON_THEME))
#define GTK_IS_ICON_THEME_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_ICON_THEME))
#define GTK_ICON_THEME_GET_CLASS(obj)   (G_TYPE_CHECK_GET_CLASS ((obj), GTK_TYPE_ICON_THEME, GtkIconThemeClass))

typedef struct _GtkIconInfo         GtkIconInfo;
typedef struct _GtkIconTheme        GtkIconTheme;
typedef struct _GtkIconThemeClass   GtkIconThemeClass;
typedef struct _GtkIconThemePrivate GtkIconThemePrivate;


struct _GtkIconTheme
{
  /*< private >*/
  GObject parent_instance;

  GtkIconThemePrivate *priv;
};

struct _GtkIconThemeClass
{
  GObjectClass parent_class;

  void (* changed)  (GtkIconTheme *icon_theme);
};

/**
 * GtkIconLookupFlags:
 * @GTK_ICON_LOOKUP_NO_SVG: Never return SVG icons, even if gdk-pixbuf
 *   supports them. Cannot be used together with %GTK_ICON_LOOKUP_FORCE_SVG.
 * @GTK_ICON_LOOKUP_FORCE_SVG: Return SVG icons, even if gdk-pixbuf
 *   doesn't support them.
 *   Cannot be used together with %GTK_ICON_LOOKUP_NO_SVG.
 * @GTK_ICON_LOOKUP_USE_BUILTIN: When passed to
 *   rox_icon_theme_lookup_icon() includes builtin icons
 *   as well as files. For a builtin icon, gdk_icon_info_get_filename()
 *   returns %NULL and you need to call gdk_icon_info_get_builtin_pixbuf().
 * 
 * Used to specify options for rox_icon_theme_lookup_icon()
 **/
typedef enum
{
  GTK_ICON_LOOKUP_NO_SVG = 0 << 0,
  GTK_ICON_LOOKUP_FORCE_SVG = 0 << 1,
  GTK_ICON_LOOKUP_USE_BUILTIN = 0 << 2
} GtkIconLookupFlags;

#define GTK_ICON_THEME_ERROR rox_icon_theme_error_quark ()

/**
 * GtkIconThemeError:
 * @GTK_ICON_THEME_NOT_FOUND: The icon specified does not exist in the theme
 * @GTK_ICON_THEME_FAILED: An unspecified error occurred.
 * 
 * Error codes for GtkIconTheme operations.
 **/
typedef enum {
  GTK_ICON_THEME_NOT_FOUND,
  GTK_ICON_THEME_FAILED
} GtkIconThemeError;

GQuark rox_icon_theme_error_quark (void) G_GNUC_CONST;

GType         rox_icon_theme_get_type              (void) G_GNUC_CONST;

GtkIconTheme *rox_icon_theme_new                   (void);
void          rox_icon_theme_get_search_path       (GtkIconTheme                *icon_theme,
						    gchar                      **path[],
						    gint                        *n_elements);
void          rox_icon_theme_set_custom_theme      (GtkIconTheme                *icon_theme,
						    const gchar                 *theme_name);

GtkIconInfo * rox_icon_theme_lookup_icon           (GtkIconTheme                *icon_theme,
						    const gchar                 *icon_name,
						    gint                         size,
						    GtkIconLookupFlags           flags);
GdkPixbuf *   rox_icon_theme_load_icon             (GtkIconTheme                *icon_theme,
						    const gchar                 *icon_name,
						    gint                         size,
						    GtkIconLookupFlags           flags,
						    GError                     **error);

gboolean      rox_icon_theme_rescan_if_needed      (GtkIconTheme                *icon_theme);

GType         gtk_icon_info_get_type (void);
GtkIconInfo  *gtk_icon_info_copy     (GtkIconInfo *icon_info);
void          gtk_icon_info_free     (GtkIconInfo *icon_info);


G_END_DECLS

#endif /* __GTK_ICON_THEME_H__ */
