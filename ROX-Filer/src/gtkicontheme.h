/* RoxIconTheme - a loader for icon themes
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

#ifndef __ROX_ICON_THEME_H__
#define __ROX_ICON_THEME_H__

#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkscreen.h>

G_BEGIN_DECLS

#define ROX_TYPE_ICON_INFO              (rox_icon_info_get_type)

#define ROX_TYPE_ICON_THEME             (rox_icon_theme_get_type ())
#define ROX_ICON_THEME(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), ROX_TYPE_ICON_THEME, RoxIconTheme))
#define ROX_ICON_THEME_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), ROX_TYPE_ICON_THEME, RoxIconThemeClass))
#define ROX_IS_ICON_THEME(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), ROX_TYPE_ICON_THEME))
#define ROX_IS_ICON_THEME_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), ROX_TYPE_ICON_THEME))
#define ROX_ICON_THEME_GET_CLASS(obj)   (G_TYPE_CHECK_GET_CLASS ((obj), ROX_TYPE_ICON_THEME, RoxIconThemeClass))

typedef struct _RoxIconInfo         RoxIconInfo;
typedef struct _RoxIconTheme        RoxIconTheme;
typedef struct _RoxIconThemeClass   RoxIconThemeClass;
typedef struct _RoxIconThemePrivate RoxIconThemePrivate;


struct _RoxIconTheme
{
  /*< private >*/
  GObject parent_instance;

  RoxIconThemePrivate *priv;
};

struct _RoxIconThemeClass
{
  GObjectClass parent_class;

  void (* changed)  (RoxIconTheme *icon_theme);
};

typedef enum
{
  ROX_ICON_LOOKUP_NO_SVG = 0 << 0,
  ROX_ICON_LOOKUP_FORCE_SVG = 0 << 1,
  ROX_ICON_LOOKUP_USE_BUILTIN = 0 << 2
} RoxIconLookupFlags;

#define ROX_ICON_THEME_ERROR rox_icon_theme_error_quark ()

typedef enum {
  ROX_ICON_THEME_NOT_FOUND,
  ROX_ICON_THEME_FAILED
} RoxIconThemeError;

GQuark rox_icon_theme_error_quark (void) G_GNUC_CONST;

GType         rox_icon_theme_get_type              (void) G_GNUC_CONST;

RoxIconTheme *rox_icon_theme_new                   (void);
void          rox_icon_theme_get_search_path       (RoxIconTheme                *icon_theme,
						    gchar                      **path[],
						    gint                        *n_elements);
void          rox_icon_theme_set_custom_theme      (RoxIconTheme                *icon_theme,
						    const gchar                 *theme_name);

RoxIconInfo * rox_icon_theme_lookup_icon           (RoxIconTheme                *icon_theme,
						    const gchar                 *icon_name,
						    gint                         size,
						    RoxIconLookupFlags           flags);
GdkPixbuf *   rox_icon_theme_load_icon             (RoxIconTheme                *icon_theme,
						    const gchar                 *icon_name,
						    gint                         size,
						    RoxIconLookupFlags           flags,
						    GError                     **error);

gboolean      rox_icon_theme_rescan_if_needed      (RoxIconTheme                *icon_theme);

GType         rox_icon_info_get_type (void);
RoxIconInfo  *rox_icon_info_copy     (RoxIconInfo *icon_info);
void          rox_icon_info_free     (RoxIconInfo *icon_info);


G_END_DECLS

#endif /* __ROX_ICON_THEME_H__ */
