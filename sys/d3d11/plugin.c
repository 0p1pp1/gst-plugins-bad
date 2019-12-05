/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#include <gst/gst.h>
#include "gstd3d11videosink.h"
#include "gstd3d11upload.h"
#include "gstd3d11download.h"
#include "gstd3d11colorconvert.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_element_register (plugin,
      "d3d11videosink", GST_RANK_SECONDARY - 1, GST_TYPE_D3D11_VIDEO_SINK);

  gst_element_register (plugin,
      "d3d11upload", GST_RANK_NONE, GST_TYPE_D3D11_UPLOAD);
  gst_element_register (plugin,
      "d3d11download", GST_RANK_NONE, GST_TYPE_D3D11_DOWNLOAD);
  gst_element_register (plugin,
      "d3d11colorconvert", GST_RANK_NONE, GST_TYPE_D3D11_COLOR_CONVERT);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    d3d11,
    "Direct3D11 plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
