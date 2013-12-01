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

#include "mpegts.h"
#include "gstmpegts-private.h"

/**
 * SECTION:gst-isdb-descriptor
 * @title: ISDB variants of MPEG-TS descriptors
 * @short_description: Descriptors for the various ISDB specifications
 * @include: gst/mpegts/mpegts.h
 *
 */

/**
 * gst_mpegts_descriptor_parse_series:
 * @descriptor: a %GST_MTS_DESC_ISDB_SERIES #GstMpegtsDescriptor
 * @res: (out) (transfer none): the #GstMpegtsIsdbEventSeries to fill
 *
 * Extracts the event series info from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_series (const GstMpegtsDescriptor * descriptor,
    GstMpegtsIsdbEventSeries * res)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL && descriptor->data != NULL, FALSE);
  g_return_val_if_fail (res != NULL, FALSE);
  g_return_val_if_fail (descriptor->tag == 0xD5, FALSE);

  data = (guint8 *) descriptor->data + 2;
  res->series_id = GST_READ_UINT16_BE (data);
  data += 2;
  res->repeat_label = *data >> 4;
  res->program_pattern = (*data & 0x0E) >> 1;
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
    res->expire_date = gst_date_time_new (9.0, year, month, day, -1, 0, 0.0);
  } else
    res->expire_date = NULL;
  data += 3;
  res->episode_number = GST_READ_UINT16_BE (data);
  data += 2;
  res->episode_number = GST_READ_UINT16_BE (data);
  data += 2;
  res->series_name =
      get_encoding_and_convert ((const gchar *) data, descriptor->length - 8);
  return TRUE;
}

/**
 * gst_mpegts_descriptor_parse_event_group:
 * @descriptor: a %GST_MTS_DESC_ISDB_EVENT_GROUP #GstMpegtsDescriptor
 * @res: (out) (transfer none): the #GstMpegtsIsdbEventGroup to fill
 *
 * Extracts the event group info from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_event_group (const GstMpegtsDescriptor * descriptor,
    GstMpegtsIsdbEventGroup * res)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL && descriptor->data != NULL, FALSE);
  g_return_val_if_fail (res != NULL, FALSE);
  g_return_val_if_fail (descriptor->tag == 0xD6, FALSE);

  data = (guint8 *) descriptor->data + 2;
  res->group_type = *data >> 4;
  res->event_count = *data & 0x0F;
  data++;

  memset (res->events, 0, sizeof (res->events));
  if (res->group_type < GST_MPEGTS_EVENT_GROUP_TYPE_RELAYED_TO) {
    guint i;

    for (i = 0; i < res->event_count; i++) {
      res->events[i].original_network_id = 0;
      res->events[i].trasnport_stream_id = 0;
      res->events[i].service_id = GST_READ_UINT16_BE (data);
      data += 2;
      res->events[i].event_id = GST_READ_UINT16_BE (data);
      data += 2;
    }
  } else {
    guint len = descriptor->length - 1;

    res->event_count = 0;
    while (len >= 8 && res->event_count < 16) {
      GstMpegtsIsdbEventRef *e = &res->events[res->event_count];
      e->original_network_id = GST_READ_UINT16_BE (data);
      data += 2;
      e->trasnport_stream_id = GST_READ_UINT16_BE (data);
      data += 2;
      e->service_id = GST_READ_UINT16_BE (data);
      data += 2;
      e->event_id = GST_READ_UINT16_BE (data);
      data += 2;
      res->event_count++;
      len -= 8;
    }
  }
  return TRUE;
}
