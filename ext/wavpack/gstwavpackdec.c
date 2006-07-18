/* GStreamer Wavpack plugin
 * Copyright (c) 2005 Arwed v. Merkatz <v.merkatz@gmx.net>
 * Copyright (c) 2006 Edward Hervey <bilboed@gmail.com>
 * Copyright (c) 2006 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstwavpackdec.c: raw Wavpack bitstream decoder
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

#include <gst/gst.h>
#include <gst/audio/audio.h>

#include <math.h>
#include <string.h>

#include <wavpack/wavpack.h>
#include "gstwavpackdec.h"
#include "gstwavpackcommon.h"
#include "gstwavpackstreamreader.h"


#define WAVPACK_DEC_MAX_ERRORS 16

GST_DEBUG_CATEGORY_STATIC (gst_wavpack_dec_debug);
#define GST_CAT_DEFAULT gst_wavpack_dec_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "width = (int) { 8, 16, 24, 32 }, "
        "channels = (int) [ 1, 2 ], "
        "rate = (int) [ 6000, 192000 ], " "framed = (boolean) true")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) { 8, 16, 32 }, "
        "depth = (int) [ 8, 32 ], "
        "channels = (int) [ 1, 2 ], "
        "rate = (int) [ 6000, 192000 ], "
        "endianness = (int) BYTE_ORDER, " "signed = (boolean) true")
    );

static GstFlowReturn gst_wavpack_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_wavpack_dec_sink_event (GstPad * pad, GstEvent * event);
static void gst_wavpack_dec_finalize (GObject * object);
static GstStateChangeReturn gst_wavpack_dec_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_wavpack_dec_sink_event (GstPad * pad, GstEvent * event);

GST_BOILERPLATE (GstWavpackDec, gst_wavpack_dec, GstElement, GST_TYPE_ELEMENT);

static void
gst_wavpack_dec_base_init (gpointer klass)
{
  static const GstElementDetails plugin_details =
      GST_ELEMENT_DETAILS ("WavePack audio decoder",
      "Codec/Decoder/Audio",
      "Decode Wavpack audio data",
      "Arwed v. Merkatz <v.merkatz@gmx.net>, "
      "Sebastian Dröge <slomo@circular-chaos.org>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_wavpack_dec_class_init (GstWavpackDecClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_change_state);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_wavpack_dec_finalize);
}

static void
gst_wavpack_dec_init (GstWavpackDec * dec, GstWavpackDecClass * gklass)
{
  dec->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_chain));
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_sink_event));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->context = NULL;
  dec->stream_reader = gst_wavpack_stream_reader_new ();

  dec->wv_id.buffer = NULL;
  dec->wv_id.position = dec->wv_id.length = 0;

  dec->error_count = 0;

  dec->channels = 0;
  dec->sample_rate = 0;
  dec->width = 0;
  dec->depth = 0;

  gst_segment_init (&dec->segment, GST_FORMAT_UNDEFINED);
}

static void
gst_wavpack_dec_finalize (GObject * object)
{
  GstWavpackDec *dec = GST_WAVPACK_DEC (object);

  g_free (dec->stream_reader);
  dec->stream_reader = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_wavpack_dec_format_samples (GstWavpackDec * dec, guint8 * out_buffer,
    int32_t * samples, guint num_samples)
{
  switch (dec->width) {
    case 8:{
      gint8 *dst = (gint8 *) out_buffer;
      gint8 *end = dst + (num_samples * dec->channels);

      while (dst < end) {
        *dst++ = (gint8) * samples++;
      }
      break;
    }
    case 16:{
      gint16 *dst = (gint16 *) out_buffer;
      gint16 *end = dst + (num_samples * dec->channels);

      while (dst < end) {
        *dst++ = (gint16) * samples++;
      }
      break;
    }
    case 24:
    case 32:{
      gint32 *dst = (gint32 *) out_buffer;
      gint32 *end = dst + (num_samples * dec->channels);

      while (dst < end) {
        *dst++ = *samples++;
      }
      break;
    }
    default:
      g_return_if_reached ();
      break;
  }
}

static gboolean
gst_wavpack_dec_clip_outgoing_buffer (GstWavpackDec * dec, GstBuffer * buf)
{
  gint64 start, stop, cstart, cstop, diff;

  if (dec->segment.format != GST_FORMAT_TIME)
    return TRUE;

  start = GST_BUFFER_TIMESTAMP (buf);
  stop = start + GST_BUFFER_DURATION (buf);

  if (gst_segment_clip (&dec->segment, GST_FORMAT_TIME,
          start, stop, &cstart, &cstop)) {

    diff = cstart - start;
    if (diff > 0) {
      GST_BUFFER_TIMESTAMP (buf) = cstart;
      GST_BUFFER_DURATION (buf) -= diff;

      diff = (dec->width / 8) * dec->channels
          * GST_CLOCK_TIME_TO_FRAMES (diff, dec->sample_rate);
      GST_BUFFER_DATA (buf) += diff;
      GST_BUFFER_SIZE (buf) -= diff;
    }

    diff = cstop - stop;
    if (diff > 0) {
      GST_BUFFER_DURATION (buf) -= diff;

      diff = (dec->width / 8) * dec->channels
          * GST_CLOCK_TIME_TO_FRAMES (diff, dec->sample_rate);
      GST_BUFFER_SIZE (buf) -= diff;
    }
  } else {
    GST_DEBUG_OBJECT (dec, "buffer is outside configured segment");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_wavpack_dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstWavpackDec *dec;
  GstBuffer *outbuf;
  GstFlowReturn ret = GST_FLOW_OK;
  WavpackHeader wph;
  int32_t *unpack_buf = NULL;
  int32_t decoded, unpacked_size;
  gboolean format_changed;

  dec = GST_WAVPACK_DEC (GST_PAD_PARENT (pad));

  /* check input, we only accept framed input with complete chunks */
  if (GST_BUFFER_SIZE (buf) < sizeof (WavpackHeader))
    goto input_not_framed;

  if (!gst_wavpack_read_header (&wph, GST_BUFFER_DATA (buf)))
    goto invalid_header;

  if (GST_BUFFER_SIZE (buf) != wph.ckSize + 4 * 1 + 4)
    goto input_not_framed;

  dec->wv_id.buffer = GST_BUFFER_DATA (buf);
  dec->wv_id.length = GST_BUFFER_SIZE (buf);
  dec->wv_id.position = 0;

  /* create a new wavpack context if there is none yet but if there
   * was already one (i.e. caps were set on the srcpad) check whether
   * the new one has the same caps */
  if (!dec->context) {
    gchar error_msg[80];

    dec->context = WavpackOpenFileInputEx (dec->stream_reader,
        &dec->wv_id, NULL, error_msg, OPEN_STREAMING, 0);

    if (!dec->context) {
      GST_WARNING ("Couldn't decode buffer: %s", error_msg);
      dec->error_count++;
      if (dec->error_count <= WAVPACK_DEC_MAX_ERRORS) {
        goto out;               /* just return OK for now */
      } else {
        goto decode_error;
      }
    }
  }

  g_assert (dec->context != NULL);

  dec->error_count = 0;

  format_changed =
      (dec->sample_rate != WavpackGetSampleRate (dec->context)) ||
      (dec->channels != WavpackGetNumChannels (dec->context)) ||
      (dec->depth != WavpackGetBitsPerSample (dec->context));

  if (!GST_PAD_CAPS (dec->srcpad) || format_changed) {
    GstCaps *caps;

    dec->sample_rate = WavpackGetSampleRate (dec->context);
    dec->channels = WavpackGetNumChannels (dec->context);
    dec->depth = WavpackGetBitsPerSample (dec->context);
    dec->width =
        (GST_ROUND_UP_8 (dec->depth) == 24) ? 32 : GST_ROUND_UP_8 (dec->depth);

    caps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT, dec->sample_rate,
        "channels", G_TYPE_INT, dec->channels,
        "depth", G_TYPE_INT, dec->depth,
        "width", G_TYPE_INT, dec->width,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE, NULL);

    GST_DEBUG_OBJECT (dec, "setting caps %" GST_PTR_FORMAT, caps);

    /* should always succeed */
    gst_pad_set_caps (dec->srcpad, caps);
    gst_caps_unref (caps);
  }

  unpacked_size = wph.block_samples * (dec->width / 8) * dec->channels;

  /* alloc buffer */
  ret = gst_pad_alloc_buffer (dec->srcpad, GST_BUFFER_OFFSET (buf),
      unpacked_size, GST_PAD_CAPS (dec->srcpad), &outbuf);

  if (ret != GST_FLOW_OK)
    goto out;

  /* decode */
  unpack_buf = g_new (int32_t, wph.block_samples * dec->channels);
  decoded = WavpackUnpackSamples (dec->context, unpack_buf, wph.block_samples);
  if (decoded != wph.block_samples)
    goto decode_error;

  /* put samples into outbuf buffer */
  gst_wavpack_dec_format_samples (dec, GST_BUFFER_DATA (outbuf),
      unpack_buf, wph.block_samples);
  gst_buffer_stamp (outbuf, buf);

  if (gst_wavpack_dec_clip_outgoing_buffer (dec, outbuf)) {
    GST_LOG_OBJECT (dec, "pushing buffer with time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));
    ret = gst_pad_push (dec->srcpad, outbuf);
  } else {
    gst_buffer_unref (outbuf);
  }

out:

  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_DEBUG_OBJECT (dec, "flow: %s", gst_flow_get_name (ret));
  }

  g_free (unpack_buf);
  gst_buffer_unref (buf);

  return ret;

/* ERRORS */
input_not_framed:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("Expected framed input"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
invalid_header:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("Invalid wavpack header"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
decode_error:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL),
        ("Failed to decode wavpack stream"));
    g_free (unpack_buf);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_wavpack_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstWavpackDec *dec = GST_WAVPACK_DEC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dec, "Received %s event", GST_EVENT_TYPE_NAME (event));
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat fmt;
      gboolean is_update;
      gint64 start, end, base;
      gdouble rate;

      gst_event_parse_new_segment (event, &is_update, &rate, &fmt, &start,
          &end, &base);
      if (fmt == GST_FORMAT_TIME) {
        GST_DEBUG ("Got NEWSEGMENT event in GST_FORMAT_TIME, passing on (%"
            GST_TIME_FORMAT " - %" GST_TIME_FORMAT ")", GST_TIME_ARGS (start),
            GST_TIME_ARGS (end));
        gst_segment_set_newsegment (&dec->segment, is_update, rate, fmt,
            start, end, base);
      } else {
        gst_segment_init (&dec->segment, GST_FORMAT_UNDEFINED);
      }
      break;
    }
    default:
      break;
  }

  gst_object_unref (dec);
  return gst_pad_event_default (pad, event);
}

static GstStateChangeReturn
gst_wavpack_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstWavpackDec *dec = GST_WAVPACK_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&dec->segment, GST_FORMAT_UNDEFINED);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (dec->context) {
        WavpackCloseFile (dec->context);
        dec->context = NULL;
      }
      dec->wv_id.buffer = NULL;
      dec->wv_id.position = 0;
      dec->wv_id.length = 0;
      dec->channels = 0;
      dec->sample_rate = 0;
      dec->width = 0;
      dec->depth = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_wavpack_dec_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "wavpackdec",
          GST_RANK_PRIMARY, GST_TYPE_WAVPACK_DEC))
    return FALSE;
  GST_DEBUG_CATEGORY_INIT (gst_wavpack_dec_debug, "wavpackdec", 0,
      "wavpack decoder");
  return TRUE;
}
