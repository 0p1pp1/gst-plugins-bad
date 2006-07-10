/* -*- c-basic-offset: 2 -*-
 * 
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 *               2006 Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>
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
 * 
 * 
 * this windowed sinc filter is taken from the freely downloadable DSP book,
 * "The Scientist and Engineer's Guide to Digital Signal Processing",
 * chapter 16
 * available at http://www.dspguide.com/
 *
 * FIXME:
 * - this filter is totally unoptimized !
 * - we do not destroy the allocated memory for filters and residue
 * - this might be improved upon with bytestream
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/controller/gstcontroller.h>

#include "gstlpwsinc.h"

#define GST_CAT_DEFAULT gst_lpwsinc_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails lpwsinc_details =
GST_ELEMENT_DETAILS ("Low-pass Windowed sinc filter",
    "Filter/Effect/Audio",
    "Low-pass Windowed sinc filter",
    "Thomas <thomas@apestaart.org>, "
    "Steven W. Smith, "
    "Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>");

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LENGTH,
  PROP_FREQUENCY
};

static GstStaticPadTemplate lpwsinc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, " "width = (int) 32")
    );

static GstStaticPadTemplate lpwsinc_src_template = GST_STATIC_PAD_TEMPLATE
    ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, " "width = (int) 32")
    );

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_lpwsinc_debug, "lpwsinc", 0, "Low-pass Windowed sinc filter plugin");

GST_BOILERPLATE_FULL (GstLPWSinc, gst_lpwsinc, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void lpwsinc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void lpwsinc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn lpwsinc_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);
static gboolean lpwsinc_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps);

/* Element class */

static void
gst_lpwsinc_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_lpwsinc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&lpwsinc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&lpwsinc_sink_template));
  gst_element_class_set_details (element_class, &lpwsinc_details);
}

static void
gst_lpwsinc_class_init (GstLPWSincClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = lpwsinc_set_property;
  gobject_class->get_property = lpwsinc_get_property;
  gobject_class->dispose = gst_lpwsinc_dispose;

  g_object_class_install_property (gobject_class, PROP_FREQUENCY,
      g_param_spec_double ("frequency", "Frequency",
          "Cut-off Frequency relative to sample rate",
          0.0, 0.5, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_LENGTH,
      g_param_spec_int ("length", "Length",
          "N such that the filter length = 2N + 1",
          1, G_MAXINT, 1, G_PARAM_READWRITE));

  trans_class->transform_ip = GST_DEBUG_FUNCPTR (lpwsinc_transform_ip);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (lpwsinc_set_caps);
}

static void
gst_lpwsinc_init (GstLPWSinc * this, GstLPWSincClass * g_class)
{
  this->wing_size = 50;
  this->frequency = 0.25;
  this->kernel = NULL;
}


/* GstBaseTransform vmethod implementations */

/* get notified of caps and plug in the correct process function */
static gboolean
lpwsinc_set_caps (GstBaseTransform * base, GstCaps * incaps, GstCaps * outcaps)
{
  int i = 0;
  double sum = 0.0;
  int len = 0;
  GstLPWSinc *this = GST_LPWSINC (base);

  GST_DEBUG_OBJECT (this,
      "set_caps: in %" GST_PTR_FORMAT " out %" GST_PTR_FORMAT, incaps, outcaps);

  /* FIXME: remember to free it */
  /* fill the kernel */
  g_print ("DEBUG: initing filter kernel\n");
  len = this->wing_size;
  GST_DEBUG ("lpwsinc: initializing filter kernel of length %d", len * 2 + 1);
  this->kernel = (double *) g_malloc (sizeof (double) * (2 * len + 1));

  for (i = 0; i <= len * 2; ++i) {
    if (i == len)
      this->kernel[i] = 2 * M_PI * this->frequency;
    else
      this->kernel[i] =
          sin (2 * M_PI * this->frequency * (i - len)) / (i - len);
    /* windowing */
    this->kernel[i] *= (0.54 - 0.46 * cos (M_PI * i / len));
  }

  /* normalize for unity gain at DC
   * FIXME: sure this is not supposed to be quadratic ? */
  for (i = 0; i <= len * 2; ++i)
    sum += this->kernel[i];
  for (i = 0; i <= len * 2; ++i)
    this->kernel[i] /= sum;

  /* set up the residue memory space */
  this->residue = (gfloat *) g_malloc (sizeof (gfloat) * (len * 2 + 1));
  for (i = 0; i <= len * 2; ++i)
    this->residue[i] = 0.0;

  return TRUE;
}

static GstFlowReturn
lpwsinc_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstLPWSinc *this = GST_LPWSINC (base);
  GstClockTime timestamp;

  /* don't process data in passthrough-mode */
  if (gst_base_transform_is_passthrough (base))
    return GST_FLOW_OK;

  /* FIXME: subdivide GST_BUFFER_SIZE into small chunks for smooth fades */
  timestamp = GST_BUFFER_TIMESTAMP (outbuf);

  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (this), timestamp);

  gfloat *src;
  gfloat *input;
  int residue_samples;
  gint input_samples;
  gint total_samples;
  int i, j;

  /* FIXME: out of laziness, we copy the left-over bit from last buffer
   * together with the incoming buffer to a new buffer to make the loop
   * easy; this could be a lot more optimized though
   * to make amends we keep the incoming buffer around and write our
   * output samples there */

  src = (gfloat *) GST_BUFFER_DATA (outbuf);
  residue_samples = this->wing_size * 2 + 1;
  input_samples = GST_BUFFER_SIZE (outbuf) / sizeof (gfloat);
  total_samples = residue_samples + input_samples;

  input = (gfloat *) g_malloc (sizeof (gfloat) * total_samples);

  /* copy the left-over bit */
  memcpy (input, this->residue, sizeof (gfloat) * residue_samples);

  /* copy the new buffer */
  memcpy (&input[residue_samples], src, sizeof (gfloat) * input_samples);
  /* copy the tail of the current input buffer to the residue */
  memcpy (this->residue, &src[input_samples - residue_samples],
      sizeof (gfloat) * residue_samples);

  /* convolution */
  /* since we copied the previous set of samples we needed before the actual
   * input data, we need to add the filter length to our indices for input */
  for (i = 0; i < input_samples; ++i) {
    src[i] = 0.0;
    for (j = 0; j < residue_samples; ++j)
      src[i] += input[i - j + residue_samples] * this->kernel[j];
  }

  g_free (input);

  return GST_FLOW_OK;
}

static void
lpwsinc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstLPWSinc *this = GST_LPWSINC (object);

  g_return_if_fail (GST_IS_LPWSINC (this));

  switch (prop_id) {
    case PROP_LENGTH:
      this->wing_size = g_value_get_int (value);
      break;
    case PROP_FREQUENCY:
      this->frequency = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
lpwsinc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstLPWSinc *this = GST_LPWSINC (object);

  switch (prop_id) {
    case PROP_LENGTH:
      g_value_set_int (value, this->wing_size);
      break;
    case PROP_FREQUENCY:
      g_value_set_double (value, this->frequency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
