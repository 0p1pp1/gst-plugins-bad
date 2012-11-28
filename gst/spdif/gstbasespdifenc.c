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

#define do_init G_STMT_START { \
    GST_DEBUG_CATEGORY_INIT (basespdif_enc_dbg, "basespdif_enc", 0, \
                             "basespdif_enc"); \
} G_STMT_END

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstBaseSpdifEnc, gst_base_spdif_enc,
    GST_TYPE_BASE_TRANSFORM, do_init);


static GstCaps *gst_base_spdif_enc_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_base_spdif_enc_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, guint size,
    GstCaps * othercaps, guint * othersize);
static void gst_base_spdif_enc_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer);
static GstFlowReturn gst_base_spdif_enc_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static gboolean gst_base_spdif_enc_frame_info (GstBaseSpdifEnc * enc,
    GstBuffer * buffer);

static gboolean
gst_base_spdif_enc_accept_caps (GstBaseTransform * trans,
    GstPadDirection dir, GstCaps * caps)
{
  return TRUE;
}

static GstStaticPadTemplate gst_base_spdif_enc_src_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_TRANSFORM_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-iec958")
    );

static void
gst_base_spdif_enc_class_init (GstBaseSpdifEncClass * klass)
{
  GstBaseTransformClass *basetrans_class = (GstBaseTransformClass *) klass;


  /* overrides for GstBaseTransform */
  basetrans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_base_spdif_enc_transform_caps);
  basetrans_class->transform_size =
      GST_DEBUG_FUNCPTR (gst_base_spdif_enc_transform_size);
  basetrans_class->transform = GST_DEBUG_FUNCPTR (gst_base_spdif_enc_transform);
  basetrans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_base_spdif_enc_before_transform);
  basetrans_class->accept_caps =
      GST_DEBUG_FUNCPTR (gst_base_spdif_enc_accept_caps);

  /* Default handlers */
  klass->parse_frame_info = GST_DEBUG_FUNCPTR (gst_base_spdif_enc_frame_info);

}

static void
gst_base_spdif_enc_init (GstBaseSpdifEnc * self)
{
  GST_WRITE_UINT16_BE (&self->header[0], SYNCWORD1);
  GST_WRITE_UINT16_BE (&self->header[2], SYNCWORD2);
}

static GstCaps *
gst_base_spdif_enc_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstElementClass *klass;
  GstPadTemplate *template;

  if (caps == NULL)
    return NULL;

  if (direction == GST_PAD_SINK)
    return gst_static_pad_template_get_caps (&gst_base_spdif_enc_src_template);

  klass = GST_ELEMENT_CLASS (GST_BASE_TRANSFORM_GET_CLASS (trans));
  template = gst_element_class_get_pad_template (klass,
      GST_BASE_TRANSFORM_SINK_NAME);
  return gst_pad_template_get_caps (template);
}

/**
 * usually unsed. originally used from prepare_output_buffer(), 
 * but overrides here for safety.
 * subclass can override this func, for calculating src_caps size
 */
static gboolean
gst_base_spdif_enc_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps,
    guint size, GstCaps * othercaps, guint * othersize)
{
  GstBaseSpdifEnc *enc = GST_BASE_SPDIF_ENC_CAST (trans);

  *othersize = 0;

  if (direction == GST_PAD_SRC) {
    /* src_pad & src_caps -> sink_caps size */
    *othersize = size;
  } else {
    /* sink_pad & sink_caps -> src_caps size */
    if (enc->pkt_offset)
      *othersize = enc->pkt_offset;
  }

  return (*othersize != 0);
}

/* dummy func. subclass should override this */
static gboolean
gst_base_spdif_enc_frame_info (GstBaseSpdifEnc * encoder, GstBuffer * buffer)
{
  encoder->pkt_offset = 0;
  return FALSE;
}

static void
gst_base_spdif_enc_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer)
{
  GstBaseSpdifEnc *enc = GST_BASE_SPDIF_ENC_CAST (trans);
  GstBaseSpdifEncClass *klass = GST_BASE_SPDIF_ENC_GET_CLASS (enc);

  enc->use_preamble = TRUE;     // set the default
  enc->extra_bswap = FALSE;
  if (klass->parse_frame_info)
    enc->last_parse_ret = klass->parse_frame_info (enc, buffer);
}

static GstFlowReturn
gst_base_spdif_enc_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstBaseSpdifEnc *enc = GST_BASE_SPDIF_ENC_CAST (trans);
  guint data_len, padding_len, cp_len;
  guint8 *p;
  GstCaps *out_caps;
  GValue rate = { 0 };

  if (!enc->last_parse_ret || enc->pkt_offset == 0) {
    GST_DEBUG_OBJECT (enc, "failed to parse frame info for %p", inbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  data_len = GST_ROUND_UP_2 (GST_BUFFER_SIZE (inbuf));
  if (enc->use_preamble)
    data_len += BURST_HEADER_SIZE;

  if (GST_BUFFER_SIZE (outbuf) < enc->pkt_offset || data_len > enc->pkt_offset) {
    GST_DEBUG_OBJECT (enc, "too much in-data:%d for repetition_bytes:%d",
        data_len, enc->pkt_offset);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  out_caps = gst_buffer_get_caps (outbuf);
  if (out_caps == NULL) {
    GST_DEBUG_OBJECT (enc, "failed to get the caps for %p", outbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
  g_value_init (&rate, G_TYPE_INT);
  g_value_set_int (&rate, enc->framerate);
  gst_caps_set_value (out_caps, "rate", &rate);
  gst_caps_unref (out_caps);

  p = GST_BUFFER_DATA (outbuf);
  if (enc->use_preamble) {
    memcpy (p, enc->header, BURST_HEADER_SIZE);
    p += BURST_HEADER_SIZE;
  }

  cp_len = GST_ROUND_DOWN_2 (GST_BUFFER_SIZE (inbuf));
  if ((G_BYTE_ORDER == G_BIG_ENDIAN) ^ enc->extra_bswap)
    memcpy (p, GST_BUFFER_DATA (inbuf), cp_len);
  else {
    gint i;
    guint16 *src, *dst;

    GST_LOG_OBJECT (enc, "swapped copy to outbuf");
    dst = (guint16 *) p;
    src = (guint16 *) GST_BUFFER_DATA (inbuf);
    for (i = 0; i < cp_len >> 1; i++, src++, dst++)
      *dst = GUINT16_SWAP_LE_BE (*src);
  }
  p += cp_len;
  if (cp_len != data_len) {     // insize is odd. pack to BE16
    GST_WRITE_UINT16_BE (p, GST_BUFFER_DATA (inbuf)[cp_len]);
    p += 2;
  }
  padding_len = enc->pkt_offset - data_len;
  if (padding_len > 0)
    memset (p, 0, padding_len);
  /* meta data (incl. TIMESTAMP, DURATION) are copied from inbuf by default */

  return GST_FLOW_OK;
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

  g_return_if_fail (GST_IS_BASE_SPDIF_ENC_CLASS (klass));
  g_return_if_fail (GST_IS_CAPS (sink_caps));

  sink_pad_template = gst_pad_template_new (GST_BASE_TRANSFORM_SINK_NAME,
      GST_PAD_SINK, GST_PAD_ALWAYS, sink_caps);
  gst_element_class_add_pad_template (element_class, sink_pad_template);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_base_spdif_enc_src_template));
}
