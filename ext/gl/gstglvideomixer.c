/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-glvideomixer
 *
 * Composites a number of streams into a single output scene using OpenGL in
 * a similar fashion to compositor and videomixer. See the compositor plugin
 * for documentation about the #GstGLVideoMixerPad properties.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-1.0  glvideomixer name=m ! glimagesink \
 *     videotestsrc ! video/x-raw, format=YUY2 ! m. \
 *     videotestsrc pattern=12 ! video/x-raw, format=I420, framerate=5/1, width=100, height=200 ! queue ! m. \
 *     videotestsrc ! video/x-raw, format=RGB, framerate=15/1, width=1500, height=1500 ! gleffects effect=3 ! queue ! m. \
 *     videotestsrc ! gleffects effect=2 ! queue ! m.  \
 *     videotestsrc ! glfiltercube ! queue ! m. \
 *     videotestsrc ! gleffects effect=6 ! queue ! m.
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglvideomixer.h"
#include "gstglmixerbin.h"

#define GST_CAT_DEFAULT gst_gl_video_mixer_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_GL_TYPE_VIDEO_MIXER_BACKGROUND (gst_gl_video_mixer_background_get_type())
static GType
gst_gl_video_mixer_background_get_type (void)
{
  static GType mixer_background_type = 0;

  static const GEnumValue mixer_background[] = {
    {GST_GL_VIDEO_MIXER_BACKGROUND_CHECKER, "Checker pattern", "checker"},
    {GST_GL_VIDEO_MIXER_BACKGROUND_BLACK, "Black", "black"},
    {GST_GL_VIDEO_MIXER_BACKGROUND_WHITE, "White", "white"},
    {GST_GL_VIDEO_MIXER_BACKGROUND_TRANSPARENT,
        "Transparent Background to enable further compositing", "transparent"},
    {0, NULL, NULL},
  };

  if (!mixer_background_type) {
    mixer_background_type =
        g_enum_register_static ("GstGLVideoMixerBackground", mixer_background);
  }
  return mixer_background_type;
}

typedef struct _GstGLMixerControlBindingProxy GstGLMixerControlBindingProxy;
typedef struct _GstGLMixerControlBindingProxyClass
    GstGLMixerControlBindingProxyClass;

struct _GstGLMixerControlBindingProxy
{
  GstControlBinding parent;

  GstObject *ref_object;
  const gchar *property_name;
};

struct _GstGLMixerControlBindingProxyClass
{
  GstControlBindingClass parent_class;
};

GType gst_gl_mixer_control_binding_proxy_get_type (void);
#define GST_TYPE_GL_MIXER_CONTROL_BINDING \
  (gst_gl_mixer_control_binding_proxy_get_type())

G_DEFINE_TYPE (GstGLMixerControlBindingProxy,
    gst_gl_mixer_control_binding_proxy, GST_TYPE_CONTROL_BINDING);

static void
gst_gl_mixer_control_binding_proxy_init (GstGLMixerControlBindingProxy * self)
{
}

static gboolean
gst_gl_mixer_control_binding_proxy_sync_values (GstControlBinding * binding,
    GstObject * object, GstClockTime timestamp, GstClockTime last_sync)
{
  GstGLMixerControlBindingProxy *self = (GstGLMixerControlBindingProxy *)
      binding;
  GstControlBinding *ref_binding;
  gboolean ret = TRUE;

  ref_binding = gst_object_get_control_binding (self->ref_object,
      self->property_name);

  if (ref_binding) {
    ret = gst_control_binding_sync_values (ref_binding, self->ref_object,
        timestamp, last_sync);
    gst_object_unref (ref_binding);
  }

  return ret;
}

static GValue *
gst_gl_mixer_control_binding_proxy_get_value (GstControlBinding * binding,
    GstClockTime timestamp)
{
  GstGLMixerControlBindingProxy *self = (GstGLMixerControlBindingProxy *)
      binding;
  GstControlBinding *ref_binding;
  GValue *ret = NULL;

  ref_binding = gst_object_get_control_binding (self->ref_object,
      self->property_name);

  if (ref_binding) {
    ret = gst_control_binding_get_value (ref_binding, timestamp);
    gst_object_unref (ref_binding);
  }

  return ret;
}

static gboolean
gst_gl_mixer_control_binding_proxy_get_value_array (GstControlBinding * binding,
    GstClockTime timestamp, GstClockTime interval, guint n_values,
    gpointer values)
{
  GstGLMixerControlBindingProxy *self = (GstGLMixerControlBindingProxy *)
      binding;
  GstControlBinding *ref_binding;
  gboolean ret = FALSE;

  ref_binding = gst_object_get_control_binding (self->ref_object,
      self->property_name);

  if (ref_binding) {
    ret = gst_control_binding_get_value_array (ref_binding, timestamp,
        interval, n_values, values);
    gst_object_unref (ref_binding);
  }

  return ret;
}

static gboolean
gst_gl_mixer_control_binding_proxy_get_g_value_array (GstControlBinding *
    binding, GstClockTime timestamp, GstClockTime interval, guint n_values,
    GValue * values)
{
  GstGLMixerControlBindingProxy *self = (GstGLMixerControlBindingProxy *)
      binding;
  GstControlBinding *ref_binding;
  gboolean ret = FALSE;

  ref_binding = gst_object_get_control_binding (self->ref_object,
      self->property_name);

  if (ref_binding) {
    ret = gst_control_binding_get_g_value_array (ref_binding, timestamp,
        interval, n_values, values);
    gst_object_unref (ref_binding);
  }

  return ret;
}


static void
    gst_gl_mixer_control_binding_proxy_class_init
    (GstGLMixerControlBindingProxyClass * klass)
{
  GstControlBindingClass *cb_class = GST_CONTROL_BINDING_CLASS (klass);

  cb_class->sync_values = gst_gl_mixer_control_binding_proxy_sync_values;
  cb_class->get_value = gst_gl_mixer_control_binding_proxy_get_value;
  cb_class->get_value_array =
      gst_gl_mixer_control_binding_proxy_get_value_array;
  cb_class->get_g_value_array =
      gst_gl_mixer_control_binding_proxy_get_g_value_array;
}

static GstControlBinding *
gst_gl_mixer_control_binding_proxy_new (GstObject * object,
    const gchar * property_name, GstObject * ref_object,
    const gchar * ref_property_name)
{
  GstGLMixerControlBindingProxy *self =
      g_object_new (GST_TYPE_GL_MIXER_CONTROL_BINDING, "object", object,
      "name", property_name, NULL);

  self->ref_object = ref_object;
  self->property_name = ref_property_name;

  return (GstControlBinding *) self;
}


#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0
#define DEFAULT_PAD_WIDTH  0
#define DEFAULT_PAD_HEIGHT 0
#define DEFAULT_PAD_ALPHA  1.0
#define DEFAULT_PAD_ZORDER 0

enum
{
  PROP_INPUT_0,
  PROP_INPUT_XPOS,
  PROP_INPUT_YPOS,
  PROP_INPUT_WIDTH,
  PROP_INPUT_HEIGHT,
  PROP_INPUT_ALPHA,
  PROP_INPUT_ZORDER,
};

static void gst_gl_video_mixer_input_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_gl_video_mixer_input_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

typedef struct _GstGLVideoMixerInput GstGLVideoMixerInput;
typedef GstGhostPadClass GstGLVideoMixerInputClass;

struct _GstGLVideoMixerInput
{
  GstGhostPad parent;

  GstSegment segment;

  GstPad *mixer_pad;
};

GType gst_gl_video_mixer_input_get_type (void);
G_DEFINE_TYPE (GstGLVideoMixerInput, gst_gl_video_mixer_input,
    GST_TYPE_GHOST_PAD);

static void
gst_gl_video_mixer_input_init (GstGLVideoMixerInput * self)
{
}

static void
gst_gl_video_mixer_input_class_init (GstGLVideoMixerInputClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_gl_video_mixer_input_set_property;
  gobject_class->get_property = gst_gl_video_mixer_input_get_property;

  g_object_class_install_property (gobject_class, PROP_INPUT_ZORDER,
      g_param_spec_uint ("zorder", "Z-Order", "Z Order of the picture",
          0, 10000, DEFAULT_PAD_ZORDER,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_XPOS,
      g_param_spec_int ("xpos", "X Position", "X Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_WIDTH,
      g_param_spec_int ("width", "Width", "Width of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_WIDTH,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_HEIGHT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_PAD_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_gl_video_mixer_input_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLVideoMixerInput *self = (GstGLVideoMixerInput *) object;

  if (self->mixer_pad)
    g_object_get_property (G_OBJECT (self->mixer_pad), pspec->name, value);
}

static void
gst_gl_video_mixer_input_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLVideoMixerInput *self = (GstGLVideoMixerInput *) object;

  if (self->mixer_pad)
    g_object_set_property (G_OBJECT (self->mixer_pad), pspec->name, value);
}

static GstGhostPad *
_create_video_mixer_input (GstGLMixerBin * self, GstPad * mixer_pad)
{
  GstGLVideoMixerInput *input =
      g_object_new (gst_gl_video_mixer_input_get_type (), "name",
      GST_OBJECT_NAME (mixer_pad), "direction", GST_PAD_DIRECTION (mixer_pad),
      NULL);
  GstControlBinding *cb;

  if (!gst_ghost_pad_construct (GST_GHOST_PAD (input))) {
    gst_object_unref (input);
    return NULL;
  }
#define ADD_PROXY_CONTROL_BINDING(prop)                                \
  cb = gst_gl_mixer_control_binding_proxy_new (GST_OBJECT (mixer_pad), \
      G_STRINGIFY (prop), GST_OBJECT (input), G_STRINGIFY (prop));     \
  gst_object_add_control_binding (GST_OBJECT (mixer_pad), cb)

  ADD_PROXY_CONTROL_BINDING (zorder);
  ADD_PROXY_CONTROL_BINDING (xpos);
  ADD_PROXY_CONTROL_BINDING (ypos);
  ADD_PROXY_CONTROL_BINDING (width);
  ADD_PROXY_CONTROL_BINDING (height);
  ADD_PROXY_CONTROL_BINDING (alpha);

#undef ADD_PROXY_CONTROL_BINDING

  input->mixer_pad = mixer_pad;

  return GST_GHOST_PAD (input);
}

enum
{
  PROP_BIN_0,
  PROP_BIN_BACKGROUND,
};
#define DEFAULT_BACKGROUND GST_GL_VIDEO_MIXER_BACKGROUND_CHECKER

static void gst_gl_video_mixer_bin_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_gl_video_mixer_bin_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

typedef GstGLMixerBin GstGLVideoMixerBin;
typedef GstGLMixerBinClass GstGLVideoMixerBinClass;

G_DEFINE_TYPE (GstGLVideoMixerBin, gst_gl_video_mixer_bin,
    GST_TYPE_GL_MIXER_BIN);

static void
gst_gl_video_mixer_bin_init (GstGLVideoMixerBin * self)
{
  GstGLMixerBin *mix_bin = GST_GL_MIXER_BIN (self);

  gst_gl_mixer_bin_finish_init_with_element (mix_bin,
      g_object_new (GST_TYPE_GL_VIDEO_MIXER, NULL));
}

static void
gst_gl_video_mixer_bin_class_init (GstGLVideoMixerBinClass * klass)
{
  GstGLMixerBinClass *mixer_class = GST_GL_MIXER_BIN_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  mixer_class->create_input_pad = _create_video_mixer_input;

  gobject_class->set_property = gst_gl_video_mixer_bin_set_property;
  gobject_class->get_property = gst_gl_video_mixer_bin_get_property;

  g_object_class_install_property (gobject_class, PROP_BIN_BACKGROUND,
      g_param_spec_enum ("background", "Background", "Background type",
          GST_GL_TYPE_VIDEO_MIXER_BACKGROUND,
          DEFAULT_BACKGROUND, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class, "OpenGL video_mixer bin",
      "Bin/Filter/Effect/Video/Compositor", "OpenGL video_mixer bin",
      "Matthew Waters <matthew@centricular.com>");
}

static void
gst_gl_video_mixer_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLMixerBin *self = GST_GL_MIXER_BIN (object);

  if (self->mixer)
    g_object_get_property (G_OBJECT (self->mixer), pspec->name, value);
}

static void
gst_gl_video_mixer_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLMixerBin *self = GST_GL_MIXER_BIN (object);

  if (self->mixer)
    g_object_set_property (G_OBJECT (self->mixer), pspec->name, value);
}

enum
{
  PROP_0,
  PROP_BACKGROUND,
};

#define DEBUG_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_gl_video_mixer_debug, "glvideomixer", 0, "glvideomixer element");

G_DEFINE_TYPE_WITH_CODE (GstGLVideoMixer, gst_gl_video_mixer, GST_TYPE_GL_MIXER,
    DEBUG_INIT);

static void gst_gl_video_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_video_mixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *_update_caps (GstVideoAggregator * vagg, GstCaps * caps);
static void gst_gl_video_mixer_reset (GstGLMixer * mixer);
static gboolean gst_gl_video_mixer_init_shader (GstGLMixer * mixer,
    GstCaps * outcaps);

static gboolean gst_gl_video_mixer_process_textures (GstGLMixer * mixer,
    GPtrArray * in_frames, guint out_tex);
static void gst_gl_video_mixer_callback (gpointer stuff);

/* *INDENT-OFF* */
/* vertex source */
static const gchar *video_mixer_v_src =
    "attribute vec4 a_position;                                   \n"
    "attribute vec2 a_texCoord;                                   \n"
    "varying vec2 v_texCoord;                                     \n"
    "void main()                                                  \n"
    "{                                                            \n"
    "   gl_Position = a_position;                                 \n"
    "   v_texCoord = a_texCoord;                                  \n" "}";

/* fragment source */
static const gchar *video_mixer_f_src =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform sampler2D texture;                     \n"
    "uniform float alpha;\n"
    "varying vec2 v_texCoord;                            \n"
    "void main()                                         \n"
    "{                                                   \n"
    "  vec4 rgba = texture2D( texture, v_texCoord );\n"
    "  gl_FragColor = vec4(rgba.rgb, rgba.a * alpha);\n"
    "}                                                   \n";

/* checker vertex source */
static const gchar *checker_v_src =
    "attribute vec4 a_position;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = a_position;\n"
    "}\n";

/* checker fragment source */
static const gchar *checker_f_src =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "const float blocksize = 8.0;\n"
    "void main ()\n"
    "{\n"
    "  vec4 high = vec4(0.667, 0.667, 0.667, 1.0);\n"
    "  vec4 low = vec4(0.333, 0.333, 0.333, 1.0);\n"
    "  if (mod(gl_FragCoord.x, blocksize * 2.0) >= blocksize) {\n"
    "    if (mod(gl_FragCoord.y, blocksize * 2.0) >= blocksize)\n"
    "      gl_FragColor = low;\n"
    "    else\n"
    "      gl_FragColor = high;\n"
    "  } else {\n"
    "    if (mod(gl_FragCoord.y, blocksize * 2.0) < blocksize)\n"
    "      gl_FragColor = low;\n"
    "    else\n"
    "      gl_FragColor = high;\n"
    "  }\n"
    "}\n";
/* *INDENT-ON* */

#define GST_TYPE_GL_VIDEO_MIXER_PAD (gst_gl_video_mixer_pad_get_type())
#define GST_GL_VIDEO_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_VIDEO_MIXER_PAD, GstGLVideoMixerPad))
#define GST_GL_VIDEO_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_VIDEO_MIXER_PAD, GstGLVideoMixerPadClass))
#define GST_IS_GL_VIDEO_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_VIDEO_MIXER_PAD))
#define GST_IS_GL_VIDEO_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_VIDEO_MIXER_PAD))

typedef struct _GstGLVideoMixerPad GstGLVideoMixerPad;
typedef struct _GstGLVideoMixerPadClass GstGLVideoMixerPadClass;
typedef struct _GstGLVideoMixerCollect GstGLVideoMixerCollect;

/**
 * GstGLVideoMixerPad:
 *
 * The opaque #GstGLVideoMixerPad structure.
 */
struct _GstGLVideoMixerPad
{
  GstGLMixerPad parent;

  /* < private > */
  /* properties */
  gint xpos, ypos;
  gint width, height;
  gdouble alpha;

  gboolean geometry_change;
  GLuint vertex_buffer;
};

struct _GstGLVideoMixerPadClass
{
  GstGLMixerPadClass parent_class;
};

GType gst_gl_video_mixer_pad_get_type (void);
G_DEFINE_TYPE (GstGLVideoMixerPad, gst_gl_video_mixer_pad,
    GST_TYPE_GL_MIXER_PAD);

static void gst_gl_video_mixer_pad_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_gl_video_mixer_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

enum
{
  PROP_PAD_0,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_WIDTH,
  PROP_PAD_HEIGHT,
  PROP_PAD_ALPHA
};

static void
gst_gl_video_mixer_pad_init (GstGLVideoMixerPad * pad)
{
  pad->alpha = 1.0;
}

static void
gst_gl_video_mixer_pad_class_init (GstGLVideoMixerPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_gl_video_mixer_pad_set_property;
  gobject_class->get_property = gst_gl_video_mixer_pad_get_property;

  g_object_class_install_property (gobject_class, PROP_PAD_XPOS,
      g_param_spec_int ("xpos", "X Position", "X Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_WIDTH,
      g_param_spec_int ("width", "Width", "Width of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_WIDTH,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_HEIGHT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_PAD_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_gl_video_mixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLVideoMixerPad *pad = GST_GL_VIDEO_MIXER_PAD (object);

  switch (prop_id) {
    case PROP_PAD_XPOS:
      g_value_set_int (value, pad->xpos);
      break;
    case PROP_PAD_YPOS:
      g_value_set_int (value, pad->ypos);
      break;
    case PROP_PAD_WIDTH:
      g_value_set_int (value, pad->width);
      break;
    case PROP_PAD_HEIGHT:
      g_value_set_int (value, pad->height);
      break;
    case PROP_PAD_ALPHA:
      g_value_set_double (value, pad->alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_video_mixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLVideoMixerPad *pad = GST_GL_VIDEO_MIXER_PAD (object);
  GstGLMixer *mix = GST_GL_MIXER (gst_pad_get_parent (GST_PAD (pad)));

  switch (prop_id) {
    case PROP_PAD_XPOS:
      pad->xpos = g_value_get_int (value);
      pad->geometry_change = TRUE;
      break;
    case PROP_PAD_YPOS:
      pad->ypos = g_value_get_int (value);
      pad->geometry_change = TRUE;
      break;
    case PROP_PAD_WIDTH:
      pad->width = g_value_get_int (value);
      pad->geometry_change = TRUE;
      break;
    case PROP_PAD_HEIGHT:
      pad->height = g_value_get_int (value);
      pad->geometry_change = TRUE;
      break;
    case PROP_PAD_ALPHA:
      pad->alpha = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_object_unref (mix);
}

static void
gst_gl_video_mixer_class_init (GstGLVideoMixerClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;
  GstVideoAggregatorClass *vagg_class = (GstVideoAggregatorClass *) klass;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_video_mixer_set_property;
  gobject_class->get_property = gst_gl_video_mixer_get_property;

  gst_element_class_set_metadata (element_class, "OpenGL video_mixer",
      "Filter/Effect/Video/Compositor", "OpenGL video_mixer",
      "Matthew Waters <matthew@centricular.com>");

  g_object_class_install_property (gobject_class, PROP_BACKGROUND,
      g_param_spec_enum ("background", "Background", "Background type",
          GST_GL_TYPE_VIDEO_MIXER_BACKGROUND,
          DEFAULT_BACKGROUND, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_GL_MIXER_CLASS (klass)->set_caps = gst_gl_video_mixer_init_shader;
  GST_GL_MIXER_CLASS (klass)->reset = gst_gl_video_mixer_reset;
  GST_GL_MIXER_CLASS (klass)->process_textures =
      gst_gl_video_mixer_process_textures;

  vagg_class->update_caps = _update_caps;

  agg_class->sinkpads_type = GST_TYPE_GL_VIDEO_MIXER_PAD;

  GST_GL_BASE_MIXER_CLASS (klass)->supported_gl_api =
      GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2;
}

static void
gst_gl_video_mixer_init (GstGLVideoMixer * video_mixer)
{
  video_mixer->background = DEFAULT_BACKGROUND;
  video_mixer->shader = NULL;
  video_mixer->input_frames = NULL;
}

static void
gst_gl_video_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLVideoMixer *mixer = GST_GL_VIDEO_MIXER (object);

  switch (prop_id) {
    case PROP_BACKGROUND:
      mixer->background = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_video_mixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLVideoMixer *mixer = GST_GL_VIDEO_MIXER (object);

  switch (prop_id) {
    case PROP_BACKGROUND:
      g_value_set_enum (value, mixer->background);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
_mixer_pad_get_output_size (GstGLVideoMixer * mix,
    GstGLVideoMixerPad * mix_pad, gint * width, gint * height)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (mix);
  GstVideoAggregatorPad *vagg_pad = GST_VIDEO_AGGREGATOR_PAD (mix_pad);
  gint pad_width, pad_height;
  guint dar_n, dar_d;

  /* FIXME: Anything better we can do here? */
  if (!vagg_pad->info.finfo
      || vagg_pad->info.finfo->format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_DEBUG_OBJECT (mix_pad, "Have no caps yet");
    *width = 0;
    *height = 0;
    return;
  }

  pad_width =
      mix_pad->width <=
      0 ? GST_VIDEO_INFO_WIDTH (&vagg_pad->info) : mix_pad->width;
  pad_height =
      mix_pad->height <=
      0 ? GST_VIDEO_INFO_HEIGHT (&vagg_pad->info) : mix_pad->height;

  gst_video_calculate_display_ratio (&dar_n, &dar_d, pad_width, pad_height,
      GST_VIDEO_INFO_PAR_N (&vagg_pad->info),
      GST_VIDEO_INFO_PAR_D (&vagg_pad->info),
      GST_VIDEO_INFO_PAR_N (&vagg->info), GST_VIDEO_INFO_PAR_D (&vagg->info)
      );
  GST_LOG_OBJECT (mix_pad, "scaling %ux%u by %u/%u (%u/%u / %u/%u)", pad_width,
      pad_height, dar_n, dar_d, GST_VIDEO_INFO_PAR_N (&vagg_pad->info),
      GST_VIDEO_INFO_PAR_D (&vagg_pad->info),
      GST_VIDEO_INFO_PAR_N (&vagg->info), GST_VIDEO_INFO_PAR_D (&vagg->info));

  if (pad_height % dar_n == 0) {
    pad_width = gst_util_uint64_scale_int (pad_height, dar_n, dar_d);
  } else if (pad_width % dar_d == 0) {
    pad_height = gst_util_uint64_scale_int (pad_width, dar_d, dar_n);
  } else {
    pad_width = gst_util_uint64_scale_int (pad_height, dar_n, dar_d);
  }

  if (width)
    *width = pad_width;
  if (height)
    *height = pad_height;
}

static GstCaps *
_update_caps (GstVideoAggregator * vagg, GstCaps * caps)
{
  GstGLVideoMixer *mix = GST_GL_VIDEO_MIXER (vagg);
  GList *l;
  gint best_width = -1, best_height = -1;
  GstVideoInfo info;
  GstCaps *ret = NULL;
  int i;

  caps = gst_caps_make_writable (caps);
  gst_video_info_from_caps (&info, caps);

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *vaggpad = l->data;
    GstGLVideoMixerPad *mixer_pad = GST_GL_VIDEO_MIXER_PAD (vaggpad);
    gint this_width, this_height;
    gint width, height;

    _mixer_pad_get_output_size (mix, mixer_pad, &width, &height);

    if (width == 0 || height == 0)
      continue;

    this_width = width + MAX (mixer_pad->xpos, 0);
    this_height = height + MAX (mixer_pad->ypos, 0);

    if (best_width < this_width)
      best_width = this_width;
    if (best_height < this_height)
      best_height = this_height;
  }
  GST_OBJECT_UNLOCK (vagg);

  ret =
      GST_VIDEO_AGGREGATOR_CLASS (gst_gl_video_mixer_parent_class)->update_caps
      (vagg, caps);

  for (i = 0; i < gst_caps_get_size (ret); i++) {
    GstStructure *s = gst_caps_get_structure (ret, i);

    gst_structure_set (s, "width", G_TYPE_INT, best_width, "height", G_TYPE_INT,
        best_height, NULL);
  }

  return ret;
}

static gboolean
_reset_pad_gl (GstAggregator * agg, GstAggregatorPad * aggpad, gpointer udata)
{
  const GstGLFuncs *gl = GST_GL_BASE_MIXER (agg)->context->gl_vtable;
  GstGLVideoMixerPad *pad = GST_GL_VIDEO_MIXER_PAD (aggpad);

  if (pad->vertex_buffer) {
    gl->DeleteBuffers (1, &pad->vertex_buffer);
    pad->vertex_buffer = 0;
  }

  return FALSE;
}

static void
_reset_gl (GstGLContext * context, GstGLVideoMixer * video_mixer)
{
  const GstGLFuncs *gl = GST_GL_BASE_MIXER (video_mixer)->context->gl_vtable;

  if (video_mixer->vao) {
    gl->DeleteVertexArrays (1, &video_mixer->vao);
    video_mixer->vao = 0;
  }

  if (video_mixer->vbo_indices) {
    gl->DeleteBuffers (1, &video_mixer->vbo_indices);
    video_mixer->vbo_indices = 0;
  }

  gst_aggregator_iterate_sinkpads (GST_AGGREGATOR (video_mixer), _reset_pad_gl,
      NULL);
}

static void
gst_gl_video_mixer_reset (GstGLMixer * mixer)
{
  GstGLVideoMixer *video_mixer = GST_GL_VIDEO_MIXER (mixer);
  GstGLContext *context = GST_GL_BASE_MIXER (mixer)->context;

  video_mixer->input_frames = NULL;

  GST_DEBUG_OBJECT (mixer, "context:%p", context);

  if (video_mixer->shader)
    gst_gl_context_del_shader (context, video_mixer->shader);
  video_mixer->shader = NULL;

  if (video_mixer->checker)
    gst_gl_context_del_shader (context, video_mixer->checker);
  video_mixer->checker = NULL;

  if (GST_GL_BASE_MIXER (mixer)->context)
    gst_gl_context_thread_add (context, (GstGLContextThreadFunc) _reset_gl,
        mixer);
}

static gboolean
gst_gl_video_mixer_init_shader (GstGLMixer * mixer, GstCaps * outcaps)
{
  GstGLVideoMixer *video_mixer = GST_GL_VIDEO_MIXER (mixer);

  if (video_mixer->shader)
    gst_gl_context_del_shader (GST_GL_BASE_MIXER (mixer)->context,
        video_mixer->shader);

  return gst_gl_context_gen_shader (GST_GL_BASE_MIXER (mixer)->context,
      video_mixer_v_src, video_mixer_f_src, &video_mixer->shader);
}

static gboolean
gst_gl_video_mixer_process_textures (GstGLMixer * mix, GPtrArray * frames,
    guint out_tex)
{
  GstGLVideoMixer *video_mixer = GST_GL_VIDEO_MIXER (mix);

  video_mixer->input_frames = frames;

  gst_gl_context_use_fbo_v2 (GST_GL_BASE_MIXER (mix)->context,
      GST_VIDEO_INFO_WIDTH (&GST_VIDEO_AGGREGATOR (mix)->info),
      GST_VIDEO_INFO_HEIGHT (&GST_VIDEO_AGGREGATOR (mix)->info),
      mix->fbo, mix->depthbuffer,
      out_tex, gst_gl_video_mixer_callback, (gpointer) video_mixer);

  return TRUE;
}

static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

static void
_init_vbo_indices (GstGLVideoMixer * mixer)
{
  const GstGLFuncs *gl = GST_GL_BASE_MIXER (mixer)->context->gl_vtable;

  if (!mixer->vbo_indices) {
    gl->GenBuffers (1, &mixer->vbo_indices);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, mixer->vbo_indices);
    gl->BufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices), indices,
        GL_STATIC_DRAW);
  }
}

static gboolean
_draw_checker_background (GstGLVideoMixer * video_mixer)
{
  GstGLMixer *mixer = GST_GL_MIXER (video_mixer);
  const GstGLFuncs *gl = GST_GL_BASE_MIXER (mixer)->context->gl_vtable;
  gint attr_position_loc = 0;

  /* *INDENT-OFF* */
  gfloat v_vertices[] = {
    -1.0,-1.0,-1.0f,
     1.0,-1.0,-1.0f,
     1.0, 1.0,-1.0f,
    -1.0, 1.0,-1.0f,
  };
  /* *INDENT-ON* */

  if (!video_mixer->checker) {
    if (!gst_gl_context_gen_shader (GST_GL_BASE_MIXER (mixer)->context,
            checker_v_src, checker_f_src, &video_mixer->checker))
      return FALSE;
  }

  gst_gl_shader_use (video_mixer->checker);
  attr_position_loc =
      gst_gl_shader_get_attribute_location (video_mixer->checker, "a_position");

  _init_vbo_indices (video_mixer);

  if (!video_mixer->checker_vbo) {
    gl->GenBuffers (1, &video_mixer->checker_vbo);
    gl->BindBuffer (GL_ARRAY_BUFFER, video_mixer->checker_vbo);
    gl->BufferData (GL_ARRAY_BUFFER, 4 * 3 * sizeof (GLfloat), v_vertices,
        GL_STATIC_DRAW);
  } else {
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, video_mixer->vbo_indices);
    gl->BindBuffer (GL_ARRAY_BUFFER, video_mixer->checker_vbo);
  }

  gl->VertexAttribPointer (attr_position_loc, 3, GL_FLOAT,
      GL_FALSE, 3 * sizeof (GLfloat), (void *) 0);

  gl->EnableVertexAttribArray (attr_position_loc);

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

  gl->DisableVertexAttribArray (attr_position_loc);
  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);

  return TRUE;
}

static gboolean
_draw_background (GstGLVideoMixer * video_mixer)
{
  GstGLMixer *mixer = GST_GL_MIXER (video_mixer);
  const GstGLFuncs *gl = GST_GL_BASE_MIXER (mixer)->context->gl_vtable;

  switch (video_mixer->background) {
    case GST_GL_VIDEO_MIXER_BACKGROUND_BLACK:
      gl->ClearColor (0.0, 0.0, 0.0, 1.0);
      gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      break;
    case GST_GL_VIDEO_MIXER_BACKGROUND_WHITE:
      gl->ClearColor (1.0, 1.0, 1.0, 1.0);
      gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      break;
    case GST_GL_VIDEO_MIXER_BACKGROUND_TRANSPARENT:
      gl->ClearColor (0.0, 0.0, 0.0, 0.0);
      gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      break;
    case GST_GL_VIDEO_MIXER_BACKGROUND_CHECKER:
      return _draw_checker_background (video_mixer);
      break;
    default:
      break;
  }

  return TRUE;
}

/* opengl scene, params: input texture (not the output mixer->texture) */
static void
gst_gl_video_mixer_callback (gpointer stuff)
{
  GstGLVideoMixer *video_mixer = GST_GL_VIDEO_MIXER (stuff);
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (stuff);
  GstGLMixer *mixer = GST_GL_MIXER (video_mixer);
  GstGLFuncs *gl = GST_GL_BASE_MIXER (mixer)->context->gl_vtable;

  GLint attr_position_loc = 0;
  GLint attr_texture_loc = 0;
  guint out_width, out_height;

  guint count = 0;

  out_width = GST_VIDEO_INFO_WIDTH (&vagg->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&vagg->info);

  gst_gl_context_clear_shader (GST_GL_BASE_MIXER (mixer)->context);
  gl->BindTexture (GL_TEXTURE_2D, 0);

  gl->Disable (GL_DEPTH_TEST);
  gl->Disable (GL_CULL_FACE);

  if (gl->GenVertexArrays) {
    if (!video_mixer->vao)
      gl->GenVertexArrays (1, &video_mixer->vao);
    gl->BindVertexArray (video_mixer->vao);
  }

  if (!_draw_background (video_mixer))
    return;

  gst_gl_shader_use (video_mixer->shader);

  attr_position_loc =
      gst_gl_shader_get_attribute_location (video_mixer->shader, "a_position");
  attr_texture_loc =
      gst_gl_shader_get_attribute_location (video_mixer->shader, "a_texCoord");

  gl->Enable (GL_BLEND);

  while (count < video_mixer->input_frames->len) {
    GstGLMixerFrameData *frame;
    GstGLVideoMixerPad *pad;
    GstVideoInfo *v_info;
    guint in_tex;
    guint in_width, in_height;

    /* *INDENT-OFF* */
    gfloat v_vertices[] = {
      -1.0,-1.0,-1.0f, 0.0f, 0.0f,
       1.0,-1.0,-1.0f, 1.0f, 0.0f,
       1.0, 1.0,-1.0f, 1.0f, 1.0f,
      -1.0, 1.0,-1.0f, 0.0f, 1.0f,
    };
    /* *INDENT-ON* */

    frame = g_ptr_array_index (video_mixer->input_frames, count);
    if (!frame) {
      GST_DEBUG ("skipping texture, null frame");
      count++;
      continue;
    }
    pad = (GstGLVideoMixerPad *) frame->pad;
    v_info = &GST_VIDEO_AGGREGATOR_PAD (pad)->info;
    in_width = GST_VIDEO_INFO_WIDTH (v_info);
    in_height = GST_VIDEO_INFO_HEIGHT (v_info);

    if (!frame->texture || in_width <= 0 || in_height <= 0
        || pad->alpha == 0.0f) {
      GST_DEBUG ("skipping texture:%u frame:%p width:%u height:%u alpha:%f",
          frame->texture, frame, in_width, in_height, pad->alpha);
      count++;
      continue;
    }

    in_tex = frame->texture;

    _init_vbo_indices (video_mixer);

    if (pad->geometry_change || !pad->vertex_buffer) {
      gint pad_width, pad_height;
      gfloat w, h;

      _mixer_pad_get_output_size (video_mixer, pad, &pad_width, &pad_height);

      w = ((gfloat) pad_width / (gfloat) out_width);
      h = ((gfloat) pad_height / (gfloat) out_height);

      /* top-left */
      v_vertices[0] = v_vertices[15] =
          2.0f * (gfloat) pad->xpos / (gfloat) out_width - 1.0f;
      /* bottom-left */
      v_vertices[1] = v_vertices[6] =
          2.0f * (gfloat) pad->ypos / (gfloat) out_height - 1.0f;
      /* top-right */
      v_vertices[5] = v_vertices[10] = v_vertices[0] + 2.0f * w;
      /* bottom-right */
      v_vertices[11] = v_vertices[16] = v_vertices[1] + 2.0f * h;
      GST_TRACE ("processing texture:%u dimensions:%ux%u, at %f,%f %fx%f with "
          "alpha:%f", in_tex, in_width, in_height, v_vertices[0], v_vertices[1],
          v_vertices[5], v_vertices[11], pad->alpha);

      if (!pad->vertex_buffer)
        gl->GenBuffers (1, &pad->vertex_buffer);

      gl->BindBuffer (GL_ARRAY_BUFFER, pad->vertex_buffer);

      gl->BufferData (GL_ARRAY_BUFFER, 4 * 5 * sizeof (GLfloat), v_vertices,
          GL_STATIC_DRAW);

      pad->geometry_change = FALSE;
    } else {
      gl->BindBuffer (GL_ARRAY_BUFFER, pad->vertex_buffer);
    }
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, video_mixer->vbo_indices);

    gl->BlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl->BlendEquation (GL_FUNC_ADD);

    gl->ActiveTexture (GL_TEXTURE0);
    gl->BindTexture (GL_TEXTURE_2D, in_tex);
    gst_gl_shader_set_uniform_1i (video_mixer->shader, "texture", 0);
    gst_gl_shader_set_uniform_1f (video_mixer->shader, "alpha", pad->alpha);

    gl->EnableVertexAttribArray (attr_position_loc);
    gl->EnableVertexAttribArray (attr_texture_loc);

    gl->VertexAttribPointer (attr_position_loc, 3, GL_FLOAT,
        GL_FALSE, 5 * sizeof (GLfloat), (void *) 0);

    gl->VertexAttribPointer (attr_texture_loc, 2, GL_FLOAT,
        GL_FALSE, 5 * sizeof (GLfloat), (void *) (3 * sizeof (GLfloat)));

    gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    ++count;
  }

  gl->DisableVertexAttribArray (attr_position_loc);
  gl->DisableVertexAttribArray (attr_texture_loc);

  if (gl->GenVertexArrays)
    gl->BindVertexArray (0);

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);
  gl->BindTexture (GL_TEXTURE_2D, 0);

  gl->Disable (GL_BLEND);

  gst_gl_context_clear_shader (GST_GL_BASE_MIXER (mixer)->context);
}
