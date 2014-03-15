/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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

#include "gstglapi.h"

/**
 * gst_gl_api_to_string():
 *
 * @api: a #GstGLAPI to stringify
 *
 * Returns: A space seperated string of the OpenGL api's enabled in @api
 */
gchar *
gst_gl_api_to_string (GstGLAPI api)
{
  GString *str = NULL;
  gchar *ret;

  if (api == GST_GL_API_NONE) {
    str = g_string_new ("none");
    return str->str;
  } else if (api == GST_GL_API_ANY) {
    str = g_string_new ("any");
    return str->str;
  }

  if (api & GST_GL_API_OPENGL) {
    str = g_string_new (GST_GL_API_OPENGL_NAME);
  }
  if (api & GST_GL_API_OPENGL3) {
    if (str) {
      g_string_append (str, " " GST_GL_API_OPENGL3_NAME);
    } else {
      str = g_string_new (GST_GL_API_OPENGL3_NAME);
    }
  }
  if (api & GST_GL_API_GLES1) {
    if (str) {
      g_string_append (str, " " GST_GL_API_GLES1_NAME);
    } else {
      str = g_string_new (GST_GL_API_GLES1_NAME);
    }
  }
  if (api & GST_GL_API_GLES2) {
    if (str) {
      g_string_append (str, " " GST_GL_API_GLES2_NAME);
    } else {
      str = g_string_new (GST_GL_API_GLES2_NAME);
    }
  }
  if (api & GST_GL_API_GLES3) {
    if (str) {
      g_string_append (str, " " GST_GL_API_GLES3_NAME);
    } else {
      str = g_string_new (GST_GL_API_GLES3_NAME);
    }
  }

  ret = g_string_free (str, FALSE);

  return ret;
}

/**
 * gst_gl_api_from_string():
 *
 * @apis_s: a space seperated string of OpenGL apis
 *
 * Returns: The #GstGLAPI represented by @apis_s
 */
GstGLAPI
gst_gl_api_from_string (const gchar * apis_s)
{
  GstGLAPI ret = GST_GL_API_NONE;
  gchar *apis = (gchar *) apis_s;

  if (!apis || apis[0] == '\0') {
    ret = GST_GL_API_ANY;
  } else {
    while (apis) {
      if (apis[0] == '\0') {
        break;
      } else if (apis[0] == ' ' || apis[0] == ',') {
        apis = &apis[1];
      } else if (g_strstr_len (apis, 7, GST_GL_API_OPENGL3_NAME)) {
        ret |= GST_GL_API_OPENGL3;
        apis = &apis[7];
      } else if (g_strstr_len (apis, 6, GST_GL_API_OPENGL_NAME)) {
        ret |= GST_GL_API_OPENGL;
        apis = &apis[6];
      } else if (g_strstr_len (apis, 5, GST_GL_API_GLES1_NAME)) {
        ret |= GST_GL_API_GLES1;
        apis = &apis[5];
      } else if (g_strstr_len (apis, 5, GST_GL_API_GLES2_NAME)) {
        ret |= GST_GL_API_GLES2;
        apis = &apis[5];
      } else if (g_strstr_len (apis, 5, GST_GL_API_GLES3_NAME)) {
        ret |= GST_GL_API_GLES3;
        apis = &apis[5];
      } else {
        GST_ERROR ("Error parsing \'%s\'", apis);
        break;
      }
    }
  }

  return ret;
}
