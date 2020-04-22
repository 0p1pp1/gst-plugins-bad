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

#include "gstd3d11config.h"

#include <gst/gst.h>
#include "gstd3d11videosink.h"
#include "gstd3d11upload.h"
#include "gstd3d11download.h"
#include "gstd3d11colorconvert.h"
#include "gstd3d11videosinkbin.h"
#ifdef HAVE_DXVA_H
#include "gstd3d11utils.h"
#include "gstd3d11h264dec.h"
#include "gstd3d11h265dec.h"
#include "gstd3d11vp9dec.h"
#include "gstd3d11vp8dec.h"
#endif

GST_DEBUG_CATEGORY (gst_d3d11_shader_debug);
GST_DEBUG_CATEGORY (gst_d3d11_colorconverter_debug);
GST_DEBUG_CATEGORY (gst_d3d11_utils_debug);
GST_DEBUG_CATEGORY (gst_d3d11_format_debug);
GST_DEBUG_CATEGORY (gst_d3d11_device_debug);
GST_DEBUG_CATEGORY (gst_d3d11_overlay_compositor_debug);
GST_DEBUG_CATEGORY (gst_d3d11_window_debug);
GST_DEBUG_CATEGORY (gst_d3d11_video_processor_debug);

#if (HAVE_D3D11SDKLAYERS_H || HAVE_DXGIDEBUG_H)
GST_DEBUG_CATEGORY (gst_d3d11_debug_layer_debug);
#endif

#ifdef HAVE_DXVA_H
GST_DEBUG_CATEGORY (gst_d3d11_h264_dec_debug);
GST_DEBUG_CATEGORY (gst_d3d11_h265_dec_debug);
GST_DEBUG_CATEGORY (gst_d3d11_vp9_dec_debug);
GST_DEBUG_CATEGORY (gst_d3d11_vp8_dec_debug);
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_d3d11_shader_debug,
      "d3d11shader", 0, "d3d11shader");
  GST_DEBUG_CATEGORY_INIT (gst_d3d11_colorconverter_debug,
      "d3d11colorconverter", 0, "d3d11colorconverter");
  GST_DEBUG_CATEGORY_INIT (gst_d3d11_utils_debug,
      "d3d11utils", 0, "d3d11 utility functions");
  GST_DEBUG_CATEGORY_INIT (gst_d3d11_format_debug,
      "d3d11format", 0, "d3d11 specific formats");
  GST_DEBUG_CATEGORY_INIT (gst_d3d11_device_debug,
      "d3d11device", 0, "d3d11 device object");
  GST_DEBUG_CATEGORY_INIT (gst_d3d11_overlay_compositor_debug,
      "d3d11overlaycompositor", 0, "d3d11overlaycompositor");
  GST_DEBUG_CATEGORY_INIT (gst_d3d11_window_debug,
      "d3d11window", 0, "d3d11window");
  GST_DEBUG_CATEGORY_INIT (gst_d3d11_video_processor_debug,
      "d3d11videoprocessor", 0, "d3d11videoprocessor");

#if (HAVE_D3D11SDKLAYERS_H || HAVE_DXGIDEBUG_H)
  /* NOTE: enabled only for debug build */
  GST_DEBUG_CATEGORY_INIT (gst_d3d11_debug_layer_debug,
      "d3d11debuglayer", 0, "native d3d11 and dxgi debug");
#endif

  gst_element_register (plugin,
      "d3d11upload", GST_RANK_NONE, GST_TYPE_D3D11_UPLOAD);
  gst_element_register (plugin,
      "d3d11download", GST_RANK_NONE, GST_TYPE_D3D11_DOWNLOAD);
  gst_element_register (plugin,
      "d3d11colorconvert", GST_RANK_NONE, GST_TYPE_D3D11_COLOR_CONVERT);
  gst_element_register (plugin,
      "d3d11videosinkelement", GST_RANK_NONE, GST_TYPE_D3D11_VIDEO_SINK);
  gst_element_register (plugin,
      "d3d11videosink", GST_RANK_SECONDARY - 1, GST_TYPE_D3D11_VIDEO_SINK_BIN);

#ifdef HAVE_DXVA_H
  /* DXVA2 API is availble since Windows 8 */
  if (gst_d3d11_is_windows_8_or_greater ()) {
    GstD3D11Device *device = NULL;
    gint i = 0;

    GST_DEBUG_CATEGORY_INIT (gst_d3d11_h264_dec_debug,
        "d3d11h264dec", 0, "Direct3D11 H.264 Video Decoder");
    GST_DEBUG_CATEGORY_INIT (gst_d3d11_vp9_dec_debug,
        "d3d11vp9dec", 0, "Direct3D11 VP9 Video Decoder");
    GST_DEBUG_CATEGORY_INIT (gst_d3d11_h265_dec_debug,
        "d3d11h265dec", 0, "Direct3D11 H.265 Video Decoder");
    GST_DEBUG_CATEGORY_INIT (gst_d3d11_vp8_dec_debug,
        "d3d11vp8dec", 0, "Direct3D11 VP8 Decoder");

    while ((device = gst_d3d11_device_new (i)) != NULL) {
      GstD3D11Decoder *decoder = NULL;
      gboolean legacy;
      gboolean hardware;

      g_object_get (device, "hardware", &hardware, NULL);
      if (!hardware)
        goto clear;

      decoder = gst_d3d11_decoder_new (device);
      if (!decoder)
        goto clear;

      legacy = gst_d3d11_decoder_util_is_legacy_device (device);

      gst_d3d11_h264_dec_register (plugin,
          device, decoder, GST_RANK_SECONDARY, legacy);
      if (!legacy) {
        gst_d3d11_h265_dec_register (plugin, device, decoder,
            GST_RANK_SECONDARY);
        gst_d3d11_vp9_dec_register (plugin, device, decoder,
            GST_RANK_SECONDARY);
        gst_d3d11_vp8_dec_register (plugin, device, decoder,
            GST_RANK_SECONDARY);
      }

    clear:
      gst_clear_object (&device);
      gst_clear_object (&decoder);
      i++;
    }
  }
#endif

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    d3d11,
    "Direct3D11 plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
