/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 *
 * m3u8.c:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <glib.h>
#include <string.h>

#include "gsthls.h"
#include "m3u8.h"

#define GST_CAT_DEFAULT hls_debug

static GstM3U8 *gst_m3u8_new (void);
static void gst_m3u8_free (GstM3U8 * m3u8);
static gboolean gst_m3u8_update (GstM3U8Client * client, GstM3U8 * m3u8,
    gchar * data, gboolean * updated);
static GstM3U8MediaFile *gst_m3u8_media_file_new (gchar * uri,
    gchar * title, GstClockTime duration, guint sequence);
static void gst_m3u8_media_file_free (GstM3U8MediaFile * self);
gchar *uri_join (const gchar * uri, const gchar * path);

static GstM3U8 *
gst_m3u8_new (void)
{
  GstM3U8 *m3u8;

  m3u8 = g_new0 (GstM3U8, 1);

  return m3u8;
}

static void
gst_m3u8_set_uri (GstM3U8 * self, gchar * uri, gchar * base_uri, gchar * name)
{
  g_return_if_fail (self != NULL);

  g_free (self->uri);
  self->uri = uri;

  g_free (self->base_uri);
  self->base_uri = base_uri;

  g_free (self->name);
  self->name = name;
}

static void
gst_m3u8_free (GstM3U8 * self)
{
  g_return_if_fail (self != NULL);

  g_free (self->uri);
  g_free (self->base_uri);
  g_free (self->name);
  g_free (self->codecs);

  g_list_foreach (self->files, (GFunc) gst_m3u8_media_file_free, NULL);
  g_list_free (self->files);

  g_free (self->last_data);
  g_list_foreach (self->lists, (GFunc) gst_m3u8_free, NULL);
  g_list_free (self->lists);
  g_list_foreach (self->iframe_lists, (GFunc) gst_m3u8_free, NULL);
  g_list_free (self->iframe_lists);

  g_free (self);
}

static GstM3U8MediaFile *
gst_m3u8_media_file_new (gchar * uri, gchar * title, GstClockTime duration,
    guint sequence)
{
  GstM3U8MediaFile *file;

  file = g_new0 (GstM3U8MediaFile, 1);
  file->uri = uri;
  file->title = title;
  file->duration = duration;
  file->sequence = sequence;

  return file;
}

static void
gst_m3u8_media_file_free (GstM3U8MediaFile * self)
{
  g_return_if_fail (self != NULL);

  g_free (self->title);
  g_free (self->uri);
  g_free (self->key);
  g_free (self);
}

static gboolean
int_from_string (gchar * ptr, gchar ** endptr, gint * val)
{
  gchar *end;
  gint64 ret;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  errno = 0;
  ret = g_ascii_strtoll (ptr, &end, 10);
  if ((errno == ERANGE && (ret == G_MAXINT64 || ret == G_MININT64))
      || (errno != 0 && ret == 0)) {
    GST_WARNING ("%s", g_strerror (errno));
    return FALSE;
  }

  if (ret > G_MAXINT || ret < G_MININT) {
    GST_WARNING ("%s", g_strerror (ERANGE));
    return FALSE;
  }

  if (endptr)
    *endptr = end;

  *val = (gint) ret;

  return end != ptr;
}

static gboolean
int64_from_string (gchar * ptr, gchar ** endptr, gint64 * val)
{
  gchar *end;
  gint64 ret;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  errno = 0;
  ret = g_ascii_strtoll (ptr, &end, 10);
  if ((errno == ERANGE && (ret == G_MAXINT64 || ret == G_MININT64))
      || (errno != 0 && ret == 0)) {
    GST_WARNING ("%s", g_strerror (errno));
    return FALSE;
  }

  if (endptr)
    *endptr = end;

  *val = ret;

  return end != ptr;
}

static gboolean
double_from_string (gchar * ptr, gchar ** endptr, gdouble * val)
{
  gchar *end;
  gdouble ret;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  errno = 0;
  ret = g_ascii_strtod (ptr, &end);
  if ((errno == ERANGE && (ret == HUGE_VAL || ret == -HUGE_VAL))
      || (errno != 0 && ret == 0)) {
    GST_WARNING ("%s", g_strerror (errno));
    return FALSE;
  }

  if (!isfinite (ret)) {
    GST_WARNING ("%s", g_strerror (ERANGE));
    return FALSE;
  }

  if (endptr)
    *endptr = end;

  *val = (gdouble) ret;

  return end != ptr;
}

static gboolean
parse_attributes (gchar ** ptr, gchar ** a, gchar ** v)
{
  gchar *end = NULL, *p, *ve;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (*ptr != NULL, FALSE);
  g_return_val_if_fail (a != NULL, FALSE);
  g_return_val_if_fail (v != NULL, FALSE);

  /* [attribute=value,]* */

  *a = *ptr;
  end = p = g_utf8_strchr (*ptr, -1, ',');
  if (end) {
    gchar *q = g_utf8_strchr (*ptr, -1, '"');
    if (q && q < end) {
      /* special case, such as CODECS="avc1.77.30, mp4a.40.2" */
      q = g_utf8_next_char (q);
      if (q) {
        q = g_utf8_strchr (q, -1, '"');
      }
      if (q) {
        end = p = g_utf8_strchr (q, -1, ',');
      }
    }
  }
  if (end) {
    do {
      end = g_utf8_next_char (end);
    } while (end && *end == ' ');
    *p = '\0';
  }

  *v = p = g_utf8_strchr (*ptr, -1, '=');
  if (*v) {
    *p = '\0';
    *v = g_utf8_next_char (*v);
    if (**v == '"') {
      ve = g_utf8_next_char (*v);
      if (ve) {
        ve = g_utf8_strchr (ve, -1, '"');
      }
      if (ve) {
        *v = g_utf8_next_char (*v);
        *ve = '\0';
      } else {
        GST_WARNING ("Cannot remove quotation marks from %s", *a);
      }
    }
  } else {
    GST_WARNING ("missing = after attribute");
    return FALSE;
  }

  *ptr = end;
  return TRUE;
}

static gint
_m3u8_compare_uri (GstM3U8 * a, gchar * uri)
{
  g_return_val_if_fail (a != NULL, 0);
  g_return_val_if_fail (uri != NULL, 0);

  return g_strcmp0 (a->uri, uri);
}

static gint
gst_m3u8_compare_playlist_by_bitrate (gconstpointer a, gconstpointer b)
{
  return ((GstM3U8 *) (a))->bandwidth - ((GstM3U8 *) (b))->bandwidth;
}

/*
 * @data: a m3u8 playlist text data, taking ownership
 */
static gboolean
gst_m3u8_update (GstM3U8Client * client, GstM3U8 * self, gchar * data,
    gboolean * updated)
{
  gint val;
  GstClockTime duration;
  gchar *title, *end;
  gboolean discontinuity = FALSE;
  GstM3U8 *list;
  gchar *current_key = NULL;
  gboolean have_iv = FALSE;
  guint8 iv[16] = { 0, };
  gint64 size = -1, offset = -1;
  gint64 mediasequence;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (updated != NULL, FALSE);

  *updated = TRUE;

  /* check if the data changed since last update */
  if (self->last_data && g_str_equal (self->last_data, data)) {
    GST_DEBUG ("Playlist is the same as previous one");
    *updated = FALSE;
    g_free (data);
    return TRUE;
  }

  if (!g_str_has_prefix (data, "#EXTM3U")) {
    GST_WARNING ("Data doesn't start with #EXTM3U");
    *updated = FALSE;
    g_free (data);
    return FALSE;
  }

  GST_TRACE ("data:\n%s", data);

  g_free (self->last_data);
  self->last_data = data;

  client->current_file = NULL;
  if (self->files) {
    g_list_foreach (self->files, (GFunc) gst_m3u8_media_file_free, NULL);
    g_list_free (self->files);
    self->files = NULL;
  }
  client->duration = GST_CLOCK_TIME_NONE;
  mediasequence = 0;

  /* By default, allow caching */
  self->allowcache = TRUE;

  list = NULL;
  duration = 0;
  title = NULL;
  data += 7;
  while (TRUE) {
    gchar *r;

    end = g_utf8_strchr (data, -1, '\n');
    if (end)
      *end = '\0';

    r = g_utf8_strchr (data, -1, '\r');
    if (r)
      *r = '\0';

    if (data[0] != '#' && data[0] != '\0') {
      gchar *name = data;
      if (duration <= 0 && list == NULL) {
        GST_LOG ("%s: got line without EXTINF or EXTSTREAMINF, dropping", data);
        goto next_line;
      }

      data = uri_join (self->base_uri ? self->base_uri : self->uri, data);
      if (data == NULL)
        goto next_line;

      if (list != NULL) {
        if (g_list_find_custom (self->lists, data,
                (GCompareFunc) _m3u8_compare_uri)) {
          GST_DEBUG ("Already have a list with this URI");
          gst_m3u8_free (list);
          g_free (data);
        } else {
          gst_m3u8_set_uri (list, data, NULL, g_strdup (name));
          self->lists = g_list_append (self->lists, list);
        }
        list = NULL;
      } else {
        GstM3U8MediaFile *file;
        file = gst_m3u8_media_file_new (data, title, duration, mediasequence++);

        /* set encryption params */
        file->key = current_key ? g_strdup (current_key) : NULL;
        if (file->key) {
          if (have_iv) {
            memcpy (file->iv, iv, sizeof (iv));
          } else {
            guint8 *iv = file->iv + 12;
            GST_WRITE_UINT32_BE (iv, file->sequence);
          }
        }

        if (size != -1) {
          file->size = size;
          if (offset != -1) {
            file->offset = offset;
          } else {
            GstM3U8MediaFile *prev = self->files ? self->files->data : NULL;

            if (!prev) {
              offset = 0;
            } else {
              offset = prev->offset + prev->size;
            }
            file->offset = offset;
          }
        } else {
          file->size = -1;
          file->offset = 0;
        }

        file->discont = discontinuity;

        duration = 0;
        title = NULL;
        discontinuity = FALSE;
        size = offset = -1;
        self->files = g_list_prepend (self->files, file);
      }

    } else if (g_str_has_prefix (data, "#EXTINF:")) {
      gdouble fval;
      if (!double_from_string (data + 8, &data, &fval)) {
        GST_WARNING ("Can't read EXTINF duration");
        goto next_line;
      }
      duration = fval * (gdouble) GST_SECOND;
      if (self->targetduration > 0 && duration > self->targetduration) {
        GST_WARNING ("EXTINF duration (%" GST_TIME_FORMAT
            ") > TARGETDURATION (%" GST_TIME_FORMAT ")",
            GST_TIME_ARGS (duration), GST_TIME_ARGS (self->targetduration));
      }
      if (!data || *data != ',')
        goto next_line;
      data = g_utf8_next_char (data);
      if (data != end) {
        g_free (title);
        title = g_strdup (data);
      }
    } else if (g_str_has_prefix (data, "#EXT-X-")) {
      gchar *data_ext_x = data + 7;

      /* All these entries start with #EXT-X- */
      if (g_str_has_prefix (data_ext_x, "ENDLIST")) {
        self->endlist = TRUE;
      } else if (g_str_has_prefix (data_ext_x, "VERSION:")) {
        if (int_from_string (data + 15, &data, &val))
          self->version = val;
      } else if (g_str_has_prefix (data_ext_x, "STREAM-INF:") ||
          g_str_has_prefix (data_ext_x, "I-FRAME-STREAM-INF:")) {
        gchar *v, *a;
        gboolean iframe = g_str_has_prefix (data_ext_x, "I-FRAME-STREAM-INF:");
        GstM3U8 *new_list;

        new_list = gst_m3u8_new ();
        new_list->iframe = iframe;
        data = data + (iframe ? 26 : 18);
        while (data && parse_attributes (&data, &a, &v)) {
          if (g_str_equal (a, "BANDWIDTH")) {
            if (!int_from_string (v, NULL, &new_list->bandwidth))
              GST_WARNING ("Error while reading BANDWIDTH");
          } else if (g_str_equal (a, "PROGRAM-ID")) {
            if (!int_from_string (v, NULL, &new_list->program_id))
              GST_WARNING ("Error while reading PROGRAM-ID");
          } else if (g_str_equal (a, "CODECS")) {
            g_free (new_list->codecs);
            new_list->codecs = g_strdup (v);
          } else if (g_str_equal (a, "RESOLUTION")) {
            if (!int_from_string (v, &v, &new_list->width))
              GST_WARNING ("Error while reading RESOLUTION width");
            if (!v || *v != 'x') {
              GST_WARNING ("Missing height");
            } else {
              v = g_utf8_next_char (v);
              if (!int_from_string (v, NULL, &new_list->height))
                GST_WARNING ("Error while reading RESOLUTION height");
            }
          } else if (iframe && g_str_equal (a, "URI")) {
            gchar *name;
            gchar *uri;

            uri = uri_join (self->base_uri ? self->base_uri : self->uri, v);
            if (uri) {
              name = g_strdup (uri);
              gst_m3u8_set_uri (new_list, uri, NULL, name);
            }
          }
        }

        if (iframe) {
          if (g_list_find_custom (self->iframe_lists, new_list->uri,
                  (GCompareFunc) _m3u8_compare_uri)) {
            GST_DEBUG ("Already have a list with this URI");
            gst_m3u8_free (new_list);
          } else {
            self->iframe_lists = g_list_append (self->iframe_lists, new_list);
          }
        } else {
          if (list != NULL) {
            GST_WARNING ("Found a list without a uri..., dropping");
            gst_m3u8_free (list);
          }
          list = new_list;
        }
      } else if (g_str_has_prefix (data_ext_x, "TARGETDURATION:")) {
        if (int_from_string (data + 22, &data, &val))
          self->targetduration = val * GST_SECOND;
      } else if (g_str_has_prefix (data_ext_x, "MEDIA-SEQUENCE:")) {
        if (int_from_string (data + 22, &data, &val))
          mediasequence = val;
      } else if (g_str_has_prefix (data_ext_x, "DISCONTINUITY")) {
        discontinuity = TRUE;
      } else if (g_str_has_prefix (data_ext_x, "PROGRAM-DATE-TIME:")) {
        /* <YYYY-MM-DDThh:mm:ssZ> */
        GST_DEBUG ("FIXME parse date");
      } else if (g_str_has_prefix (data_ext_x, "ALLOW-CACHE:")) {
        self->allowcache = g_ascii_strcasecmp (data + 19, "YES") == 0;
      } else if (g_str_has_prefix (data_ext_x, "KEY:")) {
        gchar *v, *a;

        data = data + 11;

        /* IV and KEY are only valid until the next #EXT-X-KEY */
        have_iv = FALSE;
        g_free (current_key);
        current_key = NULL;
        while (data && parse_attributes (&data, &a, &v)) {
          if (g_str_equal (a, "URI")) {
            current_key =
                uri_join (self->base_uri ? self->base_uri : self->uri, v);
          } else if (g_str_equal (a, "IV")) {
            gchar *ivp = v;
            gint i;

            if (strlen (ivp) < 32 + 2 || (!g_str_has_prefix (ivp, "0x")
                    && !g_str_has_prefix (ivp, "0X"))) {
              GST_WARNING ("Can't read IV");
              continue;
            }

            ivp += 2;
            for (i = 0; i < 16; i++) {
              gint h, l;

              h = g_ascii_xdigit_value (*ivp);
              ivp++;
              l = g_ascii_xdigit_value (*ivp);
              ivp++;
              if (h == -1 || l == -1) {
                i = -1;
                break;
              }
              iv[i] = (h << 4) | l;
            }

            if (i == -1) {
              GST_WARNING ("Can't read IV");
              continue;
            }
            have_iv = TRUE;
          } else if (g_str_equal (a, "METHOD")) {
            if (!g_str_equal (v, "AES-128")) {
              GST_WARNING ("Encryption method %s not supported", v);
              continue;
            }
          }
        }
      } else if (g_str_has_prefix (data_ext_x, "BYTERANGE:")) {
        gchar *v = data + 17;

        if (int64_from_string (v, &v, &size)) {
          if (*v == '@' && !int64_from_string (v + 1, &v, &offset))
            goto next_line;
        } else {
          goto next_line;
        }
      } else {
        GST_LOG ("Ignored line: %s", data);
      }
    } else {
      GST_LOG ("Ignored line: %s", data);
    }

  next_line:
    if (!end)
      break;
    data = g_utf8_next_char (end);      /* skip \n */
  }

  g_free (current_key);
  current_key = NULL;

  self->files = g_list_reverse (self->files);

  /* reorder playlists by bitrate */
  if (self->lists) {
    gchar *top_variant_uri = NULL;
    gboolean iframe = FALSE;

    if (!self->current_variant) {
      top_variant_uri = GST_M3U8 (self->lists->data)->uri;
    } else {
      top_variant_uri = GST_M3U8 (self->current_variant->data)->uri;
      iframe = GST_M3U8 (self->current_variant->data)->iframe;
    }

    self->lists =
        g_list_sort (self->lists,
        (GCompareFunc) gst_m3u8_compare_playlist_by_bitrate);

    self->iframe_lists =
        g_list_sort (self->iframe_lists,
        (GCompareFunc) gst_m3u8_compare_playlist_by_bitrate);

    if (iframe)
      self->current_variant =
          g_list_find_custom (self->iframe_lists, top_variant_uri,
          (GCompareFunc) _m3u8_compare_uri);
    else
      self->current_variant = g_list_find_custom (self->lists, top_variant_uri,
          (GCompareFunc) _m3u8_compare_uri);
  }
  /* calculate the start and end times of this media playlist. */
  if (self->files) {
    GList *walk;
    GstM3U8MediaFile *file;
    GstClockTime duration = 0;

    for (walk = self->files; walk; walk = walk->next) {
      file = walk->data;
      duration += file->duration;
      if (file->sequence > client->highest_sequence_number) {
        if (client->highest_sequence_number >= 0) {
          /* if an update of the media playlist has been missed, there
             will be a gap between self->highest_sequence_number and the
             first sequence number in this media playlist. In this situation
             assume that the missing fragments had a duration of
             targetduration each */
          client->last_file_end +=
              (file->sequence - client->highest_sequence_number -
              1) * self->targetduration;
        }
        client->last_file_end += file->duration;
        client->highest_sequence_number = file->sequence;
      }
    }
    if (GST_M3U8_CLIENT_IS_LIVE (client)) {
      client->first_file_start = client->last_file_end - duration;
      GST_DEBUG ("Live playlist range %" GST_TIME_FORMAT " -> %"
          GST_TIME_FORMAT, GST_TIME_ARGS (client->first_file_start),
          GST_TIME_ARGS (client->last_file_end));
    }
    client->duration = duration;
  }

  return TRUE;
}

GstM3U8Client *
gst_m3u8_client_new (const gchar * uri, const gchar * base_uri)
{
  GstM3U8Client *client;

  g_return_val_if_fail (uri != NULL, NULL);

  client = g_new0 (GstM3U8Client, 1);
  client->main = gst_m3u8_new ();
  client->current = NULL;
  client->current_file = NULL;
  client->current_file_duration = GST_CLOCK_TIME_NONE;
  client->sequence = -1;
  client->sequence_position = 0;
  client->highest_sequence_number = -1;
  client->duration = GST_CLOCK_TIME_NONE;
  g_mutex_init (&client->lock);
  gst_m3u8_set_uri (client->main, g_strdup (uri), g_strdup (base_uri), NULL);

  return client;
}

void
gst_m3u8_client_free (GstM3U8Client * self)
{
  g_return_if_fail (self != NULL);

  gst_m3u8_free (self->main);
  g_mutex_clear (&self->lock);
  g_free (self);
}

void
gst_m3u8_client_set_current (GstM3U8Client * self, GstM3U8 * m3u8)
{
  g_return_if_fail (self != NULL);

  GST_M3U8_CLIENT_LOCK (self);
  if (m3u8 != self->current) {
    self->current = m3u8;
    self->duration = GST_CLOCK_TIME_NONE;
    self->current_file = NULL;
  }
  GST_M3U8_CLIENT_UNLOCK (self);
}

gboolean
gst_m3u8_client_update (GstM3U8Client * self, gchar * data)
{
  GstM3U8 *m3u8;
  gboolean updated = FALSE;
  gboolean ret = FALSE;

  g_return_val_if_fail (self != NULL, FALSE);

  GST_M3U8_CLIENT_LOCK (self);
  m3u8 = self->current ? self->current : self->main;

  if (!gst_m3u8_update (self, m3u8, data, &updated))
    goto out;

  if (!updated)
    goto out;

  if (self->current && !self->current->files) {
    GST_ERROR ("Invalid media playlist, it does not contain any media files");
    goto out;
  }

  /* select the first playlist, for now */
  if (!self->current) {
    if (self->main->lists) {
      self->current = self->main->current_variant->data;
    } else {
      self->current = self->main;
    }
  }

  if (m3u8->files && self->sequence == -1) {
    if (GST_M3U8_CLIENT_IS_LIVE (self)) {
      /* for live streams, start GST_M3U8_LIVE_MIN_FRAGMENT_DISTANCE from
         the end of the playlist. See section 6.3.3 of HLS draft */
      gint pos =
          g_list_length (m3u8->files) - GST_M3U8_LIVE_MIN_FRAGMENT_DISTANCE;
      self->current_file = g_list_nth (m3u8->files, pos >= 0 ? pos : 0);
    } else {
      self->current_file = g_list_first (m3u8->files);
    }
    self->sequence = GST_M3U8_MEDIA_FILE (self->current_file->data)->sequence;
    self->sequence_position = 0;
    GST_DEBUG ("Setting first sequence at %u", (guint) self->sequence);
  }

  ret = TRUE;
out:
  GST_M3U8_CLIENT_UNLOCK (self);
  return ret;
}

static gint
_find_m3u8_list_match (const GstM3U8 * a, const GstM3U8 * b)
{
  if (g_strcmp0 (a->name, b->name) == 0 &&
      a->bandwidth == b->bandwidth &&
      a->program_id == b->program_id &&
      g_strcmp0 (a->codecs, b->codecs) == 0 &&
      a->width == b->width &&
      a->height == b->height && a->iframe == b->iframe) {
    return 0;
  }

  return 1;
}

gboolean
gst_m3u8_client_update_variant_playlist (GstM3U8Client * self, gchar * data,
    const gchar * uri, const gchar * base_uri)
{
  gboolean ret = FALSE;
  GList *list_entry, *unmatched_lists;
  GstM3U8Client *new_client;
  GstM3U8 *old;

  g_return_val_if_fail (self != NULL, FALSE);

  new_client = gst_m3u8_client_new (uri, base_uri);
  if (gst_m3u8_client_update (new_client, data)) {
    if (!new_client->main->lists) {
      GST_ERROR
          ("Cannot update variant playlist: New playlist is not a variant playlist");
      gst_m3u8_client_free (new_client);
      return FALSE;
    }

    GST_M3U8_CLIENT_LOCK (self);

    if (!self->main->lists) {
      GST_ERROR
          ("Cannot update variant playlist: Current playlist is not a variant playlist");
      goto out;
    }

    /* Now see if the variant playlist still has the same lists */
    unmatched_lists = g_list_copy (self->main->lists);
    for (list_entry = new_client->main->lists; list_entry;
        list_entry = list_entry->next) {
      GList *match = g_list_find_custom (unmatched_lists, list_entry->data,
          (GCompareFunc) _find_m3u8_list_match);
      if (match)
        unmatched_lists = g_list_delete_link (unmatched_lists, match);
    }

    if (unmatched_lists != NULL) {
      GST_WARNING ("Unable to match all playlists");

      for (list_entry = unmatched_lists; list_entry;
          list_entry = list_entry->next) {
        if (list_entry->data == self->current) {
          GST_WARNING ("Unable to match current playlist");
        }
      }

      g_list_free (unmatched_lists);
    }

    /* Switch out the variant playlist, steal it from new_client */
    old = self->main;

    self->main = new_client->main;
    new_client->main = gst_m3u8_new ();

    if (self->main->lists)
      self->current = self->main->current_variant->data;
    else
      self->current = self->main;

    gst_m3u8_free (old);

    ret = TRUE;

  out:
    GST_M3U8_CLIENT_UNLOCK (self);
  }

  gst_m3u8_client_free (new_client);
  return ret;
}

static GList *
find_next_fragment (GstM3U8Client * client, GList * l, gboolean forward)
{
  GstM3U8MediaFile *file;

  if (forward) {
    while (l) {
      file = l->data;

      if (file->sequence >= client->sequence)
        break;

      l = l->next;
    }
  } else {
    l = g_list_last (l);

    while (l) {
      file = l->data;

      if (file->sequence <= client->sequence)
        break;

      l = l->prev;
    }
  }

  return l;
}

static gboolean
has_next_fragment (GstM3U8Client * client, GList * l, gboolean forward)
{
  l = find_next_fragment (client, l, forward);

  if (l) {
    return (forward && l->next) || (!forward && l->prev);
  }

  return FALSE;
}

gboolean
gst_m3u8_client_get_next_fragment (GstM3U8Client * client,
    gboolean * discontinuity, gchar ** uri, GstClockTime * duration,
    GstClockTime * timestamp, gint64 * range_start, gint64 * range_end,
    gchar ** key, guint8 ** iv, gboolean forward)
{
  GstM3U8MediaFile *file;

  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->current != NULL, FALSE);

  GST_M3U8_CLIENT_LOCK (client);
  GST_DEBUG ("Looking for fragment %" G_GINT64_FORMAT, client->sequence);
  if (client->sequence < 0) {
    GST_M3U8_CLIENT_UNLOCK (client);
    return FALSE;
  }
  if (!client->current_file) {
    client->current_file =
        find_next_fragment (client, client->current->files, forward);
  }

  if (!client->current_file) {
    GST_M3U8_CLIENT_UNLOCK (client);
    return FALSE;
  }

  file = GST_M3U8_MEDIA_FILE (client->current_file->data);
  GST_DEBUG ("Got fragment with sequence %u (client sequence %u)",
      (guint) file->sequence, (guint) client->sequence);

  client->current_file_duration = file->duration;
  if (timestamp)
    *timestamp = client->sequence_position;

  if (discontinuity)
    *discontinuity = client->sequence != file->sequence || file->discont;
  if (uri)
    *uri = g_strdup (file->uri);
  if (duration)
    *duration = file->duration;
  if (range_start)
    *range_start = file->offset;
  if (range_end)
    *range_end = file->size != -1 ? file->offset + file->size - 1 : -1;
  if (key)
    *key = g_strdup (file->key);
  if (iv) {
    *iv = g_new (guint8, sizeof (file->iv));
    memcpy (*iv, file->iv, sizeof (file->iv));
  }

  client->sequence = file->sequence;

  GST_M3U8_CLIENT_UNLOCK (client);
  return TRUE;
}

gboolean
gst_m3u8_client_has_next_fragment (GstM3U8Client * client, gboolean forward)
{
  gboolean ret;

  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->current != NULL, FALSE);

  GST_M3U8_CLIENT_LOCK (client);
  GST_DEBUG ("Checking if has next fragment %" G_GINT64_FORMAT,
      client->sequence + (forward ? 1 : -1));
  if (client->current_file) {
    ret =
        (forward ? client->current_file->next : client->current_file->prev) !=
        NULL;
  } else {
    ret = has_next_fragment (client, client->current->files, forward);
  }
  GST_M3U8_CLIENT_UNLOCK (client);
  return ret;
}

static void
alternate_advance (GstM3U8Client * client, gboolean forward)
{
  gint targetnum = client->sequence;
  GList *tmp;
  GstM3U8MediaFile *mf;

  /* figure out the target seqnum */
  if (forward)
    targetnum += 1;
  else
    targetnum -= 1;

  for (tmp = client->current->files; tmp; tmp = tmp->next) {
    mf = (GstM3U8MediaFile *) tmp->data;
    if (mf->sequence == targetnum)
      break;
  }
  if (tmp == NULL) {
    GST_WARNING ("Can't find next fragment");
    return;
  }
  client->current_file = tmp;
  client->sequence = targetnum;
  client->current_file_duration =
      GST_M3U8_MEDIA_FILE (client->current_file->data)->duration;
}

void
gst_m3u8_client_advance_fragment (GstM3U8Client * client, gboolean forward)
{
  GstM3U8MediaFile *file;

  g_return_if_fail (client != NULL);
  g_return_if_fail (client->current != NULL);

  GST_M3U8_CLIENT_LOCK (client);
  GST_DEBUG ("Sequence position was %" GST_TIME_FORMAT,
      GST_TIME_ARGS (client->sequence_position));
  if (GST_CLOCK_TIME_IS_VALID (client->current_file_duration)) {
    /* Advance our position based on the previous fragment we played */
    if (forward)
      client->sequence_position += client->current_file_duration;
    else if (client->current_file_duration < client->sequence_position)
      client->sequence_position -= client->current_file_duration;
    else
      client->sequence_position = 0;
    client->current_file_duration = GST_CLOCK_TIME_NONE;
    GST_DEBUG ("Sequence position now %" GST_TIME_FORMAT,
        GST_TIME_ARGS (client->sequence_position));
  }
  if (!client->current_file) {
    GList *l;

    GST_DEBUG ("Looking for fragment %" G_GINT64_FORMAT, client->sequence);
    for (l = client->current->files; l != NULL; l = l->next) {
      if (GST_M3U8_MEDIA_FILE (l->data)->sequence == client->sequence) {
        client->current_file = l;
        break;
      }
    }
    if (client->current_file == NULL) {
      GST_DEBUG
          ("Could not find current fragment, trying next fragment directly");
      alternate_advance (client, forward);

      /* Resync sequence number if the above has failed for live streams */
      if (client->current_file == NULL && GST_M3U8_CLIENT_IS_LIVE (client)) {
        /* for live streams, start GST_M3U8_LIVE_MIN_FRAGMENT_DISTANCE from
           the end of the playlist. See section 6.3.3 of HLS draft */
        gint pos =
            g_list_length (client->current->files) -
            GST_M3U8_LIVE_MIN_FRAGMENT_DISTANCE;
        client->current_file =
            g_list_nth (client->current->files, pos >= 0 ? pos : 0);
        client->current_file_duration =
            GST_M3U8_MEDIA_FILE (client->current_file->data)->duration;

        GST_WARNING ("Resyncing live playlist");
      }
      GST_M3U8_CLIENT_UNLOCK (client);
      return;
    }
  }

  file = GST_M3U8_MEDIA_FILE (client->current_file->data);
  GST_DEBUG ("Advancing from sequence %u", (guint) file->sequence);
  if (forward) {
    client->current_file = client->current_file->next;
    if (client->current_file) {
      client->sequence =
          GST_M3U8_MEDIA_FILE (client->current_file->data)->sequence;
    } else {
      client->sequence = file->sequence + 1;
    }
  } else {
    client->current_file = client->current_file->prev;
    if (client->current_file) {
      client->sequence =
          GST_M3U8_MEDIA_FILE (client->current_file->data)->sequence;
    } else {
      client->sequence = file->sequence - 1;
    }
  }
  if (client->current_file) {
    /* Store duration of the fragment we're using to update the position 
     * the next time we advance */
    client->current_file_duration =
        GST_M3U8_MEDIA_FILE (client->current_file->data)->duration;
  }
  GST_M3U8_CLIENT_UNLOCK (client);
}

static void
_sum_duration (GstM3U8MediaFile * self, GstClockTime * duration)
{
  *duration += self->duration;
}

GstClockTime
gst_m3u8_client_get_duration (GstM3U8Client * client)
{
  GstClockTime duration = GST_CLOCK_TIME_NONE;

  g_return_val_if_fail (client != NULL, GST_CLOCK_TIME_NONE);

  GST_M3U8_CLIENT_LOCK (client);
  /* We can only get the duration for on-demand streams */
  if (!client->current || !client->current->endlist) {
    GST_M3U8_CLIENT_UNLOCK (client);
    return GST_CLOCK_TIME_NONE;
  }

  if (!GST_CLOCK_TIME_IS_VALID (client->duration) && client->current->files) {
    client->duration = 0;
    g_list_foreach (client->current->files, (GFunc) _sum_duration,
        &client->duration);
  }
  duration = client->duration;
  GST_M3U8_CLIENT_UNLOCK (client);

  return duration;
}

GstClockTime
gst_m3u8_client_get_target_duration (GstM3U8Client * client)
{
  GstClockTime duration = 0;

  g_return_val_if_fail (client != NULL, GST_CLOCK_TIME_NONE);

  GST_M3U8_CLIENT_LOCK (client);
  duration = client->current->targetduration;
  GST_M3U8_CLIENT_UNLOCK (client);
  return duration;
}

gchar *
gst_m3u8_client_get_uri (GstM3U8Client * client)
{
  gchar *uri;

  g_return_val_if_fail (client != NULL, NULL);

  GST_M3U8_CLIENT_LOCK (client);
  uri = client->main ? g_strdup (client->main->uri) : NULL;
  GST_M3U8_CLIENT_UNLOCK (client);
  return uri;
}

gchar *
gst_m3u8_client_get_current_uri (GstM3U8Client * client)
{
  gchar *uri;

  g_return_val_if_fail (client != NULL, NULL);

  GST_M3U8_CLIENT_LOCK (client);
  uri = g_strdup (client->current->uri);
  GST_M3U8_CLIENT_UNLOCK (client);
  return uri;
}

gboolean
gst_m3u8_client_has_variant_playlist (GstM3U8Client * client)
{
  gboolean ret;

  g_return_val_if_fail (client != NULL, FALSE);

  GST_M3U8_CLIENT_LOCK (client);
  ret = (client->main->lists != NULL);
  GST_M3U8_CLIENT_UNLOCK (client);
  return ret;
}

gboolean
gst_m3u8_client_is_live (GstM3U8Client * client)
{
  gboolean ret;

  g_return_val_if_fail (client != NULL, FALSE);

  GST_M3U8_CLIENT_LOCK (client);
  ret = GST_M3U8_CLIENT_IS_LIVE (client);
  GST_M3U8_CLIENT_UNLOCK (client);
  return ret;
}

GList *
gst_m3u8_client_get_playlist_for_bitrate (GstM3U8Client * client, guint bitrate)
{
  GList *list, *current_variant;

  GST_M3U8_CLIENT_LOCK (client);
  current_variant = client->main->current_variant;

  /*  Go to the highest possible bandwidth allowed */
  while (GST_M3U8 (current_variant->data)->bandwidth <= bitrate) {
    list = g_list_next (current_variant);
    if (!list)
      break;
    current_variant = list;
  }

  while (GST_M3U8 (current_variant->data)->bandwidth > bitrate) {
    list = g_list_previous (current_variant);
    if (!list)
      break;
    current_variant = list;
  }
  GST_M3U8_CLIENT_UNLOCK (client);

  return current_variant;
}

gchar *
uri_join (const gchar * uri1, const gchar * uri2)
{
  gchar *uri_copy, *tmp, *ret = NULL;

  if (gst_uri_is_valid (uri2))
    return g_strdup (uri2);

  uri_copy = g_strdup (uri1);
  if (uri2[0] != '/') {
    /* uri2 is a relative uri2 */
    /* look for query params */
    tmp = g_utf8_strchr (uri_copy, -1, '?');
    if (tmp) {
      /* find last / char, ignoring query params */
      tmp = g_utf8_strrchr (uri_copy, tmp - uri_copy, '/');
    } else {
      /* find last / char in URL */
      tmp = g_utf8_strrchr (uri_copy, -1, '/');
    }
    if (!tmp) {
      GST_WARNING ("Can't build a valid uri_copy");
      goto out;
    }

    *tmp = '\0';
    ret = g_strdup_printf ("%s/%s", uri_copy, uri2);
  } else {
    /* uri2 is an absolute uri2 */
    char *scheme, *hostname;

    scheme = uri_copy;
    /* find the : in <scheme>:// */
    tmp = g_utf8_strchr (uri_copy, -1, ':');
    if (!tmp) {
      GST_WARNING ("Can't build a valid uri_copy");
      goto out;
    }

    *tmp = '\0';

    /* skip :// */
    hostname = tmp + 3;

    tmp = g_utf8_strchr (hostname, -1, '/');
    if (tmp)
      *tmp = '\0';

    ret = g_strdup_printf ("%s://%s%s", scheme, hostname, uri2);
  }

out:
  g_free (uri_copy);
  return ret;
}

gboolean
gst_m3u8_client_get_seek_range (GstM3U8Client * client, gint64 * start,
    gint64 * stop)
{
  GstClockTime duration = 0;
  GList *walk;
  GstM3U8MediaFile *file;
  guint count;
  guint min_distance = 0;

  g_return_val_if_fail (client != NULL, FALSE);

  GST_M3U8_CLIENT_LOCK (client);

  if (client->current == NULL || client->current->files == NULL) {
    GST_M3U8_CLIENT_UNLOCK (client);
    return FALSE;
  }

  if (GST_M3U8_CLIENT_IS_LIVE (client)) {
    /* min_distance is used to make sure the seek range is never closer than
       GST_M3U8_LIVE_MIN_FRAGMENT_DISTANCE fragments from the end of a live
       playlist - see 6.3.3. "Playing the Playlist file" of the HLS draft */
    min_distance = GST_M3U8_LIVE_MIN_FRAGMENT_DISTANCE;
  }
  count = g_list_length (client->current->files);

  for (walk = client->current->files;
      walk && count >= min_distance; walk = walk->next) {
    file = walk->data;
    --count;
    duration += file->duration;
  }

  if (duration <= 0) {
    GST_M3U8_CLIENT_UNLOCK (client);
    return FALSE;
  }
  *start = client->first_file_start;
  *stop = *start + duration;
  GST_M3U8_CLIENT_UNLOCK (client);
  return TRUE;
}
