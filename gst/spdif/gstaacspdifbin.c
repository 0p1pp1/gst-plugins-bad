/* GStreamer 
 * Copyright (C) 2011 0p1pp1
 *
 * gstaacspdifbin.c: IEC61937 encapsulators of AAC ADTS,
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

#include <gst/gst.h>
#include "gstaacspdifbin.h"

GST_DEBUG_CATEGORY_STATIC (aacspdif_bin_dbg);
#define GST_CAT_DEFAULT aacspdif_bin_dbg

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (aacspdif_bin_dbg, "aacspdif_bin", 0, "aacspdif_bin");
}

GST_BOILERPLATE_FULL (GstAacSpdifBin, gst_aac_spdif_bin, GstBin, GST_TYPE_BIN,
    _do_init);


static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "framed = (boolean) false, " "mpegversion = (int) { 2, 4 };"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-iec958")
    );


static void
gst_aac_spdif_bin_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  /* FIXME: get pad templates from the children elements? */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details_simple (element_class, "AAC S/PDIF bin",
      "Decoder/Audio/Bin",
      "Bin aacparse and aac2spdif to fit in auto-pluggers", "0p1pp1");
}

static void
gst_aac_spdif_bin_class_init (GstAacSpdifBinClass * klass)
{
  /* nothing to do here */
}

static void
gst_aac_spdif_bin_init (GstAacSpdifBin * self, GstAacSpdifBinClass * klass)
{
  GstPad *pad;

  self->aacparse = gst_element_factory_make ("aacparse", NULL);
  self->aac2spdif = gst_element_factory_make ("aac2spdif", NULL);
  g_return_if_fail (self->aacparse && self->aac2spdif);

  gst_bin_add_many (GST_BIN (self), self->aacparse, self->aac2spdif, NULL);
  gst_element_link_many (self->aacparse, self->aac2spdif, NULL);

  pad = gst_element_get_static_pad (self->aacparse, "sink");
  g_return_if_fail (pad);
  gst_element_add_pad (GST_ELEMENT (self), gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT (pad));

  pad = gst_element_get_static_pad (self->aac2spdif, "src");
  g_return_if_fail (pad);
  gst_element_add_pad (GST_ELEMENT (self), gst_ghost_pad_new ("src", pad));
  gst_object_unref (GST_OBJECT (pad));
}
