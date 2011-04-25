/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstbasevideoencoder.h"

GST_DEBUG_CATEGORY (basevideoencoder_debug);
#define GST_CAT_DEFAULT basevideoencoder_debug

static void gst_base_video_encoder_finalize (GObject * object);

static gboolean gst_base_video_encoder_sink_setcaps (GstPad * pad,
    GstCaps * caps);
static gboolean gst_base_video_encoder_src_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_base_video_encoder_sink_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_base_video_encoder_chain (GstPad * pad,
    GstBuffer * buf);
//static GstFlowReturn gst_base_video_encoder_process (GstBaseVideoEncoder *base_video_encoder);
static GstStateChangeReturn gst_base_video_encoder_change_state (GstElement *
    element, GstStateChange transition);
static const GstQueryType *gst_base_video_encoder_get_query_types (GstPad *
    pad);
static gboolean gst_base_video_encoder_src_query (GstPad * pad,
    GstQuery * query);


GST_BOILERPLATE (GstBaseVideoEncoder, gst_base_video_encoder, GstBaseVideoCodec,
    GST_TYPE_BASE_VIDEO_CODEC);

static void
gst_base_video_encoder_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (basevideoencoder_debug, "basevideoencoder", 0,
      "Base Video Encoder");

}

static void
gst_base_video_encoder_class_init (GstBaseVideoEncoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_base_video_encoder_finalize;

  gstelement_class->change_state = gst_base_video_encoder_change_state;

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_base_video_encoder_init (GstBaseVideoEncoder * base_video_encoder,
    GstBaseVideoEncoderClass * klass)
{
  GstPad *pad;

  GST_DEBUG ("gst_base_video_encoder_init");

  pad = GST_BASE_VIDEO_CODEC_SINK_PAD (base_video_encoder);

  gst_pad_set_chain_function (pad, gst_base_video_encoder_chain);
  gst_pad_set_event_function (pad, gst_base_video_encoder_sink_event);
  gst_pad_set_setcaps_function (pad, gst_base_video_encoder_sink_setcaps);
  //gst_pad_set_query_function (pad, gst_base_video_encoder_sink_query);

  pad = GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder);

  gst_pad_set_query_type_function (pad, gst_base_video_encoder_get_query_types);
  gst_pad_set_query_function (pad, gst_base_video_encoder_src_query);
  gst_pad_set_event_function (pad, gst_base_video_encoder_src_event);

  base_video_encoder->a.at_eos = FALSE;
}

static gboolean
gst_base_video_encoder_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseVideoEncoder *base_video_encoder;
  GstBaseVideoEncoderClass *base_video_encoder_class;
  GstStructure *structure;
  GstVideoState *state;
  gboolean ret;

  base_video_encoder = GST_BASE_VIDEO_ENCODER (gst_pad_get_parent (pad));
  base_video_encoder_class =
      GST_BASE_VIDEO_ENCODER_GET_CLASS (base_video_encoder);

  GST_DEBUG ("setcaps");

  state = &GST_BASE_VIDEO_CODEC (base_video_encoder)->state;
  structure = gst_caps_get_structure (caps, 0);

  gst_video_format_parse_caps (caps, &state->format,
      &state->width, &state->height);

  state->fps_n = 0;
  state->fps_d = 1;
  gst_video_parse_caps_framerate (caps, &state->fps_n, &state->fps_d);
  if (state->fps_d == 0) {
    state->fps_n = 0;
    state->fps_d = 1;
  }

  state->par_n = 1;
  state->par_d = 1;
  gst_video_parse_caps_pixel_aspect_ratio (caps, &state->par_n, &state->par_d);

  state->have_interlaced = gst_structure_get_boolean (structure,
      "interlaced", &state->interlaced);

  state->clean_width = state->width;
  state->clean_height = state->height;
  state->clean_offset_left = 0;
  state->clean_offset_top = 0;

  ret = base_video_encoder_class->set_format (base_video_encoder,
      &GST_BASE_VIDEO_CODEC (base_video_encoder)->state);
  if (ret) {
    ret = base_video_encoder_class->start (base_video_encoder);
  }

  g_object_unref (base_video_encoder);

  return ret;
}

static void
gst_base_video_encoder_finalize (GObject * object)
{
  GST_DEBUG_OBJECT (object, "finalize");

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_base_video_encoder_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoEncoder *base_video_encoder;
  GstBaseVideoEncoderClass *base_video_encoder_class;
  gboolean ret = FALSE;

  base_video_encoder = GST_BASE_VIDEO_ENCODER (gst_pad_get_parent (pad));
  base_video_encoder_class =
      GST_BASE_VIDEO_ENCODER_GET_CLASS (base_video_encoder);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      base_video_encoder->a.at_eos = TRUE;
      if (base_video_encoder_class->finish) {
        base_video_encoder_class->finish (base_video_encoder);
      }

      ret =
          gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder),
          event);
    }
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      double rate;
      double applied_rate;
      GstFormat format;
      gint64 start;
      gint64 stop;
      gint64 position;

      gst_event_parse_new_segment_full (event, &update, &rate,
          &applied_rate, &format, &start, &stop, &position);

      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      GST_DEBUG ("new segment %" GST_TIME_FORMAT " %" GST_TIME_FORMAT,
          GST_TIME_ARGS (start), GST_TIME_ARGS (position));

      base_video_encoder->a.at_eos = FALSE;
      gst_segment_set_newsegment_full (&GST_BASE_VIDEO_CODEC
          (base_video_encoder)->segment, update, rate, applied_rate, format,
          start, stop, position);

      ret =
          gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder),
          event);
    }
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      const GstStructure *s;

      s = gst_event_get_structure (event);

      if (gst_structure_has_name (s, "GstForceKeyUnit")) {
        GST_OBJECT_LOCK (base_video_encoder);
        base_video_encoder->force_keyframe = TRUE;
        GST_OBJECT_UNLOCK (base_video_encoder);
        gst_event_unref (event);
        ret = GST_FLOW_OK;
      } else {
        ret =
            gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD
            (base_video_encoder), event);
      }
      break;
    }
    default:
      /* FIXME this changes the order of events */
      ret =
          gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder),
          event);
      break;
  }

done:
  gst_object_unref (base_video_encoder);
  return ret;

newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (base_video_encoder, "received non TIME newsegment");
    gst_event_unref (event);
    goto done;
  }
}

static gboolean
gst_base_video_encoder_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoEncoder *base_video_encoder;
  gboolean ret = FALSE;

  base_video_encoder = GST_BASE_VIDEO_ENCODER (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      const GstStructure *s;

      s = gst_event_get_structure (event);

      if (gst_structure_has_name (s, "GstForceKeyUnit")) {
        GST_OBJECT_LOCK (base_video_encoder);
        base_video_encoder->force_keyframe = TRUE;
        GST_OBJECT_UNLOCK (base_video_encoder);

        gst_event_unref (event);
        ret = TRUE;
      } else {
        ret =
            gst_pad_push_event (GST_BASE_VIDEO_CODEC_SINK_PAD
            (base_video_encoder), event);
      }
      break;
    }
    default:
      ret =
          gst_pad_push_event (GST_BASE_VIDEO_CODEC_SINK_PAD
          (base_video_encoder), event);
      break;
  }

  gst_object_unref (base_video_encoder);
  return ret;
}

static const GstQueryType *
gst_base_video_encoder_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    //GST_QUERY_POSITION,
    //GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    GST_QUERY_LATENCY,
    0
  };

  return query_types;
}

static gboolean
gst_base_video_encoder_src_query (GstPad * pad, GstQuery * query)
{
  GstBaseVideoEncoder *enc;
  gboolean res;
  GstPad *peerpad;

  enc = GST_BASE_VIDEO_ENCODER (gst_pad_get_parent (pad));
  peerpad = gst_pad_get_peer (GST_BASE_VIDEO_CODEC_SINK_PAD (enc));

  switch GST_QUERY_TYPE
    (query) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res =
          gst_base_video_encoded_video_convert (&GST_BASE_VIDEO_CODEC
          (enc)->state, src_fmt, src_val, &dest_fmt, &dest_val);
      if (!res)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min_latency, max_latency;

      res = gst_pad_query (peerpad, query);
      if (res) {
        gst_query_parse_latency (query, &live, &min_latency, &max_latency);

        min_latency += enc->min_latency;
        if (max_latency != GST_CLOCK_TIME_NONE) {
          max_latency += enc->max_latency;
        }

        gst_query_set_latency (query, live, min_latency, max_latency);
      }
    }
      break;
    default:
      res = gst_pad_query_default (pad, query);
    }
  gst_object_unref (peerpad);
  gst_object_unref (enc);
  return res;

error:
  GST_DEBUG_OBJECT (enc, "query failed");
  gst_object_unref (peerpad);
  gst_object_unref (enc);
  return res;
}

static gboolean
gst_pad_is_negotiated (GstPad * pad)
{
  GstCaps *caps;

  g_return_val_if_fail (pad != NULL, FALSE);

  caps = gst_pad_get_negotiated_caps (pad);
  if (caps) {
    gst_caps_unref (caps);
    return TRUE;
  }

  return FALSE;
}

static GstFlowReturn
gst_base_video_encoder_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseVideoEncoder *base_video_encoder;
  GstBaseVideoEncoderClass *klass;
  GstVideoFrame *frame;

  if (!gst_pad_is_negotiated (pad)) {
    return GST_FLOW_NOT_NEGOTIATED;
  }

  base_video_encoder = GST_BASE_VIDEO_ENCODER (gst_pad_get_parent (pad));
  klass = GST_BASE_VIDEO_ENCODER_GET_CLASS (base_video_encoder);

  if (base_video_encoder->a.at_eos) {
    return GST_FLOW_UNEXPECTED;
  }

  if (base_video_encoder->sink_clipping) {
    gint64 start = GST_BUFFER_TIMESTAMP (buf);
    gint64 stop = start + GST_BUFFER_DURATION (buf);
    gint64 clip_start;
    gint64 clip_stop;

    if (!gst_segment_clip (&GST_BASE_VIDEO_CODEC (base_video_encoder)->segment,
            GST_FORMAT_TIME, start, stop, &clip_start, &clip_stop)) {
      GST_DEBUG ("clipping to segment dropped frame");
      goto done;
    }
  }

  frame =
      gst_base_video_codec_new_frame (GST_BASE_VIDEO_CODEC
      (base_video_encoder));
  frame->sink_buffer = buf;
  frame->presentation_timestamp = GST_BUFFER_TIMESTAMP (buf);
  frame->presentation_duration = GST_BUFFER_DURATION (buf);
  frame->presentation_frame_number =
      base_video_encoder->presentation_frame_number;
  base_video_encoder->presentation_frame_number++;

  GST_BASE_VIDEO_CODEC (base_video_encoder)->frames =
      g_list_append (GST_BASE_VIDEO_CODEC (base_video_encoder)->frames, frame);

  klass->handle_frame (base_video_encoder, frame);

done:
  g_object_unref (base_video_encoder);

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_base_video_encoder_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseVideoEncoder *base_video_encoder;
  GstBaseVideoEncoderClass *base_video_encoder_class;
  GstStateChangeReturn ret;

  base_video_encoder = GST_BASE_VIDEO_ENCODER (element);
  base_video_encoder_class = GST_BASE_VIDEO_ENCODER_GET_CLASS (element);

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (base_video_encoder_class->stop) {
        base_video_encoder_class->stop (base_video_encoder);
      }
      break;
    default:
      break;
  }

  return ret;
}

GstFlowReturn
gst_base_video_encoder_finish_frame (GstBaseVideoEncoder * base_video_encoder,
    GstVideoFrame * frame)
{
  GstFlowReturn ret;
  GstBaseVideoEncoderClass *base_video_encoder_class;

  base_video_encoder_class =
      GST_BASE_VIDEO_ENCODER_GET_CLASS (base_video_encoder);

  if (frame->is_sync_point) {
    base_video_encoder->distance_from_sync = 0;
    GST_BUFFER_FLAG_UNSET (frame->src_buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_BUFFER_FLAG_SET (frame->src_buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  frame->distance_from_sync = base_video_encoder->distance_from_sync;
  base_video_encoder->distance_from_sync++;

  frame->decode_frame_number = frame->system_frame_number - 1;
  if (frame->decode_frame_number < 0) {
    frame->decode_timestamp = 0;
  } else {
    frame->decode_timestamp = gst_util_uint64_scale (frame->decode_frame_number,
        GST_SECOND * GST_BASE_VIDEO_CODEC (base_video_encoder)->state.fps_d,
        GST_BASE_VIDEO_CODEC (base_video_encoder)->state.fps_n);
  }

  GST_BUFFER_TIMESTAMP (frame->src_buffer) = frame->presentation_timestamp;
  GST_BUFFER_DURATION (frame->src_buffer) = frame->presentation_duration;
  GST_BUFFER_OFFSET (frame->src_buffer) = frame->decode_timestamp;

  GST_BASE_VIDEO_CODEC (base_video_encoder)->frames =
      g_list_remove (GST_BASE_VIDEO_CODEC (base_video_encoder)->frames, frame);

  if (!base_video_encoder->set_output_caps) {
    if (base_video_encoder_class->get_caps) {
      GST_BASE_VIDEO_CODEC (base_video_encoder)->caps =
          base_video_encoder_class->get_caps (base_video_encoder);
    } else {
      GST_BASE_VIDEO_CODEC (base_video_encoder)->caps =
          gst_caps_new_simple ("video/unknown", NULL);
    }
    gst_pad_set_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder),
        GST_BASE_VIDEO_CODEC (base_video_encoder)->caps);
    base_video_encoder->set_output_caps = TRUE;
  }

  gst_buffer_set_caps (GST_BUFFER (frame->src_buffer),
      GST_BASE_VIDEO_CODEC (base_video_encoder)->caps);

  if (frame->force_keyframe) {
    GstClockTime stream_time;
    GstClockTime running_time;
    GstStructure *s;

    running_time =
        gst_segment_to_running_time (&GST_BASE_VIDEO_CODEC
        (base_video_encoder)->segment, GST_FORMAT_TIME,
        frame->presentation_timestamp);
    stream_time =
        gst_segment_to_stream_time (&GST_BASE_VIDEO_CODEC
        (base_video_encoder)->segment, GST_FORMAT_TIME,
        frame->presentation_timestamp);

    /* FIXME this should send the event that we got on the sink pad
       instead of creating a new one */
    s = gst_structure_new ("GstForceKeyUnit",
        "timestamp", G_TYPE_UINT64, frame->presentation_timestamp,
        "stream-time", G_TYPE_UINT64, stream_time,
        "running-time", G_TYPE_UINT64, running_time, NULL);

    gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder),
        gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s));
  }

  if (base_video_encoder_class->shape_output) {
    ret = base_video_encoder_class->shape_output (base_video_encoder, frame);
  } else {
    ret =
        gst_pad_push (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder),
        frame->src_buffer);
  }

  gst_base_video_codec_free_frame (frame);

  return ret;
}

int
gst_base_video_encoder_get_height (GstBaseVideoEncoder * base_video_encoder)
{
  return GST_BASE_VIDEO_CODEC (base_video_encoder)->state.height;
}

int
gst_base_video_encoder_get_width (GstBaseVideoEncoder * base_video_encoder)
{
  return GST_BASE_VIDEO_CODEC (base_video_encoder)->state.width;
}

const GstVideoState *
gst_base_video_encoder_get_state (GstBaseVideoEncoder * base_video_encoder)
{
  return &GST_BASE_VIDEO_CODEC (base_video_encoder)->state;
}

GstFlowReturn
gst_base_video_encoder_end_of_stream (GstBaseVideoEncoder * base_video_encoder,
    GstBuffer * buffer)
{

  if (GST_BASE_VIDEO_CODEC (base_video_encoder)->frames) {
    GST_WARNING ("EOS with frames left over");
  }

  return gst_pad_push (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder),
      buffer);
}

void
gst_base_video_encoder_set_latency (GstBaseVideoEncoder * base_video_encoder,
    GstClockTime min_latency, GstClockTime max_latency)
{
  g_return_if_fail (min_latency >= 0);
  g_return_if_fail (max_latency >= min_latency);

  base_video_encoder->min_latency = min_latency;
  base_video_encoder->max_latency = max_latency;

  gst_element_post_message (GST_ELEMENT_CAST (base_video_encoder),
      gst_message_new_latency (GST_OBJECT_CAST (base_video_encoder)));
}

void
gst_base_video_encoder_set_latency_fields (GstBaseVideoEncoder *
    base_video_encoder, int n_fields)
{
  gint64 latency;

  latency = gst_util_uint64_scale (n_fields,
      GST_BASE_VIDEO_CODEC (base_video_encoder)->state.fps_d * GST_SECOND,
      2 * GST_BASE_VIDEO_CODEC (base_video_encoder)->state.fps_n);

  gst_base_video_encoder_set_latency (base_video_encoder, latency, latency);

}

GstVideoFrame *
gst_base_video_encoder_get_oldest_frame (GstBaseVideoEncoder *
    base_video_encoder)
{
  GList *g;

  g = g_list_first (GST_BASE_VIDEO_CODEC (base_video_encoder)->frames);

  if (g == NULL)
    return NULL;
  return (GstVideoFrame *) (g->data);
}
