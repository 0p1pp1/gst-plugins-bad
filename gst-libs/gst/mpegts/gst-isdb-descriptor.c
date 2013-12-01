/*
 * gst-isdb-descriptor.c -
 * Copyright (C) 2015 0p1pp1
 *
 * Authors:
 *   0p1pp1@fms.freenet
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/>.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mpegts.h"
#include "gstmpegts-private.h"

/**
 * SECTION:gst-isdb-descriptor
 * @title: ISDB variants of MPEG-TS descriptors
 * @short_description: Descriptors for the various ISDB specifications
 * @include: gst/mpegts/mpegts.h
 */

/* copied from gst-dvb-descriptor.c */
#define DEFINE_STATIC_COPY_FUNCTION(type, name) \
static type * _##name##_copy (type * source) \
{ \
  return g_slice_dup (type, source); \
}

#define DEFINE_STATIC_FREE_FUNCTION(type, name) \
static void _##name##_free (type * source) \
{ \
  g_slice_free (type, source); \
}

/* GST_MTS_DESC_ISDB_SERIES (0xD5) */

static GstMpegtsIsdbEventSeries *
_gst_mpegts_isdb_event_series_copy (GstMpegtsIsdbEventSeries * source)
{
  GstMpegtsIsdbEventSeries *copy;

  copy = g_slice_dup (GstMpegtsIsdbEventSeries, source);
  copy->expire_date = gst_date_time_ref (source->expire_date);
  copy->series_name = g_strdup (source->series_name);

  return copy;
}

static void
_gst_mpegts_isdb_event_series_free (GstMpegtsIsdbEventSeries * source)
{
  g_free (source->series_name);
  gst_date_time_unref (source->expire_date);
  g_slice_free (GstMpegtsIsdbEventSeries, source);
}

G_DEFINE_BOXED_TYPE (GstMpegtsIsdbEventSeries, gst_mpegts_isdb_event_series,
    (GBoxedCopyFunc) _gst_mpegts_isdb_event_series_copy,
    (GFreeFunc) _gst_mpegts_isdb_event_series_free);

/**
 * gst_mpegts_descriptor_parse_series:
 * @descriptor: a %GST_MTS_DESC_ISDB_SERIES #GstMpegtsDescriptor
 * @res: (out) (transfer full): the #GstMpegtsIsdbEventSeries to fill
 *
 * Extracts the event series info from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_series (const GstMpegtsDescriptor * descriptor,
    GstMpegtsIsdbEventSeries ** res)
{
  guint8 *data;
  GstMpegtsIsdbEventSeries *desc;

  g_return_val_if_fail (descriptor != NULL && res != NULL, FALSE);
  __common_desc_checks (descriptor, GST_MTS_DESC_ISDB_SERIES, 8, FALSE);

  desc = g_slice_new0 (GstMpegtsIsdbEventSeries);
  *res = desc;

  data = (guint8 *) descriptor->data + 2;
  desc->series_id = GST_READ_UINT16_BE (data);
  data += 2;
  desc->repeat_label = *data >> 4;
  desc->program_pattern = (*data & 0x0E) >> 1;
  if (*data & 0x01) {
    guint year, month, day;
    guint16 mjd = GST_READ_UINT16_BE (data + 1);

    /* See EN 300 468 Annex C */
    year = (guint32) (((mjd - 15078.2) / 365.25));
    month = (guint8) ((mjd - 14956.1 - (guint) (year * 365.25)) / 30.6001);
    day = mjd - 14956 - (guint) (year * 365.25) - (guint) (month * 30.6001);
    if (month == 14 || month == 15) {
      year++;
      month = month - 1 - 12;
    } else {
      month--;
    }
    year += 1900;
    desc->expire_date = gst_date_time_new (9.0, year, month, day, -1, 0, 0.0);
  } else
    desc->expire_date = NULL;
  data += 3;
  desc->episode_number = GST_READ_UINT16_BE (data) >> 4;
  data += 1;
  desc->last_episode_number = GST_READ_UINT16_BE (data) & 0x0fff;
  data += 2;
  desc->series_name =
      get_encoding_and_convert ((const gchar *) data, descriptor->length - 8);
  return TRUE;
}

/* GST_MTS_DESC_ISDB_SERIES (0xD6) */

static GstMpegtsIsdbEventGroupDescriptor *
_gst_mpegts_isdb_event_group_descriptor_copy (GstMpegtsIsdbEventGroupDescriptor
    * source)
{
  GstMpegtsIsdbEventGroupDescriptor *copy;

  copy = g_slice_dup (GstMpegtsIsdbEventGroupDescriptor, source);
  copy->events = g_ptr_array_ref (source->events);

  return copy;
}

void
gst_mpegts_isdb_event_group_descriptor_free (GstMpegtsIsdbEventGroupDescriptor
    * source)
{
  g_ptr_array_unref (source->events);
  g_slice_free (GstMpegtsIsdbEventGroupDescriptor, source);
}

G_DEFINE_BOXED_TYPE (GstMpegtsIsdbEventGroupDescriptor,
    gst_mpegts_isdb_event_group_descriptor,
    (GBoxedCopyFunc) _gst_mpegts_isdb_event_group_descriptor_copy,
    (GFreeFunc) gst_mpegts_isdb_event_group_descriptor_free);

DEFINE_STATIC_COPY_FUNCTION (GstMpegtsIsdbEventRef, gst_mpegts_isdb_event_ref);
DEFINE_STATIC_FREE_FUNCTION (GstMpegtsIsdbEventRef, gst_mpegts_isdb_event_ref);
G_DEFINE_BOXED_TYPE (GstMpegtsIsdbEventRef, gst_mpegts_isdb_event_ref,
    (GBoxedCopyFunc) _gst_mpegts_isdb_event_ref_copy,
    (GFreeFunc) _gst_mpegts_isdb_event_ref_free);

/**
 * gst_mpegts_descriptor_parse_event_group:
 * @descriptor: a %GST_MTS_DESC_ISDB_EVENT_GROUP #GstMpegtsDescriptor
 * @res: (out) (transfer full): the #GstMpegtsIsdbEventGroupDesciptor to fill
 * related events of this event,
 * like common(shared) events, or the re-scheduled/moved event of this event
 *
 * Extracts the event group info from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_event_group (const GstMpegtsDescriptor * descriptor,
    GstMpegtsIsdbEventGroupDescriptor ** desc)
{
  guint8 *data;
  guint8 count;
  guint len;
  GstMpegtsIsdbEventGroupDescriptor *res;

  g_return_val_if_fail (descriptor != NULL && desc != NULL, FALSE);
  __common_desc_check_base (descriptor, GST_MTS_DESC_ISDB_EVENT_GROUP, FALSE);

  res = g_slice_new0 (GstMpegtsIsdbEventGroupDescriptor);

  data = (guint8 *) descriptor->data + 2;
  res->group_type = *data >> 4;
  count = *data & 0x0F;
  data++;

  res->events = g_ptr_array_new_with_free_func (
      (GDestroyNotify) _gst_mpegts_isdb_event_ref_free);

  len = descriptor->length - 1;
  if (res->group_type >= GST_MPEGTS_EVENT_GROUP_TYPE_RELAYED_TO)
    count = 1;
  while (len >= 4 && count > 0) {
    GstMpegtsIsdbEventRef *ref = g_slice_new0 (GstMpegtsIsdbEventRef);

    g_ptr_array_add (res->events, ref);
    if (res->group_type >= GST_MPEGTS_EVENT_GROUP_TYPE_RELAYED_TO) {
      if (len < 8)
        goto bad_desc;
      ref->original_network_id = GST_READ_UINT16_BE (data);
      data += 2;
      ref->transport_stream_id = GST_READ_UINT16_BE (data);
      data += 2;
      len -= 4;
    } else {
      count--;
      ref->original_network_id = 0;
      ref->transport_stream_id = 0;
    }

    ref->service_id = GST_READ_UINT16_BE (data);
    data += 2;
    ref->event_id = GST_READ_UINT16_BE (data);
    data += 2;
    len -= 4;
  };
  if (res->group_type < GST_MPEGTS_EVENT_GROUP_TYPE_RELAYED_TO && count != 0)
    goto bad_desc;

  *desc = res;
  return TRUE;

bad_desc:
  g_ptr_array_unref (res->events);
  g_slice_free (GstMpegtsIsdbEventGroupDescriptor, res);
  *desc = NULL;
  return FALSE;
}
