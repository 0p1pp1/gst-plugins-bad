/*
 * GStreamer
 * Copyright (C) 2016 Vivia Nikolaidou <vivia@toolsonair.com>
 *
 * gsttimecodestamper.h
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

#ifndef __GST_TIME_CODE_STAMPER_H__
#define __GST_TIME_CODE_STAMPER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

#if HAVE_LTC
#include <ltc.h>
#endif

#define GST_TYPE_TIME_CODE_STAMPER            (gst_timecodestamper_get_type())
#define GST_TIME_CODE_STAMPER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TIME_CODE_STAMPER,GstTimeCodeStamper))
#define GST_TIME_CODE_STAMPER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_TIME_CODE_STAMPER,GstTimeCodeStamperClass))
#define GST_TIME_CODE_STAMPER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_TIME_CODE_STAMPER,GstTimeCodeStamperClass))
#define GST_IS_TIME_CODE_STAMPER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TIME_CODE_STAMPER))
#define GST_IS_TIME_CODE_STAMPER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_TIME_CODE_STAMPER))

#define GST_TYPE_TIME_CODE_STAMPER_SOURCE (gst_timecodestamper_source_get_type())
#define GST_TYPE_TIME_CODE_STAMPER_SET (gst_timecodestamper_set_get_type())

typedef struct _GstTimeCodeStamper GstTimeCodeStamper;
typedef struct _GstTimeCodeStamperClass GstTimeCodeStamperClass;

typedef enum GstTimeCodeStamperSource
{
  GST_TIME_CODE_STAMPER_SOURCE_INTERNAL,
  GST_TIME_CODE_STAMPER_SOURCE_ZERO,
  GST_TIME_CODE_STAMPER_SOURCE_LAST_KNOWN,
  GST_TIME_CODE_STAMPER_SOURCE_LTC,
  GST_TIME_CODE_STAMPER_SOURCE_RTC,
} GstTimeCodeStamperSource;

typedef enum GstTimeCodeStamperSet {
  GST_TIME_CODE_STAMPER_SET_NEVER,
  GST_TIME_CODE_STAMPER_SET_KEEP,
  GST_TIME_CODE_STAMPER_SET_ALWAYS,
} GstTimeCodeStamperSet;

/**
 * GstTimeCodeStamper:
 *
 * Opaque data structure.
 */
struct _GstTimeCodeStamper
{
  GstBaseTransform videofilter;

  /* protected by object lock */
  GstPad *ltcpad;

  /* < private > */

  /* Properties, protected by object lock */
  GstTimeCodeStamperSource tc_source;
  GstTimeCodeStamperSet tc_set;
  gboolean drop_frame;
  gboolean post_messages;
  GstVideoTimeCode *set_internal_tc;
  GDateTime *ltc_daily_jam;
  gboolean ltc_auto_resync;
  GstClockTime rtc_max_drift;
  gboolean rtc_auto_resync;
  gint timecode_offset;

  /* Timecode tracking, protected by object lock */
  GstVideoTimeCode *internal_tc;
  GstVideoTimeCode *last_tc;
  GstVideoTimeCode *rtc_tc;

  /* Internal state */
  GstVideoInfo vinfo; /* protected by object lock, changed only from video streaming thread */

  /* LTC specific fields */
#if HAVE_LTC
  GMutex mutex;
  GCond ltc_cond_video;
  GCond ltc_cond_audio;

  /* Only accessed from audio streaming thread */
  GstAudioInfo ainfo;
  GstAudioStreamAlign *stream_align;
  GstSegment ltc_segment;
  /* Running time of the first audio buffer passed to the LTC decoder */
  GstClockTime ltc_first_running_time;
  /* Running time of the last sample we passed to the LTC decoder so far */
  GstClockTime ltc_current_running_time;

  /* Protected by object lock */
  /* Current LTC timecode that we last read close
   * to our video running time */
  GstVideoTimeCode *ltc_current_tc;
  GstClockTime ltc_current_tc_running_time;

  /* LTC timecode we last synced to and potentially incremented manually since
   * then */
  GstVideoTimeCode *ltc_internal_tc;

  /* Protected by mutex above */
  LTCDecoder *ltc_dec;
  ltc_off_t ltc_total;

  /* Protected by mutex above */
  gboolean video_flushing;
  gboolean video_eos;

  /* Protected by mutex above */
  gboolean ltc_flushing;
  gboolean ltc_eos;

  GstPadActivateModeFunction video_activatemode_default;
#endif
};

struct _GstTimeCodeStamperClass
{
  GstBaseTransformClass parent_class;
};

GType gst_timecodestamper_get_type (void);

GType gst_timecodestamper_source_get_type (void);
GType gst_timecodestamper_set_get_type (void);

G_END_DECLS
#endif /* __GST_TIME_CODE_STAMPER_H__ */
