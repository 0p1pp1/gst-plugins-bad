/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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

#include "gstgltestsrc.h"
#include "gstglupload.h"
#include "gstglfiltercube.h"
#include "gstglfilterlaplacian.h"
#include "gstglfilterapp.h"
#include "gstgldownload.h"
#include "gstglimagesink.h"
#include "gstglcolorscale.h"

GType gst_gl_filter_app_get_type (void);
GType gst_gl_filter_cube_get_type (void);
GType gst_gl_filterblur_get_type (void);
GType gst_gl_filter_edge_get_type (void);
GType gst_gl_filter_laplacian_get_type (void);

#define GST_CAT_DEFAULT gst_gl_gstgl_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* Register filters that make up the gstgl plugin */
static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_gl_gstgl_debug, "gstopengl", 0, "gstopengl");

  if (!gst_element_register (plugin, "gltestsrc",
          GST_RANK_NONE, GST_TYPE_GL_TEST_SRC)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glupload",
          GST_RANK_NONE, GST_TYPE_GL_UPLOAD)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfiltercube",
          GST_RANK_NONE, GST_TYPE_GL_FILTER_CUBE)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfilterblur",
          GST_RANK_NONE, gst_gl_filterblur_get_type())) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfilterlaplacian",
          GST_RANK_NONE, GST_TYPE_GL_FILTER_LAPLACIAN)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfilterapp",
          GST_RANK_NONE, GST_TYPE_GL_FILTER_APP)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "gldownload",
          GST_RANK_NONE, GST_TYPE_GL_DOWNLOAD)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glimagesink",
          GST_RANK_NONE, GST_TYPE_GLIMAGE_SINK)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glcolorscale",
          GST_RANK_NONE, GST_TYPE_GL_COLORSCALE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "opengl",
    "OpenGL plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
