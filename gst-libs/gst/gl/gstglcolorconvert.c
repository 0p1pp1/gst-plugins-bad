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

#include <string.h>
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

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

static void _do_convert (GstGLContext * context, GstGLColorConvert * convert);
static gboolean _init_convert (GstGLColorConvert * convert);
static gboolean _init_convert_fbo (GstGLColorConvert * convert);
static GstBuffer *_gst_gl_color_convert_perform_unlocked (GstGLColorConvert *
    convert, GstBuffer * inbuf);

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

/* GRAY16 to RGB conversion
 *  data transfered as GL_LUMINANCE_ALPHA then convert back to GRAY16 
 *  high byte weight as : 255*256/65535 
 *  ([0~1] denormalize to [0~255],shift to high byte,normalize to [0~1])
 *  low byte weight as : 255/65535 (similar)
 * */
#define COMPOSE_WEIGHT \
    "const vec2 compose_weight = vec2(0.996109, 0.003891);\n"

#define DEFAULT_UNIFORMS         \
    "#ifdef GL_ES\n"             \
    "precision mediump float;\n" \
    "#endif\n"                   \
    "uniform vec2 tex_scale0;\n" \
    "uniform vec2 tex_scale1;\n" \
    "uniform vec2 tex_scale2;\n" \
    "uniform float width;\n"     \
    "uniform float height;\n"

#define MAX_FUNCTIONS 4

#define glsl_OES_extension_string "#extension GL_OES_EGL_image_external : require \n"

struct shader_templ
{
  const gchar *extensions;
  const gchar *uniforms;
  const gchar *functions[MAX_FUNCTIONS];
  const gchar *body;

  GstGLTextureTarget target;
};

#define glsl_func_yuv_to_rgb \
    "vec3 yuv_to_rgb (vec3 val, vec3 offset, vec3 ycoeff, vec3 ucoeff, vec3 vcoeff) {\n" \
    "  vec3 rgb;\n"                 \
    "  val += offset;\n"            \
    "  rgb.r = dot(val, ycoeff);\n" \
    "  rgb.g = dot(val, ucoeff);\n" \
    "  rgb.b = dot(val, vcoeff);\n" \
    "  return rgb;\n"               \
    "}\n"

#define glsl_func_rgb_to_yuv \
    "vec3 rgb_to_yuv (vec3 val, vec3 offset, vec3 rcoeff, vec3 gcoeff, vec3 bcoeff) {\n" \
    "  vec3 yuv;\n"                         \
    "  yuv.r = dot(val.rgb, rcoeff);\n"     \
    "  yuv.g = dot(val.rgb, gcoeff);\n"     \
    "  yuv.b = dot(val.rgb, bcoeff);\n"     \
    "  yuv += offset;\n"                    \
    "  return yuv;\n"                       \
    "}\n"

/* Channel reordering for XYZ <-> ZYX conversion */
static const struct shader_templ templ_REORDER =
  { NULL,
    DEFAULT_UNIFORMS "uniform sampler2D tex;\n",
    { NULL, },
    "vec4 t = texture2D(tex, texcoord * tex_scale0);\n"
    "%s\n" /* clobber alpha channel? */
    "gl_FragColor = vec4(t.%c, t.%c, t.%c, t.%c);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

/* GRAY16 to RGB conversion
 *  data transfered as GL_LUMINANCE_ALPHA then convert back to GRAY16 
 *  high byte weight as : 255*256/65535 
 *  ([0~1] denormalize to [0~255],shift to high byte,normalize to [0~1])
 *  low byte weight as : 255/65535 (similar)
 * */
static const struct shader_templ templ_COMPOSE =
  { NULL,
    DEFAULT_UNIFORMS COMPOSE_WEIGHT "uniform sampler2D tex;\n",
    { NULL, },
    "vec4 rgba;\n"
    "vec4 t = texture2D(tex, texcoord * tex_scale0);\n"
    "rgba.rgb = dot(t.%c%c, compose_weight);"
    "rgba.a = 1.0;\n"
    "gl_FragColor = vec4(rgba.%c, rgba.%c, rgba.%c, rgba.%c);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

static const struct shader_templ templ_AYUV_to_RGB =
  { NULL,
    DEFAULT_UNIFORMS YUV_TO_RGB_COEFFICIENTS "uniform sampler2D tex;\n",
    { glsl_func_yuv_to_rgb, NULL, },
    "vec4 texel, rgba;\n"
    "texel = texture2D(tex, texcoord * tex_scale0);\n"
    "rgba.rgb = yuv_to_rgb (texel.yzw, offset, coeff1, coeff2, coeff3);\n"
    "rgba.a = texel.r;\n"
    "gl_FragColor=vec4(rgba.%c,rgba.%c,rgba.%c,rgba.%c);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

static const struct shader_templ templ_RGB_to_AYUV =
  { NULL,
    DEFAULT_UNIFORMS RGB_TO_YUV_COEFFICIENTS "uniform sampler2D tex;\n",
    { glsl_func_rgb_to_yuv, NULL, },
    "vec4 texel, ayuv;\n"
    "texel = texture2D(tex, texcoord).%c%c%c%c;\n"
    "ayuv.yzw = rgb_to_yuv (texel.rgb, offset, coeff1, coeff2, coeff3);\n"
    "ayuv.x = %s;\n"
    "gl_FragColor = ayuv;\n",
    GST_GL_TEXTURE_TARGET_2D
  };

/* YUV to RGB conversion */
static const struct shader_templ templ_PLANAR_YUV_to_RGB =
  { NULL,
    DEFAULT_UNIFORMS YUV_TO_RGB_COEFFICIENTS "uniform sampler2D Ytex, Utex, Vtex;\n",
    { glsl_func_yuv_to_rgb, NULL, },
    "vec4 texel, rgba;\n"
    /* FIXME: should get the sampling right... */
    "texel.x = texture2D(Ytex, texcoord * tex_scale0).r;\n"
    "texel.y = texture2D(Utex, texcoord * tex_scale1).r;\n"
    "texel.z = texture2D(Vtex, texcoord * tex_scale2).r;\n"
    "rgba.rgb = yuv_to_rgb (texel.xyz, offset, coeff1, coeff2, coeff3);\n"
    "rgba.a = 1.0;\n"
    "gl_FragColor=vec4(rgba.%c,rgba.%c,rgba.%c,rgba.%c);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

static const struct shader_templ templ_RGB_to_PLANAR_YUV =
  { NULL,
    DEFAULT_UNIFORMS RGB_TO_YUV_COEFFICIENTS "uniform sampler2D tex;\n"
    "uniform vec2 chroma_sampling;\n",
    { glsl_func_rgb_to_yuv, NULL, },
    "vec4 texel;\n"
    "vec3 yuv;\n"
    "texel = texture2D(tex, texcoord).%c%c%c%c;\n"
    /* FIXME: this is not quite correct yet */
    "vec4 uv_texel = vec4(0.0);\n"
    /* One u and v sample can be generated by a nxm sized block given by
     * @chroma_sampling.  The result is the average of all the values in the
     * block computed with a rolling average.
     */
    "vec2 size = vec2(width, height);\n"
    "vec2 pos = texcoord * size;\n"
     /* scale for chroma size */
    "vec2 chroma_pos = texcoord * chroma_sampling * size;\n"
     /* offset chroma to the center of the first texel in the block */
    "chroma_pos -= clamp(chroma_sampling * 0.5 - 0.5, vec2(0.0), chroma_sampling);\n"
    "if (chroma_pos.x < width && chroma_pos.y < height) {\n"
    "  for (int i = 0; i < int(chroma_sampling.x); i++) {\n"
    "    vec2 delta = vec2 (float(i), 0.0);\n"
    "    for (int j = 0; j < int(chroma_sampling.y); j++) {\n"
    "      int n = (i+1)*(j+1);\n"
    "      delta.y = float(j);\n"
    "      vec4 sample = texture2D(tex, (chroma_pos + delta) / size).%c%c%c%c;\n"
           /* rolling average */
    "      uv_texel = (float(n-1) * uv_texel + sample) / float(n);\n"
    "    }\n"
    "  }\n"
    "}\n"

    "yuv.x = rgb_to_yuv (texel.rgb, offset, coeff1, coeff2, coeff3).x;\n"
    "yuv.yz = rgb_to_yuv (uv_texel.rgb, offset, coeff1, coeff2, coeff3).yz;\n"
    "gl_FragData[0] = vec4(yuv.x, 0.0, 0.0, 1.0);\n"
    "gl_FragData[1] = vec4(yuv.y, 0.0, 0.0, 1.0);\n"
    "gl_FragData[2] = vec4(yuv.z, 0.0, 0.0, 1.0);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

/* NV12/NV21 to RGB conversion */
static const struct shader_templ templ_NV12_NV21_to_RGB =
  { NULL,
    DEFAULT_UNIFORMS YUV_TO_RGB_COEFFICIENTS "uniform sampler2D Ytex, UVtex;\n",
    { glsl_func_yuv_to_rgb, NULL, },
    "vec4 rgba;\n"
    "vec3 yuv;\n"
    /* FIXME: should get the sampling right... */
    "yuv.x=texture2D(Ytex, texcoord * tex_scale0).r;\n"
    "yuv.yz=texture2D(UVtex, texcoord * tex_scale1).%c%c;\n"
    "rgba.rgb = yuv_to_rgb (yuv, offset, coeff1, coeff2, coeff3);\n"
    "rgba.a = 1.0;\n"
    "gl_FragColor=vec4(rgba.%c,rgba.%c,rgba.%c,rgba.%c);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

/* RGB to NV12/NV21 conversion */
/* NV12: u, v
   NV21: v, u */
static const struct shader_templ templ_RGB_to_NV12_NV21 =
  { NULL,
    DEFAULT_UNIFORMS RGB_TO_YUV_COEFFICIENTS "uniform sampler2D tex;\n",
    { glsl_func_rgb_to_yuv, NULL, },
    "vec4 texel, uv_texel;\n"
    "vec3 yuv;\n"
    "texel = texture2D(tex, texcoord).%c%c%c%c;\n"
    "uv_texel = texture2D(tex, texcoord * tex_scale0 * 2.0).%c%c%c%c;\n"
    "yuv.x = rgb_to_yuv (texel.rgb, offset, coeff1, coeff2, coeff3).x;\n"
    "yuv.yz = rgb_to_yuv (uv_texel.rgb, offset, coeff1, coeff2, coeff3).yz;\n"
    "gl_FragData[0] = vec4(yuv.x, 0.0, 0.0, 1.0);\n"
    "gl_FragData[1] = vec4(yuv.%c, yuv.%c, 0.0, 1.0);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

/* YUY2:r,g,a
   UYVY:a,b,r */
static const struct shader_templ templ_YUY2_UYVY_to_RGB =
  { NULL,
    DEFAULT_UNIFORMS YUV_TO_RGB_COEFFICIENTS "uniform sampler2D Ytex;\n",
    { glsl_func_yuv_to_rgb, NULL, },
    "vec4 rgba, uv_texel;\n"
    "vec3 yuv;\n"
    /* FIXME: should get the sampling right... */
    "float dx1 = -1.0 / width;\n"
    "float dx2 = 0.0;\n"
    "yuv.x = texture2D(Ytex, texcoord * tex_scale0).%c;\n"
    "float inorder = mod (texcoord.x * width, 2.0);\n"
    "if (inorder < 1.0) {\n"
    "  dx2 = -dx1;\n"
    "  dx1 = 0.0;\n"
    "}\n"
    "uv_texel.rg = texture2D(Ytex, texcoord * tex_scale0 + dx1).r%c;\n"
    "uv_texel.ba = texture2D(Ytex, texcoord * tex_scale0 + dx2).r%c;\n"
    "yuv.yz = uv_texel.%c%c;\n"
    "rgba.rgb = yuv_to_rgb (yuv, offset, coeff1, coeff2, coeff3);\n"
    "rgba.a = 1.0;\n"
    "gl_FragColor = vec4(rgba.%c,rgba.%c,rgba.%c,rgba.%c);\n",
    GST_GL_TEXTURE_TARGET_2D
  };

static const struct shader_templ templ_RGB_to_YUY2_UYVY =
  { NULL,
    DEFAULT_UNIFORMS RGB_TO_YUV_COEFFICIENTS "uniform sampler2D tex;\n",
    { glsl_func_rgb_to_yuv, NULL, },
    "vec4 texel1, texel2;\n"
    "vec3 yuv, yuv1, yuv2;\n"
    "float fx, dx, fy;\n"
    "float inorder = mod (texcoord.x * width, 2.0);\n"
    "fx = texcoord.x;\n"
    "dx = 1.0 / width;\n"
    "if (texcoord.x >= (1.0 - 0.5 * dx) || (texcoord.x > 0.5 * dx && inorder < 1.0)) {\n"
    "  dx = -dx;\n"
    "}\n"
    "fy = texcoord.y;\n"
    "texel1 = texture2D(tex, vec2(fx, fy)).%c%c%c%c;\n"
    "texel2 = texture2D(tex, vec2(fx + dx, fy)).%c%c%c%c;\n"
    "yuv1 = rgb_to_yuv (texel1.rgb, offset, coeff1, coeff2, coeff3);"
    "yuv2 = rgb_to_yuv (texel2.rgb, offset, coeff1, coeff2, coeff3);"
    "yuv = (yuv1 + yuv2) / 2.0;\n"
    "if (inorder < 1.0) {\n"
    "  gl_FragColor = vec4(yuv.%c, yuv.%c, 0.0, 0.0);\n"
    "} else {\n"
    "  gl_FragColor = vec4(yuv.%c, yuv.%c, 0.0, 0.0);\n"
    "}\n",
    GST_GL_TEXTURE_TARGET_2D
  };

static const gchar text_vertex_shader[] =
    "attribute vec4 a_position;   \n"
    "attribute vec2 a_texcoord;   \n"
    "varying vec2 v_texcoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "  gl_Position = a_position; \n"
    "  v_texcoord = a_texcoord;  \n"
    "}                            \n";

static const GLfloat vertices[] = {
     1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 0.0f, 1.0f, 1.0f
};

static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

/* *INDENT-ON* */

struct ConvertInfo
{
  gint in_n_textures;
  gint out_n_textures;
  const struct shader_templ *templ;
  gchar *frag_body;
  gchar *frag_prog;
  const gchar *shader_tex_names[GST_VIDEO_MAX_PLANES];
  gfloat *cms_offset;
  gfloat *cms_coeff1;           /* r,y */
  gfloat *cms_coeff2;           /* g,u */
  gfloat *cms_coeff3;           /* b,v */
  gfloat chroma_sampling[2];
};

struct _GstGLColorConvertPrivate
{
  gboolean result;

  struct ConvertInfo convert_info;

  GstGLTextureTarget from_texture_target;
  GstGLTextureTarget to_texture_target;

  GstGLMemory *in_tex[GST_VIDEO_MAX_PLANES];
  GstGLMemory *out_tex[GST_VIDEO_MAX_PLANES];

  GLuint vao;
  GLuint vertex_buffer;
  GLuint vbo_indices;
  GLuint attr_position;
  GLuint attr_texture;
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

  gst_gl_color_convert_reset (convert);
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

  convert = g_object_new (GST_TYPE_GL_COLOR_CONVERT, NULL);

  convert->context = gst_object_ref (context);

  gst_video_info_set_format (&convert->in_info, GST_VIDEO_FORMAT_ENCODED, 0, 0);
  gst_video_info_set_format (&convert->out_info, GST_VIDEO_FORMAT_ENCODED, 0,
      0);

  GST_DEBUG_OBJECT (convert,
      "Created new colorconvert for context %" GST_PTR_FORMAT, context);

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

  G_OBJECT_CLASS (gst_gl_color_convert_parent_class)->finalize (object);
}

static void
_reset_gl (GstGLContext * context, GstGLColorConvert * convert)
{
  const GstGLFuncs *gl = context->gl_vtable;

  if (convert->priv->vao) {
    gl->DeleteVertexArrays (1, &convert->priv->vao);
    convert->priv->vao = 0;
  }

  if (convert->priv->vertex_buffer) {
    gl->DeleteBuffers (1, &convert->priv->vertex_buffer);
    convert->priv->vertex_buffer = 0;
  }

  if (convert->priv->vbo_indices) {
    gl->DeleteBuffers (1, &convert->priv->vbo_indices);
    convert->priv->vbo_indices = 0;
  }
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
    if (convert->priv->out_tex[i])
      gst_memory_unref ((GstMemory *) convert->priv->out_tex[i]);
    convert->priv->out_tex[i] = NULL;
  }

  convert->priv->convert_info.chroma_sampling[0] = 1.0f;
  convert->priv->convert_info.chroma_sampling[1] = 1.0f;

  if (convert->shader) {
    gst_object_unref (convert->shader);
    convert->shader = NULL;
  }

  if (convert->context) {
    gst_gl_context_thread_add (convert->context,
        (GstGLContextThreadFunc) _reset_gl, convert);
  }
}

static gboolean
_gst_gl_color_convert_can_passthrough_info (GstVideoInfo * in,
    GstVideoInfo * out)
{
  gint i;

  if (GST_VIDEO_INFO_FORMAT (in) != GST_VIDEO_INFO_FORMAT (out))
    return FALSE;
  if (GST_VIDEO_INFO_WIDTH (in) != GST_VIDEO_INFO_WIDTH (out))
    return FALSE;
  if (GST_VIDEO_INFO_HEIGHT (in) != GST_VIDEO_INFO_HEIGHT (out))
    return FALSE;
  if (GST_VIDEO_INFO_SIZE (in) != GST_VIDEO_INFO_SIZE (out))
    return FALSE;

  for (i = 0; i < in->finfo->n_planes; i++) {
    if (in->stride[i] != out->stride[i])
      return FALSE;
    if (in->offset[i] != out->offset[i])
      return FALSE;
  }

  if (!gst_video_colorimetry_is_equal (&in->colorimetry, &out->colorimetry))
    return FALSE;
  if (in->chroma_site != out->chroma_site)
    return FALSE;

  return TRUE;
}

static gboolean
_gst_gl_color_convert_set_caps_unlocked (GstGLColorConvert * convert,
    GstCaps * in_caps, GstCaps * out_caps)
{
  GstVideoInfo in_info, out_info;
  GstCapsFeatures *in_features, *out_features;
  GstGLTextureTarget from_target, to_target;
  gboolean passthrough;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (in_caps, FALSE);
  g_return_val_if_fail (out_caps, FALSE);

  GST_LOG_OBJECT (convert, "Setting caps in %" GST_PTR_FORMAT
      " out %" GST_PTR_FORMAT, in_caps, out_caps);

  if (!gst_video_info_from_caps (&in_info, in_caps))
    g_assert_not_reached ();

  if (!gst_video_info_from_caps (&out_info, out_caps))
    g_assert_not_reached ();

  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&in_info) !=
      GST_VIDEO_FORMAT_UNKNOWN, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&in_info) !=
      GST_VIDEO_FORMAT_ENCODED, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&out_info) !=
      GST_VIDEO_FORMAT_UNKNOWN, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&out_info) !=
      GST_VIDEO_FORMAT_ENCODED, FALSE);

  in_features = gst_caps_get_features (in_caps, 0);
  out_features = gst_caps_get_features (out_caps, 0);

  if (!gst_caps_features_contains (in_features,
          GST_CAPS_FEATURE_MEMORY_GL_MEMORY)
      || !gst_caps_features_contains (out_features,
          GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
    return FALSE;
  }

  {
    GstStructure *in_s = gst_caps_get_structure (in_caps, 0);
    GstStructure *out_s = gst_caps_get_structure (out_caps, 0);

    if (gst_structure_has_field_typed (in_s, "texture-target", G_TYPE_STRING))
      from_target =
          gst_gl_texture_target_from_string (gst_structure_get_string (in_s,
              "texture-target"));
    else
      from_target = GST_GL_TEXTURE_TARGET_2D;

    if (gst_structure_has_field_typed (out_s, "texture-target", G_TYPE_STRING))
      to_target =
          gst_gl_texture_target_from_string (gst_structure_get_string (out_s,
              "texture-target"));
    else
      to_target = GST_GL_TEXTURE_TARGET_2D;

    if (to_target == GST_GL_TEXTURE_TARGET_NONE
        || from_target == GST_GL_TEXTURE_TARGET_NONE)
      /* invalid caps */
      return FALSE;
  }

  if (gst_video_info_is_equal (&convert->in_info, &in_info) &&
      gst_video_info_is_equal (&convert->out_info, &out_info) &&
      convert->priv->from_texture_target == from_target &&
      convert->priv->to_texture_target == to_target)
    return TRUE;

  /* If input and output are identical, pass through directly */
  passthrough =
      _gst_gl_color_convert_can_passthrough_info (&in_info, &out_info) &&
      from_target == to_target;

  if (!passthrough && to_target != GST_GL_TEXTURE_TARGET_2D
      && to_target != GST_GL_TEXTURE_TARGET_RECTANGLE)
    return FALSE;

  gst_gl_color_convert_reset (convert);
  convert->in_info = in_info;
  convert->out_info = out_info;
  convert->priv->from_texture_target = from_target;
  convert->priv->to_texture_target = to_target;
  convert->initted = FALSE;

  convert->passthrough = passthrough;
#ifndef GST_DISABLE_GST_DEBUG
  if (G_UNLIKELY (convert->passthrough))
    GST_DEBUG_OBJECT (convert,
        "Configuring passthrough mode for same in/out caps");
  else {
    GST_DEBUG_OBJECT (convert, "Color converting %" GST_PTR_FORMAT
        " to %" GST_PTR_FORMAT, in_caps, out_caps);
  }
#endif

  return TRUE;
}

/**
 * gst_gl_color_convert_set_caps:
 * @convert: a #GstGLColorConvert
 * @in_caps: input #GstCaps
 * @out_caps: output #GstCaps
 *
 * Initializes @convert with the information required for conversion.
 */
gboolean
gst_gl_color_convert_set_caps (GstGLColorConvert * convert,
    GstCaps * in_caps, GstCaps * out_caps)
{
  gboolean ret;

  GST_OBJECT_LOCK (convert);
  ret = _gst_gl_color_convert_set_caps_unlocked (convert, in_caps, out_caps);
  GST_OBJECT_UNLOCK (convert);

  return ret;
}

static guint
_get_target_bitmask_from_g_value (const GValue * targets)
{
  guint new_targets = 0;

  if (targets == NULL) {
    new_targets = 1 << GST_GL_TEXTURE_TARGET_2D;
  } else if (G_TYPE_CHECK_VALUE_TYPE (targets, G_TYPE_STRING)) {
    GstGLTextureTarget target;
    const gchar *str;

    str = g_value_get_string (targets);
    target = gst_gl_texture_target_from_string (str);

    if (target)
      new_targets |= 1 << target;
  } else if (G_TYPE_CHECK_VALUE_TYPE (targets, GST_TYPE_LIST)) {
    gint j, m;

    m = gst_value_list_get_size (targets);
    for (j = 0; j < m; j++) {
      const GValue *val = gst_value_list_get_value (targets, j);
      GstGLTextureTarget target;
      const gchar *str;

      str = g_value_get_string (val);
      target = gst_gl_texture_target_from_string (str);
      if (target)
        new_targets |= 1 << target;
    }
  }

  return new_targets;
}

/* copies the given caps */
static GstCaps *
gst_gl_color_convert_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, st, f))
      continue;

    st = gst_structure_copy (st);
    gst_structure_remove_fields (st, "format", "colorimetry", "chroma-site",
        "texture-target", NULL);

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }

  return res;
}

GstCaps *
gst_gl_color_convert_transform_caps (GstGLContext * convert,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *templ, *result;

  templ = gst_caps_from_string (GST_GL_COLOR_CONVERT_VIDEO_CAPS);

  caps = gst_gl_color_convert_caps_remove_format_info (caps);

  result = gst_caps_intersect (caps, templ);
  gst_caps_unref (caps);
  gst_caps_unref (templ);

  if (filter) {
    GstCaps *tmp;

    tmp = gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = tmp;
  }

  return result;
}

GstCaps *
gst_gl_color_convert_fixate_caps (GstGLContext * convert,
    GstPadDirection direction, GstCaps * caps, GstCaps * other)
{
  GValue item = G_VALUE_INIT;
  const GValue *targets, *other_targets;
  guint targets_mask = 0, other_targets_mask = 0, result_mask;
  GstStructure *s;

  other = gst_caps_make_writable (other);
  s = gst_caps_get_structure (other, 0);

  other_targets = gst_structure_get_value (s, "texture-target");
  targets = gst_structure_get_value (s, "texture-target");

  targets_mask = _get_target_bitmask_from_g_value (targets);
  other_targets_mask = _get_target_bitmask_from_g_value (other_targets);

  result_mask = targets_mask & other_targets_mask;
  if (result_mask == 0) {
    /* nothing we can do here */
    return gst_caps_fixate (other);
  }

  if (direction == GST_PAD_SINK) {
    result_mask &=
        (1 << GST_GL_TEXTURE_TARGET_2D | 1 << GST_GL_TEXTURE_TARGET_RECTANGLE);
  } else {
    /* if the src caps has 2D support we can 'convert' to anything */
    if (targets_mask & (1 << GST_GL_TEXTURE_TARGET_2D))
      result_mask = -1;
    else
      result_mask = other_targets_mask;
  }

  g_value_init (&item, G_TYPE_STRING);
  if (result_mask & (1 << GST_GL_TEXTURE_TARGET_2D)) {
    g_value_set_static_string (&item, GST_GL_TEXTURE_TARGET_2D_STR);
  } else if (result_mask & (1 << GST_GL_TEXTURE_TARGET_RECTANGLE)) {
    g_value_set_static_string (&item, GST_GL_TEXTURE_TARGET_RECTANGLE_STR);
  } else if (result_mask & (1 << GST_GL_TEXTURE_TARGET_EXTERNAL_OES)) {
    g_value_set_static_string (&item, GST_GL_TEXTURE_TARGET_EXTERNAL_OES_STR);
  }

  gst_structure_set_value (s, "texture-target", &item);

  g_value_unset (&item);

  other = gst_caps_fixate (other);

  return other;
}

/**
 * gst_gl_color_convert_perform:
 * @convert: a #GstGLColorConvert
 * @inbuf: the texture ids for input formatted according to in_info
 *
 * Converts the data contained by @inbuf using the formats specified by the
 * #GstVideoInfo<!--  -->s passed to gst_gl_color_convert_set_caps() 
 *
 * Returns: a converted #GstBuffer or %NULL%
 */
GstBuffer *
gst_gl_color_convert_perform (GstGLColorConvert * convert, GstBuffer * inbuf)
{
  GstBuffer *ret;

  g_return_val_if_fail (convert != NULL, FALSE);

  GST_OBJECT_LOCK (convert);
  ret = _gst_gl_color_convert_perform_unlocked (convert, inbuf);
  GST_OBJECT_UNLOCK (convert);

  return ret;
}

static GstBuffer *
_gst_gl_color_convert_perform_unlocked (GstGLColorConvert * convert,
    GstBuffer * inbuf)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (inbuf, FALSE);

  if (G_UNLIKELY (convert->passthrough))
    return gst_buffer_ref (inbuf);

  convert->inbuf = inbuf;

  gst_gl_context_thread_add (convert->context,
      (GstGLContextThreadFunc) _do_convert, convert);

  if (!convert->priv->result) {
    if (convert->outbuf)
      gst_buffer_unref (convert->outbuf);
    convert->outbuf = NULL;
    return NULL;
  }

  return convert->outbuf;
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
  gchar *orig_want;
  int len;
  gboolean discard_output = TRUE;

  if (g_ascii_strcasecmp (expected, wanted) == 0) {
    g_string_free (ret, TRUE);
    return g_ascii_strdown (expected, -1);
  }

  expect = g_ascii_strdown (expected, -1);
  orig_want = want = g_ascii_strdown (wanted, -1);

  if (strcmp (expect, "rgb16") == 0 || strcmp (expect, "bgr16") == 0) {
    gchar *temp = expect;
    expect = g_strndup (temp, 3);
    g_free (temp);
  }

  if (strcmp (want, "rgb16") == 0 || strcmp (want, "bgr16") == 0) {
    gchar *temp = want;
    orig_want = want = g_strndup (temp, 3);
    g_free (temp);
  }

  /* pad want with 'a's */
  if ((len = strlen (want)) < 4) {
    gchar *new_want = g_strndup (want, 4);
    while (len < 4) {
      new_want[len] = 'a';
      len++;
    }
    g_free (want);
    orig_want = want = new_want;
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
      goto out;

    idx = (gint) (val - expect);

    ret = g_string_append_c (ret, _index_to_shader_swizzle (idx));
    want = &want[1];
  }

  discard_output = FALSE;
out:
  g_free (orig_want);
  g_free (expect);

  return g_string_free (ret, discard_output);
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
  gchar *alpha = NULL;

  info->in_n_textures = 1;
  info->out_n_textures = 1;
  if (_is_RGBx (in_format)) {
    int i;
    char input_alpha_channel = 'a';
    for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      if (in_format_str[i] == 'X' || in_format_str[i] == 'x') {
        input_alpha_channel = _index_to_shader_swizzle (i);
        break;
      }
    }
    alpha = g_strdup_printf ("t.%c = 1.0;", input_alpha_channel);
  }
  info->templ = &templ_REORDER;
  info->frag_body = g_strdup_printf (info->templ->body, alpha ? alpha : "",
      pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
  info->shader_tex_names[0] = "tex";

  g_free (alpha);
  g_free (pixel_order);
}

static void
_YUV_to_RGB (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GstVideoFormat out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);
  const gchar *out_format_str = gst_video_format_to_string (out_format);
  gchar *pixel_order = _RGB_pixel_order ("rgba", out_format_str);
  gboolean texture_rg =
      gst_gl_context_check_feature (convert->context, "GL_EXT_texture_rg")
      || gst_gl_context_check_gl_version (convert->context, GST_GL_API_GLES2, 3,
      0)
      || gst_gl_context_check_feature (convert->context, "GL_ARB_texture_rg")
      || gst_gl_context_check_gl_version (convert->context, GST_GL_API_OPENGL3,
      3, 0);
  gboolean apple_ycbcr = gst_gl_context_check_feature (convert->context,
      "GL_APPLE_ycbcr_422");
  gboolean in_tex_rectangular = FALSE;

#if GST_GL_HAVE_OPENGL
  GstMemory *memory = gst_buffer_peek_memory (convert->inbuf, 0);
  if (gst_is_gl_memory (memory) && (USING_OPENGL (convert->context)
          || USING_OPENGL3 (convert->context))) {
    in_tex_rectangular =
        convert->priv->from_texture_target == GST_GL_TEXTURE_TARGET_RECTANGLE;
  }
#endif

  info->out_n_textures = 1;

  if (in_tex_rectangular && apple_ycbcr
      && gst_buffer_n_memory (convert->inbuf) == 1) {
    /* FIXME: We should probably also check if tex_target actually is using
     * the Apple YCbCr422 extension. It could also be a normal UYVY texture
     * with RB or Lum/Alpha
     */
    /* The mangling will change this to the correct texture2DRect, sampler2DRect
     * for us */
    info->templ = &templ_REORDER;
    info->frag_body =
        g_strdup_printf (info->templ->body, pixel_order[0], pixel_order[1],
        pixel_order[2], pixel_order[3]);
    info->in_n_textures = 1;
    info->shader_tex_names[0] = "tex";
  } else {
    switch (GST_VIDEO_INFO_FORMAT (&convert->in_info)) {
      case GST_VIDEO_FORMAT_AYUV:
        info->templ = &templ_AYUV_to_RGB;
        info->frag_body = g_strdup_printf (info->templ->body, pixel_order[0],
            pixel_order[1], pixel_order[2], pixel_order[3]);
        info->in_n_textures = 1;
        info->shader_tex_names[0] = "tex";
        break;
      case GST_VIDEO_FORMAT_I420:
      case GST_VIDEO_FORMAT_Y444:
      case GST_VIDEO_FORMAT_Y42B:
      case GST_VIDEO_FORMAT_Y41B:
        info->templ = &templ_PLANAR_YUV_to_RGB;
        info->frag_body =
            g_strdup_printf (info->templ->body, pixel_order[0],
            pixel_order[1], pixel_order[2], pixel_order[3]);
        info->in_n_textures = 3;
        info->shader_tex_names[0] = "Ytex";
        info->shader_tex_names[1] = "Utex";
        info->shader_tex_names[2] = "Vtex";
        break;
      case GST_VIDEO_FORMAT_YV12:
        info->templ = &templ_PLANAR_YUV_to_RGB;
        info->frag_body =
            g_strdup_printf (info->templ->body, pixel_order[0],
            pixel_order[1], pixel_order[2], pixel_order[3]);
        info->in_n_textures = 3;
        info->shader_tex_names[0] = "Ytex";
        info->shader_tex_names[1] = "Vtex";
        info->shader_tex_names[2] = "Utex";
        break;
      case GST_VIDEO_FORMAT_YUY2:
      {
        char uv_val = texture_rg ? 'g' : 'a';
        info->templ = &templ_YUY2_UYVY_to_RGB;
        info->frag_body = g_strdup_printf (info->templ->body, 'r', uv_val,
            uv_val, 'g', 'a', pixel_order[0], pixel_order[1], pixel_order[2],
            pixel_order[3]);
        info->in_n_textures = 1;
        info->shader_tex_names[0] = "Ytex";
        break;
      }
      case GST_VIDEO_FORMAT_UYVY:
      {
        char y_val = texture_rg ? 'g' : 'a';
        info->templ = &templ_YUY2_UYVY_to_RGB;
        info->frag_body = g_strdup_printf (info->templ->body, y_val, 'g',
            'g', 'r', 'b', pixel_order[0], pixel_order[1], pixel_order[2],
            pixel_order[3]);
        info->in_n_textures = 1;
        info->shader_tex_names[0] = "Ytex";
        break;
      }
      case GST_VIDEO_FORMAT_NV12:
      {
        char val2 = texture_rg ? 'g' : 'a';
        info->templ = &templ_NV12_NV21_to_RGB;
        info->frag_body = g_strdup_printf (info->templ->body, 'r', val2,
            pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
        info->in_n_textures = 2;
        info->shader_tex_names[0] = "Ytex";
        info->shader_tex_names[1] = "UVtex";
        break;
      }
      case GST_VIDEO_FORMAT_NV21:
      {
        char val2 = texture_rg ? 'g' : 'a';
        info->templ = &templ_NV12_NV21_to_RGB;
        info->frag_body = g_strdup_printf (info->templ->body, val2, 'r',
            pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
        info->in_n_textures = 2;
        info->shader_tex_names[0] = "Ytex";
        info->shader_tex_names[1] = "UVtex";
        break;
      }
      default:
        break;
    }
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
  GstVideoFormat out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);
  gchar *pixel_order = _RGB_pixel_order (in_format_str, "rgba");
  const gchar *alpha;

  info->frag_prog = NULL;
  info->in_n_textures = 1;

  info->shader_tex_names[0] = "tex";

  switch (out_format) {
    case GST_VIDEO_FORMAT_AYUV:
      alpha = _is_RGBx (in_format) ? "1.0" : "texel.a";
      info->templ = &templ_RGB_to_AYUV;
      info->frag_body = g_strdup_printf (info->templ->body, pixel_order[0],
          pixel_order[1], pixel_order[2], pixel_order[3], alpha);
      info->out_n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      info->templ = &templ_RGB_to_PLANAR_YUV;
      info->frag_body = g_strdup_printf (info->templ->body,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      info->out_n_textures = 3;
      if (out_format == GST_VIDEO_FORMAT_Y444) {
        info->chroma_sampling[0] = info->chroma_sampling[1] = 1.0f;
      } else if (out_format == GST_VIDEO_FORMAT_Y42B) {
        info->chroma_sampling[0] = 2.0f;
        info->chroma_sampling[1] = 1.0f;
      } else if (out_format == GST_VIDEO_FORMAT_Y41B) {
        info->chroma_sampling[0] = 4.0f;
        info->chroma_sampling[1] = 1.0f;
      } else {
        info->chroma_sampling[0] = info->chroma_sampling[1] = 2.0f;
      }
      break;
    case GST_VIDEO_FORMAT_YUY2:
      info->templ = &templ_RGB_to_YUY2_UYVY;
      info->frag_body = g_strdup_printf (info->templ->body,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          'x', 'y', 'x', 'z');
      info->out_n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      info->templ = &templ_RGB_to_YUY2_UYVY,
          info->frag_body = g_strdup_printf (info->templ->body,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          'y', 'x', 'z', 'x');
      info->out_n_textures = 1;
      break;
    case GST_VIDEO_FORMAT_NV12:
      info->templ = &templ_RGB_to_NV12_NV21,
          info->frag_body = g_strdup_printf (info->templ->body,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          'y', 'z');
      info->out_n_textures = 2;
      break;
    case GST_VIDEO_FORMAT_NV21:
      info->templ = &templ_RGB_to_NV12_NV21,
          info->frag_body = g_strdup_printf (info->templ->body,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3],
          'z', 'y');
      info->out_n_textures = 2;
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
  gchar *alpha = NULL;

  info->in_n_textures = 1;
  info->out_n_textures = 1;
  info->shader_tex_names[0] = "tex";

  if (_is_RGBx (in_format))
    alpha = g_strdup_printf ("t.%c = 1.0;", pixel_order[3]);

  switch (GST_VIDEO_INFO_FORMAT (&convert->out_info)) {
    case GST_VIDEO_FORMAT_GRAY8:
      info->templ = &templ_REORDER;
      info->frag_body = g_strdup_printf (info->templ->body, alpha ? alpha : "",
          pixel_order[0], pixel_order[0], pixel_order[0], pixel_order[3]);
      break;
    default:
      break;
  }

  g_free (alpha);
  g_free (pixel_order);
}

static void
_GRAY_to_RGB (GstGLColorConvert * convert)
{
  struct ConvertInfo *info = &convert->priv->convert_info;
  GstVideoFormat out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);
  const gchar *out_format_str = gst_video_format_to_string (out_format);
  gchar *pixel_order = _RGB_pixel_order ("rgba", out_format_str);
  gboolean texture_rg =
      gst_gl_context_check_feature (convert->context, "GL_EXT_texture_rg")
      || gst_gl_context_check_gl_version (convert->context, GST_GL_API_GLES2, 3,
      0)
      || gst_gl_context_check_feature (convert->context, "GL_ARB_texture_rg")
      || gst_gl_context_check_gl_version (convert->context, GST_GL_API_OPENGL3,
      3, 0);

  info->in_n_textures = 1;
  info->out_n_textures = 1;
  info->shader_tex_names[0] = "tex";

  switch (GST_VIDEO_INFO_FORMAT (&convert->in_info)) {
    case GST_VIDEO_FORMAT_GRAY8:
      info->templ = &templ_REORDER;
      info->frag_body = g_strdup_printf (info->templ->body, "", pixel_order[0],
          pixel_order[0], pixel_order[0], pixel_order[3]);
      break;
    case GST_VIDEO_FORMAT_GRAY16_LE:
    {
      char val2 = texture_rg ? 'g' : 'a';
      info->templ = &templ_COMPOSE;
      info->frag_body = g_strdup_printf (info->templ->body, val2, 'r',
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      break;
    }
    case GST_VIDEO_FORMAT_GRAY16_BE:
    {
      char val2 = texture_rg ? 'g' : 'a';
      info->templ = &templ_COMPOSE;
      info->frag_body = g_strdup_printf (info->templ->body, 'r', val2,
          pixel_order[0], pixel_order[1], pixel_order[2], pixel_order[3]);
      break;
    }
    default:
      break;
  }

  g_free (pixel_order);
}

static void
_bind_buffer (GstGLColorConvert * convert)
{
  const GstGLFuncs *gl = convert->context->gl_vtable;

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, convert->priv->vbo_indices);
  gl->BindBuffer (GL_ARRAY_BUFFER, convert->priv->vertex_buffer);

  /* Load the vertex position */
  gl->VertexAttribPointer (convert->priv->attr_position, 3, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) 0);

  /* Load the texture coordinate */
  gl->VertexAttribPointer (convert->priv->attr_texture, 2, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) (3 * sizeof (GLfloat)));

  gl->EnableVertexAttribArray (convert->priv->attr_position);
  gl->EnableVertexAttribArray (convert->priv->attr_texture);
}

static void
_unbind_buffer (GstGLColorConvert * convert)
{
  const GstGLFuncs *gl = convert->context->gl_vtable;

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);

  gl->DisableVertexAttribArray (convert->priv->attr_position);
  gl->DisableVertexAttribArray (convert->priv->attr_texture);
}

static gchar *
_mangle_texture_access (const gchar * str, GstGLTextureTarget from,
    GstGLTextureTarget to)
{
  const gchar *from_str = NULL, *to_str = NULL;
  gchar *ret, *tmp;
  gchar *regex_find;
  GRegex *regex;

  if (from == GST_GL_TEXTURE_TARGET_2D)
    from_str = "texture2D";
  if (from == GST_GL_TEXTURE_TARGET_RECTANGLE)
    from_str = "texture2DRect";
  if (from == GST_GL_TEXTURE_TARGET_EXTERNAL_OES)
    from_str = "texture2D";

  if (to == GST_GL_TEXTURE_TARGET_2D)
    to_str = "texture2D";
  if (to == GST_GL_TEXTURE_TARGET_RECTANGLE)
    to_str = "texture2DRect";
  if (to == GST_GL_TEXTURE_TARGET_EXTERNAL_OES)
    to_str = "texture2D";

  /* followed by any amount of whitespace then a bracket */
  regex_find = g_strdup_printf ("%s(?=\\s*\\()", from_str);
  regex = g_regex_new (regex_find, 0, 0, NULL);
  tmp = g_regex_replace_literal (regex, str, -1, 0, to_str, 0, NULL);
  g_free (regex_find);
  g_regex_unref (regex);

  if (tmp) {
    ret = tmp;
  } else {
    GST_FIXME ("Couldn't mangle texture access successfully from %s to %s",
        from_str, to_str);
    ret = g_strdup (str);
  }

  return ret;
}

static gchar *
_mangle_sampler_type (const gchar * str, GstGLTextureTarget from,
    GstGLTextureTarget to)
{
  const gchar *from_str = NULL, *to_str = NULL;
  gchar *ret, *tmp;
  gchar *regex_find;
  GRegex *regex;

  if (from == GST_GL_TEXTURE_TARGET_2D)
    from_str = "sampler2D";
  if (from == GST_GL_TEXTURE_TARGET_RECTANGLE)
    from_str = "sampler2DRect";
  if (from == GST_GL_TEXTURE_TARGET_EXTERNAL_OES)
    from_str = "samplerExternalOES";

  if (to == GST_GL_TEXTURE_TARGET_2D)
    to_str = "sampler2D";
  if (to == GST_GL_TEXTURE_TARGET_RECTANGLE)
    to_str = "sampler2DRect";
  if (to == GST_GL_TEXTURE_TARGET_EXTERNAL_OES)
    to_str = "samplerExternalOES";

  /* followed by some whitespace  */
  regex_find = g_strdup_printf ("%s(?=\\s)", from_str);
  regex = g_regex_new (regex_find, 0, 0, NULL);
  tmp = g_regex_replace_literal (regex, str, -1, 0, to_str, 0, NULL);
  g_free (regex_find);
  g_regex_unref (regex);

  if (tmp) {
    ret = tmp;
  } else {
    GST_FIXME ("Couldn't mangle sampler type successfully from %s to %s",
        from_str, to_str);
    ret = g_strdup (str);
  }

  return ret;
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

  if (!info->frag_body || info->in_n_textures == 0 || info->out_n_textures == 0)
    goto unhandled_format;

  /* XXX: poor mans shader templating */
  {
    GString *str = g_string_new (NULL);
    int i;

    if (info->templ->extensions)
      g_string_append (str, info->templ->extensions);

    if (convert->priv->from_texture_target == GST_GL_TEXTURE_TARGET_EXTERNAL_OES
        && info->templ->target != GST_GL_TEXTURE_TARGET_EXTERNAL_OES)
      g_string_append (str, glsl_OES_extension_string);

    if (info->templ->uniforms) {
      gchar *uniforms =
          _mangle_sampler_type (info->templ->uniforms, info->templ->target,
          convert->priv->from_texture_target);
      g_string_append (str, uniforms);
      g_free (uniforms);
    }

    for (i = 0; i < MAX_FUNCTIONS; i++) {
      gchar *function;

      if (info->templ->functions[i] == NULL)
        break;

      function =
          _mangle_texture_access (info->templ->functions[i],
          info->templ->target, convert->priv->from_texture_target);
      g_string_append_c (str, '\n');
      g_string_append (str, function);
      g_string_append_c (str, '\n');
      g_free (function);
    }

    g_string_append (str, "\nvarying vec2 v_texcoord;\nvoid main (void) {\n");
    if (info->frag_body) {
      gchar *body;

      g_string_append (str, "vec2 texcoord;\n");
      if (convert->priv->from_texture_target == GST_GL_TEXTURE_TARGET_RECTANGLE
          && info->templ->target != GST_GL_TEXTURE_TARGET_RECTANGLE) {
        g_string_append (str,
            "texcoord = v_texcoord * vec2 (width, height);\n");
      } else {
        g_string_append (str, "texcoord = v_texcoord;\n");
      }

      body =
          _mangle_texture_access (info->frag_body, info->templ->target,
          convert->priv->from_texture_target);
      g_string_append (str, body);
      g_free (body);
    }
    g_string_append (str, "\n}");
    info->frag_prog = g_string_free (str, FALSE);
  }

  if (!info->frag_prog)
    goto unhandled_format;

  /* multiple draw targets not supported on GLES2... */
  if (info->out_n_textures > 1 && !gl->DrawBuffers) {
    g_free (info->frag_prog);
    GST_ERROR ("Conversion requires output to multiple draw buffers");
    goto incompatible_api;
  }

  /* Requires reading from a RG/LA framebuffer... */
  if (USING_GLES2 (convert->context) &&
      (GST_VIDEO_INFO_FORMAT (&convert->out_info) == GST_VIDEO_FORMAT_YUY2 ||
          GST_VIDEO_INFO_FORMAT (&convert->out_info) ==
          GST_VIDEO_FORMAT_UYVY)) {
    g_free (info->frag_prog);
    GST_ERROR ("Conversion requires reading with an unsupported format");
    goto incompatible_api;
  }

  res =
      gst_gl_context_gen_shader (convert->context, text_vertex_shader,
      info->frag_prog, &convert->shader);
  g_free (info->frag_prog);
  if (!res)
    goto error;

  convert->priv->attr_position =
      gst_gl_shader_get_attribute_location (convert->shader, "a_position");
  convert->priv->attr_texture =
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
    if (info->shader_tex_names[i])
      gst_gl_shader_set_uniform_1i (convert->shader, info->shader_tex_names[i],
          i);
  }

  gst_gl_shader_set_uniform_1f (convert->shader, "width",
      GST_VIDEO_INFO_WIDTH (&convert->in_info));
  gst_gl_shader_set_uniform_1f (convert->shader, "height",
      GST_VIDEO_INFO_HEIGHT (&convert->in_info));

  if (info->chroma_sampling[0] > 0.0f && info->chroma_sampling[1] > 0.0f) {
    gst_gl_shader_set_uniform_2fv (convert->shader, "chroma_sampling", 1,
        info->chroma_sampling);
  }

  gst_gl_context_clear_shader (convert->context);

  if (!_init_convert_fbo (convert)) {
    goto error;
  }

  if (!convert->priv->vertex_buffer) {
    if (gl->GenVertexArrays) {
      gl->GenVertexArrays (1, &convert->priv->vao);
      gl->BindVertexArray (convert->priv->vao);
    }

    gl->GenBuffers (1, &convert->priv->vertex_buffer);
    gl->BindBuffer (GL_ARRAY_BUFFER, convert->priv->vertex_buffer);
    gl->BufferData (GL_ARRAY_BUFFER, 4 * 5 * sizeof (GLfloat), vertices,
        GL_STATIC_DRAW);

    gl->GenBuffers (1, &convert->priv->vbo_indices);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, convert->priv->vbo_indices);
    gl->BufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices), indices,
        GL_STATIC_DRAW);

    if (gl->GenVertexArrays) {
      _bind_buffer (convert);
      gl->BindVertexArray (0);
    }

    gl->BindBuffer (GL_ARRAY_BUFFER, 0);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
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
  GLenum internal_format;

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
  if (USING_OPENGL (convert->context) || USING_OPENGL3 (convert->context)) {
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
  internal_format =
      gst_gl_sized_gl_format_from_gl_format_type (convert->context, GL_RGBA,
      GL_UNSIGNED_BYTE);
  gl->TexImage2D (GL_TEXTURE_2D, 0, internal_format, out_width, out_height, 0,
      GL_RGBA, GL_UNSIGNED_BYTE, NULL);
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

    gl->DeleteTextures (1, &fake_texture);

    return FALSE;
  }

  /* unbind the FBO */
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  gl->DeleteTextures (1, &fake_texture);

  return TRUE;
}

static gboolean
_do_convert_one_view (GstGLContext * context, GstGLColorConvert * convert,
    guint view_num)
{
  guint in_width, in_height, out_width, out_height;
  struct ConvertInfo *c_info = &convert->priv->convert_info;
  GstMapInfo out_info[GST_VIDEO_MAX_PLANES], in_info[GST_VIDEO_MAX_PLANES];
  gboolean res = TRUE;
  gint i, j = 0;
  const gint in_plane_offset = view_num * c_info->in_n_textures;
  const gint out_plane_offset = view_num * c_info->out_n_textures;

  out_width = GST_VIDEO_INFO_WIDTH (&convert->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&convert->out_info);
  in_width = GST_VIDEO_INFO_WIDTH (&convert->in_info);
  in_height = GST_VIDEO_INFO_HEIGHT (&convert->in_info);

  for (i = 0; i < c_info->in_n_textures; i++) {
    convert->priv->in_tex[i] =
        (GstGLMemory *) gst_buffer_peek_memory (convert->inbuf,
        i + in_plane_offset);
    if (!gst_is_gl_memory ((GstMemory *) convert->priv->in_tex[i])) {
      GST_ERROR_OBJECT (convert, "input must be GstGLMemory");
      res = FALSE;
      goto out;
    }
    if (!gst_memory_map ((GstMemory *) convert->priv->in_tex[i], &in_info[i],
            GST_MAP_READ | GST_MAP_GL)) {
      GST_ERROR_OBJECT (convert, "failed to map input memory %p",
          convert->priv->in_tex[i]);
      res = FALSE;
      goto out;
    }
  }

  for (j = 0; j < c_info->out_n_textures; j++) {
    GstGLMemory *out_tex =
        (GstGLMemory *) gst_buffer_peek_memory (convert->outbuf,
        j + out_plane_offset);
    gint mem_width, mem_height;

    if (!gst_is_gl_memory ((GstMemory *) out_tex)) {
      GST_ERROR_OBJECT (convert, "output must be GstGLMemory");
      res = FALSE;
      goto out;
    }

    mem_width = gst_gl_memory_get_texture_width (out_tex);
    mem_height = gst_gl_memory_get_texture_height (out_tex);

    if (out_tex->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE
        || out_tex->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA
        || out_width != mem_width || out_height != mem_height) {
      /* Luminance formats are not color renderable */
      /* renderering to a framebuffer only renders the intersection of all
       * the attachments i.e. the smallest attachment size */
      GstVideoInfo temp_info;

      gst_video_info_set_format (&temp_info, GST_VIDEO_FORMAT_RGBA, out_width,
          out_height);

      if (!convert->priv->out_tex[j])
        convert->priv->out_tex[j] =
            (GstGLMemory *) gst_gl_memory_alloc (context,
            convert->priv->to_texture_target, NULL, &temp_info, 0, NULL);
    } else {
      convert->priv->out_tex[j] = out_tex;
    }

    if (!gst_memory_map ((GstMemory *) convert->priv->out_tex[j], &out_info[j],
            GST_MAP_WRITE | GST_MAP_GL)) {
      GST_ERROR_OBJECT (convert, "failed to map output memory %p",
          convert->priv->out_tex[i]);
      res = FALSE;
      goto out;
    }
  }

  GST_LOG_OBJECT (convert, "converting to textures:%p,%p,%p,%p "
      "dimensions:%ux%u, from textures:%p,%p,%p,%p dimensions:%ux%u",
      convert->priv->out_tex[0], convert->priv->out_tex[1],
      convert->priv->out_tex[2], convert->priv->out_tex[3], out_width,
      out_height, convert->priv->in_tex[0], convert->priv->in_tex[1],
      convert->priv->in_tex[2], convert->priv->in_tex[3], in_width, in_height);

  if (!_do_convert_draw (context, convert))
    res = FALSE;

out:
  for (j--; j >= 0; j--) {
    GstGLMemory *out_tex =
        (GstGLMemory *) gst_buffer_peek_memory (convert->outbuf,
        j + out_plane_offset);
    gint mem_width, mem_height;

    gst_memory_unmap ((GstMemory *) convert->priv->out_tex[j], &out_info[j]);

    mem_width = gst_gl_memory_get_texture_width (out_tex);
    mem_height = gst_gl_memory_get_texture_height (out_tex);

    if (out_tex->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE
        || out_tex->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA
        || out_width != mem_width || out_height != mem_height) {
      GstMapInfo to_info, from_info;

      if (!gst_memory_map ((GstMemory *) convert->priv->out_tex[j], &from_info,
              GST_MAP_READ | GST_MAP_GL)) {
        gst_gl_context_set_error (convert->context, "Failed to map "
            "intermediate memory");
        res = FALSE;
        continue;
      }
      if (!gst_memory_map ((GstMemory *) out_tex, &to_info,
              GST_MAP_WRITE | GST_MAP_GL)) {
        gst_gl_context_set_error (convert->context, "Failed to map "
            "intermediate memory");
        res = FALSE;
        continue;
      }
      gst_gl_memory_copy_into_texture (convert->priv->out_tex[j],
          out_tex->tex_id, convert->priv->to_texture_target, out_tex->tex_type,
          mem_width, mem_height, GST_VIDEO_INFO_PLANE_STRIDE (&out_tex->info,
              out_tex->plane), FALSE);
      gst_memory_unmap ((GstMemory *) convert->priv->out_tex[j], &from_info);
      gst_memory_unmap ((GstMemory *) out_tex, &to_info);
    } else {
      convert->priv->out_tex[j] = NULL;
    }
  }

  /* YV12 the same as I420 except planes 1+2 swapped */
  if (GST_VIDEO_INFO_FORMAT (&convert->out_info) == GST_VIDEO_FORMAT_YV12) {
    GstMemory *mem1 =
        gst_buffer_get_memory (convert->outbuf, 1 + out_plane_offset);
    GstMemory *mem2 =
        gst_buffer_get_memory (convert->outbuf, 2 + out_plane_offset);

    gst_buffer_replace_memory (convert->outbuf, 1 + out_plane_offset, mem2);
    gst_buffer_replace_memory (convert->outbuf, 2 + out_plane_offset, mem1);
  }

  for (i--; i >= 0; i--) {
    gst_memory_unmap ((GstMemory *) convert->priv->in_tex[i], &in_info[i]);
  }

  return res;
}

/* Called by the idle function in the gl thread */
void
_do_convert (GstGLContext * context, GstGLColorConvert * convert)
{
  GstVideoInfo *in_info = &convert->in_info;
  gboolean res = TRUE;
  gint views, v;
  GstVideoOverlayCompositionMeta *composition_meta;
  GstGLSyncMeta *sync_meta;

  convert->outbuf = NULL;

  if (!_init_convert (convert)) {
    convert->priv->result = FALSE;
    return;
  }

  sync_meta = gst_buffer_get_gl_sync_meta (convert->inbuf);
  if (sync_meta)
    gst_gl_sync_meta_wait (sync_meta, convert->context);

  convert->outbuf = gst_buffer_new ();
  if (!gst_gl_memory_setup_buffer (convert->context,
          convert->priv->to_texture_target, NULL, &convert->out_info, NULL,
          convert->outbuf)) {
    convert->priv->result = FALSE;
    return;
  }

  if (GST_VIDEO_INFO_MULTIVIEW_MODE (in_info) ==
      GST_VIDEO_MULTIVIEW_MODE_SEPARATED)
    views = GST_VIDEO_INFO_VIEWS (in_info);
  else
    views = 1;

  gst_gl_insert_debug_marker (context, "%s converting from %s to %s",
      GST_OBJECT_NAME (convert),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&convert->out_info)));
  /* Handle all views on input and output one at a time */
  for (v = 0; res && v < views; v++)
    res = _do_convert_one_view (context, convert, v);

  if (!res) {
    gst_buffer_unref (convert->outbuf);
    convert->outbuf = NULL;
  }

  if (convert->outbuf) {
    GstGLSyncMeta *sync_meta =
        gst_buffer_add_gl_sync_meta (convert->context, convert->outbuf);

    if (sync_meta)
      gst_gl_sync_meta_set_sync_point (sync_meta, convert->context);
  }

  composition_meta =
      gst_buffer_get_video_overlay_composition_meta (convert->inbuf);
  if (composition_meta) {
    GST_DEBUG ("found video overlay composition meta, appliying on output.");
    gst_buffer_add_video_overlay_composition_meta
        (convert->outbuf, composition_meta->overlay);
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

  GLint viewport_dim[4] = { 0 };

  GLenum multipleRT[] = {
    GL_COLOR_ATTACHMENT0,
    GL_COLOR_ATTACHMENT1,
    GL_COLOR_ATTACHMENT2
  };

  gl = context->gl_vtable;

  out_width = GST_VIDEO_INFO_WIDTH (&convert->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&convert->out_info);

  gl->BindFramebuffer (GL_FRAMEBUFFER, convert->fbo);

  /* attach the texture to the FBO to renderer to */
  for (i = 0; i < c_info->out_n_textures; i++) {
    guint gl_target =
        gst_gl_texture_target_to_gl (convert->priv->to_texture_target);

    /* needed? */
    gl->BindTexture (gl_target, convert->priv->out_tex[i]->tex_id);

    gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
        gl_target, convert->priv->out_tex[i]->tex_id, 0);
  }

  if (gl->DrawBuffers)
    gl->DrawBuffers (c_info->out_n_textures, multipleRT);
  else if (gl->DrawBuffer)
    gl->DrawBuffer (GL_COLOR_ATTACHMENT0);

  gl->GetIntegerv (GL_VIEWPORT, viewport_dim);

  gl->Viewport (0, 0, out_width, out_height);

  gst_gl_shader_use (convert->shader);

  if (gl->BindVertexArray)
    gl->BindVertexArray (convert->priv->vao);
  else
    _bind_buffer (convert);

  for (i = c_info->in_n_textures - 1; i >= 0; i--) {
    gchar *scale_name = g_strdup_printf ("tex_scale%u", i);
    guint gl_target =
        gst_gl_texture_target_to_gl (convert->priv->from_texture_target);

    gl->ActiveTexture (GL_TEXTURE0 + i);
    gl->BindTexture (gl_target, convert->priv->in_tex[i]->tex_id);
    gl->TexParameteri (gl_target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri (gl_target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri (gl_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri (gl_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gst_gl_shader_set_uniform_2fv (convert->shader, scale_name, 1,
        convert->priv->in_tex[i]->tex_scaling);

    g_free (scale_name);
  }

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

  if (gl->BindVertexArray)
    gl->BindVertexArray (0);
  else
    _unbind_buffer (convert);

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
