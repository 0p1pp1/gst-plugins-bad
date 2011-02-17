/* GStreamer 
 * Copyright (C) 2011 0p1pp1
 *
 * gstaacspdifsink.h: S/PDIF (IEC958) sink bin for AAC ADTS.
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

#ifndef __GST_AAC_SPDIF_SINK_H__
#define __GST_AAC_SPDIF_SINK_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gstbin.h>

G_BEGIN_DECLS

#define GST_TYPE_AAC_SPDIF_SINK    (gst_aac_spdif_sink_get_type())

#define GST_AAC_SPDIF_SINK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AAC_SPDIF_SINK, \
     GstAacSpdifSink))

#define GST_AAC_SPDIF_SINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AAC_SPDIF_SINK, \
     GstAacSpdifSinkClass))

#define GST_AAC_SPDIF_SINK_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_AAC_SPDIF_SINK, \
     GstAacSpdifSinkClass))

#define GST_IS_AAC_SPDIF_SINK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AAC_SPDIF_SINK))

#define GST_IS_AAC_SPDIF_SINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AAC_SPDIF_SINK))

#define GST_AAC_SPDIF_SINK_CAST(obj)    ((GstAacSpdifSink *)(obj))


typedef struct _GstAacSpdifSink GstAacSpdifSink;
typedef struct _GstAacSpdifSinkClass GstAacSpdifSinkClass;


struct _GstAacSpdifSink {
  GstBin parent;

  GstElement *aacparse;
  GstElement *aac2spdif;
  GstElement *sink;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};


struct _GstAacSpdifSinkClass {
  GstBinClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_aac_spdif_sink_get_type (void);

G_END_DECLS

#endif /* __GST_AAC_SPDIF_SINK_H__ */
