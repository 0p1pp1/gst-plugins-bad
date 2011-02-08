/* GStreamer 
 * Copyright (C) 2011 0p1pp1
 *
 * gstbasespdifenc.h: Base class for IEC61937 encoders/encapsulators,
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

#ifndef __GST_BASE_SPDIF_ENC_H__
#define __GST_BASE_SPDIF_ENC_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_SPDIF_ENC    (gst_base_spdif_enc_get_type())

#define GST_BASE_SPDIF_ENC(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_BASE_SPDIF_ENC, \
     GstBaseSpdifEnc))

#define GST_BASE_SPDIF_ENC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_BASE_SPDIF_ENC, \
     GstBaseSpdifEncClass))

#define GST_BASE_SPDIF_ENC_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_BASE_SPDIF_ENC, \
     GstBaseSpdifEncClass))

#define GST_IS_BASE_SPDIF_ENC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_BASE_SPDIF_ENC))

#define GST_IS_BASE_SPDIF_ENC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_BASE_SPDIF_ENC))

#define GST_BASE_SPDIF_ENC_CAST(obj)    ((GstBaseSpdifEnc *)(obj))


typedef struct _GstBaseSpdifEnc GstBaseSpdifEnc;
typedef struct _GstBaseSpdifEncClass GstBaseSpdifEncClass;

/**
 * Code definitions for iec61937 burst data.
 *   copied and modifed from ffmpeg/libavformat/spdif.h
 */

/*
 * Terminology used in specification:
 * data-burst - IEC61937 frame, contains header and encapsuled frame
 * burst-preambule - IEC61937 frame header, contains 16-bits words named Pa, Pb, Pc and Pd
 * burst-payload - encapsuled frame
 * Pa, Pb - syncword - 0xF872, 0x4E1F
 * Pc - burst-info, contains data-type (bits 0-6), error flag (bit 7), data-type-dependent info (bits 8-12)
 *      and bitstream number (bits 13-15)
 * data-type - determines type of encapsuled frames
 * Pd - length code (number of bits or bytes of encapsuled frame - according to data_type)
 *
 * IEC 61937 frames at normal usage start every specific count of bytes,
 *      dependent from data-type (spaces between packets are filled by zeros)
 */

#define SYNCWORD1 0xF872
#define SYNCWORD2 0x4E1F
#define BURST_HEADER_SIZE 0x8

enum IEC61937DataType {
    IEC61937_AC3                = 0x01,          ///< AC-3 data
    IEC61937_MPEG1_LAYER1       = 0x04,          ///< MPEG-1 layer 1
    IEC61937_MPEG1_LAYER23      = 0x05,          ///< MPEG-1 layer 2 or 3 data or MPEG-2 without extension
    IEC61937_MPEG2_EXT          = 0x06,          ///< MPEG-2 data with extension
    IEC61937_MPEG2_AAC          = 0x07,          ///< MPEG-2 AAC ADTS
    IEC61937_MPEG2_LAYER1_LSF   = 0x08,          ///< MPEG-2, layer-1 low sampling frequency
    IEC61937_MPEG2_LAYER2_LSF   = 0x09,          ///< MPEG-2, layer-2 low sampling frequency
    IEC61937_MPEG2_LAYER3_LSF   = 0x0A,          ///< MPEG-2, layer-3 low sampling frequency
    IEC61937_DTS1               = 0x0B,          ///< DTS type I   (512 samples)
    IEC61937_DTS2               = 0x0C,          ///< DTS type II  (1024 samples)
    IEC61937_DTS3               = 0x0D,          ///< DTS type III (2048 samples)
    IEC61937_ATRAC              = 0x0E,          ///< Atrac data
    IEC61937_ATRAC3             = 0x0F,          ///< Atrac 3 data
    IEC61937_ATRACX             = 0x10,          ///< Atrac 3 plus data
    IEC61937_DTSHD              = 0x11,          ///< DTS HD data
    IEC61937_WMAPRO             = 0x12,          ///< WMA 9 Professional data
    IEC61937_MPEG2_AAC_LSF_2048 = 0x13,          ///< MPEG-2 AAC ADTS half-rate low sampling frequency
    IEC61937_MPEG2_AAC_LSF_4096 = 0x13 | 0x20,   ///< MPEG-2 AAC ADTS quarter-rate low sampling frequency
    IEC61937_EAC3               = 0x15,          ///< E-AC-3 data
    IEC61937_TRUEHD             = 0x16,          ///< TrueHD data
};

/**
 * GstBaseSpdifEnc:
 * @transform: the parent GstBaseTransform element.
 *
 * The opaque #GstBaseSpdifEnc data structure.
 */
struct _GstBaseSpdifEnc {
  GstElement  Element;

  guint    pkt_offset;      ///< data burst repetition period in bytes
  gint     framerate;
  /* optional. default:TRUE */
  gboolean use_preamble;    ///< preamble enabled (disabled for exactly pre-padded DTS)
  /* optional. default:FALSE */
  gboolean extra_bswap;     ///< extra bswap for payload (for LE DTS => standard BE DTS)
  guint8   header[BURST_HEADER_SIZE];

  GstPad   *sinkpad;
  GstPad   *srcpad;

  /*< private >*/
  GstAdapter *adapter;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstBaseSpdifEncClass:
 *
 * @parse_frame_info:  Called from the beginning of chain() ,
 *                     stores the return status & IEC61937 parameters like
 *                     pkt_offset, header.
 *
 * subclass must override  @parse_frame_info.
 */
struct _GstBaseSpdifEncClass {
  GstElementClass parent_class;

  /*< public >*/
  /* virtual methods for subclasses */

  gboolean (*parse_frame_info)    (GstBaseSpdifEnc *encoder,
                                       GstBuffer *buffer);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_base_spdif_enc_get_type (void);

/**
 * subclass can call this func. in base_init(),
 *  to install the default pad_templates.
 */
extern void gst_base_spdif_enc_class_add_pad_templates (
    GstBaseSpdifEncClass * klass, GstCaps * sink_caps);

G_END_DECLS

#endif /* __GST_BASE_SPDIF_ENC_H__ */
