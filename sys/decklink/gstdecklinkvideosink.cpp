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

#include "gstdecklinkvideosink.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_decklink_video_sink_debug);
#define GST_CAT_DEFAULT gst_decklink_video_sink_debug

class GStreamerVideoOutputCallback:public IDeckLinkVideoOutputCallback
{
public:
  GStreamerVideoOutputCallback (GstDecklinkVideoSink * sink)
  :IDeckLinkVideoOutputCallback (), m_refcount (1)
  {
    m_sink = GST_DECKLINK_VIDEO_SINK_CAST (gst_object_ref (sink));
    g_mutex_init (&m_mutex);
  }

  virtual HRESULT QueryInterface (REFIID, LPVOID *)
  {
    return E_NOINTERFACE;
  }

  virtual ULONG AddRef (void)
  {
    ULONG ret;

    g_mutex_lock (&m_mutex);
    m_refcount++;
    ret = m_refcount;
    g_mutex_unlock (&m_mutex);

    return ret;
  }

  virtual ULONG Release (void)
  {
    ULONG ret;

    g_mutex_lock (&m_mutex);
    m_refcount--;
    ret = m_refcount;
    g_mutex_unlock (&m_mutex);

    if (ret == 0) {
      delete this;
    }

    return ret;
  }

  virtual HRESULT ScheduledFrameCompleted (IDeckLinkVideoFrame * completedFrame,
      BMDOutputFrameCompletionResult result)
  {
    switch (result) {
      case bmdOutputFrameCompleted:
        GST_LOG_OBJECT (m_sink, "Completed frame %p", completedFrame);
        break;
      case bmdOutputFrameDisplayedLate:
        GST_INFO_OBJECT (m_sink, "Late Frame %p", completedFrame);
        break;
      case bmdOutputFrameDropped:
        GST_INFO_OBJECT (m_sink, "Dropped Frame %p", completedFrame);
        break;
      case bmdOutputFrameFlushed:
        GST_DEBUG_OBJECT (m_sink, "Flushed Frame %p", completedFrame);
        break;
      default:
        GST_INFO_OBJECT (m_sink, "Unknown Frame %p: %d", completedFrame,
            (gint) result);
        break;
    }

    return S_OK;
  }

  virtual HRESULT ScheduledPlaybackHasStopped (void)
  {
    GST_LOG_OBJECT (m_sink, "Scheduled playback stopped");

    return S_OK;
  }

  virtual ~ GStreamerVideoOutputCallback () {
    gst_object_unref (m_sink);
    g_mutex_clear (&m_mutex);
  }

private:
  GstDecklinkVideoSink * m_sink;
  GMutex m_mutex;
  gint m_refcount;
};

enum
{
  PROP_0,
  PROP_MODE,
  PROP_DEVICE_NUMBER,
  PROP_VIDEO_FORMAT
};

static void gst_decklink_video_sink_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_decklink_video_sink_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_decklink_video_sink_finalize (GObject * object);

static GstStateChangeReturn
gst_decklink_video_sink_change_state (GstElement * element,
    GstStateChange transition);
static GstClock *gst_decklink_video_sink_provide_clock (GstElement * element);

static GstCaps *gst_decklink_video_sink_get_caps (GstBaseSink * bsink,
    GstCaps * filter);
static gboolean gst_decklink_video_sink_set_caps (GstBaseSink * bsink,
    GstCaps * caps);
static GstFlowReturn gst_decklink_video_sink_prepare (GstBaseSink * bsink,
    GstBuffer * buffer);
static GstFlowReturn gst_decklink_video_sink_render (GstBaseSink * bsink,
    GstBuffer * buffer);
static gboolean gst_decklink_video_sink_open (GstBaseSink * bsink);
static gboolean gst_decklink_video_sink_close (GstBaseSink * bsink);
static gboolean gst_decklink_video_sink_stop (GstDecklinkVideoSink * self);
static gboolean gst_decklink_video_sink_propose_allocation (GstBaseSink * bsink,
    GstQuery * query);

static void
gst_decklink_video_sink_start_scheduled_playback (GstElement * element);

#define parent_class gst_decklink_video_sink_parent_class
G_DEFINE_TYPE (GstDecklinkVideoSink, gst_decklink_video_sink,
    GST_TYPE_BASE_SINK);

static gboolean
reset_framerate (GstCapsFeatures * features, GstStructure * structure,
    gpointer user_data)
{
  gst_structure_set (structure, "framerate", GST_TYPE_FRACTION_RANGE, 0, 1,
      G_MAXINT, 1, NULL);

  return TRUE;
}

static void
gst_decklink_video_sink_class_init (GstDecklinkVideoSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);
  GstCaps *templ_caps;

  gobject_class->set_property = gst_decklink_video_sink_set_property;
  gobject_class->get_property = gst_decklink_video_sink_get_property;
  gobject_class->finalize = gst_decklink_video_sink_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_decklink_video_sink_change_state);
  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_decklink_video_sink_provide_clock);

  basesink_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_decklink_video_sink_get_caps);
  basesink_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_decklink_video_sink_set_caps);
  basesink_class->prepare = GST_DEBUG_FUNCPTR (gst_decklink_video_sink_prepare);
  basesink_class->render = GST_DEBUG_FUNCPTR (gst_decklink_video_sink_render);
  // FIXME: These are misnamed in basesink!
  basesink_class->start = GST_DEBUG_FUNCPTR (gst_decklink_video_sink_open);
  basesink_class->stop = GST_DEBUG_FUNCPTR (gst_decklink_video_sink_close);
  basesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_decklink_video_sink_propose_allocation);

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Playback Mode",
          "Video Mode to use for playback",
          GST_TYPE_DECKLINK_MODE, GST_DECKLINK_MODE_NTSC,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_DEVICE_NUMBER,
      g_param_spec_int ("device-number", "Device number",
          "Output device instance to use", 0, G_MAXINT, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_VIDEO_FORMAT,
      g_param_spec_enum ("video-format", "Video format",
          "Video format type to use for playback",
          GST_TYPE_DECKLINK_VIDEO_FORMAT, GST_DECKLINK_VIDEO_FORMAT_8BIT_YUV,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  templ_caps = gst_decklink_mode_get_template_caps ();
  templ_caps = gst_caps_make_writable (templ_caps);
  /* For output we support any framerate and only really care about timestamps */
  gst_caps_map_in_place (templ_caps, reset_framerate, NULL);
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, templ_caps));
  gst_caps_unref (templ_caps);

  gst_element_class_set_static_metadata (element_class, "Decklink Video Sink",
      "Video/Sink", "Decklink Sink", "David Schleef <ds@entropywave.com>, "
      "Sebastian Dröge <sebastian@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (gst_decklink_video_sink_debug, "decklinkvideosink",
      0, "debug category for decklinkvideosink element");
}

static void
gst_decklink_video_sink_init (GstDecklinkVideoSink * self)
{
  self->mode = GST_DECKLINK_MODE_NTSC;
  self->device_number = 0;
  self->video_format = GST_DECKLINK_VIDEO_FORMAT_8BIT_YUV;

  gst_base_sink_set_max_lateness (GST_BASE_SINK_CAST (self), 20 * GST_MSECOND);
  gst_base_sink_set_qos_enabled (GST_BASE_SINK_CAST (self), TRUE);
}

void
gst_decklink_video_sink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecklinkVideoSink *self = GST_DECKLINK_VIDEO_SINK_CAST (object);

  switch (property_id) {
    case PROP_MODE:
      self->mode = (GstDecklinkModeEnum) g_value_get_enum (value);
      break;
    case PROP_DEVICE_NUMBER:
      self->device_number = g_value_get_int (value);
      break;
    case PROP_VIDEO_FORMAT:
      self->video_format = (GstDecklinkVideoFormat) g_value_get_enum (value);
      switch (self->video_format) {
        case GST_DECKLINK_VIDEO_FORMAT_AUTO:
        case GST_DECKLINK_VIDEO_FORMAT_8BIT_YUV:
        case GST_DECKLINK_VIDEO_FORMAT_10BIT_YUV:
        case GST_DECKLINK_VIDEO_FORMAT_8BIT_ARGB:
        case GST_DECKLINK_VIDEO_FORMAT_8BIT_BGRA:
          break;
        default:
          GST_ELEMENT_WARNING (GST_ELEMENT (self), CORE, NOT_IMPLEMENTED,
              ("Format %d not supported", self->video_format), (NULL));
          break;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_decklink_video_sink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstDecklinkVideoSink *self = GST_DECKLINK_VIDEO_SINK_CAST (object);

  switch (property_id) {
    case PROP_MODE:
      g_value_set_enum (value, self->mode);
      break;
    case PROP_DEVICE_NUMBER:
      g_value_set_int (value, self->device_number);
      break;
    case PROP_VIDEO_FORMAT:
      g_value_set_enum (value, self->video_format);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_decklink_video_sink_finalize (GObject * object)
{
  //GstDecklinkVideoSink *self = GST_DECKLINK_VIDEO_SINK_CAST (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_decklink_video_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstDecklinkVideoSink *self = GST_DECKLINK_VIDEO_SINK_CAST (bsink);
  const GstDecklinkMode *mode;
  HRESULT ret;

  GST_DEBUG_OBJECT (self, "Setting caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&self->info, caps))
    return FALSE;

  self->output->output->SetScheduledFrameCompletionCallback (new
      GStreamerVideoOutputCallback (self));

  if (self->mode == GST_DECKLINK_MODE_AUTO) {
    BMDPixelFormat f;
    mode = gst_decklink_find_mode_and_format_for_caps (caps, &f);
    if (mode == NULL) {
      GST_WARNING_OBJECT (self,
          "Failed to find compatible mode for caps  %" GST_PTR_FORMAT, caps);
      return FALSE;
    }
    if (self->video_format != GST_DECKLINK_VIDEO_FORMAT_AUTO &&
        gst_decklink_pixel_format_from_type (self->video_format) != f) {
      GST_WARNING_OBJECT (self, "Failed to set pixel format to %d",
          self->video_format);
      return FALSE;
    }
  } else {
    /* We don't have to give the format in EnableVideoOutput. Therefore,
     * even if it's AUTO, we have it stored in self->info and set it in
     * gst_decklink_video_sink_prepare */
    mode = gst_decklink_get_mode (self->mode);
    g_assert (mode != NULL);
  };

  ret = self->output->output->EnableVideoOutput (mode->mode,
      bmdVideoOutputFlagDefault);
  if (ret != S_OK) {
    GST_WARNING_OBJECT (self, "Failed to enable video output");
    return FALSE;
  }

  g_mutex_lock (&self->output->lock);
  self->output->mode = mode;
  self->output->video_enabled = TRUE;
  if (self->output->start_scheduled_playback)
    self->output->start_scheduled_playback (self->output->videosink);
  g_mutex_unlock (&self->output->lock);

  return TRUE;
}

static GstCaps *
gst_decklink_video_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstDecklinkVideoSink *self = GST_DECKLINK_VIDEO_SINK_CAST (bsink);
  GstCaps *mode_caps, *caps;

  if (self->mode == GST_DECKLINK_MODE_AUTO
      && self->video_format == GST_DECKLINK_VIDEO_FORMAT_AUTO)
    mode_caps = gst_decklink_mode_get_template_caps ();
  else if (self->video_format == GST_DECKLINK_VIDEO_FORMAT_AUTO)
    mode_caps = gst_decklink_mode_get_caps_all_formats (self->mode);
  else if (self->mode == GST_DECKLINK_MODE_AUTO)
    mode_caps =
        gst_decklink_pixel_format_get_caps (gst_decklink_pixel_format_from_type
        (self->video_format));
  else
    mode_caps =
        gst_decklink_mode_get_caps (self->mode,
        gst_decklink_pixel_format_from_type (self->video_format));
  mode_caps = gst_caps_make_writable (mode_caps);
  /* For output we support any framerate and only really care about timestamps */
  gst_caps_map_in_place (mode_caps, reset_framerate, NULL);

  if (filter) {
    caps =
        gst_caps_intersect_full (filter, mode_caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (mode_caps);
  } else {
    caps = mode_caps;
  }

  return caps;
}

static GstFlowReturn
gst_decklink_video_sink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  return GST_FLOW_OK;
}

static void
convert_to_internal_clock (GstDecklinkVideoSink * self,
    GstClockTime * timestamp, GstClockTime * duration)
{
  GstClock *clock, *audio_clock;

  g_assert (timestamp != NULL);

  clock = gst_element_get_clock (GST_ELEMENT_CAST (self));
  audio_clock = gst_decklink_output_get_audio_clock (self->output);
  if (clock && clock != self->output->clock && clock != audio_clock) {
    GstClockTime internal, external, rate_n, rate_d;
    gst_clock_get_calibration (self->output->clock, &internal, &external,
        &rate_n, &rate_d);

    if (self->internal_base_time != GST_CLOCK_TIME_NONE) {
      GstClockTime external_timestamp = *timestamp;
      GstClockTime base_time;

      // Convert to the running time corresponding to both clock times
      if (internal < self->internal_base_time)
        internal = 0;
      else
        internal -= self->internal_base_time;

      if (external < self->external_base_time)
        external = 0;
      else
        external -= self->external_base_time;

      // Convert timestamp to the "running time" since we started scheduled
      // playback, that is the difference between the pipeline's base time
      // and our own base time.
      base_time = gst_element_get_base_time (GST_ELEMENT_CAST (self));
      if (base_time > self->external_base_time)
        base_time = 0;
      else
        base_time = self->external_base_time - base_time;

      if (external_timestamp < base_time)
        external_timestamp = 0;
      else
        external_timestamp = external_timestamp - base_time;

      // Get the difference in the external time, note
      // that the running time is external time.
      // Then scale this difference and offset it to
      // our internal time. Now we have the running time
      // according to our internal clock.
      //
      // For the duration we just scale
      if (external > external_timestamp) {
        guint64 diff = external - external_timestamp;
        diff = gst_util_uint64_scale (diff, rate_d, rate_n);
        *timestamp = internal - diff;
      } else {
        guint64 diff = external_timestamp - external;
        diff = gst_util_uint64_scale (diff, rate_d, rate_n);
        *timestamp = internal + diff;
      }

      GST_LOG_OBJECT (self,
          "Converted %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT " (internal: %"
          GST_TIME_FORMAT " external %" GST_TIME_FORMAT " rate: %lf)",
          GST_TIME_ARGS (external_timestamp), GST_TIME_ARGS (*timestamp),
          GST_TIME_ARGS (internal), GST_TIME_ARGS (external),
          ((gdouble) rate_n) / ((gdouble) rate_d));

      if (duration) {
        GstClockTime external_duration = *duration;

        *duration = gst_util_uint64_scale (external_duration, rate_d, rate_n);

        GST_LOG_OBJECT (self,
            "Converted duration %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
            " (internal: %" GST_TIME_FORMAT " external %" GST_TIME_FORMAT
            " rate: %lf)", GST_TIME_ARGS (external_duration),
            GST_TIME_ARGS (*duration), GST_TIME_ARGS (internal),
            GST_TIME_ARGS (external), ((gdouble) rate_n) / ((gdouble) rate_d));
      }
    } else {
      GST_LOG_OBJECT (self, "No clock conversion needed, not started yet");
    }
  } else {
    GST_LOG_OBJECT (self, "No clock conversion needed, same clocks");
  }
}

static GstFlowReturn
gst_decklink_video_sink_prepare (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstDecklinkVideoSink *self = GST_DECKLINK_VIDEO_SINK_CAST (bsink);
  GstVideoFrame vframe;
  IDeckLinkMutableVideoFrame *frame;
  guint8 *outdata, *indata;
  GstFlowReturn flow_ret;
  HRESULT ret;
  GstClockTime timestamp, duration;
  GstClockTime running_time, running_time_duration;
  GstClockTime latency, render_delay;
  GstClockTimeDiff ts_offset;
  gint i;
  GstDecklinkVideoFormat caps_format;
  BMDPixelFormat format;
  gint bpp;

  GST_DEBUG_OBJECT (self, "Preparing buffer %p", buffer);

  // FIXME: Handle no timestamps
  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    return GST_FLOW_ERROR;
  }

  caps_format = gst_decklink_type_from_video_format (self->info.finfo->format);
  format = gst_decklink_pixel_format_from_type (caps_format);
  bpp = gst_decklink_bpp_from_type (caps_format);

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);
  if (duration == GST_CLOCK_TIME_NONE) {
    duration =
        gst_util_uint64_scale_int (GST_SECOND, self->info.fps_d,
        self->info.fps_n);
  }
  running_time =
      gst_segment_to_running_time (&GST_BASE_SINK_CAST (self)->segment,
      GST_FORMAT_TIME, timestamp);
  running_time_duration =
      gst_segment_to_running_time (&GST_BASE_SINK_CAST (self)->segment,
      GST_FORMAT_TIME, timestamp + duration) - running_time;

  /* See gst_base_sink_adjust_time() */
  latency = gst_base_sink_get_latency (bsink);
  render_delay = gst_base_sink_get_render_delay (bsink);
  ts_offset = gst_base_sink_get_ts_offset (bsink);

  running_time += latency;

  if (ts_offset < 0) {
    ts_offset = -ts_offset;
    if ((GstClockTime) ts_offset < running_time)
      running_time -= ts_offset;
    else
      running_time = 0;
  } else {
    running_time += ts_offset;
  }

  if (running_time > render_delay)
    running_time -= render_delay;
  else
    running_time = 0;

  ret = self->output->output->CreateVideoFrame (self->info.width,
      self->info.height, self->info.stride[0], format, bmdFrameFlagDefault,
      &frame);
  if (ret != S_OK) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED,
        (NULL), ("Failed to create video frame: 0x%08x", ret));
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&vframe, &self->info, buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map video frame");
    flow_ret = GST_FLOW_ERROR;
    goto out;
  }

  frame->GetBytes ((void **) &outdata);
  indata = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0);
  for (i = 0; i < self->info.height; i++) {
    memcpy (outdata, indata, GST_VIDEO_FRAME_WIDTH (&vframe) * bpp);
    indata += GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
    outdata += frame->GetRowBytes ();
  }
  gst_video_frame_unmap (&vframe);

  convert_to_internal_clock (self, &running_time, &running_time_duration);

  GST_LOG_OBJECT (self, "Scheduling video frame %p at %" GST_TIME_FORMAT
      " with duration %" GST_TIME_FORMAT, frame, GST_TIME_ARGS (running_time),
      GST_TIME_ARGS (running_time_duration));

  ret = self->output->output->ScheduleVideoFrame (frame,
      running_time, running_time_duration, GST_SECOND);
  if (ret != S_OK) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED,
        (NULL), ("Failed to schedule frame: 0x%08x", ret));
    flow_ret = GST_FLOW_ERROR;
    goto out;
  }

  flow_ret = GST_FLOW_OK;

out:

  frame->Release ();

  return flow_ret;
}

static gboolean
gst_decklink_video_sink_open (GstBaseSink * bsink)
{
  GstDecklinkVideoSink *self = GST_DECKLINK_VIDEO_SINK_CAST (bsink);
  const GstDecklinkMode *mode;

  GST_DEBUG_OBJECT (self, "Stopping");

  self->output =
      gst_decklink_acquire_nth_output (self->device_number,
      GST_ELEMENT_CAST (self), FALSE);
  if (!self->output) {
    GST_ERROR_OBJECT (self, "Failed to acquire output");
    return FALSE;
  }

  mode = gst_decklink_get_mode (self->mode);
  g_assert (mode != NULL);

  g_mutex_lock (&self->output->lock);
  self->output->mode = mode;
  self->output->start_scheduled_playback =
      gst_decklink_video_sink_start_scheduled_playback;
  self->output->clock_start_time = GST_CLOCK_TIME_NONE;
  self->output->clock_epoch += self->output->clock_last_time;
  self->output->clock_last_time = 0;
  self->output->clock_offset = 0;
  g_mutex_unlock (&self->output->lock);

  return TRUE;
}

static gboolean
gst_decklink_video_sink_close (GstBaseSink * bsink)
{
  GstDecklinkVideoSink *self = GST_DECKLINK_VIDEO_SINK_CAST (bsink);

  GST_DEBUG_OBJECT (self, "Closing");

  if (self->output) {
    g_mutex_lock (&self->output->lock);
    self->output->mode = NULL;
    self->output->video_enabled = FALSE;
    if (self->output->start_scheduled_playback)
      self->output->start_scheduled_playback (self->output->videosink);
    g_mutex_unlock (&self->output->lock);

    self->output->output->DisableVideoOutput ();
    gst_decklink_release_nth_output (self->device_number,
        GST_ELEMENT_CAST (self), FALSE);
    self->output = NULL;
  }

  return TRUE;
}

static gboolean
gst_decklink_video_sink_stop (GstDecklinkVideoSink * self)
{
  GST_DEBUG_OBJECT (self, "Stopping");

  if (self->output && self->output->video_enabled) {
    g_mutex_lock (&self->output->lock);
    self->output->video_enabled = FALSE;
    g_mutex_unlock (&self->output->lock);

    self->output->output->DisableVideoOutput ();
    self->output->output->SetScheduledFrameCompletionCallback (NULL);
  }

  return TRUE;
}

static void
gst_decklink_video_sink_start_scheduled_playback (GstElement * element)
{
  GstDecklinkVideoSink *self = GST_DECKLINK_VIDEO_SINK_CAST (element);
  GstClockTime start_time;
  HRESULT res;
  bool active;

  if (self->output->video_enabled && (!self->output->audiosink
          || self->output->audio_enabled)
      && (GST_STATE (self) == GST_STATE_PLAYING
          || GST_STATE_PENDING (self) == GST_STATE_PLAYING)) {
    // Need to unlock to get the clock time
    g_mutex_unlock (&self->output->lock);

    // FIXME: start time is the same for the complete pipeline,
    // but what we need here is the start time of this element!
    start_time = gst_element_get_base_time (element);
    if (start_time != GST_CLOCK_TIME_NONE)
      start_time = gst_clock_get_time (GST_ELEMENT_CLOCK (self)) - start_time;

    // FIXME: This will probably not work
    if (start_time == GST_CLOCK_TIME_NONE)
      start_time = 0;

    // Current times of internal and external clock when we go to
    // playing. We need this to convert the pipeline running time
    // to the running time of the hardware
    //
    // We can't use the normal base time for the external clock
    // because we might go to PLAYING later than the pipeline
    self->internal_base_time =
        gst_clock_get_internal_time (self->output->clock);
    self->external_base_time =
        gst_clock_get_internal_time (GST_ELEMENT_CLOCK (self));

    convert_to_internal_clock (self, &start_time, NULL);

    g_mutex_lock (&self->output->lock);
    // Check if someone else started in the meantime
    if (self->output->started)
      return;

    active = false;
    self->output->output->IsScheduledPlaybackRunning (&active);
    if (active) {
      GST_DEBUG_OBJECT (self, "Stopping scheduled playback");

      self->output->started = FALSE;

      res = self->output->output->StopScheduledPlayback (0, 0, 0);
      if (res != S_OK) {
        GST_ELEMENT_ERROR (self, STREAM, FAILED,
            (NULL), ("Failed to stop scheduled playback: 0x%08x", res));
        return;
      }
    }

    GST_DEBUG_OBJECT (self,
        "Starting scheduled playback at %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start_time));

    res =
        self->output->output->StartScheduledPlayback (start_time,
        GST_SECOND, 1.0);
    if (res != S_OK) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED,
          (NULL), ("Failed to start scheduled playback: 0x%08x", res));
      return;
    }

    self->output->started = TRUE;
    self->output->clock_restart = TRUE;

    // Need to unlock to get the clock time
    g_mutex_unlock (&self->output->lock);

    // Sample the clocks again to get the most accurate values
    // after we started scheduled playback
    self->internal_base_time =
        gst_clock_get_internal_time (self->output->clock);
    self->external_base_time =
        gst_clock_get_internal_time (GST_ELEMENT_CLOCK (self));
    g_mutex_lock (&self->output->lock);
  } else {
    GST_DEBUG_OBJECT (self, "Not starting scheduled playback yet");
  }
}

static GstStateChangeReturn
gst_decklink_video_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstDecklinkVideoSink *self = GST_DECKLINK_VIDEO_SINK_CAST (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      g_mutex_lock (&self->output->lock);
      self->output->clock_start_time = GST_CLOCK_TIME_NONE;
      self->output->clock_epoch += self->output->clock_last_time;
      self->output->clock_last_time = 0;
      self->output->clock_offset = 0;
      g_mutex_unlock (&self->output->lock);
      gst_element_post_message (element,
          gst_message_new_clock_provide (GST_OBJECT_CAST (element),
              self->output->clock, TRUE));
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:{
      GstClock *clock, *audio_clock;

      clock = gst_element_get_clock (GST_ELEMENT_CAST (self));
      audio_clock = gst_decklink_output_get_audio_clock (self->output);
      if (clock && clock != self->output->clock && clock != audio_clock) {
        gst_clock_set_master (self->output->clock, clock);
      }
      if (clock)
        gst_object_unref (clock);
      if (audio_clock)
        gst_object_unref (audio_clock);

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
      gst_element_post_message (element,
          gst_message_new_clock_lost (GST_OBJECT_CAST (element),
              self->output->clock));
      gst_clock_set_master (self->output->clock, NULL);
      // Reset calibration to make the clock reusable next time we use it
      gst_clock_set_calibration (self->output->clock, 0, 0, 1, 1);
      g_mutex_lock (&self->output->lock);
      self->output->clock_start_time = GST_CLOCK_TIME_NONE;
      self->output->clock_epoch += self->output->clock_last_time;
      self->output->clock_last_time = 0;
      self->output->clock_offset = 0;
      g_mutex_unlock (&self->output->lock);
      gst_decklink_video_sink_stop (self);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:{
      GstClockTime start_time;
      HRESULT res;

      // FIXME: start time is the same for the complete pipeline,
      // but what we need here is the start time of this element!
      start_time = gst_element_get_base_time (element);
      if (start_time != GST_CLOCK_TIME_NONE)
        start_time = gst_clock_get_time (GST_ELEMENT_CLOCK (self)) - start_time;

      // FIXME: This will probably not work
      if (start_time == GST_CLOCK_TIME_NONE)
        start_time = 0;

      convert_to_internal_clock (self, &start_time, NULL);

      // The start time is now the running time when we stopped
      // playback

      GST_DEBUG_OBJECT (self,
          "Stopping scheduled playback at %" GST_TIME_FORMAT,
          GST_TIME_ARGS (start_time));

      g_mutex_lock (&self->output->lock);
      self->output->started = FALSE;
      g_mutex_unlock (&self->output->lock);
      res =
          self->output->output->StopScheduledPlayback (start_time, 0,
          GST_SECOND);
      if (res != S_OK) {
        GST_ELEMENT_ERROR (self, STREAM, FAILED,
            (NULL), ("Failed to stop scheduled playback: 0x%08x", res));
        ret = GST_STATE_CHANGE_FAILURE;
      }
      self->internal_base_time = GST_CLOCK_TIME_NONE;
      self->external_base_time = GST_CLOCK_TIME_NONE;
      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:{
      g_mutex_lock (&self->output->lock);
      if (self->output->start_scheduled_playback)
        self->output->start_scheduled_playback (self->output->videosink);
      g_mutex_unlock (&self->output->lock);
      break;
    }
    default:
      break;
  }

  return ret;
}

static GstClock *
gst_decklink_video_sink_provide_clock (GstElement * element)
{
  GstDecklinkVideoSink *self = GST_DECKLINK_VIDEO_SINK_CAST (element);

  if (!self->output)
    return NULL;

  return GST_CLOCK_CAST (gst_object_ref (self->output->clock));
}

static gboolean
gst_decklink_video_sink_propose_allocation (GstBaseSink * bsink,
    GstQuery * query)
{
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool;
  guint size;

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstStructure *structure;
    GstAllocator *allocator = NULL;
    GstAllocationParams params = { (GstMemoryFlags) 0, 15, 0, 0 };

    if (gst_query_get_n_allocation_params (query) > 0)
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    else
      gst_query_add_allocation_param (query, allocator, &params);

    pool = gst_video_buffer_pool_new ();

    structure = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (structure, caps, size, 0, 0);
    gst_buffer_pool_config_set_allocator (structure, allocator, &params);

    if (allocator)
      gst_object_unref (allocator);

    if (!gst_buffer_pool_set_config (pool, structure))
      goto config_failed;

    gst_query_add_allocation_pool (query, pool, size, 0, 0);
    gst_object_unref (pool);
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  }

  return TRUE;
  // ERRORS
config_failed:
  {
    GST_ERROR_OBJECT (bsink, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }
}
