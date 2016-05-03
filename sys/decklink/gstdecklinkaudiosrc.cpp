/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@entropywave.com>
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdecklinkaudiosrc.h"
#include "gstdecklinkvideosrc.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_decklink_audio_src_debug);
#define GST_CAT_DEFAULT gst_decklink_audio_src_debug

#define DEFAULT_CONNECTION            (GST_DECKLINK_AUDIO_CONNECTION_AUTO)
#define DEFAULT_BUFFER_SIZE           (5)

#define DEFAULT_ALIGNMENT_THRESHOLD   (40 * GST_MSECOND)
#define DEFAULT_DISCONT_WAIT          (1 * GST_SECOND)

enum
{
  PROP_0,
  PROP_CONNECTION,
  PROP_DEVICE_NUMBER,
  PROP_ALIGNMENT_THRESHOLD,
  PROP_DISCONT_WAIT,
  PROP_BUFFER_SIZE
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("audio/x-raw, format={S16LE,S32LE}, channels=2, rate=48000, "
        "layout=interleaved")
    );

typedef struct
{
  IDeckLinkAudioInputPacket *packet;
  GstClockTime capture_time;
  gboolean discont;
} CapturePacket;

static void
capture_packet_free (void *data)
{
  CapturePacket *packet = (CapturePacket *) data;

  packet->packet->Release ();
  g_free (packet);
}

typedef struct
{
  IDeckLinkAudioInputPacket *packet;
  IDeckLinkInput *input;
} AudioPacket;

static void
audio_packet_free (void *data)
{
  AudioPacket *packet = (AudioPacket *) data;

  packet->packet->Release ();
  packet->input->Release ();
  g_free (packet);
}

static void gst_decklink_audio_src_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_decklink_audio_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_decklink_audio_src_finalize (GObject * object);

static GstStateChangeReturn
gst_decklink_audio_src_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_decklink_audio_src_set_caps (GstBaseSrc * bsrc,
    GstCaps * caps);
static GstCaps *gst_decklink_audio_src_get_caps (GstBaseSrc * bsrc,
    GstCaps * filter);
static gboolean gst_decklink_audio_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_decklink_audio_src_unlock_stop (GstBaseSrc * bsrc);
static gboolean gst_decklink_audio_src_query (GstBaseSrc * bsrc,
    GstQuery * query);

static GstFlowReturn gst_decklink_audio_src_create (GstPushSrc * psrc,
    GstBuffer ** buffer);

static gboolean gst_decklink_audio_src_open (GstDecklinkAudioSrc * self);
static gboolean gst_decklink_audio_src_close (GstDecklinkAudioSrc * self);

static gboolean gst_decklink_audio_src_stop (GstDecklinkAudioSrc * self);

#define parent_class gst_decklink_audio_src_parent_class
G_DEFINE_TYPE (GstDecklinkAudioSrc, gst_decklink_audio_src, GST_TYPE_PUSH_SRC);

static void
gst_decklink_audio_src_class_init (GstDecklinkAudioSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *pushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->set_property = gst_decklink_audio_src_set_property;
  gobject_class->get_property = gst_decklink_audio_src_get_property;
  gobject_class->finalize = gst_decklink_audio_src_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_src_change_state);

  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_decklink_audio_src_get_caps);
  basesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_decklink_audio_src_set_caps);
  basesrc_class->query = GST_DEBUG_FUNCPTR (gst_decklink_audio_src_query);
  basesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_decklink_audio_src_unlock);
  basesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_src_unlock_stop);

  pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_decklink_audio_src_create);

  g_object_class_install_property (gobject_class, PROP_CONNECTION,
      g_param_spec_enum ("connection", "Connection",
          "Audio input connection to use",
          GST_TYPE_DECKLINK_AUDIO_CONNECTION, DEFAULT_CONNECTION,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_DEVICE_NUMBER,
      g_param_spec_int ("device-number", "Device number",
          "Output device instance to use", 0, G_MAXINT, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_ALIGNMENT_THRESHOLD,
      g_param_spec_uint64 ("alignment-threshold", "Alignment Threshold",
          "Timestamp alignment threshold in nanoseconds", 0,
          G_MAXUINT64 - 1, DEFAULT_ALIGNMENT_THRESHOLD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DISCONT_WAIT,
      g_param_spec_uint64 ("discont-wait", "Discont Wait",
          "Window of time in nanoseconds to wait before "
          "creating a discontinuity", 0,
          G_MAXUINT64 - 1, DEFAULT_DISCONT_WAIT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BUFFER_SIZE,
      g_param_spec_uint ("buffer-size", "Buffer Size",
          "Size of internal buffer in number of video frames", 1,
          G_MAXINT, DEFAULT_BUFFER_SIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  gst_element_class_set_static_metadata (element_class, "Decklink Audio Source",
      "Audio/Src", "Decklink Source", "David Schleef <ds@entropywave.com>, "
      "Sebastian Dröge <sebastian@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (gst_decklink_audio_src_debug, "decklinkaudiosrc",
      0, "debug category for decklinkaudiosrc element");
}

static void
gst_decklink_audio_src_init (GstDecklinkAudioSrc * self)
{
  self->device_number = 0;
  self->alignment_threshold = DEFAULT_ALIGNMENT_THRESHOLD;
  self->discont_wait = DEFAULT_DISCONT_WAIT;
  self->buffer_size = DEFAULT_BUFFER_SIZE;

  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);

  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);

  g_queue_init (&self->current_packets);
}

void
gst_decklink_audio_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecklinkAudioSrc *self = GST_DECKLINK_AUDIO_SRC_CAST (object);

  switch (property_id) {
    case PROP_CONNECTION:
      self->connection =
          (GstDecklinkAudioConnectionEnum) g_value_get_enum (value);
      break;
    case PROP_DEVICE_NUMBER:
      self->device_number = g_value_get_int (value);
      break;
    case PROP_ALIGNMENT_THRESHOLD:
      self->alignment_threshold = g_value_get_uint64 (value);
      break;
    case PROP_DISCONT_WAIT:
      self->discont_wait = g_value_get_uint64 (value);
      break;
    case PROP_BUFFER_SIZE:
      self->buffer_size = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_decklink_audio_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstDecklinkAudioSrc *self = GST_DECKLINK_AUDIO_SRC_CAST (object);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_enum (value, self->connection);
      break;
    case PROP_DEVICE_NUMBER:
      g_value_set_int (value, self->device_number);
      break;
    case PROP_ALIGNMENT_THRESHOLD:
      g_value_set_uint64 (value, self->alignment_threshold);
      break;
    case PROP_DISCONT_WAIT:
      g_value_set_uint64 (value, self->discont_wait);
      break;
    case PROP_BUFFER_SIZE:
      g_value_set_uint (value, self->buffer_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_decklink_audio_src_finalize (GObject * object)
{
  GstDecklinkAudioSrc *self = GST_DECKLINK_AUDIO_SRC_CAST (object);

  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_decklink_audio_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstDecklinkAudioSrc *self = GST_DECKLINK_AUDIO_SRC_CAST (bsrc);
  BMDAudioSampleType sample_depth;
  GstCaps *current_caps;
  HRESULT ret;
  BMDAudioConnection conn = (BMDAudioConnection) - 1;

  GST_DEBUG_OBJECT (self, "Setting caps %" GST_PTR_FORMAT, caps);

  if ((current_caps = gst_pad_get_current_caps (GST_BASE_SRC_PAD (bsrc)))) {
    GST_DEBUG_OBJECT (self, "Pad already has caps %" GST_PTR_FORMAT, caps);

    if (!gst_caps_is_equal (caps, current_caps)) {
      GST_ERROR_OBJECT (self, "New caps are not equal to old caps");
      gst_caps_unref (current_caps);
      return FALSE;
    } else {
      gst_caps_unref (current_caps);
      return TRUE;
    }
  }

  if (!gst_audio_info_from_caps (&self->info, caps))
    return FALSE;

  if (self->info.finfo->format == GST_AUDIO_FORMAT_S16LE) {
    sample_depth = bmdAudioSampleType16bitInteger;
  } else {
    sample_depth = bmdAudioSampleType32bitInteger;
  }

  switch (self->connection) {
    case GST_DECKLINK_AUDIO_CONNECTION_AUTO:{
      GstElement *videosrc = NULL;
      GstDecklinkConnectionEnum vconn;

      // Try to get the connection from the videosrc and try
      // to select a sensible audio connection based on that
      g_mutex_lock (&self->input->lock);
      if (self->input->videosrc)
        videosrc = GST_ELEMENT_CAST (gst_object_ref (self->input->videosrc));
      g_mutex_unlock (&self->input->lock);

      if (videosrc) {
        g_object_get (videosrc, "connection", &vconn, NULL);
        gst_object_unref (videosrc);

        switch (vconn) {
          case GST_DECKLINK_CONNECTION_SDI:
            conn = bmdAudioConnectionEmbedded;
            break;
          case GST_DECKLINK_CONNECTION_HDMI:
            conn = bmdAudioConnectionEmbedded;
            break;
          case GST_DECKLINK_CONNECTION_OPTICAL_SDI:
            conn = bmdAudioConnectionEmbedded;
            break;
          case GST_DECKLINK_CONNECTION_COMPONENT:
            conn = bmdAudioConnectionAnalog;
            break;
          case GST_DECKLINK_CONNECTION_COMPOSITE:
            conn = bmdAudioConnectionAnalog;
            break;
          case GST_DECKLINK_CONNECTION_SVIDEO:
            conn = bmdAudioConnectionAnalog;
            break;
          default:
            // Use default
            break;
        }
      }

      break;
    }
    case GST_DECKLINK_AUDIO_CONNECTION_EMBEDDED:
      conn = bmdAudioConnectionEmbedded;
      break;
    case GST_DECKLINK_AUDIO_CONNECTION_AES_EBU:
      conn = bmdAudioConnectionAESEBU;
      break;
    case GST_DECKLINK_AUDIO_CONNECTION_ANALOG:
      conn = bmdAudioConnectionAnalog;
      break;
    case GST_DECKLINK_AUDIO_CONNECTION_ANALOG_XLR:
      conn = bmdAudioConnectionAnalogXLR;
      break;
    case GST_DECKLINK_AUDIO_CONNECTION_ANALOG_RCA:
      conn = bmdAudioConnectionAnalogRCA;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (conn != (BMDAudioConnection) - 1) {
    ret =
        self->input->config->SetInt (bmdDeckLinkConfigAudioInputConnection,
        conn);
    if (ret != S_OK) {
      GST_ERROR ("set configuration (audio input connection)");
      return FALSE;
    }
  }

  ret = self->input->input->EnableAudioInput (bmdAudioSampleRate48kHz,
      sample_depth, 2);
  if (ret != S_OK) {
    GST_WARNING_OBJECT (self, "Failed to enable audio input");
    return FALSE;
  }

  g_mutex_lock (&self->input->lock);
  self->input->audio_enabled = TRUE;
  if (self->input->start_streams && self->input->videosrc)
    self->input->start_streams (self->input->videosrc);
  g_mutex_unlock (&self->input->lock);

  return TRUE;
}

static GstCaps *
gst_decklink_audio_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstCaps *caps;

  // We don't support renegotiation
  caps = gst_pad_get_current_caps (GST_BASE_SRC_PAD (bsrc));

  if (!caps)
    caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));

  if (filter) {
    GstCaps *tmp =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = tmp;
  }

  return caps;
}

static void
gst_decklink_audio_src_got_packet (GstElement * element,
    IDeckLinkAudioInputPacket * packet, GstClockTime capture_time,
    gboolean discont)
{
  GstDecklinkAudioSrc *self = GST_DECKLINK_AUDIO_SRC_CAST (element);
  GstDecklinkVideoSrc *videosrc = NULL;

  GST_LOG_OBJECT (self, "Got audio packet at %" GST_TIME_FORMAT,
      GST_TIME_ARGS (capture_time));

  g_mutex_lock (&self->input->lock);
  if (self->input->videosrc)
    videosrc =
        GST_DECKLINK_VIDEO_SRC_CAST (gst_object_ref (self->input->videosrc));
  g_mutex_unlock (&self->input->lock);

  if (videosrc) {
    gst_decklink_video_src_convert_to_external_clock (videosrc, &capture_time,
        NULL);
    gst_object_unref (videosrc);
    GST_LOG_OBJECT (self, "Actual timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (capture_time));
  }

  g_mutex_lock (&self->lock);
  if (!self->flushing) {
    CapturePacket *p;

    while (g_queue_get_length (&self->current_packets) >= self->buffer_size) {
      p = (CapturePacket *) g_queue_pop_head (&self->current_packets);
      GST_WARNING_OBJECT (self, "Dropping old packet at %" GST_TIME_FORMAT,
          GST_TIME_ARGS (p->capture_time));
      capture_packet_free (p);
    }

    p = (CapturePacket *) g_malloc0 (sizeof (CapturePacket));
    p->packet = packet;
    p->capture_time = capture_time;
    p->discont = discont;
    packet->AddRef ();
    g_queue_push_tail (&self->current_packets, p);
    g_cond_signal (&self->cond);
  }
  g_mutex_unlock (&self->lock);
}

static GstFlowReturn
gst_decklink_audio_src_create (GstPushSrc * bsrc, GstBuffer ** buffer)
{
  GstDecklinkAudioSrc *self = GST_DECKLINK_AUDIO_SRC_CAST (bsrc);
  GstFlowReturn flow_ret = GST_FLOW_OK;
  const guint8 *data;
  glong sample_count;
  gsize data_size;
  CapturePacket *p;
  AudioPacket *ap;
  GstClockTime timestamp, duration;
  GstClockTime start_time, end_time;
  guint64 start_offset, end_offset;
  gboolean discont = FALSE;

retry:
  g_mutex_lock (&self->lock);
  while (g_queue_is_empty (&self->current_packets) && !self->flushing) {
    g_cond_wait (&self->cond, &self->lock);
  }

  p = (CapturePacket *) g_queue_pop_head (&self->current_packets);
  g_mutex_unlock (&self->lock);

  if (self->flushing) {
    if (p)
      capture_packet_free (p);
    GST_DEBUG_OBJECT (self, "Flushing");
    return GST_FLOW_FLUSHING;
  }

  p->packet->GetBytes ((gpointer *) & data);
  sample_count = p->packet->GetSampleFrameCount ();
  data_size = self->info.bpf * sample_count;

  if (p->capture_time == GST_CLOCK_TIME_NONE
      && self->next_offset == (guint64) - 1) {
    GST_DEBUG_OBJECT (self,
        "Got packet without timestamp before initial "
        "timestamp after discont - dropping");
    capture_packet_free (p);
    goto retry;
  }

  ap = (AudioPacket *) g_malloc0 (sizeof (AudioPacket));

  *buffer =
      gst_buffer_new_wrapped_full ((GstMemoryFlags) GST_MEMORY_FLAG_READONLY,
      (gpointer) data, data_size, 0, data_size, ap,
      (GDestroyNotify) audio_packet_free);

  ap->packet = p->packet;
  p->packet->AddRef ();
  ap->input = self->input->input;
  ap->input->AddRef ();

  timestamp = p->capture_time;
  discont = p->discont;

  // Jitter and discontinuity handling, based on audiobasesrc
  start_time = timestamp;

  // Convert to the sample numbers
  start_offset =
      gst_util_uint64_scale (start_time, self->info.rate, GST_SECOND);

  end_offset = start_offset + sample_count;
  end_time = gst_util_uint64_scale_int (end_offset, GST_SECOND,
      self->info.rate);

  duration = end_time - start_time;

  if (discont || self->next_offset == (guint64) - 1) {
    discont = TRUE;
  } else {
    guint64 diff, max_sample_diff;

    // Check discont
    if (start_offset <= self->next_offset)
      diff = self->next_offset - start_offset;
    else
      diff = start_offset - self->next_offset;

    max_sample_diff =
        gst_util_uint64_scale_int (self->alignment_threshold, self->info.rate,
        GST_SECOND);

    // Discont!
    if (G_UNLIKELY (diff >= max_sample_diff)) {
      if (self->discont_wait > 0) {
        if (self->discont_time == GST_CLOCK_TIME_NONE) {
          self->discont_time = start_time;
        } else if (start_time - self->discont_time >= self->discont_wait) {
          discont = TRUE;
          self->discont_time = GST_CLOCK_TIME_NONE;
        }
      } else {
        discont = TRUE;
      }
    } else if (G_UNLIKELY (self->discont_time != GST_CLOCK_TIME_NONE)) {
      // we have had a discont, but are now back on track!
      self->discont_time = GST_CLOCK_TIME_NONE;
    }
  }

  if (discont) {
    // Have discont, need resync and use the capture timestamps
    if (self->next_offset != (guint64) - 1)
      GST_INFO_OBJECT (self, "Have discont. Expected %"
          G_GUINT64_FORMAT ", got %" G_GUINT64_FORMAT,
          self->next_offset, start_offset);
    GST_BUFFER_FLAG_SET (*buffer, GST_BUFFER_FLAG_DISCONT);
    self->next_offset = end_offset;
    // Got a discont and adjusted, reset the discont_time marker.
    self->discont_time = GST_CLOCK_TIME_NONE;
  } else {
    // No discont, just keep counting
    timestamp =
        gst_util_uint64_scale (self->next_offset, GST_SECOND, self->info.rate);
    self->next_offset += sample_count;
    duration =
        gst_util_uint64_scale (self->next_offset, GST_SECOND,
        self->info.rate) - timestamp;
  }

  GST_BUFFER_TIMESTAMP (*buffer) = timestamp;
  GST_BUFFER_DURATION (*buffer) = duration;

  GST_DEBUG_OBJECT (self,
      "Outputting buffer %p with timestamp %" GST_TIME_FORMAT " and duration %"
      GST_TIME_FORMAT, *buffer, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (*buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (*buffer)));

  capture_packet_free (p);

  return flow_ret;
}

static gboolean
gst_decklink_audio_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstDecklinkAudioSrc *self = GST_DECKLINK_AUDIO_SRC_CAST (bsrc);
  gboolean ret = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      if (self->input) {
        g_mutex_lock (&self->input->lock);
        if (self->input->mode) {
          GstClockTime min, max;

          min =
              gst_util_uint64_scale_ceil (GST_SECOND, self->input->mode->fps_d,
              self->input->mode->fps_n);
          max = self->buffer_size * min;

          gst_query_set_latency (query, TRUE, min, max);
          ret = TRUE;
        } else {
          ret = FALSE;
        }
        g_mutex_unlock (&self->input->lock);
      } else {
        ret = FALSE;
      }

      break;
    }
    default:
      ret = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }

  return ret;
}

static gboolean
gst_decklink_audio_src_unlock (GstBaseSrc * bsrc)
{
  GstDecklinkAudioSrc *self = GST_DECKLINK_AUDIO_SRC_CAST (bsrc);

  g_mutex_lock (&self->lock);
  self->flushing = TRUE;
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static gboolean
gst_decklink_audio_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstDecklinkAudioSrc *self = GST_DECKLINK_AUDIO_SRC_CAST (bsrc);

  g_mutex_lock (&self->lock);
  self->flushing = FALSE;
  g_queue_foreach (&self->current_packets, (GFunc) capture_packet_free, NULL);
  g_queue_clear (&self->current_packets);
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static gboolean
gst_decklink_audio_src_open (GstDecklinkAudioSrc * self)
{
  GST_DEBUG_OBJECT (self, "Opening");

  self->input =
      gst_decklink_acquire_nth_input (self->device_number,
      GST_ELEMENT_CAST (self), TRUE);
  if (!self->input) {
    GST_ERROR_OBJECT (self, "Failed to acquire input");
    return FALSE;
  }

  g_mutex_lock (&self->input->lock);
  self->input->got_audio_packet = gst_decklink_audio_src_got_packet;
  g_mutex_unlock (&self->input->lock);

  return TRUE;
}

static gboolean
gst_decklink_audio_src_close (GstDecklinkAudioSrc * self)
{
  GST_DEBUG_OBJECT (self, "Closing");

  if (self->input) {
    g_mutex_lock (&self->input->lock);
    self->input->got_audio_packet = NULL;
    g_mutex_unlock (&self->input->lock);

    gst_decklink_release_nth_input (self->device_number,
        GST_ELEMENT_CAST (self), TRUE);
    self->input = NULL;
  }

  return TRUE;
}

static gboolean
gst_decklink_audio_src_stop (GstDecklinkAudioSrc * self)
{
  GST_DEBUG_OBJECT (self, "Stopping");

  g_queue_foreach (&self->current_packets, (GFunc) capture_packet_free, NULL);
  g_queue_clear (&self->current_packets);

  if (self->input && self->input->audio_enabled) {
    g_mutex_lock (&self->input->lock);
    self->input->audio_enabled = FALSE;
    g_mutex_unlock (&self->input->lock);

    self->input->input->DisableAudioInput ();
  }

  return TRUE;
}

#if 0
static gboolean
in_same_pipeline (GstElement * a, GstElement * b)
{
  GstObject *root = NULL, *tmp;
  gboolean ret = FALSE;

  tmp = gst_object_get_parent (GST_OBJECT_CAST (a));
  while (tmp != NULL) {
    if (root)
      gst_object_unref (root);
    root = tmp;
    tmp = gst_object_get_parent (root);
  }

  ret = root && gst_object_has_ancestor (GST_OBJECT_CAST (b), root);

  if (root)
    gst_object_unref (root);

  return ret;
}
#endif

static GstStateChangeReturn
gst_decklink_audio_src_change_state (GstElement * element,
    GstStateChange transition)
{
  GstDecklinkAudioSrc *self = GST_DECKLINK_AUDIO_SRC_CAST (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_decklink_audio_src_open (self)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto out;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
      GstElement *videosrc = NULL;

      // Check if there is a video src for this input too and if it
      // is actually in the same pipeline
      g_mutex_lock (&self->input->lock);
      if (self->input->videosrc)
        videosrc = GST_ELEMENT_CAST (gst_object_ref (self->input->videosrc));
      g_mutex_unlock (&self->input->lock);

      if (!videosrc) {
        GST_ELEMENT_ERROR (self, STREAM, FAILED,
            (NULL), ("Audio src needs a video src for its operation"));
        ret = GST_STATE_CHANGE_FAILURE;
        goto out;
      }
      // FIXME: This causes deadlocks sometimes
#if 0
      else if (!in_same_pipeline (GST_ELEMENT_CAST (self), videosrc)) {
        GST_ELEMENT_ERROR (self, STREAM, FAILED,
            (NULL),
            ("Audio src and video src need to be in the same pipeline"));
        ret = GST_STATE_CHANGE_FAILURE;
        gst_object_unref (videosrc);
        goto out;
      }
#endif

      if (videosrc)
        gst_object_unref (videosrc);

      self->flushing = FALSE;
      self->next_offset = -1;
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_decklink_audio_src_stop (self);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_decklink_audio_src_close (self);
      break;
    default:
      break;
  }
out:

  return ret;
}
