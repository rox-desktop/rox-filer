/* RoxIconTheme - a loader for icon themes
 * gtk-icon-theme.c Copyright (C) 2002, 2003 Red Hat, Inc.
 *
 * This was LGPL; it's now GPL, as allowed by the LGPL. It's also very
 * stripped down. GTK 2.4 will have this stuff built-in.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <glib.h>

#ifdef G_OS_WIN32
#ifndef S_ISDIR
#define S_ISDIR(mode) ((mode)&_S_IFDIR)
#endif
#endif /* G_OS_WIN32 */

#include "gtkicontheme.h"
#include "gtkiconthemeparser.h"
/* #include "gtkintl.h" */
#include <gtk/gtksettings.h>
#include <gtk/gtkprivate.h>

#define DEFAULT_THEME_NAME "hicolor"

static GdkPixbuf *rox_icon_info_load_icon(RoxIconInfo *icon_info,
							GError **error);

typedef struct _GtkIconData GtkIconData;

typedef enum
{
  ICON_THEME_DIR_FIXED,  
  ICON_THEME_DIR_SCALABLE,  
  ICON_THEME_DIR_THRESHOLD,
  ICON_THEME_DIR_UNTHEMED
} IconThemeDirType;

/* In reverse search order: */
typedef enum
{
  ICON_SUFFIX_NONE = 0,
  ICON_SUFFIX_XPM = 1 << 0,
  ICON_SUFFIX_SVG = 1 << 1,
  ICON_SUFFIX_PNG = 1 << 2,  
} IconSuffix;

struct _RoxIconThemePrivate
{
  guint custom_theme : 1;
  guint pixbuf_supports_svg : 1;
  
  char *current_theme;
  char **search_path;
  int search_path_len;

  gboolean themes_valid;
  /* A list of all the themes needed to look up icons.
   * In search order, without duplicates
   */
  GList *themes;
  GHashTable *unthemed_icons;

  /* Note: The keys of this hashtable are owned by the
   * themedir and unthemed hashtables.
   */
  GHashTable *all_icons;

  /* time when we last stat:ed for theme changes */
  long last_stat_time;
  GList *dir_mtimes;
};

struct _RoxIconInfo
{
  guint ref_count;

  /* Information about the source
   */
  gchar *filename;
  GdkPixbuf *builtin_pixbuf;

  GtkIconData *data;
  
  /* Information about the directory where
   * the source was found
   */
  IconThemeDirType dir_type;
  gint dir_size;
  gint threshold;

  /* Parameters influencing the scaled icon
   */
  gint desired_size;
  gboolean raw_coordinates;

  /* Cached information if we go ahead and try to load
   * the icon.
   */
  GdkPixbuf *pixbuf;
  GError *load_error;
  gdouble scale;
};

typedef struct
{
  char *name;
  char *display_name;
  char *comment;
  char *example;

  /* In search order */
  GList *dirs;
} IconTheme;

struct _GtkIconData
{
  gboolean has_embedded_rect;
  gint x0, y0, x1, y1;
  
  GdkPoint *attach_points;
  gint n_attach_points;

  gchar *display_name;
};

typedef struct
{
  IconThemeDirType type;
  GQuark context;

  int size;
  int min_size;
  int max_size;
  int threshold;

  char *dir;
  
  GHashTable *icons;
  GHashTable *icon_data;
} IconThemeDir;

typedef struct
{
  char *svg_filename;
  char *no_svg_filename;
} UnthemedIcon;

typedef struct
{
  gint size;
  GdkPixbuf *pixbuf;
} BuiltinIcon;

typedef struct 
{
  char *dir;
  time_t mtime; /* 0 == not existing or not a dir */
} IconThemeDirMtime;

static void  rox_icon_theme_class_init (RoxIconThemeClass    *klass);
static void  rox_icon_theme_init       (RoxIconTheme         *icon_theme);
static void  rox_icon_theme_finalize   (GObject              *object);
static void  theme_dir_destroy         (IconThemeDir         *dir);

static void         theme_destroy     (IconTheme        *theme);
static RoxIconInfo *theme_lookup_icon (IconTheme        *theme,
				       const char       *icon_name,
				       int               size,
				       gboolean          allow_svg,
				       gboolean          use_default_icons);
static void         theme_subdir_load (RoxIconTheme     *icon_theme,
				       IconTheme        *theme,
				       GtkIconThemeFile *theme_file,
				       char             *subdir);
static void         do_theme_change   (RoxIconTheme     *icon_theme);

static void  blow_themes               (RoxIconTheme    *icon_themes);

static void  icon_data_free            (GtkIconData          *icon_data);

static RoxIconInfo *icon_info_new             (void);
static RoxIconInfo *icon_info_new_builtin     (BuiltinIcon *icon);

static IconSuffix suffix_from_name (const char *name);

static BuiltinIcon *find_builtin_icon (const gchar *icon_name,
				       gint         size,
				       gint        *min_difference_p,
				       gboolean    *has_larger_p);

static guint signal_changed = 0;

static GHashTable *icon_theme_builtin_icons;

GType
rox_icon_theme_get_type (void)
{
  static GType type = 0;

  if (type == 0)
    {
      static const GTypeInfo info =
	{
	  sizeof (RoxIconThemeClass),
	  NULL,           /* base_init */
	  NULL,           /* base_finalize */
	  (GClassInitFunc) rox_icon_theme_class_init,
	  NULL,           /* class_finalize */
	  NULL,           /* class_data */
	  sizeof (RoxIconTheme),
	  0,              /* n_preallocs */
	  (GInstanceInitFunc) rox_icon_theme_init,
	};

      type = g_type_register_static (G_TYPE_OBJECT, "RoxIconTheme", &info, 0);
    }

  return type;
}

RoxIconTheme *
rox_icon_theme_new (void)
{
  return g_object_new (ROX_TYPE_ICON_THEME, NULL);
}

static void
rox_icon_theme_class_init (RoxIconThemeClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = rox_icon_theme_finalize;

/**
 * RoxIconTheme::changed
 * @icon_theme: the icon theme
 * 
 * Emitted when the current icon theme is switched or GTK+ detects
 * that a change has occurred in the contents of the current
 * icon theme.
 **/
  signal_changed = g_signal_new ("changed",
				 G_TYPE_FROM_CLASS (klass),
				 G_SIGNAL_RUN_LAST,
				 G_STRUCT_OFFSET (RoxIconThemeClass, changed),
				 NULL, NULL,
				 g_cclosure_marshal_VOID__VOID,
				 G_TYPE_NONE, 0);

  /* g_type_class_add_private (klass, sizeof (RoxIconThemePrivate)); */
}

static void
update_current_theme (RoxIconTheme *icon_theme)
{
  RoxIconThemePrivate *priv = icon_theme->priv;

  if (!priv->custom_theme)
    {
      gchar *theme = NULL;

      if (!theme)
	theme = g_strdup (DEFAULT_THEME_NAME);

      if (strcmp (priv->current_theme, theme) != 0)
	{
	  g_free (priv->current_theme);
	  priv->current_theme = theme;

	  do_theme_change (icon_theme);
	}
      else
	g_free (theme);
    }
}

static gboolean
pixbuf_supports_svg ()
{
  GSList *formats = gdk_pixbuf_get_formats ();
  GSList *tmp_list;
  gboolean found_svg = FALSE;

  for (tmp_list = formats; tmp_list && !found_svg; tmp_list = tmp_list->next)
    {
      gchar **mime_types = gdk_pixbuf_format_get_mime_types (tmp_list->data);
      gchar **mime_type;
      
      for (mime_type = mime_types; *mime_type && !found_svg; mime_type++)
	{
	  if (strcmp (*mime_type, "image/svg") == 0)
	    found_svg = TRUE;
	}

      g_strfreev (mime_types);
    }

  g_slist_free (formats);
    
  return found_svg;
}

static void
rox_icon_theme_init (RoxIconTheme *icon_theme)
{
  RoxIconThemePrivate *priv;

  priv = g_new0(RoxIconThemePrivate, 1);
  icon_theme->priv = priv;

  priv->custom_theme = FALSE;
  priv->current_theme = g_strdup (DEFAULT_THEME_NAME);
  priv->search_path = g_new (char *, 5);
  

  priv->search_path[0] = g_build_filename (g_get_home_dir (), ".icons", NULL);
  priv->search_path[1] = g_strdup ("/usr/local/share/icons");
  priv->search_path[2] = g_strdup ("/usr/local/share/pixmaps");
  priv->search_path[3] = g_strdup ("/usr/share/icons");
  priv->search_path[4] = g_strdup ("/usr/share/pixmaps");
  priv->search_path_len = 5;

  priv->themes_valid = FALSE;
  priv->themes = NULL;
  priv->unthemed_icons = NULL;

  priv->pixbuf_supports_svg = pixbuf_supports_svg ();
}

static void
free_dir_mtime (IconThemeDirMtime *dir_mtime)
{
  g_free (dir_mtime->dir);
  g_free (dir_mtime);
}

static void
do_theme_change (RoxIconTheme *icon_theme)
{
  blow_themes (icon_theme);
  g_signal_emit (G_OBJECT (icon_theme), signal_changed, 0);
}

static void
blow_themes (RoxIconTheme *icon_theme)
{
  RoxIconThemePrivate *priv = icon_theme->priv;
  
  if (priv->themes_valid)
    {
      g_hash_table_destroy (priv->all_icons);
      g_list_foreach (priv->themes, (GFunc)theme_destroy, NULL);
      g_list_free (priv->themes);
      g_list_foreach (priv->dir_mtimes, (GFunc)free_dir_mtime, NULL);
      g_list_free (priv->dir_mtimes);
      g_hash_table_destroy (priv->unthemed_icons);
    }
  priv->themes = NULL;
  priv->unthemed_icons = NULL;
  priv->dir_mtimes = NULL;
  priv->all_icons = NULL;
  priv->themes_valid = FALSE;
}

static void
rox_icon_theme_finalize (GObject *object)
{
  RoxIconTheme *icon_theme;
  RoxIconThemePrivate *priv;
  int i;

  icon_theme = ROX_ICON_THEME (object);
  priv = icon_theme->priv;

  g_free (priv->current_theme);
  priv->current_theme = NULL;

  for (i=0; i < priv->search_path_len; i++)
    g_free (priv->search_path[i]);

  g_free (priv->search_path);
  priv->search_path = NULL;

  blow_themes (icon_theme);
}

void
rox_icon_theme_get_search_path (RoxIconTheme      *icon_theme,
				gchar            **path[],
				gint              *n_elements)
{
  RoxIconThemePrivate *priv;
  int i;

  g_return_if_fail (ROX_IS_ICON_THEME (icon_theme));

  priv = icon_theme->priv;

  if (n_elements)
    *n_elements = priv->search_path_len;
  
  if (path)
    {
      *path = g_new (gchar *, priv->search_path_len + 1);
      for (i = 0; i < priv->search_path_len; i++)
	(*path)[i] = g_strdup (priv->search_path[i]);	/* (was +1) */
      (*path)[i] = NULL;
    }
}

void
rox_icon_theme_set_custom_theme (RoxIconTheme *icon_theme,
				 const gchar  *theme_name)
{
  RoxIconThemePrivate *priv;

  g_return_if_fail (ROX_IS_ICON_THEME (icon_theme));

  priv = icon_theme->priv;

  if (theme_name != NULL)
    {
      priv->custom_theme = TRUE;
      if (strcmp (theme_name, priv->current_theme) != 0)
	{
	  g_free (priv->current_theme);
	  priv->current_theme = g_strdup (theme_name);

	  do_theme_change (icon_theme);
	}
    }
  else
    {
      priv->custom_theme = FALSE;

      update_current_theme (icon_theme);
    }
}

static void
insert_theme (RoxIconTheme *icon_theme, const char *theme_name)
{
  int i;
  GList *l;
  char **dirs;
  char **themes;
  RoxIconThemePrivate *priv;
  IconTheme *theme;
  char *path;
  char *contents;
  char *directories;
  char *inherits;
  GtkIconThemeFile *theme_file;
  IconThemeDirMtime *dir_mtime;
  struct stat stat_buf;
  
  priv = icon_theme->priv;
  
  for (l = priv->themes; l != NULL; l = l->next)
    {
      theme = l->data;
      if (strcmp (theme->name, theme_name) == 0)
	return;
    }
  
  for (i = 0; i < priv->search_path_len; i++)
    {
      path = g_build_filename (priv->search_path[i],
			       theme_name,
			       NULL);
      dir_mtime = g_new (IconThemeDirMtime, 1);
      dir_mtime->dir = path;
      if (stat (path, &stat_buf) == 0 && S_ISDIR (stat_buf.st_mode))
	dir_mtime->mtime = stat_buf.st_mtime;
      else
	dir_mtime->mtime = 0;

      priv->dir_mtimes = g_list_prepend (priv->dir_mtimes, dir_mtime);
    }

  theme_file = NULL;
  for (i = 0; i < priv->search_path_len; i++)
    {
      path = g_build_filename (priv->search_path[i],
			       theme_name,
			       "index.theme",
			       NULL);
      if (g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
	if (g_file_get_contents (path, &contents, NULL, NULL)) {
	  theme_file = _rox_icon_theme_file_new_from_string (contents, NULL);
	  g_free (contents);
	  g_free (path);
	  break;
	}
      }
      g_free (path);
    }

  if (theme_file == NULL)
    return;
  
  theme = g_new (IconTheme, 1);
  if (!_rox_icon_theme_file_get_locale_string (theme_file,
					       "Icon Theme",
					       "Name",
					       &theme->display_name))
    {
      g_warning ("Theme file for %s has no name\n", theme_name);
      g_free (theme);
      _rox_icon_theme_file_free (theme_file);
      return;
    }

  if (!_rox_icon_theme_file_get_string (theme_file,
					"Icon Theme",
					"Directories",
					&directories))
    {
      g_warning ("Theme file for %s has no directories\n", theme_name);
      g_free (theme->display_name);
      g_free (theme);
      _rox_icon_theme_file_free (theme_file);
      return;
    }
  
  theme->name = g_strdup (theme_name);
  _rox_icon_theme_file_get_locale_string (theme_file,
					  "Icon Theme",
					  "Comment",
					  &theme->comment);
  _rox_icon_theme_file_get_string (theme_file,
				   "Icon Theme",
				   "Example",
				   &theme->example);
  
  dirs = g_strsplit (directories, ",", 0);

  theme->dirs = NULL;
  for (i = 0; dirs[i] != NULL; i++)
      theme_subdir_load (icon_theme, theme, theme_file, dirs[i]);
  
  g_strfreev (dirs);
  
  theme->dirs = g_list_reverse (theme->dirs);

  g_free (directories);

  /* Prepend the finished theme */
  priv->themes = g_list_prepend (priv->themes, theme);

  if (_rox_icon_theme_file_get_string (theme_file,
				       "Icon Theme",
				       "Inherits",
				       &inherits))
    {
      themes = g_strsplit (inherits, ",", 0);

      for (i = 0; themes[i] != NULL; i++)
	insert_theme (icon_theme, themes[i]);
      
      g_strfreev (themes);

      g_free (inherits);
    }

  _rox_icon_theme_file_free (theme_file);
}

static void
free_unthemed_icon (UnthemedIcon *unthemed_icon)
{
  if (unthemed_icon->svg_filename)
    g_free (unthemed_icon->svg_filename);
  if (unthemed_icon->svg_filename)
    g_free (unthemed_icon->no_svg_filename);
  g_free (unthemed_icon);
}

static void
load_themes (RoxIconTheme *icon_theme)
{
  RoxIconThemePrivate *priv;
  GDir *gdir;
  int base;
  char *dir, *base_name, *dot;
  const char *file;
  char *abs_file;
  UnthemedIcon *unthemed_icon;
  IconSuffix old_suffix, new_suffix;
  GTimeVal tv;
  
  priv = icon_theme->priv;

  priv->all_icons = g_hash_table_new (g_str_hash, g_str_equal);
  
  insert_theme (icon_theme, priv->current_theme);
  
  /* Always look in the "default" icon theme */
  insert_theme (icon_theme, DEFAULT_THEME_NAME);
  priv->themes = g_list_reverse (priv->themes);
  
  priv->unthemed_icons = g_hash_table_new_full (g_str_hash, g_str_equal,
						g_free, (GDestroyNotify)free_unthemed_icon);

  for (base = 0; base < icon_theme->priv->search_path_len; base++)
    {
      dir = icon_theme->priv->search_path[base];
      gdir = g_dir_open (dir, 0, NULL);

      if (gdir == NULL)
	continue;
      
      while ((file = g_dir_read_name (gdir)))
	{
	  new_suffix = suffix_from_name (file);

	  if (new_suffix != ICON_SUFFIX_NONE)
	    {
	      abs_file = g_build_filename (dir, file, NULL);

	      base_name = g_strdup (file);
		  
	      dot = strrchr (base_name, '.');
	      if (dot)
		*dot = 0;

	      if ((unthemed_icon = g_hash_table_lookup (priv->unthemed_icons,
							base_name)))
		{
		  if (new_suffix == ICON_SUFFIX_SVG)
		    {
		      if (unthemed_icon->no_svg_filename)
			g_free (abs_file);
		      else
			unthemed_icon->svg_filename = abs_file;
		    }
		  else
		    {
		      if (unthemed_icon->no_svg_filename)
			{
			  old_suffix = suffix_from_name (unthemed_icon->no_svg_filename);
			  if (new_suffix > old_suffix)
			    {
			      g_free (unthemed_icon->no_svg_filename);
			      unthemed_icon->no_svg_filename = abs_file;			      
			    }
			  else
			    g_free (abs_file);
			}
		      else
			unthemed_icon->no_svg_filename = abs_file;			      
		    }

		  g_free (base_name);
		}
	      else
		{
		  unthemed_icon = g_new0 (UnthemedIcon, 1);
		  
		  if (new_suffix == ICON_SUFFIX_SVG)
		    unthemed_icon->svg_filename = abs_file;
		  else
		    unthemed_icon->svg_filename = abs_file;
		  
		  g_hash_table_insert (priv->unthemed_icons,
				       base_name,
				       unthemed_icon);
		  g_hash_table_insert (priv->all_icons,
				       base_name, NULL);
		}
	    }
	}
      g_dir_close (gdir);
    }

  priv->themes_valid = TRUE;
  
  g_get_current_time(&tv);
  priv->last_stat_time = tv.tv_sec;
}

static void
ensure_valid_themes (RoxIconTheme *icon_theme)
{
  RoxIconThemePrivate *priv = icon_theme->priv;
  GTimeVal tv;
  
  if (priv->themes_valid)
    {
      g_get_current_time(&tv);

      if (ABS (tv.tv_sec - priv->last_stat_time) > 5)
	rox_icon_theme_rescan_if_needed (icon_theme);
    }
  
  if (!priv->themes_valid)
    load_themes (icon_theme);
}

RoxIconInfo *
rox_icon_theme_lookup_icon (RoxIconTheme       *icon_theme,
			    const gchar        *icon_name,
			    gint                size,
			    RoxIconLookupFlags  flags)
{
  RoxIconThemePrivate *priv;
  GList *l;
  RoxIconInfo *icon_info = NULL;
  UnthemedIcon *unthemed_icon;
  gboolean allow_svg;
  gboolean use_builtin;
  gboolean found_default;

  g_return_val_if_fail (ROX_IS_ICON_THEME (icon_theme), NULL);
  g_return_val_if_fail (icon_name != NULL, NULL);
  g_return_val_if_fail ((flags & ROX_ICON_LOOKUP_NO_SVG) == 0 ||
			(flags & ROX_ICON_LOOKUP_FORCE_SVG) == 0, NULL);
  
  priv = icon_theme->priv;

  if (flags & ROX_ICON_LOOKUP_NO_SVG)
    allow_svg = FALSE;
  else if (flags & ROX_ICON_LOOKUP_FORCE_SVG)
    allow_svg = TRUE;
  else
    allow_svg = priv->pixbuf_supports_svg;

  use_builtin = (flags & ROX_ICON_LOOKUP_USE_BUILTIN);

  ensure_valid_themes (icon_theme);

  found_default = FALSE;
  l = priv->themes;
  while (l != NULL)
    {
      IconTheme *icon_theme = l->data;
      
      if (strcmp (icon_theme->name, DEFAULT_THEME_NAME) == 0)
	found_default = TRUE;
      
      icon_info = theme_lookup_icon (icon_theme, icon_name, size, allow_svg, use_builtin);
      if (icon_info)
	goto out;
      
      l = l->next;
    }

  if (!found_default)
    {
      BuiltinIcon *builtin = find_builtin_icon (icon_name, size, NULL, NULL);
      if (builtin)
	{
	  icon_info = icon_info_new_builtin (builtin);
	  goto out;
	}
    }
  
  unthemed_icon = g_hash_table_lookup (priv->unthemed_icons, icon_name);
  if (unthemed_icon)
    {
      icon_info = icon_info_new ();

      /* A SVG icon, when allowed, beats out a XPM icon, but not
       * a PNG icon
       */
      if (allow_svg &&
	  unthemed_icon->svg_filename &&
	  (!unthemed_icon->no_svg_filename ||
	   suffix_from_name (unthemed_icon->no_svg_filename) != ICON_SUFFIX_PNG))
	icon_info->filename = g_strdup (unthemed_icon->svg_filename);
      else if (unthemed_icon->no_svg_filename)
	icon_info->filename = g_strdup (unthemed_icon->no_svg_filename);

      icon_info->dir_type = ICON_THEME_DIR_UNTHEMED;
    }

 out:
  if (icon_info)
    icon_info->desired_size = size;

  return icon_info;
}

/* Error quark */
GQuark
rox_icon_theme_error_quark (void)
{
  static GQuark q = 0;
  if (q == 0)
    q = g_quark_from_static_string ("gtk-icon-theme-error-quark");

  return q;
}

GdkPixbuf *
rox_icon_theme_load_icon (RoxIconTheme         *icon_theme,
			  const gchar          *icon_name,
			  gint                  size,
			  RoxIconLookupFlags    flags,
			  GError              **error)
{
  RoxIconInfo *icon_info;
  GdkPixbuf *pixbuf = NULL;
  
  g_return_val_if_fail (ROX_IS_ICON_THEME (icon_theme), NULL);
  g_return_val_if_fail (icon_name != NULL, NULL);
  g_return_val_if_fail ((flags & ROX_ICON_LOOKUP_NO_SVG) == 0 ||
			(flags & ROX_ICON_LOOKUP_FORCE_SVG) == 0, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  
  icon_info  = rox_icon_theme_lookup_icon (icon_theme, icon_name, size,
					   flags | ROX_ICON_LOOKUP_USE_BUILTIN);
  if (!icon_info)
    {
      g_set_error (error, ROX_ICON_THEME_ERROR,  ROX_ICON_THEME_NOT_FOUND,
		   _("Icon '%s' not present in theme"), icon_name);
      return NULL;
    }

  pixbuf = rox_icon_info_load_icon (icon_info, error);
  rox_icon_info_free (icon_info);

  return pixbuf;
}

gboolean
rox_icon_theme_rescan_if_needed (RoxIconTheme *icon_theme)
{
  RoxIconThemePrivate *priv;
  IconThemeDirMtime *dir_mtime;
  GList *d;
  int stat_res;
  struct stat stat_buf;
  GTimeVal tv;

  g_return_val_if_fail (ROX_IS_ICON_THEME (icon_theme), FALSE);

  priv = icon_theme->priv;
  
  for (d = priv->dir_mtimes; d != NULL; d = d->next)
    {
      dir_mtime = d->data;

      stat_res = stat (dir_mtime->dir, &stat_buf);

      /* dir mtime didn't change */
      if (stat_res == 0 && 
	  S_ISDIR (stat_buf.st_mode) &&
	  dir_mtime->mtime == stat_buf.st_mtime)
	continue;
      /* didn't exist before, and still doesn't */
      if (dir_mtime->mtime == 0 &&
	  (stat_res != 0 || !S_ISDIR (stat_buf.st_mode)))
	continue;
	  
      do_theme_change (icon_theme);
      return TRUE;
    }
  
  g_get_current_time (&tv);
  priv->last_stat_time = tv.tv_sec;

  return FALSE;
}

static void
theme_destroy (IconTheme *theme)
{
  g_free (theme->display_name);
  g_free (theme->comment);
  g_free (theme->name);
  g_free (theme->example);

  g_list_foreach (theme->dirs, (GFunc)theme_dir_destroy, NULL);
  g_list_free (theme->dirs);
  g_free (theme);
}

static void
theme_dir_destroy (IconThemeDir *dir)
{
  g_hash_table_destroy (dir->icons);
  if (dir->icon_data)
    g_hash_table_destroy (dir->icon_data);
  g_free (dir->dir);
  g_free (dir);
}

static int
theme_dir_size_difference (IconThemeDir *dir, int size, gboolean *smaller)
{
  int min, max;
  switch (dir->type)
    {
    case ICON_THEME_DIR_FIXED:
      *smaller = size < dir->size;
      return abs (size - dir->size);
      break;
    case ICON_THEME_DIR_SCALABLE:
      *smaller = size < dir->min_size;
      if (size < dir->min_size)
	return dir->min_size - size;
      if (size > dir->max_size)
	return size - dir->max_size;
      return 0;
      break;
    case ICON_THEME_DIR_THRESHOLD:
      min = dir->size - dir->threshold;
      max = dir->size + dir->threshold;
      *smaller = size < min;
      if (size < min)
	return min - size;
      if (size > max)
	return size - max;
      return 0;
      break;
    case ICON_THEME_DIR_UNTHEMED:
      g_assert_not_reached ();
      break;
    }
  g_assert_not_reached ();
  return 1000;
}

static const char *
string_from_suffix (IconSuffix suffix)
{
  switch (suffix)
    {
    case ICON_SUFFIX_XPM:
      return ".xpm";
    case ICON_SUFFIX_SVG:
      return ".svg";
    case ICON_SUFFIX_PNG:
      return ".png";
    default:
      g_assert_not_reached();
    }
  return NULL;
}

static IconSuffix
suffix_from_name (const char *name)
{
  IconSuffix retval;

  if (g_str_has_suffix (name, ".png"))
    retval = ICON_SUFFIX_PNG;
  else if (g_str_has_suffix (name, ".svg"))
    retval = ICON_SUFFIX_SVG;
  else if (g_str_has_suffix (name, ".xpm"))
    retval = ICON_SUFFIX_XPM;
  else
    retval = ICON_SUFFIX_NONE;

  return retval;
}

static IconSuffix
best_suffix (IconSuffix suffix,
	     gboolean   allow_svg)
{
  if ((suffix & ICON_SUFFIX_PNG) != 0)
    return ICON_SUFFIX_PNG;
  else if (allow_svg && ((suffix & ICON_SUFFIX_SVG) != 0))
    return ICON_SUFFIX_SVG;
  else if ((suffix & ICON_SUFFIX_XPM) != 0)
    return ICON_SUFFIX_XPM;
  else
    return ICON_SUFFIX_NONE;
}

static RoxIconInfo *
theme_lookup_icon (IconTheme          *theme,
		   const char         *icon_name,
		   int                 size,
		   gboolean            allow_svg,
		   gboolean            use_builtin)
{
  GList *l;
  IconThemeDir *dir, *min_dir;
  char *file;
  int min_difference, difference;
  BuiltinIcon *closest_builtin = NULL;
  gboolean smaller, has_larger;
  IconSuffix suffix;

  min_difference = G_MAXINT;
  min_dir = NULL;
  has_larger = FALSE;

  /* Builtin icons are logically part of the default theme and
   * are searched before other subdirectories of the default theme.
   */
  if (strcmp (theme->name, DEFAULT_THEME_NAME) == 0 && use_builtin)
    {
      closest_builtin = find_builtin_icon (icon_name, size,
					   &min_difference,
					   &has_larger);

      if (min_difference == 0)
	return icon_info_new_builtin (closest_builtin);
    }

  l = theme->dirs;
  while (l != NULL)
    {
      dir = l->data;

      suffix = GPOINTER_TO_UINT (g_hash_table_lookup (dir->icons, icon_name));
      
      if (suffix != ICON_SUFFIX_NONE &&
	  (allow_svg || suffix != ICON_SUFFIX_SVG))
	{
	  difference = theme_dir_size_difference (dir, size, &smaller);

	  if (difference == 0)
	    {
	      min_dir = dir;
	      break;
	    }

	  if (!has_larger)
	    {
	      if (difference < min_difference || smaller)
		{
		  min_difference = difference;
		  min_dir = dir;
		  closest_builtin = NULL;
		  has_larger = smaller;
		}
	    }
	  else
	    {
	      if (difference < min_difference && smaller)
		{
		  min_difference = difference;
		  min_dir = dir;
		  closest_builtin = NULL;
		}
	    }

	}

      l = l->next;
    }

  if (closest_builtin)
    return icon_info_new_builtin (closest_builtin);
  
  if (min_dir)
    {
      RoxIconInfo *icon_info = icon_info_new ();
      
      suffix = GPOINTER_TO_UINT (g_hash_table_lookup (min_dir->icons, icon_name));
      suffix = best_suffix (suffix, allow_svg);
      g_assert (suffix != ICON_SUFFIX_NONE);
      
      file = g_strconcat (icon_name, string_from_suffix (suffix), NULL);
      icon_info->filename = g_build_filename (min_dir->dir, file, NULL);
      g_free (file);

      if (min_dir->icon_data != NULL)
	icon_info->data = g_hash_table_lookup (min_dir->icon_data, icon_name);

      icon_info->dir_type = min_dir->type;
      icon_info->dir_size = min_dir->size;
      icon_info->threshold = min_dir->threshold;
      
      return icon_info;
    }
 
  return NULL;
}

static void
load_icon_data (IconThemeDir *dir, const char *path, const char *name)
{
  GtkIconThemeFile *icon_file;
  char *base_name;
  char **split;
  char *contents;
  char *dot;
  char *str;
  char *split_point;
  int i;
  
  GtkIconData *data;

  if (g_file_get_contents (path, &contents, NULL, NULL))
    {
      icon_file = _rox_icon_theme_file_new_from_string (contents, NULL);
      
      if (icon_file)
	{
	  base_name = g_strdup (name);
	  dot = strrchr (base_name, '.');
	  *dot = 0;
	  
	  data = g_new0 (GtkIconData, 1);
	  g_hash_table_replace (dir->icon_data, base_name, data);
	  
	  if (_rox_icon_theme_file_get_string (icon_file, "Icon Data",
					       "EmbeddedTextRectangle",
					       &str))
	    {
	      split = g_strsplit (str, ",", 4);
	      
	      i = 0;
	      while (split[i] != NULL)
		i++;

	      if (i==4)
		{
		  data->has_embedded_rect = TRUE;
		  data->x0 = atoi (split[0]);
		  data->y0 = atoi (split[1]);
		  data->x1 = atoi (split[2]);
		  data->y1 = atoi (split[3]);
		}

	      g_strfreev (split);
	      g_free (str);
	    }


	  if (_rox_icon_theme_file_get_string (icon_file, "Icon Data",
					       "AttachPoints",
					       &str))
	    {
	      split = g_strsplit (str, "|", -1);
	      
	      i = 0;
	      while (split[i] != NULL)
		i++;

	      data->n_attach_points = i;
	      data->attach_points = g_malloc (sizeof (GdkPoint) * i);

	      i = 0;
	      while (split[i] != NULL && i < data->n_attach_points)
		{
		  split_point = strchr (split[i], ',');
		  if (split_point)
		    {
		      *split_point = 0;
		      split_point++;
		      data->attach_points[i].x = atoi (split[i]);
		      data->attach_points[i].y = atoi (split_point);
		    }
		  i++;
		}
	      
	      g_strfreev (split);
	      g_free (str);
	    }
	  
	  _rox_icon_theme_file_get_locale_string (icon_file, "Icon Data",
						  "DisplayName",
						  &data->display_name);
	  
	  _rox_icon_theme_file_free (icon_file);
	}
      g_free (contents);
    }
  
}

static void
scan_directory (RoxIconThemePrivate *icon_theme,
		IconThemeDir *dir, char *full_dir)
{
  GDir *gdir;
  const char *name;
  char *base_name, *dot;
  char *path;
  IconSuffix suffix, hash_suffix;
  
  dir->icons = g_hash_table_new_full (g_str_hash, g_str_equal,
				      g_free, NULL);
  
  gdir = g_dir_open (full_dir, 0, NULL);

  if (gdir == NULL)
    return;

  while ((name = g_dir_read_name (gdir)))
    {
      if (g_str_has_suffix (name, ".icon"))
	{
	  if (dir->icon_data == NULL)
	    dir->icon_data = g_hash_table_new_full (g_str_hash, g_str_equal,
						    g_free, (GDestroyNotify)icon_data_free);
	  
	  path = g_build_filename (full_dir, name, NULL);
	  if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
	    load_icon_data (dir, path, name);
	  
	  g_free (path);
	  
	  continue;
	}

      suffix = suffix_from_name (name);
      if (suffix == ICON_SUFFIX_NONE)
	continue;
      
      base_name = g_strdup (name);
      dot = strrchr (base_name, '.');
      *dot = 0;
      
      hash_suffix = GPOINTER_TO_INT (g_hash_table_lookup (dir->icons, base_name));
      g_hash_table_replace (dir->icons, base_name, GUINT_TO_POINTER (hash_suffix| suffix));
      g_hash_table_insert (icon_theme->all_icons, base_name, NULL);
    }
  
  g_dir_close (gdir);
}

static void
theme_subdir_load (RoxIconTheme *icon_theme,
		   IconTheme *theme,
		   GtkIconThemeFile *theme_file,
		   char *subdir)
{
  int base;
  char *type_string;
  IconThemeDir *dir;
  IconThemeDirType type;
  char *context_string;
  GQuark context;
  int size;
  int min_size;
  int max_size;
  int threshold;
  char *full_dir;

  if (!_rox_icon_theme_file_get_integer (theme_file,
					 subdir,
					 "Size",
					 &size))
    {
      g_warning ("Theme directory %s of theme %s has no size field\n", subdir, theme->name);
      return;
    }
  
  type = ICON_THEME_DIR_THRESHOLD;
  if (_rox_icon_theme_file_get_string (theme_file, subdir, "Type", &type_string))
    {
      if (strcmp (type_string, "Fixed") == 0)
	type = ICON_THEME_DIR_FIXED;
      else if (strcmp (type_string, "Scalable") == 0)
	type = ICON_THEME_DIR_SCALABLE;
      else if (strcmp (type_string, "Threshold") == 0)
	type = ICON_THEME_DIR_THRESHOLD;

      g_free (type_string);
    }
  
  context = 0;
  if (_rox_icon_theme_file_get_string (theme_file, subdir, "Context", &context_string))
    {
      context = g_quark_from_string (context_string);
      g_free (context_string);
    }

  if (!_rox_icon_theme_file_get_integer (theme_file,
					 subdir,
					 "MaxSize",
				     &max_size))
    max_size = size;
  
  if (!_rox_icon_theme_file_get_integer (theme_file,
					 subdir,
					 "MinSize",
					 &min_size))
    min_size = size;
  
  if (!_rox_icon_theme_file_get_integer (theme_file,
					 subdir,
					 "Threshold",
					 &threshold))
    threshold = 2;

  for (base = 0; base < icon_theme->priv->search_path_len; base++)
    {
      full_dir = g_build_filename (icon_theme->priv->search_path[base],
				   theme->name,
				   subdir,
				   NULL);
      if (g_file_test (full_dir, G_FILE_TEST_IS_DIR))
	{
	  dir = g_new (IconThemeDir, 1);
	  dir->type = type;
	  dir->context = context;
	  dir->size = size;
	  dir->min_size = min_size;
	  dir->max_size = max_size;
	  dir->threshold = threshold;
	  dir->dir = full_dir;
	  dir->icon_data = NULL;
	  
	  scan_directory (icon_theme->priv, dir, full_dir);

	  theme->dirs = g_list_append (theme->dirs, dir);
	}
      else
	g_free (full_dir);
    }
}

static void
icon_data_free (GtkIconData *icon_data)
{
  g_free (icon_data->attach_points);
  g_free (icon_data->display_name);
  g_free (icon_data);
}

/*
 * RoxIconInfo
 */
GType
rox_icon_info_get_type (void)
{
  static GType our_type = 0;
  
  if (our_type == 0)
    our_type = g_boxed_type_register_static ("RoxIconInfo",
					     (GBoxedCopyFunc) rox_icon_info_copy,
					     (GBoxedFreeFunc) rox_icon_info_free);

  return our_type;
}

static RoxIconInfo *
icon_info_new (void)
{
  RoxIconInfo *icon_info = g_new0 (RoxIconInfo, 1);

  icon_info->ref_count = 1;
  icon_info->scale = -1.;

  return icon_info;
}

static RoxIconInfo *
icon_info_new_builtin (BuiltinIcon *icon)
{
  RoxIconInfo *icon_info = icon_info_new ();

  icon_info->builtin_pixbuf = g_object_ref (icon->pixbuf);
  icon_info->dir_type = ICON_THEME_DIR_THRESHOLD;
  icon_info->dir_size = icon->size;
  icon_info->threshold = 2;
  
  return icon_info;
}

RoxIconInfo *
rox_icon_info_copy (RoxIconInfo *icon_info)
{
  RoxIconInfo *copy;
  
  g_return_val_if_fail (icon_info != NULL, NULL);

  copy = g_memdup (icon_info, sizeof (RoxIconInfo));
  if (copy->builtin_pixbuf)
    g_object_ref (copy->builtin_pixbuf);
  if (copy->pixbuf)
    g_object_ref (copy->pixbuf);
  if (copy->load_error)
    copy->load_error = g_error_copy (copy->load_error);
  if (copy->filename)
    copy->filename = g_strdup (copy->filename);

  return copy;
}

void
rox_icon_info_free (RoxIconInfo *icon_info)
{
  g_return_if_fail (icon_info != NULL);

  if (icon_info->filename)
    g_free (icon_info->filename);
  if (icon_info->builtin_pixbuf)
    g_object_unref (icon_info->builtin_pixbuf);
  if (icon_info->pixbuf)
    g_object_unref (icon_info->pixbuf);
  
  g_free (icon_info);
}

static GdkPixbuf *
load_svg_at_size (const gchar *filename,
		  gint         size,
		  GError      **error)
{
  GdkPixbuf *pixbuf = NULL;
  GdkPixbufLoader *loader = NULL;
  gchar *contents;
  gsize length;
  
  if (!g_file_get_contents (filename,
			    &contents, &length, error))
    goto bail;
  
  loader = gdk_pixbuf_loader_new ();
  gdk_pixbuf_loader_set_size (loader, size, size);
  
  if (!gdk_pixbuf_loader_write (loader, contents, length, error))
    {
      gdk_pixbuf_loader_close (loader, NULL);
      goto bail;
    }
  
  if (!gdk_pixbuf_loader_close (loader, error))
    goto bail;
  
  pixbuf = g_object_ref (gdk_pixbuf_loader_get_pixbuf (loader));
  
 bail:
  if (loader)
    g_object_unref (loader);
  
  return pixbuf;
}

/* This function contains the complicatd logic for deciding
 * on the size at which to load the icon and loading it at
 * that size.
 */
static gboolean
icon_info_ensure_scale_and_pixbuf (RoxIconInfo *icon_info,
				   gboolean     scale_only)
{
  int image_width, image_height;
  GdkPixbuf *source_pixbuf;

  /* First check if we already succeeded have the necessary
   * information (or failed earlier)
   */
  if (scale_only && icon_info->scale >= 0)
    return TRUE;

  if (icon_info->pixbuf)
    return TRUE;

  if (icon_info->load_error)
    return FALSE;

  /* SVG icons are a special case - we just immediately scale them
   * to the desired size
   */
  if (icon_info->filename && g_str_has_suffix (icon_info->filename, ".svg"))
    {
      icon_info->scale = icon_info->desired_size / 1000.;

      if (scale_only)
	return TRUE;
      
      icon_info->pixbuf = load_svg_at_size (icon_info->filename,
					    icon_info->desired_size,
					    &icon_info->load_error);

      return icon_info->pixbuf != NULL;
    }

  /* In many cases, the scale can be determined without actual access
   * to the icon file. This is generally true when we have a size
   * for the directory where the icon is; the image size doesn't
   * matter in that case.
   */
  if (icon_info->dir_type == ICON_THEME_DIR_FIXED)
    icon_info->scale = 1.0;
  else if (icon_info->dir_type == ICON_THEME_DIR_THRESHOLD)
    {
      if (icon_info->desired_size >= icon_info->dir_size - icon_info->threshold &&
	  icon_info->desired_size <= icon_info->dir_size + icon_info->threshold)
	icon_info->scale = 1.0;
      else if (icon_info->dir_size > 0)
	icon_info->scale =(gdouble) icon_info->desired_size / icon_info->dir_size;
    }
  else if (icon_info->dir_type == ICON_THEME_DIR_SCALABLE)
    {
      if (icon_info->dir_size > 0)
	icon_info->scale = (gdouble) icon_info->desired_size / icon_info->dir_size;
    }

  if (icon_info->scale >= 0. && scale_only)
    return TRUE;

  /* At this point, we need to actually get the icon; either from the
   * builting image or by loading the file
   */
  if (icon_info->builtin_pixbuf)
    source_pixbuf = g_object_ref (icon_info->builtin_pixbuf);
  else
    {
      source_pixbuf = gdk_pixbuf_new_from_file (icon_info->filename,
						&icon_info->load_error);
      if (!source_pixbuf)
	return FALSE;
    }

  /* Do scale calculations that depend on the image size
   */
  image_width = gdk_pixbuf_get_width (source_pixbuf);
  image_height = gdk_pixbuf_get_height (source_pixbuf);

  if (icon_info->scale < 0.0)
    {
      gint image_size = MAX (image_width, image_height);
      if (image_size > 0)
	icon_info->scale = icon_info->desired_size / image_size;
      else
	icon_info->scale = 1.0;
      
      if (icon_info->dir_type == ICON_THEME_DIR_UNTHEMED)
	icon_info->scale = MAX (icon_info->scale, 1.0);
    }

  /* We don't short-circuit out here for scale_only, since, now
   * we've loaded the icon, we might as well go ahead and finish
   * the job. This is a bit of a waste when we scale here
   * and never get the final pixbuf; at the cost of a bit of
   * extra complexity, we could keep the source pixbuf around
   * but not actually scale it until neede.
   */
    
  if (icon_info->scale == 1.0)
    icon_info->pixbuf = source_pixbuf;
  else
    {
      icon_info->pixbuf = gdk_pixbuf_scale_simple (source_pixbuf,
						   0.5 + image_width * icon_info->scale,
						   0.5 + image_height * icon_info->scale,
						   GDK_INTERP_BILINEAR);
      g_object_unref (source_pixbuf);
    }

  return TRUE;
}

static GdkPixbuf *
rox_icon_info_load_icon (RoxIconInfo *icon_info,
			 GError     **error)
{
  g_return_val_if_fail (icon_info != NULL, NULL);

  g_return_val_if_fail (icon_info != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  icon_info_ensure_scale_and_pixbuf (icon_info, FALSE);

  if (icon_info->load_error)
    {
      g_propagate_error (error, icon_info->load_error);
      return NULL;
    }

  return g_object_ref (icon_info->pixbuf);
}

/*
 * Builtin icons
 */


static BuiltinIcon *
find_builtin_icon (const gchar *icon_name,
		   gint         size,
		   gint        *min_difference_p,
		   gboolean    *has_larger_p)
{
  GSList *icons = NULL;
  gint min_difference = G_MAXINT;
  gboolean has_larger = FALSE;
  BuiltinIcon *min_icon = NULL;
  
  if (!icon_theme_builtin_icons)
    return NULL;

  icons = g_hash_table_lookup (icon_theme_builtin_icons, icon_name);

  while (icons)
    {
      BuiltinIcon *default_icon = icons->data;
      int min, max, difference;
      gboolean smaller;
      
      min = default_icon->size - 2;
      max = default_icon->size + 2;
      smaller = size < min;
      if (size < min)
	difference = min - size;
      if (size > max)
	difference = size - max;
      else
	difference = 0;
      
      if (difference == 0)
	{
	  min_icon = default_icon;
	  break;
	}
      
      if (!has_larger)
	{
	  if (difference < min_difference || smaller)
	    {
	      min_difference = difference;
	      min_icon = default_icon;
	      has_larger = smaller;
	    }
	}
      else
	{
	  if (difference < min_difference && smaller)
	    {
	      min_difference = difference;
	      min_icon = default_icon;
	    }
	}
      
      icons = icons->next;
    }

  if (min_difference_p)
    *min_difference_p = min_difference;
  if (has_larger_p)
    *has_larger_p = has_larger;

  return min_icon;
}
