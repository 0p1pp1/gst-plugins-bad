/*
 * GStreamer
 * Copyright (C) 2012-2014 Matthew Waters <ystree00@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "gl.h"
#include "gstglcolorconvert.h"

/**
 * SECTION:gstglcolorconvert
 * @short_description: an object that converts between color spaces/formats
 * @see_also: #GstGLUpload, #GstGLDownload, #GstGLMemory
 *
 * #GstGLColorConvert is an object that converts between color spaces and/or
 * formats using OpenGL Shaders.
 *
 * A #GstGLColorConvert can be created with gst_gl_color_convert_new().
 *
 * For handling stride scaling in the shader, see
 * gst_gl_color_convert_set_texture_scaling().
 */

#define USING_OPENGL(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_OPENGL)
#define USING_OPENGL3(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_OPENGL3)
#define USING_GLES(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES)
#define USING_GLES2(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES2)
#define USING_GLES3(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES3)

static void _do_convert (GstGLContext * context, GstGLColorConvert * convert);
static gboolean _init_convert (GstGLColorConvert * convert);
static gboolean _init_convert_fbo (GstGLColorConvert * convert);
static gboolean _gst_gl_color_convert_perform_unlocked (GstGLColorConvert *
    convert, GstGLMemory * in_tex[GST_VIDEO_MAX_PLANES],
    GstGLMemory * out_tex[GST_VIDEO_MAX_PLANES]);

static gboolean _do_convert_draw (GstGLContext * context,
    GstGLColorConvert * convert);

/* *INDENT-OFF* */

#define YUV_TO_RGB_COEFFICIENTS \
      "uniform vec3 offset;\n" \
      "uniform vec3 coeff1;\n" \
      "uniform vec3 coeff2;\n" \
      "uniform vec3 coeff3;\n"

/* FIXME: use the colormatrix support from videoconvert */

/* BT. 601 standard with the following ranges:
 * Y = [16..235] (of 255)
 * Cb/Cr = [16..240] (of 255)
 */
static const gfloat from_yuv_bt601_offset[] = {-0.0625, -0.5, -0.5};
static const gfloat from_yuv_bt601_rcoeff[] = {1.164, 0.000, 1.596};
static const gfloat from_yuv_bt601_gcoeff[] = {1.164,-0.391,-0.813};
static const gfloat from_yuv_bt601_bcoeff[] = {1.164, 2.018, 0.000};

/* BT. 709 standard with the following ranges:
 * Y = [16..235] (of 255)
 * Cb/Cr = [16..240] (of 255)
 */
static const gfloat from_yuv_bt709_offset[] = {-0.0625, -0.5, -0.5};
static const gfloat from_yuv_bt709_rcoeff[] = {1.164, 0.000, 1.787};
static const gfloat from_yuv_bt709_gcoeff[] = {1.164,-0.213,-0.531};
static const gfloat from_yuv_bt709_bcoeff[] = {1.164,2.112, 0.000};

#define RGB_TO_YUV_COEFFICIENTS \
      "uniform vec3 offset;\n" \
      "uniform vec3 coeff1;\n" \
      "uniform vec3 coeff2;\n" \
      "uniform vec3 coeff3;\n"

/* Matrix inverses of the color matrices found above */
/* BT. 601 standard with the following ranges:
 * Y = [16..235] (of 255)
 * Cb/Cr = [16..240] (of 255)
 */
static const gfloat from_rgb_bt601_offset[] = {0.0625, 0.5, 0.5};
static const gfloat from_rgb_bt601_ycoeff[] = {0.256816, 0.504154, 0.0979137};
static const gfloat from_rgb_bt601_ucoeff[] = {-0.148246, -0.29102, 0.439266};
static const gfloat from_rgb_bt601_vcoeff[] = {0.439271, -0.367833, -0.071438};

/* BT. 709 standard with the following ranges:
 * Y = [16..235] (of 255)
 * Cb/Cr = [16..240] (of 255)
 */
static const gfloat from_rgb_bt709_offset[] = {0.0625, 0.5, 0.5};
static const gfloat from_rgb_bt709_ycoeff[] = { 0.182604, 0.614526, 0.061976 };
static const gfloat from_rgb_bt709_ucoeff[] = { -0.100640, -0.338688, 0.439327 };
static const gfloat from_rgb_bt709_vcoeff[] = { 0.440654, -0.400285, -0.040370 };


/** GRAY16 to RGB conversion 
 *  data transfered as GL_LUMINANCE_ALPHA then convert back to GRAY16 
 *  high byte weight as : 255*256/65535 
 *  ([0~1] denormalize to [0~255],shift to high byte,normalize to [0~1])
 *  low byte weight as : 255/65535 (similar)
 * */
#define COMPOSE_WEIGHT \
    "const vec2 compose_weight = vec2(0.996109, 0.003891);\n"

/* Channel reordering for XYZ <-> ZYX conversion */
static const gchar frag_REORDER[] =
      "#ifdef GL_ES\n"
      "precision mediump float;\n"
      "#endif\n"
      "varying vec2 v_texcoord;\n"
      "uniform sampler2D tex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      "void main(void)\n"
      "{\n"
      " vec4 t = texture2D(tex, v_texcoord * tex_scale0);\n"
      " %s;\n" /* clobber alpha channel? */
      " gl_FragColor = vec4(t.%c, t.%c, t.%c, t.%c);\n"
      "}";

/** GRAY16 to RGB conversion 
 *  data transfered as GL_LUMINANCE_ALPHA then convert back to GRAY16 
 *  high byte weight as : 255*256/65535 
 *  ([0~1] denormalize to [0~255],shift to high byte,normalize to [0~1])
 *  low byte weight as : 255/65535 (similar)
 * */
static const gchar frag_COMPOSE[] =
      "#ifdef GL_ES\n"
      "precision mediump float;\n"
      "#endif\n"
      "varying vec2 v_texcoord;\n"
      "uniform sampler2D tex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      COMPOSE_WEIGHT
      "void main(void)\n"
      "{\n"
      " float r, g, b, a;\n"
      " vec4 t = texture2D(tex, v_texcoord * tex_scale0);\n"
      " r = dot(t.%c%c, compose_weight);"
      " g = r;\n"
      " b = r;\n"
      " a = 1.0;\n"
      " gl_FragColor = vec4(%c, %c, %c, %c);\n"
      "}";

static const char frag_AYUV_to_RGB[] =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D tex;\n"
    "uniform vec2 tex_scale0;\n"
    "uniform vec2 tex_scale1;\n"
    "uniform vec2 tex_scale2;\n"
    YUV_TO_RGB_COEFFICIENTS
    "void main(void) {\n"
    "  float r,g,b,a;\n"
    "  vec4 texel;\n"
    "  texel = texture2D(tex, v_texcoord * tex_scale0);\n"
    "  texel.gba += offset;\n"
    "  r = dot(texel.gba, coeff1);\n"
    "  g = dot(texel.gba, coeff2);\n"
    "  b = dot(texel.gba, coeff3);\n"
    "  a = texel.r;\n"
    "  gl_FragColor=vec4(%c,%c,%c,%c);\n"
    "}";

static const gchar frag_RGB_to_AYUV[] =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D tex;\n"
    RGB_TO_YUV_COEFFICIENTS
    "void main(void) {\n"
    "  vec4 texel;\n"
    "  float y, u, v, a;\n"
    "  texel = texture2D(tex, v_texcoord).%c%c%c%c;\n"
    "  y = dot(texel.rgb, coeff1);\n"
    "  u = dot(texel.rgb, coeff2);\n"
    "  v = dot(texel.rgb, coeff3);\n"
    "  y += offset.x;\n"
    "  u += offset.y;\n"
    "  v += offset.z;\n"
    "  a = %s;\n"
    "  gl_FragColor = vec4(a,y,u,v);\n"
    "}\n";

/** YUV to RGB conversion */
static const char frag_PLANAR_YUV_to_RGB[] =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D Ytex, Utex, Vtex;\n"
    "uniform vec2 tex_scale0;\n"
    "uniform vec2 tex_scale1;\n"
    "uniform vec2 tex_scale2;\n"
    YUV_TO_RGB_COEFFICIENTS
    "void main(void) {\n"
    "  float r, g, b, a;\n"
    "  vec3 yuv;\n"
    "  yuv.x = texture2D(Ytex,v_texcoord * tex_scale0).r;\n"
    "  yuv.y = texture2D(Utex,v_texcoord * tex_scale1).r;\n"
    "  yuv.z = texture2D(Vtex,v_texcoord * tex_scale2).r;\n"
    "  yuv += offset;\n"
    "  r = dot(yuv, coeff1);\n"
    "  g = dot(yuv, coeff2);\n"
    "  b = dot(yuv, coeff3);\n"
    "  a = 1.0;\n"
    "  gl_FragColor = vec4(%c, %c, %c, %c);\n"
    "}\n";

static const gchar frag_RGB_to_PLANAR_YUV[] =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D tex;\n"
    "uniform float w, h;\n"
    RGB_TO_YUV_COEFFICIENTS
    "void main(void) {\n"
    "  float y, u, v;\n"
    "  vec4 texel = texture2D(tex, v_texcoord).%c%c%c%c;\n"
    "  vec4 texel2 = texture2D(tex, v_texcoord * 2.0).%c%c%c%c;\n"
    "  y = dot(texel.rgb, coeff1);\n"
    "  u = dot(texel2.rgb, coeff2);\n"
    "  v = dot(texel2.rgb, coeff3);\n"
    "  y += offset.x;\n"
    "  u += offset.y;\n"
    "  v += offset.z;\n"
    "  gl_FragData[0] = vec4(y, 0.0, 0.0, 1.0);\n"
    "  gl_FragData[1] = vec4(u, 0.0, 0.0, 1.0);\n"
    "  gl_FragData[2] = vec4(v, 0.0, 0.0, 1.0);\n"
    "}\n";

/** NV12/NV21 to RGB conversion */
static const char frag_NV12_NV21_to_RGB[] = {
      "#ifdef GL_ES\n"
      "precision mediump float;\n"
      "#endif\n"
      "varying vec2 v_texcoord;\n"
      "uniform sampler2D Ytex,UVtex;\n"
      "uniform vec2 tex_scale0;\n"
      "uniform vec2 tex_scale1;\n"
      "uniform vec2 tex_scale2;\n"
      YUV_TO_RGB_COEFFICIENTS
      "void main(void) {\n"
      "  float r, g, b, a;\n"
      "  vec3 yuv;\n"
      "  yuv.x=texture2D(Ytex, v_texcoord * tex_scale0).r;\n"
      "  yuv.yz=texture2D(UVtex, v_texcoord * tex_scale1).%c%c;\n"
      "  yuv += offset;\n"
      "  r = dot(yuv, coeff1);\n"
      "  g = dot(yuv, coeff2);\n"
      "  b = dot(yuv, coeff3);\n"
      "  a = 1.0;\n"
      "  gl_FragColor=vec4(%c, %c, %c, %c);\n"
      "}"
};

/* YUY2:r,g,a
   UYVY:a,b,r */
static const gchar frag_YUY2_UYVY_to_RGB[] =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D Ytex, UVtex;\n"
    "uniform vec2 tex_scale0;\n"
    "uniform vec2 tex_scale1;\n"
    "uniform vec2 tex_scale2;\n"
    YUV_TO_RGB_COEFFICIENTS
    "void main(void) {\n"
    "  vec3 yuv;\n"
    "  float r, g, b, a;\n"
    "  yuv.x = texture2D(Ytex, v_texcoord * tex_scale0).%c;\n"
    "  yuv.yz = texture2D(UVtex, v_texcoord * tex_scale1).%c%c;\n"
    "  yuv += offset;\n"
    "  r = dot(yuv, coeff1);\n"
    "  g = dot(yuv, coeff2);\n"
    "  b = dot(yuv, coeff3);\n"
    "  a = 1.0;\n"
    "  gl_FragColor = vec4(%c, %c, %c, %c);\n"
    "}\n";

static const gchar frag_RGB_to_YUY2_UYVY[] =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D tex;\n"
    "uniform float width;\n"
    RGB_TO_YUV_COEFFICIENTS
    "void main(void) {\n"
    "  vec4 texel1, texel2;\n"
    "  float fx, fy, y1, y2, u, v;\n"
    "  fx = v_texcoord.x;\n"
    "  fy = v_texcoord.y;\n"
    "  texel1 = texture2D(tex, vec2(fx*2.0,     fy)).%c%c%c%c;\n"
    "  texel2 = texture2D(tex, vec2(fx*2.0+1.0 / width, fy)).%c%c%c%c;\n"
    "  y1 = dot(texel1.rgb, coeff1);\n"
    "  y2 = dot(texel2.rgb, coeff1);\n"
    "  u = dot(texel1.rgb, coeff2);\n"
    "  v = dot(texel1.rgb, coeff3);\n"
    "  y1 += offset.x;\n"
    "  y2 += offset.x;\n"
    "  u += offset.y;\n"
    "  v += offset.z;\n"
    "  gl_FragColor = vec4(%s);\n"
    "}\n";

static const gchar text_vertex_shader[] =
    "attribute vec4 a_position;   \n"
    "attribute vec2 a_texcoord;   \n"
    "varying vec2 v_texcoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = a_position; \n"
    "   v_texcoord = a_texcoord;  \n"
    "}                            \n";

/* *INDENT-ON* */

struct ConvertInfo
{
  gint in_n_textures;
  gint out_n_textures;
  gchar *frag_prog;
  const gchar *shader_tex_names[GST_VIDEO_MAX_PLANES];
  gfloat shader_scaling[GST_VIDEO_MAX_PLANES][2];
  gfloat *cms_offset;
  gfloat *cms_coeff1;           /* r,y */
  gfloat *cms_coeff2;           /* g,u */
  gfloat *cms_coeff3;           /* b,v */
};

struct _GstGLColorConvertPrivate
{
  int n_textures;
  gboolean result;

    gboolean (*draw) (GstGLContext * context, GstGLColorConvert * download);

  struct ConvertInfo convert_info;

  GstGLMemory *scratch;
  GstGLMemory *out_temp[GST_VIDEO_MAX_PLANES];
};

GST_DEBUG_CATEGORY_STATIC (gst_gl_color_convert_debug);
#define GST_CAT_DEFAULT gst_gl_color_convert_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_color_convert_debug, "glconvert", 0, "convert");

G_DEFINE_TYPE_WITH_CODE (GstGLColorConvert, gst_gl_color_convert,
    GST_TYPE_OBJECT, DEBUG_INIT);
static void gst_gl_color_convert_finalize (GObject * object);
static void gst_gl_color_convert_reset (GstGLColorConvert * convert);

#define GST_GL_COLOR_CONVERT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GST_TYPE_GL_COLOR_CONVERT, GstGLColorConvertPrivate))

static void
gst_gl_color_convert_class_init (GstGLColorConvertClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLColorConvertPrivate));

  G_OBJECT_CLASS (klass)->finalize = gst_gl_color_convert_finalize;
}

static void
gst_gl_color_convert_init (GstGLColorConvert * convert)
{
  convert->priv = GST_GL_COLOR_CONVERT_GET_PRIVATE (convert);

  g_mutex_init (&convert->lock);
}

/**
 * gst_gl_color_convert_new:
 * @context: a #GstGLContext
 *
 * Returns: a new #GstGLColorConvert object
 */
GstGLColorConvert *
gst_gl_color_convert_new (GstGLContext * context)
{
  GstGLColorConvert *convert;
  GstGLColorConvertPrivate *priv;

  convert = g_object_new (GST_TYPE_GL_COLOR_CONVERT, NULL);

  convert->context = gst_object_ref (context);
  priv = convert->priv;

  priv->draw = _do_convert_draw;
  gst_video_info_set_format (&convert->in_info, GST_VIDEO_FORMAT_ENCODED, 0, 0);
  gst_video_info_set_format (&convert->out_info, GST_VIDEO_FORMAT_ENCODED, 0,
      0);

  return convert;
}

static void
gst_gl_color_convert_finalize (GObject * object)
{
  GstGLColorConvert *convert;

  convert = GST_GL_COLOR_CONVERT (object);

  gst_gl_color_convert_reset (convert);

  if (convert->context) {
    gst_object_unref (convert->context);
    convert->context = NULL;
  }

  g_mutex_clear (&convert->lock);

  G_OBJECT_CLASS (gst_gl_color_convert_parent_class)->finalize (object);
}

static void
gst_gl_color_convert_reset (GstGLColorConvert * convert)
{
  guint i;

  if (convert->fbo || convert->depth_buffer) {
    gst_gl_context_del_fbo (convert->context, convert->fbo,
        convert->depth_buffer);
    convert->fbo = 0;
    convert->depth_buffer = 0;
  }

  for (i = 0; i < convert->priv->convert_info.out_n_textures; i++) {
    if (convert->priv->out_temp[i])
      gst_memory_unref ((GstMemory *) convert->priv->out_temp[i]);
    convert->priv->out_temp[i] = NULL;
  }

  if (convert->shader) {
    gst_object_unref (convert->shader);
    convert->shader = NULL;
  }

  if (convert->priv->scratch) {
    gst_memory_unref ((GstMemory *) convert->priv->scratch);
    convert->priv->scratch = NULL;
  }
}

static void
_gst_gl_color_convert_set_format_unlocked (GstGLColorConvert * convert,
    GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  g_return_if_fail (convert != NULL);
  g_return_if_fail (in_info);
  g_return_if_fail (out_info);
  g_return_if_fail (GST_VIDEO_INFO_FORMAT (in_info) !=
      GST_VIDEO_FORMAT_UNKNOWN);
  g_return_if_fail (GST_VIDEO_INFO_FORMAT (in_info) !=
      GST_VIDEO_FORMAT_ENCODED);
  g_return_if_fail (GST_VIDEO_INFO_FORMAT (out_info) !=
      GST_VIDEO_FORMAT_UNKNOWN);
  g_return_if_fail (GST_VIDEO_INFO_FORMAT (out_info) !=
      GST_VIDEO_FORMAT_ENCODED);

  if (gst_video_info_is_equal (&convert->in_info, in_info) &&
      gst_video_info_is_equal (&convert->out_info, out_info))
    return;

  gst_gl_color_convert_reset (convert);
  convert->in_info = *in_info;
  convert->out_info = *out_info;
  convert->initted = FALSE;
}

/**
 * gst_gl_color_convert_set_format:
 * @convert: a #GstGLColorConvert
 * @in_info: input #GstVideoInfo
 * @out_info: output #GstVideoInfo
 *
 * Initializes @convert with the information required for conversion.
 */
void
gst_gl_color_convert_set_format (GstGLColorConvert * convert,
    GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  g_mutex_lock (&convert->lock);
  _gst_gl_color_convert_set_format_unlocked (convert, in_info, out_info);
  g_mutex_unlock (&convert->lock);
}

/**
 * gst_gl_color_convert_perform:
 * @convert: a #GstGLColorConvert
 * @in_tex: the texture ids for input formatted according to in_info
 * @out_tex: the texture ids for output formatted according to out_info
 *
 * Converts the data contained in in_tex into out_tex using the formats
 * specified by the #GstVideoInfo<!--  -->s passed to
 * gst_gl_color_convert_set_format() 
 *
 * Returns: whether the conversion was successful
 */
gboolean
gst_gl_color_convert_perform (GstGLColorConvert * convert,
    GstGLMemory * in_tex[GST_VIDEO_MAX_PLANES],
    GstGLMemory * out_tex[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;

  g_return_val_if_fail (convert != NULL, FALSE);

  g_mutex_lock (&convert->lock);
  ret = _gst_gl_color_convert_perform_unlocked (convert, in_tex, out_tex);
  g_mutex_unlock (&convert->lock);

  return ret;
}

/**
 * gst_gl_color_convert_set_texture_scaling:
 * @convert: a #GstGLColorConvert
 * @scaling: array of texture scaling coefficients stored in width, height
 *           order per texture being converted.
 *
 * Scales the input textures by the given amount.  Useful for performing stride
 * scaling in a shader on OpenGL platforms that do not support
 * GL_PIXEL_[UN]PACK_LENGTH such as GL|ES 2.0.
 */
void
gst_gl_color_convert_set_texture_scaling (GstGLColorConvert * convert,
    gfloat scaling[GST_VIDEO_MAX_PLANES][2])
{
  guint i;

  g_return_if_fail (convert != NULL);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    convert->priv->convert_info.shader_scaling[i][0] = scaling[i][0];
    convert->priv->convert_info.shader_scaling[i][1] = scaling[i][1];
  }
}

static gboolean
_gst_gl_color_convert_perform_unlocked (GstGLColorConvert * convert,
    GstGLMemory * in_tex[GST_VIDEO_MAX_PLANES],
    GstGLMemory * out_tex[GST_VIDEO_MAX_PLANES])
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (in_tex, FALSE);
  g_return_val_if_fail (out_tex, FALSE);

  convert->in_tex[0] = in_tex[0];
  convert->in_tex[1] = in_tex[1];
  convert->in_tex[2] = in_tex[2];
  convert->in_tex[3] = in_tex[3];
  convert->out_tex[0] = out_tex[0];
  convert->out_tex[1] = out_tex[1];
  convert->out_tex[2] = out_tex[2];
  convert->out_tex[3] = out_tex[3];

  GST_LOG ("Converting %s from %p,%p,%p,%p into %s using %p,%p,%p,%p",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->in_info)),
      in_tex[0], in_tex[1], in_tex[2], in_tex[3],
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->out_info)),
      out_tex[0], out_tex[1], out_tex[2], out_tex[3]);

  gst_gl_context_thread_add (convert->context,
      (GstGLContextThreadFunc) _do_convert, convert);

  return convert->priv->result;
}

static inline gboolean
_is_RGBx (GstVideoFormat v_format)
{
  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xBGR:
      return TRUE;
    default:
      return FALSE;
  }
}

static inline gchar
_index_to_shader_swizzle (int idx)
{
  switch (idx) {
    case 0:
      return 'r';
    case 1:
      return 'g';
    case 2:
      return 'b';
    case 3:
      return 'a';
    default:
      return '#';
  }
}

/* attempts to transform expected to want using swizzling */
static gchar *
_RGB_pixel_order (const gchar * expected, const gchar * wanted)
{
  GString *ret = g_string_sized_new (4);
  gchar *expect, *want;
  int len;

  if (g_ascii_strcasecmp (expected, wanted) == 0)
    return g_ascii_strdown (expected, -1);

  expect = g_ascii_strdown (expected, -1);
  want = g_ascii_strdown (wanted, -1);

  /* pad want with 'a's */
  if ((len = strlen (want)) < 4) {
    gchar *new_want = g_strndup (want, 4);
    while (len < 4) {
      new_want[len] = 'a';
      len++;
    }
    g_free (want);
    want = new_want;
  }

  /* pad expect with 'a's */
  if ((len = strlen (expect)) < 4) {
    gchar *new_expect = g_strndup (expect, 4);
    while (len < 4) {
      new_expect[len] = 'a';
      len++;
    }
    g_free (expect);
    expect = new_expect;
  }

  /* build the swizzle format */
  while (want && want[0] != '\0') {
    gchar *val;
    gint idx;
    gchar needle = want[0];

    if (needle == 'x')
      needle = 'a';

    if (!(val = strchr (expect, needle))
        && needle == 'a' && !(val = strchr (expect, 'x')))
      goto fail;

    idx = (gint) (val - expect);

    ret = g_string_append_c (ret, _index_to_shader_swizzle (idx));
    want = &want[1];
  }

  return g_string_free (ret, FALSE);

fail:
  g_string_free (ret, TRUE);
  return NULL;
}

static void
_RGB_to_RGB (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GstVideoFormat in_format = GST_VIDEO_INFO_FORMAT (&convert->in_info);
  const gchar *in_format_str = gst_video_format_to_string (in_format);
  GstVideoFormat out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);
  const gchar *out_format_str = gst_video_format_to_string (out_format);
  gchar *pixel_order = _RGB_pixel_order (in_format_str, out_format_str);
  const gchar *alpha = "";

  info->in_n_textures = 1;
  info->out_n_textures = 1;
  if (_is_RGBx (in_format))
    alpha = "t.a = 1.0";
  info->frag_prog = g_strdup_printf (frag_REORDER, alpha, pixel_order[0],
      pixel_order[1], pixel_order[2], pixel_order[3]);
  info->shader_tex_names[0] = "tex";
  info->shader_scaling[0][0] = 1.0f;
  info->shader_scaling[0][1] = 1.0f;
}

static void
_YUV_to_RGB (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GstVideoFormat out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);
  const gchar *out_format_str = gst_video_format_to_string (out_format);
  gchar *pixel_order = _RGB_pixel_order ("rgba", out_format_str);

  info->out_n_textures = 1;

  info->shader_scaling[0][0] = 1.0f;
  info->shader_scaling[0][1] = 1.0f;
  info->shader_scaling[1][0] = 1.0f;
  info->shader_scaling[1][1] = 1.0f;
  info->shader_scaling[2][0] = 1.0f;
  info->shader_scaling[2][1] = 1.0f;

  switch (GST_VIDEO_INFO_FORMAT (&convert->in_info)) {
    case GST_VIDEO_FORMAT_AYUV:
      info->frag_prog = g_strdup_printf (frag_AYUV_to_RGB, pixel_order[0],
          pixel_order[1], pixel_order[2], pixel_order[3]);
      info->in_n_textures = 1;
      info->shader_tex_names[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      info->frag_prog = g_strdup_printf (frag_PLANAR_YUV_to_RGB, pixel_order[0],
          pixel_order[1], pixel_order[2], pixel_order[3]);
      info->in_n_textures = 3;
      info->shader_tex_names[0] = "Ytex";
      info->shader_tex_names[1] = "Utex";
      info->shader_tex_names[2] = "Vtex";
      break;
    case GST_VIDEO_FORMAT_YV12:
      info->frag_prog = g_strdup_printf (frag_PLANAR_YUV_to_RGB, pixel_order[0],
          pixel_order[1], pixel_order[2], pixel_order[3]);
      info->in_n_textures = 3;
      info->shader_tex_names[0] = "Ytex";
      info->shader_tex_names[1] = "Vtex";
      info->shader_tex_names[2] = "Utex";
      break;
    case GST_VIDEO_FORMAT_YUY2:
      info->frag_prog = g_strdup_printf (frag_YUY2_UYVY_to_RGB, 'r', 'g', 'a',
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      info->in_n_textures = 1;
      info->shader_tex_names[1] = "Ytex";
      info->shader_tex_names[0] = "UVtex";
      convert->priv->scratch =
          (GstGLMemory *) gst_gl_memory_alloc (convert->context,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA,
          GST_VIDEO_INFO_WIDTH (&convert->in_info),
          GST_VIDEO_INFO_HEIGHT (&convert->in_info),
          GST_VIDEO_INFO_PLANE_STRIDE (&convert->in_info, 0));
      break;
    case GST_VIDEO_FORMAT_NV12:
      info->frag_prog = g_strdup_printf (frag_NV12_NV21_to_RGB, 'r', 'a',
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      info->in_n_textures = 2;
      info->shader_tex_names[0] = "Ytex";
      info->shader_tex_names[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_NV21:
      info->frag_prog = g_strdup_printf (frag_NV12_NV21_to_RGB, 'a', 'r',
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      info->in_n_textures = 2;
      info->shader_tex_names[0] = "Ytex";
      info->shader_tex_names[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_UYVY:
      info->frag_prog = g_strdup_printf (frag_YUY2_UYVY_to_RGB, 'a', 'r', 'b',
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      info->in_n_textures = 1;
      info->shader_tex_names[1] = "Ytex";
      info->shader_tex_names[0] = "UVtex";
      convert->priv->scratch =
          (GstGLMemory *) gst_gl_memory_alloc (convert->context,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA,
          GST_VIDEO_INFO_WIDTH (&convert->in_info),
          GST_VIDEO_INFO_HEIGHT (&convert->in_info),
          GST_VIDEO_INFO_PLANE_STRIDE (&convert->in_info, 0));
      break;
    default:
      break;
  }

  if (gst_video_colorimetry_matches (&convert->in_info.colorimetry,
          GST_VIDEO_COLORIMETRY_BT709)) {
    info->cms_offset = (gfloat *) from_yuv_bt709_offset;
    info->cms_coeff1 = (gfloat *) from_yuv_bt709_rcoeff;
    info->cms_coeff2 = (gfloat *) from_yuv_bt709_gcoeff;
    info->cms_coeff3 = (gfloat *) from_yuv_bt709_bcoeff;
  } else {
    /* defaults/bt601 */
    info->cms_offset = (gfloat *) from_yuv_bt601_offset;
    info->cms_coeff1 = (gfloat *) from_yuv_bt601_rcoeff;
    info->cms_coeff2 = (gfloat *) from_yuv_bt601_gcoeff;
    info->cms_coeff3 = (gfloat *) from_yuv_bt601_bcoeff;
  }

  g_free (pixel_order);
}

static void
_RGB_to_YUV (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GstVideoFormat in_format = GST_VIDEO_INFO_FORMAT (&convert->in_info);
  const gchar *in_format_str = gst_video_format_to_string (in_format);
  gchar *pixel_order = _RGB_pixel_order (in_format_str, "rgba");
  const gchar *alpha;

  info->frag_prog = NULL;
  info->in_n_textures = 1;

  info->shader_tex_names[0] = "tex";

  info->shader_scaling[0][0] = 1.0f;
  info->shader_scaling[0][1] = 1.0f;
  info->shader_scaling[1][0] = 1.0f;
  info->shader_scaling[1][1] = 1.0f;
  info->shader_scaling[2][0] = 1.0f;
  info->shader_scaling[2][1] = 1.0f;

  switch (GST_VIDEO_INFO_FORMAT (&convert->out_info)) {
    case GST_VIDEO_FORMAT_AYUV:
      alpha = _is_RGBx (in_format) ? "1.0" : "texel.a";
      info->frag_prog = g_strdup_printf (frag_RGB_to_AYUV, pixel_order[0],
          pixel_order[1], pixel_order[2], pixel_order[3], alpha);
      info->out_n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      info->frag_prog = g_strdup_printf (frag_RGB_to_PLANAR_YUV,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      info->out_n_textures = 3;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      info->frag_prog = g_strdup_printf (frag_RGB_to_YUY2_UYVY,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          "y1,u,y2,v");
      info->out_n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      info->frag_prog = g_strdup_printf (frag_RGB_to_YUY2_UYVY,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          "u,y1,v,y2");
      info->out_n_textures = 1;
      break;
    default:
      break;
  }

  if (gst_video_colorimetry_matches (&convert->in_info.colorimetry,
          GST_VIDEO_COLORIMETRY_BT709)) {
    info->cms_offset = (gfloat *) from_rgb_bt709_offset;
    info->cms_coeff1 = (gfloat *) from_rgb_bt709_ycoeff;
    info->cms_coeff2 = (gfloat *) from_rgb_bt709_ucoeff;
    info->cms_coeff3 = (gfloat *) from_rgb_bt709_vcoeff;
  } else {
    /* defaults/bt601 */
    info->cms_offset = (gfloat *) from_rgb_bt601_offset;
    info->cms_coeff1 = (gfloat *) from_rgb_bt601_ycoeff;
    info->cms_coeff2 = (gfloat *) from_rgb_bt601_ucoeff;
    info->cms_coeff3 = (gfloat *) from_rgb_bt601_vcoeff;
  }

  g_free (pixel_order);
}

static void
_RGB_to_GRAY (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GstVideoFormat in_format = GST_VIDEO_INFO_FORMAT (&convert->in_info);
  const gchar *in_format_str = gst_video_format_to_string (in_format);
  gchar *pixel_order = _RGB_pixel_order (in_format_str, "rgba");
  const gchar *alpha = "";

  info->in_n_textures = 1;
  info->out_n_textures = 1;
  info->shader_tex_names[0] = "tex";
  info->shader_scaling[0][0] = 1.0f;
  info->shader_scaling[0][1] = 1.0f;
  info->shader_scaling[1][0] = 1.0f;
  info->shader_scaling[1][1] = 1.0f;
  info->shader_scaling[2][0] = 1.0f;
  info->shader_scaling[2][1] = 1.0f;

  if (_is_RGBx (in_format))
    alpha = "t.a = 1.0";

  switch (GST_VIDEO_INFO_FORMAT (&convert->out_info)) {
    case GST_VIDEO_FORMAT_GRAY8:
      info->frag_prog = g_strdup_printf (frag_REORDER, alpha, pixel_order[0],
          pixel_order[0], pixel_order[0], pixel_order[3]);
      break;
    default:
      break;
  }
}

static void
_GRAY_to_RGB (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GstVideoFormat out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);
  const gchar *out_format_str = gst_video_format_to_string (out_format);
  gchar *pixel_order = _RGB_pixel_order ("rgba", out_format_str);

  info->in_n_textures = 1;
  info->out_n_textures = 1;
  info->shader_tex_names[0] = "tex";
  info->shader_scaling[0][0] = 1.0f;
  info->shader_scaling[0][1] = 1.0f;
  info->shader_scaling[1][0] = 1.0f;
  info->shader_scaling[1][1] = 1.0f;
  info->shader_scaling[2][0] = 1.0f;
  info->shader_scaling[2][1] = 1.0f;

  switch (GST_VIDEO_INFO_FORMAT (&convert->in_info)) {
    case GST_VIDEO_FORMAT_GRAY8:
      info->frag_prog = g_strdup_printf (frag_REORDER, "", pixel_order[0],
          pixel_order[1], pixel_order[2], pixel_order[3]);
      break;
    case GST_VIDEO_FORMAT_GRAY16_LE:
      info->frag_prog = g_strdup_printf (frag_COMPOSE, 'a', 'r',
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      break;
    case GST_VIDEO_FORMAT_GRAY16_BE:
      info->frag_prog = g_strdup_printf (frag_COMPOSE, 'r', 'a',
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      break;
    default:
      break;
  }
}

/* Called in the gl thread */
static gboolean
_init_convert (GstGLColorConvert * convert)
{
  GstGLFuncs *gl;
  gboolean res;
  struct ConvertInfo *info = &convert->priv->convert_info;
  gint i;

  gl = convert->context->gl_vtable;

  if (convert->initted)
    return TRUE;

  GST_INFO ("Initializing color conversion from %s to %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->out_info)));

  if (!gl->CreateProgramObject && !gl->CreateProgram) {
    gst_gl_context_set_error (convert->context,
        "Cannot perform color conversion without OpenGL shaders");
    goto error;
  }

  if (GST_VIDEO_INFO_IS_RGB (&convert->in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (&convert->out_info)) {
      _RGB_to_RGB (convert);
    }
  }

  if (GST_VIDEO_INFO_IS_YUV (&convert->in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (&convert->out_info)) {
      _YUV_to_RGB (convert);
    }
  }

  if (GST_VIDEO_INFO_IS_RGB (&convert->in_info)) {
    if (GST_VIDEO_INFO_IS_YUV (&convert->out_info)) {
      _RGB_to_YUV (convert);
    }
  }

  if (GST_VIDEO_INFO_IS_RGB (&convert->in_info)) {
    if (GST_VIDEO_INFO_IS_GRAY (&convert->out_info)) {
      _RGB_to_GRAY (convert);
    }
  }

  if (GST_VIDEO_INFO_IS_GRAY (&convert->in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (&convert->out_info)) {
      _GRAY_to_RGB (convert);
    }
  }

  if (!info->frag_prog || info->in_n_textures == 0 || info->out_n_textures == 0)
    goto unhandled_format;

  /* multiple draw targets not supported on GLES2...yet */
  if (info->out_n_textures > 1 && (!gl->DrawBuffers ||
          USING_GLES2 (convert->context))) {
    g_free (info->frag_prog);
    GST_ERROR ("Conversion requires output to multiple draw buffers");
    goto incompatible_api;
  }

  res =
      gst_gl_context_gen_shader (convert->context, text_vertex_shader,
      info->frag_prog, &convert->shader);
  g_free (info->frag_prog);
  if (!res)
    goto error;

  convert->shader_attr_position_loc =
      gst_gl_shader_get_attribute_location (convert->shader, "a_position");
  convert->shader_attr_texture_loc =
      gst_gl_shader_get_attribute_location (convert->shader, "a_texcoord");

  gst_gl_shader_use (convert->shader);

  if (info->cms_offset && info->cms_coeff1
      && info->cms_coeff2 && info->cms_coeff3) {
    gst_gl_shader_set_uniform_3fv (convert->shader, "offset", 1,
        info->cms_offset);
    gst_gl_shader_set_uniform_3fv (convert->shader, "coeff1", 1,
        info->cms_coeff1);
    gst_gl_shader_set_uniform_3fv (convert->shader, "coeff2", 1,
        info->cms_coeff2);
    gst_gl_shader_set_uniform_3fv (convert->shader, "coeff3", 1,
        info->cms_coeff3);
  }

  for (i = info->in_n_textures; i >= 0; i--) {
    gchar *scale_name = g_strdup_printf ("tex_scale%u", i);

    if (info->shader_tex_names[i])
      gst_gl_shader_set_uniform_1i (convert->shader, info->shader_tex_names[i],
          i);
    gst_gl_shader_set_uniform_2fv (convert->shader, scale_name, 1,
        info->shader_scaling[i]);

    g_free (scale_name);
  }

  gst_gl_shader_set_uniform_1f (convert->shader, "width",
      GST_VIDEO_INFO_WIDTH (&convert->in_info));

  gst_gl_context_clear_shader (convert->context);

  if (!_init_convert_fbo (convert)) {
    goto error;
  }

  gl->BindTexture (GL_TEXTURE_2D, 0);

  convert->initted = TRUE;

  return TRUE;

unhandled_format:
  gst_gl_context_set_error (convert->context,
      "Don't know how to convert from %s to %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->out_info)));

error:
  return FALSE;

incompatible_api:
  {
    gst_gl_context_set_error (convert->context,
        "Converting from %s to %s requires "
        "functionality that the current OpenGL setup does not support",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->in_info)),
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT
            (&convert->out_info)));
    return FALSE;
  }
}


/* called by _init_convert (in the gl thread) */
static gboolean
_init_convert_fbo (GstGLColorConvert * convert)
{
  GstGLFuncs *gl;
  guint out_width, out_height;
  GLuint fake_texture = 0;      /* a FBO must hava texture to init */

  gl = convert->context->gl_vtable;

  out_width = GST_VIDEO_INFO_WIDTH (&convert->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&convert->out_info);

  if (!gl->GenFramebuffers) {
    /* turn off the pipeline because Frame buffer object is a not present */
    gst_gl_context_set_error (convert->context,
        "Context, EXT_framebuffer_object supported: no");
    return FALSE;
  }

  GST_INFO ("Context, EXT_framebuffer_object supported: yes");

  /* setup FBO */
  gl->GenFramebuffers (1, &convert->fbo);
  gl->BindFramebuffer (GL_FRAMEBUFFER, convert->fbo);

  /* setup the render buffer for depth */
  gl->GenRenderbuffers (1, &convert->depth_buffer);
  gl->BindRenderbuffer (GL_RENDERBUFFER, convert->depth_buffer);
  if (USING_OPENGL (convert->context)) {
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
        out_width, out_height);
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
        out_width, out_height);
  }
  if (USING_GLES2 (convert->context)) {
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
        out_width, out_height);
  }

  /* a fake texture is attached to the convert FBO (cannot init without it) */
  gl->GenTextures (1, &fake_texture);
  gl->BindTexture (GL_TEXTURE_2D, fake_texture);
  gl->TexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, out_width, out_height,
      0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  /* attach the texture to the FBO to renderer to */
  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_2D, fake_texture, 0);

  /* attach the depth render buffer to the FBO */
  gl->FramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_RENDERBUFFER, convert->depth_buffer);

  if (USING_OPENGL (convert->context)) {
    gl->FramebufferRenderbuffer (GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, convert->depth_buffer);
  }

  if (!gst_gl_context_check_framebuffer_status (convert->context)) {
    gst_gl_context_set_error (convert->context,
        "GL framebuffer status incomplete");
    return FALSE;
  }

  /* unbind the FBO */
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  gl->DeleteTextures (1, &fake_texture);

  return TRUE;
}

/* Called by the idle function in the gl thread */
void
_do_convert (GstGLContext * context, GstGLColorConvert * convert)
{
  guint in_width, in_height, out_width, out_height;
  struct ConvertInfo *c_info = &convert->priv->convert_info;
  GstMapInfo in_infos[GST_VIDEO_MAX_PLANES], out_infos[GST_VIDEO_MAX_PLANES];
  gboolean res = TRUE;
  gint i = 0;

  out_width = GST_VIDEO_INFO_WIDTH (&convert->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&convert->out_info);
  in_width = GST_VIDEO_INFO_WIDTH (&convert->in_info);
  in_height = GST_VIDEO_INFO_HEIGHT (&convert->in_info);

  if (!_init_convert (convert)) {
    convert->priv->result = FALSE;
    return;
  }

  GST_TRACE ("converting to textures:%p,%p,%p,%p dimensions:%ux%u, "
      "from textures:%p,%p,%p,%p dimensions:%ux%u", convert->out_tex[0],
      convert->out_tex[1], convert->out_tex[2], convert->out_tex[3],
      out_width, out_height, convert->in_tex[0], convert->in_tex[1],
      convert->in_tex[2], convert->in_tex[3], in_width, in_height);

  for (i = 0; i < c_info->in_n_textures; i++) {
    gst_memory_map ((GstMemory *) convert->in_tex[i], &in_infos[i],
        GST_MAP_READ | GST_MAP_GL);
  }
  for (i = 0; i < c_info->out_n_textures; i++) {
    if (convert->out_tex[i]->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE
        || convert->out_tex[i]->tex_type ==
        GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA
        || out_width != convert->out_tex[i]->width
        || out_height != convert->out_tex[i]->height) {
      /* Luminance formats are not color renderable */
      /* renderering to a framebuffer only renders the intersection of all
       * the attachments i.e. the smallest attachment size */
      if (!convert->priv->out_temp[i])
        convert->priv->out_temp[i] =
            (GstGLMemory *) gst_gl_memory_alloc (context,
            GST_VIDEO_GL_TEXTURE_TYPE_RGBA, out_width, out_height, out_width);
    } else {
      convert->priv->out_temp[i] = convert->out_tex[i];
    }
    gst_memory_map ((GstMemory *) convert->priv->out_temp[i], &out_infos[i],
        GST_MAP_WRITE | GST_MAP_GL);
  }

  if (!convert->priv->draw (context, convert))
    res = FALSE;

  for (i = 0; i < c_info->in_n_textures; i++) {
    gst_memory_unmap ((GstMemory *) convert->in_tex[i], &in_infos[i]);
  }
  for (i = 0; i < c_info->out_n_textures; i++) {
    gst_memory_unmap ((GstMemory *) convert->priv->out_temp[i], &out_infos[i]);

    if (convert->out_tex[i]->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE
        || convert->out_tex[i]->tex_type ==
        GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA
        || out_width != convert->out_tex[i]->width
        || out_height != convert->out_tex[i]->height) {
      GstGLMemory *gl_mem = convert->out_tex[i];
      GstMapInfo from_info, to_info;

      gst_memory_map ((GstMemory *) convert->priv->out_temp[i], &from_info,
          GST_MAP_READ | GST_MAP_GL);
      gst_memory_map ((GstMemory *) gl_mem, &to_info,
          GST_MAP_WRITE | GST_MAP_GL);
      gst_gl_memory_copy_into_texture (convert->priv->out_temp[i],
          gl_mem->tex_id, gl_mem->tex_type, gl_mem->width, gl_mem->height,
          gl_mem->stride, FALSE);
      gst_memory_unmap ((GstMemory *) gl_mem, &to_info);
      gst_memory_unmap ((GstMemory *) convert->priv->out_temp[i], &from_info);
    } else {
      convert->priv->out_temp[i] = NULL;
    }
  }

  convert->priv->result = res;
  return;
}

static gboolean
_do_convert_draw (GstGLContext * context, GstGLColorConvert * convert)
{
  GstGLFuncs *gl;
  struct ConvertInfo *c_info = &convert->priv->convert_info;
  guint out_width, out_height;
  gint i;

  GLint viewport_dim[4];

  const GLfloat vVertices[] = { 1.0f, -1.0f, 0.0f,
    1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f,
    0.0f, 0.0f,
    -1.0f, 1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f, 0.0f,
    1.0f, 1.0f
  };

  GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

  GLenum multipleRT[] = {
    GL_COLOR_ATTACHMENT0,
    GL_COLOR_ATTACHMENT1,
    GL_COLOR_ATTACHMENT2
  };

  gl = context->gl_vtable;

  out_width = GST_VIDEO_INFO_WIDTH (&convert->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&convert->out_info);

  /* two sources of the same data */
  if (convert->priv->scratch) {
    gst_gl_memory_copy_into_texture (convert->in_tex[0],
        convert->priv->scratch->tex_id, convert->priv->scratch->tex_type,
        convert->priv->scratch->width, convert->priv->scratch->height,
        convert->priv->scratch->stride, TRUE);
  }

  gl->BindFramebuffer (GL_FRAMEBUFFER, convert->fbo);

  /* attach the texture to the FBO to renderer to */
  for (i = 0; i < c_info->out_n_textures; i++) {
    /* needed? */
    gl->BindTexture (GL_TEXTURE_2D, convert->out_tex[i]->tex_id);

    gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
        GL_TEXTURE_2D, convert->priv->out_temp[i]->tex_id, 0);
  }

  if (gl->DrawBuffers)
    gl->DrawBuffers (c_info->out_n_textures, multipleRT);
  else if (gl->DrawBuffer)
    gl->DrawBuffer (GL_COLOR_ATTACHMENT0);

  gl->GetIntegerv (GL_VIEWPORT, viewport_dim);

  gl->Viewport (0, 0, out_width, out_height);

  gl->ClearColor (0.0, 0.0, 0.0, 0.0);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gst_gl_shader_use (convert->shader);

  gl->VertexAttribPointer (convert->shader_attr_position_loc, 3,
      GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
  gl->VertexAttribPointer (convert->shader_attr_texture_loc, 2,
      GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

  gl->EnableVertexAttribArray (convert->shader_attr_position_loc);
  gl->EnableVertexAttribArray (convert->shader_attr_texture_loc);

  if (convert->priv->scratch) {
    gl->ActiveTexture (GL_TEXTURE0 + c_info->in_n_textures);
    gl->BindTexture (GL_TEXTURE_2D, convert->priv->scratch->tex_id);
  }

  for (i = c_info->in_n_textures - 1; i >= 0; i--) {
    gl->ActiveTexture (GL_TEXTURE0 + i);
    gl->BindTexture (GL_TEXTURE_2D, convert->in_tex[i]->tex_id);

    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

  gl->DisableVertexAttribArray (convert->shader_attr_position_loc);
  gl->DisableVertexAttribArray (convert->shader_attr_texture_loc);

  if (gl->DrawBuffer)
    gl->DrawBuffer (GL_NONE);

  /* we are done with the shader */
  gst_gl_context_clear_shader (context);

  gl->Viewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
      viewport_dim[3]);

  gst_gl_context_check_framebuffer_status (context);

  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  return TRUE;
}
