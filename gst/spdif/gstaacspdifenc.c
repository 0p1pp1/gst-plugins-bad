/* GStreamer 
 * Copyright (C) 2011 0p1pp1
 *
 * gstaacspdifenc.c: IEC61937 encapsulators of AAC ADTS,
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

/** example esage: 
 *  gst-launch filesrc location=foo.aac ! aacparse ! aac2spdif ! alsasink
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>
#include "gstaacspdifenc.h"

GST_DEBUG_CATEGORY_STATIC (aacspdif_enc_dbg);
#define GST_CAT_DEFAULT aacspdif_enc_dbg

static void
do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (aacspdif_enc_dbg, "aacspdif_enc", 0, "aacspdif_enc");
}

GST_BOILERPLATE_FULL (GstAacSpdifEnc, gst_aac_spdif_enc, GstBaseSpdifEnc,
    GST_TYPE_BASE_SPDIF_ENC, do_init);

#define ADTS_SAMPLES_PER_FRAME 1024
#define HDR_IDX_DATA_TYPE 4
#define HDR_IDX_LENGTH_CODE 6

static gboolean gst_aac_spdif_enc_frame_info (GstBaseSpdifEnc * base,
    GstBuffer * buffer);

/* TODO: add other object types, incl. LATM, ADIF?, RAW?,... */
static GstStaticCaps AAC_SINK_CAPS =
GST_STATIC_CAPS ("audio/mpeg, "
    "framed = (boolean) true,"
    "mpegversion = (int) { 2, 4 }, stream-format = (string) adts");

static void
gst_aac_spdif_enc_base_init (gpointer g_class)
{
  GstElementClass *elem_class = GST_ELEMENT_CLASS (g_class);
  GstBaseSpdifEncClass *klass = GST_BASE_SPDIF_ENC_CLASS (g_class);

  gst_element_class_set_details_simple (elem_class, "AAC to IEC958 filter",
      "Decoder/Filter/Audio",
      "Pads AAC ADTS frames into IEC61937 frames "
      "suitable for a raw S/PDIF interface", "0p1pp1");

  gst_base_spdif_enc_class_add_pad_templates (klass,
      gst_static_caps_get (&AAC_SINK_CAPS));
}

static void
gst_aac_spdif_enc_class_init (GstAacSpdifEncClass * klass)
{
  GstBaseSpdifEncClass *base_class = GST_BASE_SPDIF_ENC_CLASS (klass);

  base_class->parse_frame_info =
      GST_DEBUG_FUNCPTR (gst_aac_spdif_enc_frame_info);
}

static void
gst_aac_spdif_enc_init (GstAacSpdifEnc * self, GstAacSpdifEncClass * klass)
{
  /* nothing to do here */
}

static inline gint
gst_aac_spdif_enc_get_samplerate (guint sr_idx)
{
  static const guint aac_sample_rates[] = { 96000, 88200, 64000, 48000, 44100,
    32000, 24000, 22050, 16000, 12000, 11025, 8000
  };

  if (sr_idx < G_N_ELEMENTS (aac_sample_rates))
    return aac_sample_rates[sr_idx];
  GST_WARNING ("Invalid sample rate index %u", sr_idx);
  return 0;
}

static gboolean
gst_aac_spdif_enc_frame_info (GstBaseSpdifEnc * parent, GstBuffer * buffer)
{
  gboolean ret = FALSE;
  guint8 *p;
  GstCaps *caps;
  const GstStructure *s;
  const gchar *stype;
  guint length_code;

  p = GST_BUFFER_DATA (buffer);
  caps = gst_buffer_get_caps (buffer);

  g_return_val_if_fail (p != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  /* reset/initialize first. */
  parent->pkt_offset = 0;
  parent->framerate = 0;

  length_code = GST_ROUND_UP_2 (GST_BUFFER_SIZE (buffer));
  length_code <<= 3;

  s = gst_caps_get_structure (caps, 0);
  if (s == NULL || !gst_structure_has_field (s, "stream-format"))
    goto bailout;

  stype = gst_structure_get_string (s, "stream-format");
  if (!strncmp ("adts", stype, 4)) {
    guint num_raw_db;

    /* ADTS frame */
    if (p[0] != 0xff || (p[1] & 0xf6) != 0xf0)
      goto bailout;

    num_raw_db = (p[6] & 0x3) + 1;
    parent->pkt_offset = (num_raw_db * ADTS_SAMPLES_PER_FRAME) << 2;

    switch (num_raw_db) {
      case 1:
        GST_WRITE_UINT16_BE (&parent->header[HDR_IDX_DATA_TYPE],
            IEC61937_MPEG2_AAC);
        break;
      case 2:
        GST_WRITE_UINT16_BE (&parent->header[HDR_IDX_DATA_TYPE],
            IEC61937_MPEG2_AAC_LSF_2048);
        break;
      case 4:
        GST_WRITE_UINT16_BE (&parent->header[HDR_IDX_DATA_TYPE],
            IEC61937_MPEG2_AAC_LSF_4096);
        break;
      default:
        GST_DEBUG_OBJECT (GST_AAC_SPDIF_ENC (parent),
            "%i samples in AAC frame not supported",
            num_raw_db * ADTS_SAMPLES_PER_FRAME);
        parent->pkt_offset = 0;
        goto bailout;
    }

    GST_WRITE_UINT16_BE (&parent->header[HDR_IDX_LENGTH_CODE], length_code);

    if (!gst_structure_get_int (s, "rate", &parent->framerate))
      parent->framerate = gst_aac_spdif_enc_get_samplerate ((p[2] & 0x3c) >> 2);

    ret = TRUE;
  } else {
    GST_INFO_OBJECT (GST_AAC_SPDIF_ENC (parent),
        "caps:%" GST_PTR_FORMAT "doesnot include stream-format:adts.", caps);
  }

bailout:
  gst_caps_unref (caps);
  return ret;
}
