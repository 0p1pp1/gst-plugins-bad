/* GStreamer 
 * Copyright (C) 2011 Akihiro TSUKADA <tskd2 AT yahoo.co.jp>
 *
 * gstaacspdifsink.c: S/PDIF (IEC958) sink bin for AAC ADTS.
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

/** example usage: 
 *  gst-launch playbin2 uri=file:///foo.aac audio-sink=alsaspdifsink
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstaacspdifsink.h"

GST_DEBUG_CATEGORY_STATIC (aacspdif_sink_dbg);
#define GST_CAT_DEFAULT aacspdif_sink_dbg

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (aacspdif_sink_dbg, "aacspdif_sink", 0,
      "aacspdif_sink");
}

GST_BOILERPLATE_FULL (GstAacSpdifSink, gst_aac_spdif_sink, GstBin, GST_TYPE_BIN,
    _do_init);


static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, mpegversion = (int) { 2, 4 }"));


static void
gst_aac_spdif_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  /* FIXME: get pad templates from the children elements? */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details_simple (element_class, "AAC S/PDIF sink",
      "Sink/Audio/Bin", "Bin of aacparse, aac2spdif and alsasink",
      "Akihiro TSUKADA <tskd2@yahoo.co.jp>");
}

static void
gst_aac_spdif_sink_class_init (GstAacSpdifSinkClass * klass)
{
  /* nothing to do here */
}

static void
gst_aac_spdif_sink_init (GstAacSpdifSink * self, GstAacSpdifSinkClass * klass)
{
  GstPad *pad;

  self->aacparse = gst_element_factory_make ("aacparse", NULL);
  self->aac2spdif = gst_element_factory_make ("aac2spdif", NULL);
  self->sink = gst_element_factory_make ("alsasink", NULL);
  if (!self->aacparse || !self->aac2spdif || !self->sink)
    GST_ELEMENT_ERROR (self, LIBRARY, INIT, (NULL),
        ("failed to create internal component elements."));

  gst_bin_add_many (GST_BIN (self),
      self->aacparse, self->aac2spdif, self->sink, NULL);
  gst_element_link_many (self->aacparse, self->aac2spdif, self->sink, NULL);

  pad = gst_element_get_static_pad (self->aacparse, "sink");
  if (!pad)
    GST_ELEMENT_ERROR (self, CORE, PAD, (NULL),
        ("failed to get sink caps from aacparse"));
  gst_element_add_pad (GST_ELEMENT (self), gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT (pad));
}
