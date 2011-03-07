/* GStreamer 
 * Copyright (C) 2011 Akihiro TSUKADA <tskd2 AT yahoo.co.jp>
 *
 * gstaacspdifenc.h: IEC61937 encapsulators of AAC ADTS,
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

#ifndef __GST_AAC_SPDIF_ENC_H__
#define __GST_AAC_SPDIF_ENC_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstbasespdifenc.h"

G_BEGIN_DECLS

#define GST_TYPE_AAC_SPDIF_ENC    (gst_aac_spdif_enc_get_type())

#define GST_AAC_SPDIF_ENC(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AAC_SPDIF_ENC, \
     GstAacSpdifEnc))

#define GST_AAC_SPDIF_ENC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AAC_SPDIF_ENC, \
     GstAacSpdifEncClass))

#define GST_AAC_SPDIF_ENC_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_AAC_SPDIF_ENC, \
     GstAacSpdifEncClass))

#define GST_IS_AAC_SPDIF_ENC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AAC_SPDIF_ENC))

#define GST_IS_AAC_SPDIF_ENC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AAC_SPDIF_ENC))

#define GST_AAC_SPDIF_ENC_CAST(obj)    ((GstAacSpdifEnc *)(obj))


typedef struct _GstAacSpdifEnc GstAacSpdifEnc;
typedef struct _GstAacSpdifEncClass GstAacSpdifEncClass;


struct _GstAacSpdifEnc {
  GstBaseSpdifEnc  parent;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};


struct _GstAacSpdifEncClass {
  GstBaseSpdifEncClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_aac_spdif_enc_get_type (void);

G_END_DECLS

#endif /* __GST_AAC_SPDIF_ENC_H__ */
