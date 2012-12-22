/* GStreamer
 * Copyright (C) 2012 Roland Krikava <info@bluedigits.com>
 * Copyright (C) 2010-2011 David Hoyt <dhoyt@hoytsoft.org>
 * Copyright (C) 2010 Andoni Morales <ylatuya@gmail.com>
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
#ifndef _D3DHELPERS_H_
#define _D3DHELPERS_H_

#include <gst/gst.h>
#include <gst/video/video.h>

#include <windows.h>
#include <d3d9.h>
#include <d3dx9tex.h>

typedef struct _GstD3DVideoSink GstD3DVideoSink;
typedef struct _GstD3DVideoSinkClass GstD3DVideoSinkClass;

typedef struct _GstD3DDisplayDevice {
  UINT                   adapter;
  D3DFORMAT              format;
  D3DTEXTUREFILTERTYPE   filter_type;
  LPDIRECT3DDEVICE9      d3d_device;
  D3DPRESENT_PARAMETERS  present_params;
} GstD3DDisplayDevice;

typedef struct _GstD3DDataClass {
  guint                  refs;
  LPDIRECT3D9            d3d;
  GstD3DDisplayDevice    device;

  /* Track individual sink instances */
  GList *                sink_list;
  gboolean               device_lost;

  /* Window class for internal windows */
  WNDCLASS               wnd_class;

  /* Windows Message Handling */
  GThread *              thread;
  HWND                   hidden_window;
  gboolean               running;
  gboolean               error_exit;
} GstD3DDataClass;

typedef struct _GstD3DData {
  /* Window Proc Stuff */
  HWND                   window_handle;
  gboolean               window_is_internal;
  WNDPROC                orig_wnd_proc;

  /* Render Constructs */
  LPDIRECT3DSWAPCHAIN9   swapchain;
  LPDIRECT3DSURFACE9     surface;
  D3DTEXTUREFILTERTYPE   filtertype;
  D3DFORMAT              format;
  GstVideoRectangle    * render_rect;
  gboolean               renderable;
  gboolean               device_lost;
} GstD3DData;


gboolean       d3d_class_init(GstD3DVideoSink * klass);
void           d3d_class_destroy(GstD3DVideoSink * klass);

gboolean       d3d_prepare_window(GstD3DVideoSink * sink);
gboolean       d3d_stop(GstD3DVideoSink * sink);
void           d3d_set_window_handle(GstD3DVideoSink * sink, guintptr window_id, gboolean internal);
void           d3d_set_render_rectangle(GstD3DVideoSink * sink);
void           d3d_expose_window(GstD3DVideoSink * sink);
GstFlowReturn  d3d_render_buffer(GstD3DVideoSink * sink, GstBuffer * buf);
GstCaps *      d3d_supported_caps(GstD3DVideoSink * sink);
gboolean       d3d_set_render_format(GstD3DVideoSink * sink);

#endif /* _D3DHELPERS_H_ */
