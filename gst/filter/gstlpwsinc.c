/* -*- c-basic-offset: 2 -*-
 * 
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 *               2006 Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>
 *               2007 Sebastian Dröge <slomo@circular-chaos.org>
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
 * TODO:  - Implement the convolution in place, probably only makes sense
 *          when using FFT convolution as currently the convolution itself
 *          is probably the bottleneck
 *        - Maybe allow cascading the filter to get a better stopband attenuation.
 *          Can be done by convolving a filter kernel with itself
 *        - Drop the first kernel_length/2 samples and append the same number of
 *          samples on EOS as the first few samples are essentialy zero.
 */

/**
 * SECTION:element-lpwsinc
 * @short_description: Windows Sinc low pass and high pass filter
 *
 * <refsect2>
 * <para>
 * Attenuates all frequencies above the cutoff frequency (low-pass) or all frequencies below the
 * cutoff frequency (high-pass). The length parameter controls the rolloff, the window parameter
 * controls rolloff and stopband attenuation. The Hamming window provides a faster rolloff but a bit
 * worse stopband attenuation, the other way around for the Blackman window.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch audiotestsrc freq=1500 ! audioconvert ! lpwsinc mode=low-pass frequency=1000 length=501 ! audioconvert ! alsasink
 * gst-launch filesrc location="melo1.ogg" ! oggdemux ! vorbisdec ! audioconvert ! lpwsinc mode=high-pass frequency=15000 length=501 ! audioconvert ! alsasink
 * gst-launch audiotestsrc wave=white-noise ! audioconvert ! lpwsinc mode=low-pass frequency=1000 length=10001 window=blackman ! audioconvert ! alsasink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/controller/gstcontroller.h>

#include "gstlpwsinc.h"

#define GST_CAT_DEFAULT gst_lpwsinc_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails lpwsinc_details = GST_ELEMENT_DETAILS ("LPWSinc",
    "Filter/Effect/Audio",
    "Low-pass and High-pass Windowed sinc filter",
    "Thomas <thomas@apestaart.org>, "
    "Steven W. Smith, "
    "Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>, "
    "Sebastian Dröge <slomo@circular-chaos.org>");

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
  PROP_FREQUENCY,
  PROP_MODE,
  PROP_WINDOW
};

enum
{
  MODE_LOW_PASS = 0,
  MODE_HIGH_PASS
};

#define GST_TYPE_LPWSINC_MODE (gst_lpwsinc_mode_get_type ())
static GType
gst_lpwsinc_mode_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {MODE_LOW_PASS, "Low pass (default)",
          "low-pass"},
      {MODE_HIGH_PASS, "High pass",
          "high-pass"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstLPWSincMode", values);
  }
  return gtype;
}

enum
{
  WINDOW_HAMMING = 0,
  WINDOW_BLACKMAN
};

#define GST_TYPE_LPWSINC_WINDOW (gst_lpwsinc_window_get_type ())
static GType
gst_lpwsinc_window_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {WINDOW_HAMMING, "Hamming window (default)",
          "hamming"},
      {WINDOW_BLACKMAN, "Blackman window",
          "blackman"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstLPWSincWindow", values);
  }
  return gtype;
}

#define ALLOWED_CAPS \
    "audio/x-raw-float, "                                             \
    " width = (int) { 32, 64 }, "                                     \
    " endianness = (int) BYTE_ORDER, "                                \
    " rate = (int) [ 1, MAX ], "                                      \
    " channels = (int) [ 1, MAX ]"

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_lpwsinc_debug, "lpwsinc", 0, "Low-pass and High-pass Windowed sinc filter plugin");

GST_BOILERPLATE_FULL (GstLPWSinc, gst_lpwsinc, GstAudioFilter,
    GST_TYPE_AUDIO_FILTER, DEBUG_INIT);

static void lpwsinc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void lpwsinc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn lpwsinc_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean lpwsinc_get_unit_size (GstBaseTransform * base, GstCaps * caps,
    guint * size);
static gboolean lpwsinc_start (GstBaseTransform * base);
static gboolean lpwsinc_event (GstBaseTransform * base, GstEvent * event);
static gboolean lpwsinc_setup (GstAudioFilter * base,
    GstRingBufferSpec * format);

static gboolean lpwsinc_query (GstPad * pad, GstQuery * query);
static const GstQueryType *lpwsinc_query_type (GstPad * pad);

/* Element class */

static void
gst_lpwsinc_dispose (GObject * object)
{
  GstLPWSinc *self = GST_LPWSINC (object);

  if (self->residue) {
    g_free (self->residue);
    self->residue = NULL;
  }

  if (self->kernel) {
    g_free (self->kernel);
    self->kernel = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_lpwsinc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *caps;

  gst_element_class_set_details (element_class, &lpwsinc_details);

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (GST_AUDIO_FILTER_CLASS (g_class),
      caps);
  gst_caps_unref (caps);
}

static void
gst_lpwsinc_class_init (GstLPWSincClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;
  GstAudioFilterClass *filter_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;
  filter_class = (GstAudioFilterClass *) klass;

  gobject_class->set_property = lpwsinc_set_property;
  gobject_class->get_property = lpwsinc_get_property;
  gobject_class->dispose = gst_lpwsinc_dispose;

  g_object_class_install_property (gobject_class, PROP_FREQUENCY,
      g_param_spec_double ("frequency", "Frequency",
          "Cut-off Frequency (Hz)", 0.0, G_MAXDOUBLE, 0.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_LENGTH,
      g_param_spec_int ("length", "Length",
          "Filter kernel length, will be rounded to the next odd number",
          3, G_MAXINT, 101, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "Low pass or high pass mode", GST_TYPE_LPWSINC_MODE,
          MODE_LOW_PASS, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_WINDOW,
      g_param_spec_enum ("window", "Window",
          "Window function to use", GST_TYPE_LPWSINC_WINDOW,
          WINDOW_HAMMING, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  trans_class->transform = GST_DEBUG_FUNCPTR (lpwsinc_transform);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (lpwsinc_get_unit_size);
  trans_class->start = GST_DEBUG_FUNCPTR (lpwsinc_start);
  trans_class->event = GST_DEBUG_FUNCPTR (lpwsinc_event);
  filter_class->setup = GST_DEBUG_FUNCPTR (lpwsinc_setup);
}

static void
gst_lpwsinc_init (GstLPWSinc * self, GstLPWSincClass * g_class)
{
  self->mode = MODE_LOW_PASS;
  self->window = WINDOW_HAMMING;
  self->kernel_length = 101;
  self->latency = 50;
  self->frequency = 0.0;
  self->kernel = NULL;
  self->residue = NULL;

  self->have_kernel = FALSE;
  self->residue_length = 0;
  self->next_ts = GST_CLOCK_TIME_NONE;
  self->next_off = GST_BUFFER_OFFSET_NONE;

  gst_pad_set_query_function (GST_BASE_TRANSFORM (self)->srcpad, lpwsinc_query);
  gst_pad_set_query_type_function (GST_BASE_TRANSFORM (self)->srcpad,
      lpwsinc_query_type);
}

static void
process_32 (GstLPWSinc * self, gfloat * src, gfloat * dst, guint input_samples)
{
  gint kernel_length = self->kernel_length;
  gint i, j, k, l;
  gint channels = GST_AUDIO_FILTER (self)->format.channels;
  gint res_start;

  /* convolution */
  for (i = 0; i < input_samples; i++) {
    dst[i] = 0.0;
    k = i % channels;
    l = i / channels;
    for (j = 0; j < kernel_length; j++)
      if (l < j)
        dst[i] +=
            self->residue[(kernel_length + l - j) * channels +
            k] * self->kernel[j];
      else
        dst[i] += src[(l - j) * channels + k] * self->kernel[j];
  }

  /* copy the tail of the current input buffer to the residue, while
   * keeping parts of the residue if the input buffer is smaller than
   * the kernel length */
  if (input_samples < kernel_length * channels)
    res_start = kernel_length * channels - input_samples;
  else
    res_start = 0;

  for (i = 0; i < res_start; i++)
    self->residue[i] = self->residue[i + input_samples];
  for (i = res_start; i < kernel_length * channels; i++)
    self->residue[i] = src[input_samples - kernel_length * channels + i];

  self->residue_length += kernel_length * channels - res_start;
  if (self->residue_length > kernel_length * channels)
    self->residue_length = kernel_length * channels;
}

static void
process_64 (GstLPWSinc * self, gdouble * src, gdouble * dst,
    guint input_samples)
{
  gint kernel_length = self->kernel_length;
  gint i, j, k, l;
  gint channels = GST_AUDIO_FILTER (self)->format.channels;
  gint res_start;

  /* convolution */
  for (i = 0; i < input_samples; i++) {
    dst[i] = 0.0;
    k = i % channels;
    l = i / channels;
    for (j = 0; j < kernel_length; j++)
      if (l < j)
        dst[i] +=
            self->residue[(kernel_length + l - j) * channels +
            k] * self->kernel[j];
      else
        dst[i] += src[(l - j) * channels + k] * self->kernel[j];
  }

  /* copy the tail of the current input buffer to the residue, while
   * keeping parts of the residue if the input buffer is smaller than
   * the kernel length */
  if (input_samples < kernel_length * channels)
    res_start = kernel_length * channels - input_samples;
  else
    res_start = 0;

  for (i = 0; i < res_start; i++)
    self->residue[i] = self->residue[i + input_samples];
  for (i = res_start; i < kernel_length * channels; i++)
    self->residue[i] = src[input_samples - kernel_length * channels + i];

  self->residue_length += kernel_length * channels - res_start;
  if (self->residue_length > kernel_length * channels)
    self->residue_length = kernel_length * channels;
}

static void
lpwsinc_build_kernel (GstLPWSinc * self)
{
  gint i = 0;
  gdouble sum = 0.0;
  gint len = 0;
  gdouble w;

  len = self->kernel_length;

  if (GST_AUDIO_FILTER (self)->format.rate == 0) {
    GST_DEBUG ("rate not set yet");
    return;
  }

  if (GST_AUDIO_FILTER (self)->format.channels == 0) {
    GST_DEBUG ("channels not set yet");
    return;
  }

  /* Clamp cutoff frequency between 0 and the nyquist frequency */
  self->frequency =
      CLAMP (self->frequency, 0.0, GST_AUDIO_FILTER (self)->format.rate / 2);

  GST_DEBUG ("lpwsinc: initializing filter kernel of length %d "
      "with cutoff %.2lf Hz "
      "for mode %s",
      len, self->frequency,
      (self->mode == MODE_LOW_PASS) ? "low-pass" : "high-pass");

  /* fill the kernel */
  w = 2 * M_PI * (self->frequency / GST_AUDIO_FILTER (self)->format.rate);

  if (self->kernel)
    g_free (self->kernel);
  self->kernel = g_new (gdouble, len);

  for (i = 0; i < len; ++i) {
    if (i == len / 2)
      self->kernel[i] = w;
    else
      self->kernel[i] = sin (w * (i - len / 2)) / (i - len / 2);
    /* windowing */
    if (self->window == WINDOW_HAMMING)
      self->kernel[i] *= (0.54 - 0.46 * cos (2 * M_PI * i / len));
    else
      self->kernel[i] *=
          (0.42 - 0.5 * cos (2 * M_PI * i / len) +
          0.08 * cos (4 * M_PI * i / len));
  }

  /* normalize for unity gain at DC */
  for (i = 0; i < len; ++i)
    sum += self->kernel[i];
  for (i = 0; i < len; ++i)
    self->kernel[i] /= sum;

  /* convert to highpass if specified */
  if (self->mode == MODE_HIGH_PASS) {
    for (i = 0; i < len; ++i)
      self->kernel[i] = -self->kernel[i];
    self->kernel[len / 2] += 1.0;
  }

  /* set up the residue memory space */
  if (!self->residue)
    self->residue =
        g_new0 (gdouble, len * GST_AUDIO_FILTER (self)->format.channels);

  self->have_kernel = TRUE;
  self->residue_length = 0;
}

static void
lpwsinc_push_residue (GstLPWSinc * self)
{
  GstBuffer *outbuf;
  GstFlowReturn res;
  gint rate = GST_AUDIO_FILTER (self)->format.rate;
  gint channels = GST_AUDIO_FILTER (self)->format.channels;
  gint outsize, outsamples;
  gint diffsize, diffsamples;
  guint8 *in, *out;

  /* Calculate the number of samples and their memory size that
   * should be pushed from the residue */
  outsamples = MIN (self->latency, self->residue_length / channels);
  outsize = outsamples * channels * (GST_AUDIO_FILTER (self)->format.width / 8);
  if (outsize == 0)
    return;

  /* Process the difference between latency and residue_length samples
   * to start at the actual data instead of starting at the zeros before
   * when we only got one buffer smaller than latency */
  diffsamples = self->latency - self->residue_length / channels;
  diffsize =
      diffsamples * channels * (GST_AUDIO_FILTER (self)->format.width / 8);
  if (diffsize > 0) {
    in = g_new0 (guint8, diffsize);
    out = g_new0 (guint8, diffsize);
    self->process (self, in, out, diffsamples * channels);
    g_free (in);
    g_free (out);
  }

  res = gst_pad_alloc_buffer (GST_BASE_TRANSFORM (self)->srcpad,
      GST_BUFFER_OFFSET_NONE, outsize,
      GST_PAD_CAPS (GST_BASE_TRANSFORM (self)->srcpad), &outbuf);

  if (G_UNLIKELY (res != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (self, "failed allocating buffer of %d bytes", outsize);
    return;
  }

  /* Convolve the residue with zeros to get the actual remaining data */
  in = g_new0 (guint8, outsize);
  self->process (self, in, GST_BUFFER_DATA (outbuf), outsamples * channels);
  g_free (in);

  /* Set timestamp, offset, etc from the values we
   * saved when processing the regular buffers */
  if (GST_CLOCK_TIME_IS_VALID (self->next_ts))
    GST_BUFFER_TIMESTAMP (outbuf) = self->next_ts;
  else
    GST_BUFFER_TIMESTAMP (outbuf) = 0;
  GST_BUFFER_DURATION (outbuf) =
      gst_util_uint64_scale (outsamples, GST_SECOND, rate);
  self->next_ts += gst_util_uint64_scale (outsamples, GST_SECOND, rate);

  if (self->next_off != GST_BUFFER_OFFSET_NONE) {
    GST_BUFFER_OFFSET (outbuf) = self->next_off;
    GST_BUFFER_OFFSET_END (outbuf) = self->next_off + outsamples;
  }

  GST_DEBUG_OBJECT (self, "Pushing residue buffer of size %d with timestamp: %"
      GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT ", offset: %lld,"
      " offset_end: %lld, nsamples: %d", GST_BUFFER_SIZE (outbuf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)), GST_BUFFER_OFFSET (outbuf),
      GST_BUFFER_OFFSET_END (outbuf), outsamples);

  res = gst_pad_push (GST_BASE_TRANSFORM (self)->srcpad, outbuf);

  if (G_UNLIKELY (res != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (self, "failed to push residue");
  }

}

/* GstAudioFilter vmethod implementations */

/* get notified of caps and plug in the correct process function */
static gboolean
lpwsinc_setup (GstAudioFilter * base, GstRingBufferSpec * format)
{
  GstLPWSinc *self = GST_LPWSINC (base);

  gboolean ret = TRUE;

  if (format->width == 32)
    self->process = (GstLPWSincProcessFunc) process_32;
  else if (format->width == 64)
    self->process = (GstLPWSincProcessFunc) process_64;
  else
    ret = FALSE;

  self->have_kernel = FALSE;

  return TRUE;
}

/* GstBaseTransform vmethod implementations */

static gboolean
lpwsinc_get_unit_size (GstBaseTransform * base, GstCaps * caps, guint * size)
{
  gint width, channels;
  GstStructure *structure;
  gboolean ret;

  g_assert (size);

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "channels", &channels);

  *size = width * channels / 8;

  return ret;
}

static GstFlowReturn
lpwsinc_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstLPWSinc *self = GST_LPWSINC (base);
  GstClockTime timestamp;
  gint channels = GST_AUDIO_FILTER (self)->format.channels;
  gint rate = GST_AUDIO_FILTER (self)->format.rate;
  gint input_samples =
      GST_BUFFER_SIZE (outbuf) / (GST_AUDIO_FILTER (self)->format.width / 8);
  gint output_samples = input_samples;
  gint diff;

  /* don't process data in passthrough-mode */
  if (gst_base_transform_is_passthrough (base))
    return GST_FLOW_OK;

  /* FIXME: subdivide GST_BUFFER_SIZE into small chunks for smooth fades */
  timestamp = GST_BUFFER_TIMESTAMP (outbuf);
  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (self), timestamp);

  if (!self->have_kernel)
    lpwsinc_build_kernel (self);

  /* Reset the residue if already existing on discont buffers */
  if (GST_BUFFER_IS_DISCONT (inbuf)) {
    if (channels && self->residue)
      memset (self->residue, 0, channels *
          self->kernel_length * sizeof (gdouble));
    self->residue_length = 0;
    self->next_ts = GST_CLOCK_TIME_NONE;
    self->next_off = GST_BUFFER_OFFSET_NONE;
  }

  /* Calculate the number of samples we can push out now without outputting
   * kernel_length/2 zeros in the beginning */
  diff = (self->kernel_length / 2) * channels - self->residue_length;
  if (diff > 0)
    output_samples -= diff;

  self->process (self, GST_BUFFER_DATA (inbuf), GST_BUFFER_DATA (outbuf),
      input_samples);

  if (output_samples <= 0) {
    /* Drop buffer and save original timestamp/offset for later use */
    if (!GST_CLOCK_TIME_IS_VALID (self->next_ts)
        && GST_BUFFER_TIMESTAMP_IS_VALID (outbuf))
      self->next_ts = GST_BUFFER_TIMESTAMP (outbuf);
    if (self->next_off == GST_BUFFER_OFFSET_NONE
        && GST_BUFFER_OFFSET_IS_VALID (outbuf))
      self->next_off = GST_BUFFER_OFFSET (outbuf);
    return GST_BASE_TRANSFORM_FLOW_DROPPED;
  } else if (output_samples < input_samples) {
    /* First (probably partial) buffer after starting from
     * a clean residue. Use stored timestamp/offset here */
    if (GST_CLOCK_TIME_IS_VALID (self->next_ts))
      GST_BUFFER_TIMESTAMP (outbuf) = self->next_ts;

    if (self->next_off != GST_BUFFER_OFFSET_NONE) {
      GST_BUFFER_OFFSET (outbuf) = self->next_off;
      if (GST_BUFFER_OFFSET_END_IS_VALID (outbuf))
        GST_BUFFER_OFFSET_END (outbuf) =
            self->next_off + output_samples / channels;
    } else {
      /* We dropped no buffer, offset is valid, offset_end must be adjusted by diff */
      if (GST_BUFFER_OFFSET_END_IS_VALID (outbuf))
        GST_BUFFER_OFFSET_END (outbuf) -= diff / channels;
    }

    if (GST_BUFFER_DURATION_IS_VALID (outbuf))
      GST_BUFFER_DURATION (outbuf) -=
          gst_util_uint64_scale (diff, GST_SECOND, channels * rate);

    GST_BUFFER_DATA (outbuf) +=
        diff * (GST_AUDIO_FILTER (self)->format.width / 8);
    GST_BUFFER_SIZE (outbuf) -=
        diff * (GST_AUDIO_FILTER (self)->format.width / 8);
  } else {
    GstClockTime ts_latency =
        gst_util_uint64_scale (self->latency, GST_SECOND, rate);

    /* Normal buffer, adjust timestamp/offset/etc by latency */
    if (GST_BUFFER_TIMESTAMP (outbuf) < ts_latency) {
      GST_WARNING_OBJECT (self, "GST_BUFFER_TIMESTAMP (outbuf) < latency");
      GST_BUFFER_TIMESTAMP (outbuf) = 0;
    } else {
      GST_BUFFER_TIMESTAMP (outbuf) -= ts_latency;
    }

    if (GST_BUFFER_OFFSET_IS_VALID (outbuf)) {
      if (GST_BUFFER_OFFSET (outbuf) > self->latency) {
        GST_BUFFER_OFFSET (outbuf) -= self->latency;
      } else {
        GST_WARNING_OBJECT (self, "GST_BUFFER_OFFSET (outbuf) < latency");
        GST_BUFFER_OFFSET (outbuf) = 0;
      }
    }

    if (GST_BUFFER_OFFSET_END_IS_VALID (outbuf)) {
      if (GST_BUFFER_OFFSET_END (outbuf) > self->latency) {
        GST_BUFFER_OFFSET_END (outbuf) -= self->latency;
      } else {
        GST_WARNING_OBJECT (self, "GST_BUFFER_OFFSET_END (outbuf) < latency");
        GST_BUFFER_OFFSET_END (outbuf) = 0;
      }
    }
  }

  GST_DEBUG_OBJECT (self, "Pushing buffer of size %d with timestamp: %"
      GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT ", offset: %lld,"
      " offset_end: %lld, nsamples: %d", GST_BUFFER_SIZE (outbuf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)), GST_BUFFER_OFFSET (outbuf),
      GST_BUFFER_OFFSET_END (outbuf), output_samples / channels);

  self->next_ts = GST_BUFFER_TIMESTAMP (outbuf) + GST_BUFFER_DURATION (outbuf);
  self->next_off = GST_BUFFER_OFFSET_END (outbuf);

  return GST_FLOW_OK;
}

static gboolean
lpwsinc_start (GstBaseTransform * base)
{
  GstLPWSinc *self = GST_LPWSINC (base);
  gint channels = GST_AUDIO_FILTER (self)->format.channels;

  /* Reset the residue if already existing */
  if (channels && self->residue)
    memset (self->residue, 0, channels *
        self->kernel_length * sizeof (gdouble));

  self->residue_length = 0;
  self->next_ts = GST_CLOCK_TIME_NONE;
  self->next_off = GST_BUFFER_OFFSET_NONE;

  return TRUE;
}

static gboolean
lpwsinc_query (GstPad * pad, GstQuery * query)
{
  GstLPWSinc *self = GST_LPWSINC (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min, max;
      gboolean live;
      guint64 latency;
      GstPad *peer;
      gint rate = GST_AUDIO_FILTER (self)->format.rate;

      if ((peer = gst_pad_get_peer (GST_BASE_TRANSFORM (self)->sinkpad))) {
        if ((res = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);

          GST_DEBUG_OBJECT (self, "Peer latency: min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          /* add our own latency */
          latency =
              (rate != 0) ? gst_util_uint64_scale (self->latency, GST_SECOND,
              rate) : 0;

          GST_DEBUG_OBJECT (self, "Our latency: %"
              GST_TIME_FORMAT, GST_TIME_ARGS (latency));

          min += latency;
          if (max != GST_CLOCK_TIME_NONE)
            max += latency;

          GST_DEBUG_OBJECT (self, "Calculated total latency : min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          gst_query_set_latency (query, live, min, max);
        }
        gst_object_unref (peer);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  gst_object_unref (self);
  return res;
}

static const GstQueryType *
lpwsinc_query_type (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_LATENCY,
    0
  };

  return types;
}

static gboolean
lpwsinc_event (GstBaseTransform * base, GstEvent * event)
{
  GstLPWSinc *self = GST_LPWSINC (base);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      lpwsinc_push_residue (self);
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->event (base, event);
}

static void
lpwsinc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstLPWSinc *self = GST_LPWSINC (object);

  g_return_if_fail (GST_IS_LPWSINC (self));

  switch (prop_id) {
    case PROP_LENGTH:{
      gint val;

      GST_BASE_TRANSFORM_LOCK (self);
      val = g_value_get_int (value);
      if (val % 2 == 0)
        val++;

      if (val != self->kernel_length) {
        if (self->residue) {
          lpwsinc_push_residue (self);
          g_free (self->residue);
          self->residue = NULL;
        }
        self->kernel_length = val;
        self->latency = val / 2;
        lpwsinc_build_kernel (self);
      }
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    }
    case PROP_FREQUENCY:
      GST_BASE_TRANSFORM_LOCK (self);
      self->frequency = g_value_get_double (value);
      lpwsinc_build_kernel (self);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    case PROP_MODE:
      GST_BASE_TRANSFORM_LOCK (self);
      self->mode = g_value_get_enum (value);
      lpwsinc_build_kernel (self);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    case PROP_WINDOW:
      GST_BASE_TRANSFORM_LOCK (self);
      self->window = g_value_get_enum (value);
      lpwsinc_build_kernel (self);
      GST_BASE_TRANSFORM_UNLOCK (self);
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
  GstLPWSinc *self = GST_LPWSINC (object);

  switch (prop_id) {
    case PROP_LENGTH:
      g_value_set_int (value, self->kernel_length);
      break;
    case PROP_FREQUENCY:
      g_value_set_double (value, self->frequency);
      break;
    case PROP_MODE:
      g_value_set_enum (value, self->mode);
      break;
    case PROP_WINDOW:
      g_value_set_enum (value, self->window);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
