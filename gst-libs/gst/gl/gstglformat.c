/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#include <gst/gl/gstglformat.h>
#include <gst/gl/gstglcontext.h>

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif

#ifndef GL_TEXTURE_RECTANGLE
#define GL_TEXTURE_RECTANGLE 0x84F5
#endif
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

static inline guint
_gl_format_n_components (guint format)
{
  switch (format) {
    case GST_VIDEO_GL_TEXTURE_TYPE_RGBA:
    case GL_RGBA:
      return 4;
    case GST_VIDEO_GL_TEXTURE_TYPE_RGB:
    case GST_VIDEO_GL_TEXTURE_TYPE_RGB16:
    case GL_RGB:
      return 3;
    case GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA:
    case GST_VIDEO_GL_TEXTURE_TYPE_RG:
    case GL_LUMINANCE_ALPHA:
    case GL_RG:
      return 2;
    case GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE:
    case GST_VIDEO_GL_TEXTURE_TYPE_R:
    case GL_LUMINANCE:
    case GL_RED:
      return 1;
    default:
      return 0;
  }
}

static inline guint
_gl_type_n_components (guint type)
{
  switch (type) {
    case GL_UNSIGNED_BYTE:
      return 1;
    case GL_UNSIGNED_SHORT_5_6_5:
      return 3;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

static inline guint
_gl_type_n_bytes (guint type)
{
  switch (type) {
    case GL_UNSIGNED_BYTE:
      return 1;
    case GL_UNSIGNED_SHORT_5_6_5:
      return 2;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

/**
 * gst_gl_format_type_n_bytes:
 * @format: the OpenGL format, %GL_RGBA, %GL_LUMINANCE, etc
 * @type: the OpenGL type, %GL_UNSIGNED_BYTE, %GL_FLOAT, etc
 *
 * Returns: the number of bytes the specified @format, @type combination takes
 * per pixel
 */
guint
gst_gl_format_type_n_bytes (guint format, guint type)
{
  return _gl_format_n_components (format) / _gl_type_n_components (type) *
      _gl_type_n_bytes (type);
}

/**
 * gst_gl_texture_type_n_bytes:
 * @tex_format: a #GstVideoGLTextureType
 *
 * Returns: the number of bytes @tex_format used per pixel
 */
guint
gst_gl_texture_type_n_bytes (GstVideoGLTextureType tex_format)
{
  guint format, type;

  format = gst_gl_format_from_gl_texture_type (tex_format);
  type = GL_UNSIGNED_BYTE;
  if (tex_format == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    type = GL_UNSIGNED_SHORT_5_6_5;

  return gst_gl_format_type_n_bytes (format, type);
}

/**
 * gst_gl_format_from_gl_texture_type:
 * @tex_format: a #GstVideoGLTextureType
 *
 * Returns: the OpenGL format specified by @tex_format
 */
guint
gst_gl_format_from_gl_texture_type (GstVideoGLTextureType tex_format)
{
  switch (tex_format) {
    case GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA:
      return GL_LUMINANCE_ALPHA;
    case GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE:
      return GL_LUMINANCE;
    case GST_VIDEO_GL_TEXTURE_TYPE_RGBA:
      return GL_RGBA;
    case GST_VIDEO_GL_TEXTURE_TYPE_RGB:
    case GST_VIDEO_GL_TEXTURE_TYPE_RGB16:
      return GL_RGB;
    case GST_VIDEO_GL_TEXTURE_TYPE_RG:
      return GL_RG;
    case GST_VIDEO_GL_TEXTURE_TYPE_R:
      return GL_RED;
    default:
      return GST_VIDEO_GL_TEXTURE_TYPE_RGBA;
  }
}

/**
 * gst_gl_texture_type_from_format:
 * @context: a #GstGLContext
 * @v_format: a #GstVideoFormat
 * @plane: the plane number (starting from 0)
 *
 * Returns: the #GstVideoGLTextureType for the specified @format and @plane
 *          that can be allocated using @context
 */
GstVideoGLTextureType
gst_gl_texture_type_from_format (GstGLContext * context,
    GstVideoFormat v_format, guint plane)
{
  gboolean texture_rg =
      gst_gl_context_check_feature (context, "GL_EXT_texture_rg")
      || gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0)
      || gst_gl_context_check_feature (context, "GL_ARB_texture_rg")
      || gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 0);
  guint n_plane_components;

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_AYUV:
      n_plane_components = 4;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      n_plane_components = 3;
      break;
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
      return GST_VIDEO_GL_TEXTURE_TYPE_RGB16;
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      n_plane_components = 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      n_plane_components = plane == 0 ? 1 : 2;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      n_plane_components = 1;
      break;
    default:
      n_plane_components = 4;
      g_assert_not_reached ();
      break;
  }

  switch (n_plane_components) {
    case 4:
      return GST_VIDEO_GL_TEXTURE_TYPE_RGBA;
      break;
    case 3:
      return GST_VIDEO_GL_TEXTURE_TYPE_RGB;
      break;
    case 2:
      return texture_rg ? GST_VIDEO_GL_TEXTURE_TYPE_RG :
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA;
      break;
    case 1:
      return texture_rg ? GST_VIDEO_GL_TEXTURE_TYPE_R :
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return GST_VIDEO_GL_TEXTURE_TYPE_RGBA;
}

/**
 * gst_gl_sized_gl_format_from_gl_format_type:
 * @context: a #GstGLContext
 * @format: an OpenGL format, %GL_RGBA, %GL_LUMINANCE, etc
 * @type: an OpenGL type, %GL_UNSIGNED_BYTE, %GL_FLOAT, etc
 *
 * Returns: the sized internal format specified by @format and @type that can
 *          be used in @context
 */
guint
gst_gl_sized_gl_format_from_gl_format_type (GstGLContext * context,
    guint format, guint type)
{
  gboolean ext_texture_rg =
      gst_gl_context_check_feature (context, "GL_EXT_texture_rg");

  switch (format) {
    case GL_RGBA:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          return USING_GLES2 (context)
              && !USING_GLES3 (context) ? GL_RGBA : GL_RGBA8;
          break;
      }
      break;
    case GL_RGB:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          return GL_RGB8;
          break;
        case GL_UNSIGNED_SHORT_5_6_5:
          return GL_RGB;
          break;
      }
      break;
    case GL_RG:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          if (!USING_GLES3 (context) && USING_GLES2 (context) && ext_texture_rg)
            return GL_RG;
          return GL_RG8;
          break;
      }
      break;
    case GL_RED:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          if (!USING_GLES3 (context) && USING_GLES2 (context) && ext_texture_rg)
            return GL_RED;
          return GL_R8;
          break;
      }
      break;
    case GL_LUMINANCE:
      return GL_LUMINANCE;
      break;
    case GL_LUMINANCE_ALPHA:
      return GL_LUMINANCE_ALPHA;
      break;
    case GL_ALPHA:
      return GL_ALPHA;
      break;
    default:
      break;
  }

  g_assert_not_reached ();
  return 0;
}

/**
 * gst_gl_texture_target_to_string:
 * @target: a #GstGLTextureTarget
 *
 * Returns: the stringified version of @target or %NULL
 */
const gchar *
gst_gl_texture_target_to_string (GstGLTextureTarget target)
{
  switch (target) {
    case GST_GL_TEXTURE_TARGET_2D:
      return GST_GL_TEXTURE_TARGET_2D_STR;
    case GST_GL_TEXTURE_TARGET_RECTANGLE:
      return GST_GL_TEXTURE_TARGET_RECTANGLE_STR;
    case GST_GL_TEXTURE_TARGET_EXTERNAL_OES:
      return GST_GL_TEXTURE_TARGET_EXTERNAL_OES_STR;
    default:
      return NULL;
  }
}

/**
 * gst_gl_texture_target_to_string:
 * @str: a string equivalant to one of the GST_GL_TEXTURE_TARGET_*_STR values
 *
 * Returns: the #GstGLTextureTarget represented by @str or
 *          %GST_GL_TEXTURE_TARGET_NONE
 */
GstGLTextureTarget
gst_gl_texture_target_from_string (const gchar * str)
{
  if (!str)
    return GST_GL_TEXTURE_TARGET_NONE;

  if (g_strcmp0 (str, GST_GL_TEXTURE_TARGET_2D_STR) == 0)
    return GST_GL_TEXTURE_TARGET_2D;
  if (g_strcmp0 (str, GST_GL_TEXTURE_TARGET_RECTANGLE_STR) == 0)
    return GST_GL_TEXTURE_TARGET_RECTANGLE;
  if (g_strcmp0 (str, GST_GL_TEXTURE_TARGET_EXTERNAL_OES_STR) == 0)
    return GST_GL_TEXTURE_TARGET_EXTERNAL_OES;

  return GST_GL_TEXTURE_TARGET_NONE;
}

/**
 * gst_gl_texture_target_to_gl:
 * @target: a #GstGLTextureTarget
 *
 * Returns: the OpenGL value for binding the @target with glBindTexture() and
 *          similar functions or 0
 */
guint
gst_gl_texture_target_to_gl (GstGLTextureTarget target)
{
  switch (target) {
    case GST_GL_TEXTURE_TARGET_2D:
      return GL_TEXTURE_2D;
    case GST_GL_TEXTURE_TARGET_RECTANGLE:
      return GL_TEXTURE_RECTANGLE;
    case GST_GL_TEXTURE_TARGET_EXTERNAL_OES:
      return GL_TEXTURE_EXTERNAL_OES;
    default:
      return 0;
  }
}

/**
 * gst_gl_texture_target_from_gl:
 * @target: an OpenGL texture binding target
 *
 * Returns: the #GstGLTextureTarget that's equiavalant to @target or
 *          %GST_GL_TEXTURE_TARGET_NONE
 */
GstGLTextureTarget
gst_gl_texture_target_from_gl (guint target)
{
  switch (target) {
    case GL_TEXTURE_2D:
      return GST_GL_TEXTURE_TARGET_2D;
    case GL_TEXTURE_RECTANGLE:
      return GST_GL_TEXTURE_TARGET_RECTANGLE;
    case GL_TEXTURE_EXTERNAL_OES:
      return GST_GL_TEXTURE_TARGET_EXTERNAL_OES;
    default:
      return GST_GL_TEXTURE_TARGET_NONE;
  }
}

/**
 * gst_gl_texture_target_to_buffer_pool_option:
 * @target: a #GstGLTextureTarget
 *
 * Returns: a string representing the @GstBufferPoolOption specified by @target
 */
const gchar *
gst_gl_texture_target_to_buffer_pool_option (GstGLTextureTarget target)
{
  switch (target) {
    case GST_GL_TEXTURE_TARGET_2D:
      return GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_2D;
    case GST_GL_TEXTURE_TARGET_RECTANGLE:
      return GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_RECTANGLE;
    case GST_GL_TEXTURE_TARGET_EXTERNAL_OES:
      return GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_EXTERNAL_OES;
    default:
      return NULL;
  }
}
