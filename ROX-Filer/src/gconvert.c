/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
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

/* This code is taken from glib2 and has been converted to the GPL, as allowed
 * under the terms of the LGPL. Removed lots of stuff! When we move fully to
 * glib2, this file will disappear.
 *
 * gconvert.c: Convert between character sets using iconv
 * Copyright Red Hat Inc., 2000
 * Authors: Havoc Pennington <hp@redhat.com>, Owen Taylor <otaylor@redhat.com
 */

#include "config.h"

#ifndef GTK2

#include "gunicode.h"

#ifdef HAVE_ICONV_H
# include <iconv.h>
#endif

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <stdlib.h>
#include <string.h>

#include "global.h"

#include "gconvert.h"

static gboolean
g_utf8_get_charset_internal (const char **a)
{
  const char *charset = getenv("CHARSET");

  if (charset && *charset)
    {
      *a = charset;

      if (charset && strstr (charset, "UTF-8"))
	return TRUE;
      else
	return FALSE;
    }

  /* Assume this for compatibility at present.  */
  *a = "ISO8859-1";
  
  return FALSE;
}

static int utf8_locale_cache = -1;
static const char *utf8_charset_cache = NULL;

gboolean
g_get_charset (const char **charset) 
{
  if (utf8_locale_cache != -1)
    {
      if (charset)
	*charset = utf8_charset_cache;
      return utf8_locale_cache;
    }
  utf8_locale_cache = g_utf8_get_charset_internal (&utf8_charset_cache);
  if (charset) 
    *charset = utf8_charset_cache;
  return utf8_locale_cache;
}

/* unicode_strchr */

#ifdef HAVE_ICONV_H
size_t 
g_iconv (GIConv   converter,
	 gchar  **inbuf,
	 gsize   *inbytes_left,
	 gchar  **outbuf,
	 gsize   *outbytes_left)
{
  iconv_t cd = (iconv_t)converter;

  return iconv (cd, inbuf, inbytes_left, outbuf, outbytes_left);
}

gint
g_iconv_close (GIConv converter)
{
  iconv_t cd = (iconv_t)converter;

  return iconv_close (cd);
}

static GIConv
open_converter (const gchar *to_codeset,
                const gchar *from_codeset,
		GError     **error)
{
  GIConv cd = (GIConv) iconv_open (to_codeset, from_codeset);

  if (cd == (iconv_t) -1)
    {
      /* Something went wrong.  */
      if (errno == EINVAL)
        g_warning("Conversion from character set '%s' to '%s' is not supported",
		  from_codeset, to_codeset);
      else
	g_warning("Could not open converter from '%s' to '%s': %s",
                     from_codeset, to_codeset, strerror (errno));
    }

  return cd;

}
#endif

gchar*
g_convert (const gchar *str,
           gssize       len,  
           const gchar *to_codeset,
           const gchar *from_codeset,
           gsize       *bytes_read, 
	   gsize       *bytes_written, 
	   GError     **error)
{
  gchar *res;
#ifdef HAVE_ICONV_H
  GIConv cd;
  
  g_return_val_if_fail (str != NULL, NULL);
  g_return_val_if_fail (to_codeset != NULL, NULL);
  g_return_val_if_fail (from_codeset != NULL, NULL);

  cd = open_converter (to_codeset, from_codeset, error);

  if (cd == (GIConv) -1)
    {
      if (bytes_read)
        *bytes_read = 0;
      
      if (bytes_written)
        *bytes_written = 0;
      
      return NULL;
    }

  res = g_convert_with_iconv (str, len, cd,
			      bytes_read, bytes_written,
			      error);
  
  g_iconv_close (cd);
#else
  res = g_strdup(str);
#endif

  return res;
}

#ifdef HAVE_ICONV_H
gchar*
g_convert_with_iconv (const gchar *str,
		      gssize       len,
		      GIConv       converter,
		      gsize       *bytes_read, 
		      gsize       *bytes_written, 
		      GError     **error)
{
  gchar *dest;
  gchar *outp;
  const gchar *p;
  gsize inbytes_remaining;
  gsize outbytes_remaining;
  gsize err;
  gsize outbuf_size;
  gboolean have_error = FALSE;
  
  g_return_val_if_fail (str != NULL, NULL);
  g_return_val_if_fail (converter != (GIConv) -1, NULL);
     
  if (len < 0)
    len = strlen (str);

  p = str;
  inbytes_remaining = len;
  outbuf_size = len + 1; /* + 1 for nul in case len == 1 */
  
  outbytes_remaining = outbuf_size - 1; /* -1 for nul */
  outp = dest = g_malloc (outbuf_size);

 again:
  
  err = g_iconv (converter, (char **)&p, &inbytes_remaining, &outp, &outbytes_remaining);

  if (err == (size_t) -1)
    {
      switch (errno)
	{
	case EINVAL:
	  /* Incomplete text, do not report an error */
	  break;
	case E2BIG:
	  {
	    size_t used = outp - dest;

	    outbuf_size *= 2;
	    dest = g_realloc (dest, outbuf_size);
		
	    outp = dest + used;
	    outbytes_remaining = outbuf_size - used - 1; /* -1 for nul */

	    goto again;
	  }
	case EILSEQ:
	  g_warning("Invalid byte sequence in conversion input");
	  have_error = TRUE;
	  break;
	default:
	  g_warning("Error during conversion: %s", strerror (errno));
	  have_error = TRUE;
	  break;
	}
    }

  *outp = '\0';
  
  if (bytes_read)
    *bytes_read = p - str;
  else
    {
      if ((p - str) != len) 
	{
          if (!have_error)
            {
              g_warning("Partial character sequence at end of input");
              have_error = TRUE;
            }
	}
    }

  if (bytes_written)
    *bytes_written = outp - dest;	/* Doesn't include '\0' */

  if (have_error)
    {
      g_free (dest);
      return NULL;
    }
  else
    return dest;
}
#endif

gchar *
g_locale_to_utf8 (const gchar  *opsysstring,
		  gssize        len,            
		  gsize        *bytes_read,    
		  gsize        *bytes_written,
		  GError      **error)
{
  const char *charset;

  if (g_get_charset (&charset))
    return g_strdup (opsysstring);
  else
    return g_convert (opsysstring, len, 
		      "UTF-8", charset, bytes_read, bytes_written, error);
}

gchar *
g_locale_from_utf8 (const gchar *utf8string,
		    gssize       len,            
		    gsize       *bytes_read,    
		    gsize       *bytes_written,
		    GError     **error)
{
  const gchar *charset;

  if (g_get_charset (&charset))
    return g_strdup (utf8string);
  else
    return g_convert (utf8string, len,
		      charset, "UTF-8", bytes_read, bytes_written, error);
}

#endif
