/* GStreamer 
 * Copyright (C) 2011 0p1pp1
 *
 * gstbasespdifenc.c: Base class for IEC61937 encoders/encapsulators,
 *                     used for the S/PDIF interface.
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

#include <string.h>
#include <gst/gst.h>
#include "gstbasespdifenc.h"

GST_DEBUG_CATEGORY_STATIC (basespdif_enc_dbg);
#define GST_CAT_DEFAULT basespdif_enc_dbg

static GstElementClass *parent_class = NULL;

static void gst_base_spdif_enc_class_init (GstBaseSpdifEncClass * klass);
static void gst_base_spdif_enc_init (GstBaseSpdifEnc * enc,
    GstBaseSpdifEncClass * klass);

GType
gst_base_spdif_enc_get_type (void)
{
  static volatile gsize base_spdif_enc_type = 0;

  if (g_once_init_enter (&base_spdif_enc_type)) {
    GType _type;
    static const GTypeInfo base_spdif_enc_info = {
      sizeof (GstBaseSpdifEncClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_base_spdif_enc_class_init,
      NULL,
      NULL,
      sizeof (GstBaseSpdifEnc),
      0,
      (GInstanceInitFunc) gst_base_spdif_enc_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseSpdif", &base_spdif_enc_info, G_TYPE_FLAG_ABSTRACT);
    GST_DEBUG_CATEGORY_INIT (basespdif_enc_dbg, "basespdif_enc", 0,
        "basespdif_enc");
    g_once_init_leave (&base_spdif_enc_type, _type);
  }
  return base_spdif_enc_type;
}


#define AC3_CHANNELS 2
#define AC3_BITS 16
#define IEC958_FRAMESIZE 6144
#define ALSASPDIFSINK_BYTES_PER_FRAME ((AC3_BITS / 8) * AC3_CHANNELS)

static GstStateChangeReturn gst_base_spdif_enc_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_base_spdif_enc_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_base_spdif_enc_chain (GstPad * pad,
    GstBuffer * buffer);
static GstFlowReturn gst_base_spdif_enc_drain (GstBaseSpdifEnc * enc);
static gboolean gst_base_spdif_enc_setcaps (GstPad * pad, GstCaps * caps);

static gboolean gst_base_spdif_enc_frame_info (GstBaseSpdifEnc * enc,
    GstBuffer * buffer);

static GstStaticPadTemplate gst_base_spdif_enc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-iec958")
    );

static void
gst_base_spdif_enc_class_init (GstBaseSpdifEncClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (G_OBJECT_CLASS (klass));

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_spdif_enc_change_state);

  /* Default handlers */
  klass->parse_frame_info = GST_DEBUG_FUNCPTR (gst_base_spdif_enc_frame_info);

}

static void
gst_base_spdif_enc_init (GstBaseSpdifEnc * self, GstBaseSpdifEncClass * klass)
{
  GstElementClass *element_klass = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *template;

  self->adapter = gst_adapter_new ();

  template = gst_element_class_get_pad_template (element_klass, "sink");
  g_return_if_fail (template != NULL);
  self->sinkpad = gst_pad_new_from_template (template, "sink");
  g_return_if_fail (GST_IS_PAD (self->sinkpad));
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_spdif_enc_chain));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_spdif_enc_sink_event));
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_spdif_enc_setcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);


  template = gst_element_class_get_pad_template (element_klass, "src");
  g_return_if_fail (template != NULL);
  self->srcpad = gst_pad_new_from_template (template, "src");
  g_return_if_fail (GST_IS_PAD (self->srcpad));
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_spdif_enc_setcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
  GST_DEBUG_OBJECT (self, "InitFunc finished.");

  GST_WRITE_UINT16_BE (&self->header[0], SYNCWORD1);
  GST_WRITE_UINT16_BE (&self->header[2], SYNCWORD2);
}


static GstStateChangeReturn
gst_base_spdif_enc_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseSpdifEnc *enc = GST_BASE_SPDIF_ENC (element);
  GstStateChangeReturn result;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_pad_set_active (enc->srcpad, TRUE);
      gst_pad_set_active (enc->sinkpad, TRUE);
      gst_adapter_clear (enc->adapter);
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_pad_set_active (enc->sinkpad, FALSE);
      gst_pad_set_active (enc->srcpad, FALSE);
      break;
    default:
      break;
  }

  return result;
}

static gboolean
gst_base_spdif_enc_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseSpdifEnc *enc = GST_BASE_SPDIF_ENC (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      GST_PAD_STREAM_LOCK (pad);
      gst_base_spdif_enc_drain (enc);
      GST_PAD_STREAM_UNLOCK (pad);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
  gst_object_unref (enc);

  return res;
}

static gboolean
gst_base_spdif_enc_setcaps (GstPad * pad, GstCaps * caps)
{
  return TRUE;
}

/* dummy func. subclass should override this */
static gboolean
gst_base_spdif_enc_frame_info (GstBaseSpdifEnc * encoder, GstBuffer * buffer)
{
  encoder->pkt_offset = 0;
  return FALSE;
}

static GstFlowReturn
gst_base_spdif_enc_output_frame (GstBaseSpdifEnc * enc)
{
  GstFlowReturn ret;
  GstBuffer *outbuf;
  GstClockTime timestamp;
  GstCaps *outcaps;

  GST_LOG_OBJECT (enc, "outputting an IEC958 frame.");
  g_return_val_if_fail (gst_adapter_available (enc->adapter) >=
      IEC958_FRAMESIZE, GST_FLOW_ERROR);

  timestamp = gst_adapter_prev_timestamp (enc->adapter, NULL);
  outbuf = gst_adapter_take_buffer (enc->adapter, IEC958_FRAMESIZE);
  if (outbuf == NULL) {
    GST_WARNING_OBJECT (enc, "failed to alloc buffer.");
    return GST_FLOW_ERROR;
  }
  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) =
      gst_util_uint64_scale_int (IEC958_FRAMESIZE, GST_SECOND,
      ALSASPDIFSINK_BYTES_PER_FRAME * enc->framerate);

  outcaps = gst_pad_get_allowed_caps (enc->srcpad);
  if (!GST_IS_CAPS (outcaps)) {
    GST_LOG_OBJECT (enc, "srcpad not negotiated.");
    gst_buffer_unref (outbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
  outcaps = gst_caps_make_writable (outcaps);
  gst_caps_set_simple (outcaps, "rate", G_TYPE_INT, enc->framerate, NULL);
  gst_buffer_set_caps (outbuf, outcaps);
  gst_caps_unref (outcaps);

  ret = gst_pad_push (enc->srcpad, outbuf);
  if (ret != GST_FLOW_OK)
    GST_DEBUG_OBJECT (enc, "failed to push out buffer, ret:%d.", ret);

  return ret;
}

static GstFlowReturn
gst_base_spdif_enc_drain (GstBaseSpdifEnc * enc)
{
  GstFlowReturn ret;
  GstBuffer *tbuf;
  guint len;

  len = gst_adapter_available (enc->adapter);
  while (len >= IEC958_FRAMESIZE) {
    ret = gst_base_spdif_enc_output_frame (enc);
    if (ret != GST_FLOW_OK)
      return ret;
    len -= IEC958_FRAMESIZE;
  }

  if (len == 0)
    return GST_FLOW_OK;

  // append a 0-filling buffer,
  //as alsaspdifsink accepts only the buffer of size:IEC958_FRAMESIZE.
  tbuf = gst_buffer_new_and_alloc (IEC958_FRAMESIZE - len);
  if (!tbuf) {
    GST_WARNING_OBJECT (enc, "failed to alloc buffer.");
    return GST_FLOW_ERROR;
  }
  memset (GST_BUFFER_DATA (tbuf), 0, GST_BUFFER_SIZE (tbuf));
  GST_LOG_OBJECT (enc, "0-filled %d bytes.", IEC958_FRAMESIZE - len);
  gst_adapter_push (enc->adapter, tbuf);
  ret = gst_base_spdif_enc_output_frame (enc);

  return ret;
}

static GstFlowReturn
gst_base_spdif_enc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstElement *elem;
  GstBaseSpdifEnc *enc;
  GstBaseSpdifEncClass *klass;
  GstBuffer *tbuf;
  GstFlowReturn ret;
  guint len;

  elem = gst_pad_get_parent_element (pad);
  enc = GST_BASE_SPDIF_ENC (elem);
  klass = GST_BASE_SPDIF_ENC_GET_CLASS (enc);

  ret = GST_FLOW_OK;
  enc->use_preamble = TRUE;     // set the default
  enc->extra_bswap = FALSE;
  enc->framerate = 1;

  GST_LOG_OBJECT (enc, "pushing %d bytes", GST_BUFFER_SIZE (buffer));
  /* get frame-header info */
  if (!klass->parse_frame_info || !klass->parse_frame_info (enc, buffer)) {
    GST_DEBUG_OBJECT (enc, "failed to parse the incoming buffer.");
    gst_buffer_unref (buffer);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }

  /* handle discont. */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    GST_LOG_OBJECT (enc, "recevied a incoming buffer with discont flag.");
    ret = gst_base_spdif_enc_drain (enc);
    gst_adapter_clear (enc->adapter);   // just for safety.
    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buffer);
      goto done;
    }
  }

  /* push the incoming buffer to enc->adapter,
   * but taking into account the byte-swapping & 
   *     the alignment of the last byte at word boundary.
   */
  len = GST_BUFFER_SIZE (buffer);
  /* FIXME:  like in the original ffmpeg source, should it be
   *  if (G_BYTE_ORDER == G_BIG_ENDIAN) ^ enc->extra_bswap)  ?
   */
  if (!enc->extra_bswap) {
    // no byte-swapping needed

    // push the header fisrt.
    if (enc->use_preamble) {
      tbuf = gst_buffer_new_and_alloc (BURST_HEADER_SIZE);
      if (tbuf == NULL)
        goto failed_alloc;
      memcpy (GST_BUFFER_DATA (tbuf), enc->header, BURST_HEADER_SIZE);
      gst_buffer_copy_metadata (tbuf, buffer, GST_BUFFER_COPY_TIMESTAMPS);
      GST_BUFFER_TIMESTAMP (buffer) = GST_CLOCK_TIME_NONE;
      gst_adapter_push (enc->adapter, tbuf);
    }

    /* payload */
    if (!(len & 0x1))
      gst_adapter_push (enc->adapter, buffer);
    else {
      guint8 *p;

      // last byte needs alignment.
      tbuf = gst_buffer_create_sub (buffer, 0, GST_ROUND_DOWN_2 (len));
      if (tbuf == NULL)
        goto failed_alloc;
      gst_adapter_push (enc->adapter, tbuf);

      tbuf = gst_buffer_new_and_alloc (2);
      if (tbuf == NULL)
        goto failed_alloc;
      p = GST_BUFFER_DATA (tbuf);
      p[0] = 0;
      p[1] = GST_BUFFER_DATA (buffer)[len - 1];
      gst_adapter_push (enc->adapter, tbuf);
      gst_buffer_unref (buffer);
    }

    // padding
    if (enc->use_preamble)
      len += BURST_HEADER_SIZE;
    tbuf = gst_buffer_new_and_alloc (enc->pkt_offset - GST_ROUND_UP_2 (len));
    if (tbuf == NULL)
      goto failed_alloc;
    memset (GST_BUFFER_DATA (tbuf), 0, GST_BUFFER_SIZE (tbuf));
    gst_adapter_push (enc->adapter, tbuf);

  } else {
    guint l, i;
    guint8 *p, *q;
    guint16 *dst, *src;

    // byte-swapping required. prepare the whole-size buffer.
    tbuf = gst_buffer_new_and_alloc (enc->pkt_offset);
    if (tbuf == NULL)
      goto failed_alloc;

    p = GST_BUFFER_DATA (tbuf);
    // not byte-swap the header (?)
    if (enc->use_preamble) {
      memcpy (p, enc->header, BURST_HEADER_SIZE);
      p += BURST_HEADER_SIZE;
    }

    /* swap paylod, excluding the last 1or2 bytes */
    q = GST_BUFFER_DATA (buffer);
    l = GST_ROUND_UP_2 (len);
    dst = (guint16 *) p;
    src = (guint16 *) q;
    for (i = 0; i < (l >> 1); i++, src++, dst++)
      *dst = GUINT16_SWAP_LE_BE (*src);

    // process the last 1 or 2 bytes
    p = (guint8 *) dst;
    q = (guint8 *) src;
    if (len & 0x1) {
      p[0] = 0;
      p[1] = q[0];
      p += 2;
    }

    memset (p, 0, enc->pkt_offset - (p - GST_BUFFER_DATA (tbuf)));
    gst_adapter_push (enc->adapter, tbuf);
    gst_buffer_unref (buffer);
  }

  // check the available amount of adapter, and push.
  if (gst_adapter_available (enc->adapter) >= IEC958_FRAMESIZE)
    ret = gst_base_spdif_enc_output_frame (enc);

done:
  gst_object_unref (GST_OBJECT_CAST (elem));
  return ret;

failed_alloc:
  GST_INFO_OBJECT (enc, "failed to alloc buffer.");
  gst_object_unref (GST_OBJECT_CAST (elem));
  return GST_FLOW_ERROR;
}

/**
 * gst_base_spdif_enc_class_add_pad_templates:
 * @klass: an #GstBaseSpdifEncClass
 * @allowed_sink_caps: what formats the filter can handle, as #GstCaps
 *
 * Convenience function to add pad templates to this element class, with
 * @allowed_sink_caps as the sink caps that can be handled.
 *
 * This function is usually used from within a subclass's base_init function.
 */
void
gst_base_spdif_enc_class_add_pad_templates (GstBaseSpdifEncClass * klass,
    GstCaps * sink_caps)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *sink_pad_template;
  GstPadTemplate *src_pad_template;

  g_return_if_fail (GST_IS_BASE_SPDIF_ENC_CLASS (klass));
  g_return_if_fail (GST_IS_CAPS (sink_caps));

  sink_pad_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, sink_caps);
  g_return_if_fail (sink_pad_template != NULL);
  gst_element_class_add_pad_template (element_class, sink_pad_template);

  src_pad_template =
      gst_static_pad_template_get (&gst_base_spdif_enc_src_template);
  g_return_if_fail (src_pad_template != NULL);
  gst_element_class_add_pad_template (element_class, src_pad_template);
}
