/* GStreamer
 * Copyright (C) <2004> Benjamin Otte <otte@gnome.org>
 *               <2007> Stefan Kost <ensonic@users.sf.net>
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

#include <math.h>
#include <string.h>

#include "gstiirequalizer.h"
#include "gstiirequalizernbands.h"
#include "gstiirequalizer3bands.h"
#include "gstiirequalizer10bands.h"

GST_DEBUG_CATEGORY (equalizer_debug);
#define GST_CAT_DEFAULT equalizer_debug


enum
{
  ARG_BAND_WIDTH = 1
};

static void gst_iir_equalizer_child_proxy_interface_init (gpointer g_iface,
    gpointer iface_data);

static void gst_iir_equalizer_finalize (GObject * object);
static void gst_iir_equalizer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_iir_equalizer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_iir_equalizer_setup (GstAudioFilter * filter,
    GstRingBufferSpec * fmt);
static GstFlowReturn gst_iir_equalizer_transform_ip (GstBaseTransform * btrans,
    GstBuffer * buf);

GST_DEBUG_CATEGORY_EXTERN (equalizer_debug);
#define GST_CAT_DEFAULT equalizer_debug

#define ALLOWED_CAPS \
    "audio/x-raw-int,"                                                \
    " depth=(int)16,"                                                 \
    " width=(int)16,"                                                 \
    " endianness=(int)BYTE_ORDER,"                                    \
    " signed=(bool)TRUE,"                                             \
    " rate=(int)[1000,MAX],"                                          \
    " channels=(int)[1,MAX]; "                                        \
    "audio/x-raw-float,"                                              \
    " width=(int)32,"                                                 \
    " endianness=(int)BYTE_ORDER,"                                    \
    " rate=(int)[1000,MAX],"                                          \
    " channels=(int)[1,MAX]"

static void
_do_init (GType object_type)
{
  const GInterfaceInfo child_proxy_interface_info = {
    (GInterfaceInitFunc) gst_iir_equalizer_child_proxy_interface_init,
    NULL,                       /* interface_finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (object_type, GST_TYPE_CHILD_PROXY,
      &child_proxy_interface_info);
}

GST_BOILERPLATE_FULL (GstIirEqualizer, gst_iir_equalizer,
    GstAudioFilter, GST_TYPE_AUDIO_FILTER, _do_init);

/* child object */

enum
{
  ARG_GAIN = 1,
  ARG_FREQ
};

typedef struct _GstIirEqualizerBandClass GstIirEqualizerBandClass;

#define GST_TYPE_IIR_EQUALIZER_BAND \
  (gst_iir_equalizer_band_get_type())
#define GST_IIR_EQUALIZER_BAND(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IIR_EQUALIZER_BAND,GstIirEqualizerBand))
#define GST_IIR_EQUALIZER_BAND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IIR_EQUALIZER_BAND,GstIirEqualizerBandClass))
#define GST_IS_IIR_EQUALIZER_BAND(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IIR_EQUALIZER_BAND))
#define GST_IS_IIR_EQUALIZER_BAND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IIR_EQUALIZER_BAND))

struct _GstIirEqualizerBand
{
  GstObject object;

  /*< private > */
  /* center frequency and gain */
  gdouble freq;
  gdouble gain;

  /* second order iir filter */
  gdouble alpha;                /* IIR coefficients for outputs */
  gdouble beta;                 /* IIR coefficients for inputs */
  gdouble gamma;                /* IIR coefficients for inputs */
};

struct _GstIirEqualizerBandClass
{
  GstObjectClass parent_class;
};

static GType gst_iir_equalizer_band_get_type (void);
static void setup_filter (GstIirEqualizer * equ, GstIirEqualizerBand * band);

static void
gst_iir_equalizer_band_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIirEqualizerBand *band = GST_IIR_EQUALIZER_BAND (object);

  switch (prop_id) {
    case ARG_GAIN:{
      gdouble gain;

      gain = g_value_get_double (value);
      GST_INFO_OBJECT (band, "gain = %lf -> %lf", band->gain, gain);
      if (gain != band->gain) {
        GstIirEqualizer *equ =
            GST_IIR_EQUALIZER (gst_object_get_parent (GST_OBJECT (band)));

        band->gain = gain;
        if (GST_AUDIO_FILTER (equ)->format.rate) {
          setup_filter (equ, band);
        }
        gst_object_unref (equ);
        GST_INFO_OBJECT (band, "changed gain = %lf ", band->gain);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_iir_equalizer_band_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstIirEqualizerBand *band = GST_IIR_EQUALIZER_BAND (object);

  switch (prop_id) {
    case ARG_GAIN:
      g_value_set_double (value, band->gain);
      break;
    case ARG_FREQ:
      g_value_set_double (value, band->freq);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_iir_equalizer_band_class_init (GstIirEqualizerBandClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_iir_equalizer_band_set_property;
  gobject_class->get_property = gst_iir_equalizer_band_get_property;

  g_object_class_install_property (gobject_class, ARG_GAIN,
      g_param_spec_double ("gain", "gain",
          "gain for the frequency band ranging from -1.0 to +1.0",
          -1.0, 1.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, ARG_FREQ,
      g_param_spec_double ("freq", "freq",
          "center frequency of the band",
          0.0, 100000.0, 0.0, G_PARAM_READABLE));
}

static GType
gst_iir_equalizer_band_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (!type)) {
    const GTypeInfo type_info = {
      sizeof (GstIirEqualizerBandClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_iir_equalizer_band_class_init,
      NULL,
      NULL,
      sizeof (GstIirEqualizerBand),
      0,
      NULL,
    };
    type =
        g_type_register_static (GST_TYPE_OBJECT, "GstIirEqualizerBand",
        &type_info, 0);
  }
  return (type);
}


/* child proxy iface */
static GstObject *
gst_iir_equalizer_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (child_proxy);

  g_return_val_if_fail (index < equ->freq_band_count, NULL);

  GST_INFO ("return child[%d] '%s'", index,
      GST_OBJECT_NAME (equ->bands[index]));
  return (gst_object_ref (equ->bands[index]));
}

static guint
gst_iir_equalizer_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (child_proxy);

  GST_INFO ("we have %d children", equ->freq_band_count);
  return (equ->freq_band_count);
}

static void
gst_iir_equalizer_child_proxy_interface_init (gpointer g_iface,
    gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  GST_INFO ("initializing iface");

  iface->get_child_by_index = gst_iir_equalizer_child_proxy_get_child_by_index;
  iface->get_children_count = gst_iir_equalizer_child_proxy_get_children_count;
}


/* equalizer implementation */

static void
gst_iir_equalizer_base_init (gpointer g_class)
{
  GstAudioFilterClass *audiofilter_class = GST_AUDIO_FILTER_CLASS (g_class);
  GstCaps *caps;

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (audiofilter_class, caps);
  gst_caps_unref (caps);
}

static void
gst_iir_equalizer_class_init (GstIirEqualizerClass * klass)
{
  GstAudioFilterClass *audio_filter_class = (GstAudioFilterClass *) klass;
  GstBaseTransformClass *btrans_class = (GstBaseTransformClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_iir_equalizer_set_property;
  gobject_class->get_property = gst_iir_equalizer_get_property;
  gobject_class->finalize = gst_iir_equalizer_finalize;

  /* FIXME: move to GstIirEqualizerBand to make a full parametric eq */
  g_object_class_install_property (gobject_class, ARG_BAND_WIDTH,
      g_param_spec_double ("band-width", "band-width",
          "band width calculated as distance between bands * this value", 0.1,
          5.0, 1.0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  audio_filter_class->setup = gst_iir_equalizer_setup;
  btrans_class->transform_ip = gst_iir_equalizer_transform_ip;
}

static void
gst_iir_equalizer_init (GstIirEqualizer * eq, GstIirEqualizerClass * g_class)
{
  /* nothing to do here */
}

static void
gst_iir_equalizer_finalize (GObject * object)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (object);

  g_free (equ->bands);
  g_free (equ->history);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/*
 * converts gain values to scale factors.
 *
 * arguments are in the range [-1 ... 1] with 0 meaning "no action"
 * results are in the range [-0.2 ... 1] with 0 meaning "no action"
 * via the function
 *   f(x) = 0.25 * 5 ^ x - 0.25
 *
 * visualize via gnuplot:
 *   set xrange [-1:1]
 *   plot 0.25 * exp (log (5) * x) - 0.25
 */
static gdouble
arg_to_scale (gdouble arg)
{
  return 0.25 * exp (log (5) * arg) - 0.25;
}

static void
setup_filter (GstIirEqualizer * equ, GstIirEqualizerBand * band)
{
  g_return_if_fail (GST_AUDIO_FILTER (equ)->format.rate);

  {
    gdouble gain = arg_to_scale (band->gain);
    gdouble frequency = band->freq / GST_AUDIO_FILTER (equ)->format.rate;
    gdouble q = pow (HIGHEST_FREQ / LOWEST_FREQ,
        1.0 / (equ->freq_band_count - 1)) * equ->band_width;
    gdouble theta = frequency * 2 * M_PI;

    band->beta = (q - theta / 2) / (2 * q + theta);
    band->gamma = (0.5 + band->beta) * cos (theta);
    band->alpha = (0.5 - band->beta) / 2;

    band->beta *= 2.0;
    band->alpha *= 2.0 * gain;
    band->gamma *= 2.0;
    GST_INFO ("gain = %g, frequency = %g, alpha = %g, beta = %g, gamma=%g",
        gain, frequency, band->alpha, band->beta, band->gamma);
  }
}

void
gst_iir_equalizer_compute_frequencies (GstIirEqualizer * equ, guint new_count)
{
  guint old_count, i;
  gdouble step = pow (HIGHEST_FREQ / LOWEST_FREQ, 1.0 / (new_count - 1));
  gchar name[20];

  old_count = equ->freq_band_count;
  equ->freq_band_count = new_count;
  GST_DEBUG ("bands %u -> %u", old_count, new_count);

  if (old_count < new_count) {
    /* add new bands */
    equ->bands = g_realloc (equ->bands, sizeof (GstObject *) * new_count);
    for (i = old_count; i < new_count; i++) {
      equ->bands[i] = g_object_new (GST_TYPE_IIR_EQUALIZER_BAND, NULL);
      /* otherwise they get names like 'iirequalizerband5' */
      sprintf (name, "band%u", i);
      gst_object_set_name (GST_OBJECT (equ->bands[i]), name);

      gst_object_set_parent (GST_OBJECT (equ->bands[i]), GST_OBJECT (equ));
      gst_child_proxy_child_added (GST_OBJECT (equ),
          GST_OBJECT (equ->bands[i]));
    }
  } else {
    /* free unused bands */
    for (i = new_count; i < old_count; i++) {
      GST_DEBUG ("removing band[%d]=%p", i, equ->bands[i]);
      gst_child_proxy_child_removed (GST_OBJECT (equ),
          GST_OBJECT (equ->bands[i]));
      gst_object_unparent (GST_OBJECT (equ->bands[i]));
      equ->bands[i] = NULL;
    }
  }

  /* free + alloc = no memcpy */
  g_free (equ->history);
  equ->history =
      g_malloc0 (equ->history_size * GST_AUDIO_FILTER (equ)->format.channels *
      new_count);

  /* set center frequencies and name band objects
   * FIXME: arg! we can't change the name of parented objects :(
   *   application should read band->freq
   * FIXME: the code that calculates the center-frequencies for the bands should
   *   take the number of bands into account, when chooding the lowest frequency
   */
  equ->bands[0]->freq = LOWEST_FREQ;
  GST_DEBUG ("band[ 0] = '%lf'", equ->bands[0]->freq);
  /*
     if(equ->bands[0]->freq<10000.0) {
     sprintf (name,"%dHz",(gint)equ->bands[0]->freq);
     }
     else {
     sprintf (name,"%dkHz",(gint)(equ->bands[0]->freq/1000.0));
     }
     gst_object_set_name( GST_OBJECT (equ->bands[0]), name);
     GST_DEBUG ("band[ 0] = '%s'",name);
   */

  for (i = 1; i < new_count; i++) {
    equ->bands[i]->freq = equ->bands[i - 1]->freq * step;
    GST_DEBUG ("band[%2d] = '%lf'", i, equ->bands[i]->freq);
    /*
       if(equ->bands[i]->freq<10000.0) {
       sprintf (name,"%dHz",(gint)equ->bands[i]->freq);
       }
       else {
       sprintf (name,"%dkHz",(gint)(equ->bands[i]->freq/1000.0));
       }
       gst_object_set_name( GST_OBJECT (equ->bands[i]), name);
       GST_DEBUG ("band[%2d] = '%s'",i,name);
     */
  }

  if (GST_AUDIO_FILTER (equ)->format.rate) {
    for (i = 0; i < new_count; i++) {
      setup_filter (equ, equ->bands[i]);
    }
  }
}

static void
gst_iir_equalizer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (object);

  g_mutex_lock (((GstBaseTransform *) (equ))->transform_lock);
  //GST_BASE_TRANSFORM_LOCK (equ);
  GST_OBJECT_LOCK (equ);
  switch (prop_id) {
    case ARG_BAND_WIDTH:
      if (g_value_get_double (value) != equ->band_width) {
        equ->band_width = g_value_get_double (value);
        if (GST_AUDIO_FILTER (equ)->format.rate) {
          guint i;

          for (i = 0; i < equ->freq_band_count; i++) {
            setup_filter (equ, equ->bands[i]);
          }
        }
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (equ);
  GST_BASE_TRANSFORM_UNLOCK (equ);
}

static void
gst_iir_equalizer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (object);

  GST_BASE_TRANSFORM_LOCK (equ);
  GST_OBJECT_LOCK (equ);
  switch (prop_id) {
    case ARG_BAND_WIDTH:
      g_value_set_double (value, equ->band_width);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (equ);
  GST_BASE_TRANSFORM_UNLOCK (equ);
}

/* start of code that is type specific */

#define CREATE_OPTIMIZED_FUNCTIONS(TYPE,BIG_TYPE,MIN_VAL,MAX_VAL)       \
typedef struct {                                                        \
  TYPE x1, x2;          /* history of input values for a filter */      \
  TYPE y1, y2;          /* history of output values for a filter */     \
} SecondOrderHistory ## TYPE;                                           \
                                                                        \
static inline TYPE                                                      \
one_step_ ## TYPE (GstIirEqualizerBand *filter,                         \
    SecondOrderHistory ## TYPE *history, TYPE input)                    \
{                                                                       \
  /* calculate output */                                                \
  TYPE output = filter->alpha * (input - history->x2) +                 \
    filter->gamma * history->y1 - filter->beta * history->y2;           \
  /* update history */                                                  \
  history->y2 = history->y1;                                            \
  history->y1 = output;                                                 \
  history->x2 = history->x1;                                            \
  history->x1 = input;                                                  \
                                                                        \
  return output;                                                        \
}                                                                       \
                                                                        \
static const guint                                                      \
history_size_ ## TYPE = sizeof (SecondOrderHistory ## TYPE);            \
                                                                        \
static void                                                             \
gst_iir_equ_process_ ## TYPE (GstIirEqualizer *equ, guint8 *data, \
guint size, guint channels)                                             \
{                                                                       \
  guint frames = size / channels / sizeof (TYPE);                       \
  guint i, c, f;                                                        \
  BIG_TYPE cur;                                                         \
  TYPE val;                                                             \
                                                                        \
  for (i = 0; i < frames; i++) {                                        \
    for (c = 0; c < channels; c++) {                                    \
      SecondOrderHistory ## TYPE *history = equ->history;               \
      val = *((TYPE *) data);                                           \
      cur = 0;                                                          \
      for (f = 0; f < equ->freq_band_count; f++) {                      \
        GstIirEqualizerBand *filter = equ->bands[f];                    \
                                                                        \
        cur += one_step_ ## TYPE (filter, history, val);                \
        history++;                                                      \
      }                                                                 \
      cur += val * 0.25;                                                \
      cur = CLAMP (cur, MIN_VAL, MAX_VAL);                              \
      *((TYPE *) data) = (TYPE) cur;                                    \
      data += sizeof (TYPE);                                            \
    }                                                                   \
  }                                                                     \
}

CREATE_OPTIMIZED_FUNCTIONS (gint16, gint, -32768, 32767);
CREATE_OPTIMIZED_FUNCTIONS (gfloat, gfloat, -1.0, 1.0);

static GstFlowReturn
gst_iir_equalizer_transform_ip (GstBaseTransform * btrans, GstBuffer * buf)
{
  GstAudioFilter *filter = GST_AUDIO_FILTER (btrans);
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (btrans);
  GstClockTime timestamp;

  if (G_UNLIKELY (filter->format.channels < 1 || equ->process == NULL))
    return GST_FLOW_NOT_NEGOTIATED;

  timestamp = GST_BUFFER_TIMESTAMP (buf);
  timestamp =
      gst_segment_to_stream_time (&btrans->segment, GST_FORMAT_TIME, timestamp);

  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (equ), timestamp);

  equ->process (equ, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf),
      filter->format.channels);

  return GST_FLOW_OK;
}

static gboolean
gst_iir_equalizer_setup (GstAudioFilter * audio, GstRingBufferSpec * fmt)
{
  GstIirEqualizer *equ = GST_IIR_EQUALIZER (audio);

  switch (fmt->width) {
    case 16:
      equ->history_size = history_size_gint16;
      equ->process = gst_iir_equ_process_gint16;
      break;
    case 32:
      equ->history_size = history_size_gfloat;
      equ->process = gst_iir_equ_process_gfloat;
      break;
    default:
      return FALSE;
  }
  gst_iir_equalizer_compute_frequencies (equ, equ->freq_band_count);
  return TRUE;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (equalizer_debug, "equalizer", 0, "equalizer");

  if (!(gst_element_register (plugin, "equalizer-nbands", GST_RANK_NONE,
              GST_TYPE_IIR_EQUALIZER_NBANDS)))
    return FALSE;

  if (!(gst_element_register (plugin, "equalizer-3bands", GST_RANK_NONE,
              GST_TYPE_IIR_EQUALIZER_3BANDS)))
    return FALSE;

  if (!(gst_element_register (plugin, "equalizer-10bands", GST_RANK_NONE,
              GST_TYPE_IIR_EQUALIZER_10BANDS)))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "equalizer",
    "GStreamer audio equalizers",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
