/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include <math.h>

#ifdef HAVE_OSS_INCLUDE_IN_SYS
#include <sys/soundcard.h>
#else

#ifdef HAVE_OSS_INCLUDE_IN_ROOT
#include <soundcard.h>
#else

#include <machine/soundcard.h>

#endif /* HAVE_OSS_INCLUDE_IN_ROOT */

#endif /* HAVE_OSS_INCLUDE_IN_SYS */


/*#define DEBUG_ENABLED */
#include "gst_arts.h"
#include "gst_artsio_impl.h"


static GstStaticPadTemplate sink_temp = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "depth = (int) 16, "
        "width = (int) 16, "
        "signed = (boolean) true, "
        "channels = (int) 2, "
        "rate = (int) 44100, " "endianness = (int) byte_order")
    );

static GstStaticPadTemplate src_temp = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "depth = (int) 16, "
        "width = (int) 16, "
        "signed = (boolean) true, "
        "channels = (int) 2, "
        "rate = (int) 44100, " "endianness = (int) byte_order")
    );

enum
{
  ARG_0,
  ARG_LAST
};

static void gst_arts_base_init (gpointer g_class);
static void gst_arts_class_init (GstARTSClass * klass);
static void gst_arts_init (GstARTS * arts);

static void gst_arts_loop (GstElement * element);


static GstElementClass *parent_class = NULL;

/*static guint gst_arts_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_arts_get_type (void)
{
  static GType gst_arts_type = 0;

  if (!gst_arts_type) {
    static const GTypeInfo gst_arts_info = {
      sizeof (GstARTSClass),
      gst_arts_base_init,
      NULL,
      (GClassInitFunc) gst_arts_class_init,
      NULL,
      NULL,
      sizeof (GstARTS),
      0,
      (GInstanceInitFunc) gst_arts_init,
    };

    gst_arts_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstArts", &gst_arts_info, 0);
  }
  return gst_arts_type;
}

static void
gst_arts_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_temp));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_temp));
  gst_element_class_set_details_simple (element_class, "aRts plugin",
      "Filter/Audio", "aRts wrapper filter",
      "Erik Walthinsen <omega@temple-baptist.com, "
      "Stefan Westerfeld <stefan@space.twc.de>");
}

static void
gst_arts_class_init (GstARTSClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
}

static void
gst_arts_init (GstARTS * arts)
{
  arts->sinkpad =
      gst_pad_new_from_template (gst_element_get_pad_template (GST_ELEMENT
          (arts), "sink"), "sink");
  gst_element_add_pad (GST_ELEMENT (arts), arts->sinkpad);

  arts->srcpad =
      gst_pad_new_from_template (gst_element_get_pad_template (GST_ELEMENT
          (arts), "src"), "src");
  gst_element_add_pad (GST_ELEMENT (arts), arts->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (arts), gst_arts_loop);

  arts->wrapper = gst_arts_wrapper_new (arts->sinkpad, arts->srcpad);
}

static void
gst_arts_loop (GstElement * element)
{
  GstARTS *arts = (GstARTS *) element;

  g_return_if_fail (arts != NULL);

  gst_arts_wrapper_do (arts->wrapper);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "artsfilter", GST_RANK_NONE,
          GST_TYPE_ARTS))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "arts",
    "arTs filter wrapper",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
