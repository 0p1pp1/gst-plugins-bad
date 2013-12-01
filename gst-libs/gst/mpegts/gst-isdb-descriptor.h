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

#ifndef GST_ISDB_DESCRIPTOR_H
#define GST_ISDB_DESCRIPTOR_H

#include <gst/gst.h>
#include <gst/mpegts/mpegts-prelude.h>

G_BEGIN_DECLS

/* GST_MTS_ISDB_DESC_SERIES (0xD5) */
typedef enum {
  GST_MPEGTS_ISDB_PROGRAM_PATTERN_IRREGULAR = 0,
  GST_MPEGTS_ISDB_PROGRAM_PATTERN_SLOT,
  GST_MPEGTS_ISDB_PROGRAM_PATTERN_WEEKLY,
  GST_MPEGTS_ISDB_PROGRAM_PATTERN_MONTHLY,
  GST_MPEGTS_ISDB_PROGRAM_PATTERN_LUMPED,
  GST_MPEGTS_ISDB_PROGRAM_PATTERN_SPLIT,

} GstMpegtsIsdbProgramPattern;

typedef struct _GstMpegtsIsdbEventSeries GstMpegtsIsdbEventSeries;
struct _GstMpegtsIsdbEventSeries {
  guint16                     series_id;
  guint8                      repeat_label;
  GstMpegtsIsdbProgramPattern program_pattern;
  GstDateTime                 *expire_date;
  guint16                     episode_number;
  guint16                     last_episode_number;
  gchar                       *series_name;
};

GST_MPEGTS_API gboolean
gst_mpegts_descriptor_parse_series (const GstMpegtsDescriptor *descriptor,
                                    GstMpegtsIsdbEventSeries *res);


/* GST_MTS_ISDB_DESC_EVENT_GROUP (0xD6) */
typedef enum {
  GST_MPEGTS_EVENT_GROUP_TYPE_SHARED = 1,
  GST_MPEGTS_EVENT_GROUP_TYPE_RELAYED_TO_INTERNAL,
  GST_MPEGTS_EVENT_GROUP_TYPE_MOVED_FROM_INTERNAL,
  GST_MPEGTS_EVENT_GROUP_TYPE_RELAYED_TO,
  GST_MPEGTS_EVENT_GROUP_TYPE_MOVED_FROM,
} GstMpegtsEventGroupType;

typedef struct _GstMpegtsIsdbEventRef GstMpegtsIsdbEventRef;
typedef struct _GstMpegtsIsdbEventGroup GstMpegtsIsdbEventGroup;

struct _GstMpegtsIsdbEventRef {
  guint16 original_network_id;   /* defined only for group_type >= 4 */
  guint16 trasnport_stream_id;   /* defined only for group_type >= 4 */
  guint16 service_id;
  guint16 event_id;
};

struct _GstMpegtsIsdbEventGroup {
  GstMpegtsEventGroupType group_type;
  guint8                  event_count;
  GstMpegtsIsdbEventRef   events[16];
};

GST_MPEGTS_API gboolean
gst_mpegts_descriptor_parse_event_group (const GstMpegtsDescriptor *descriptor,
                                         GstMpegtsIsdbEventGroup *res);

G_END_DECLS

#endif				/* GST_ISDB_DESCRIPTOR_H */

