/*
 * GStreamer Wayland video sink
 * Copyright (C) 2011 Intel Corporation
 * Copyright (C) 2011 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __GST_WAYLAND_VIDEO_SINK_H__
#define __GST_WAYLAND_VIDEO_SINK_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>

#include <wayland-client.h>

#define GST_TYPE_WAYLAND_SINK \
	    (gst_wayland_sink_get_type())
#define GST_WAYLAND_SINK(obj) \
	    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WAYLAND_SINK,GstWaylandSink))
#define GST_WAYLAND_SINK_CLASS(klass) \
	    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WAYLAND_SINK,GstWaylandSinkClass))
#define GST_IS_WAYLAND_SINK(obj) \
	    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WAYLAND_SINK))
#define GST_IS_WAYLAND_SINK_CLASS(klass) \
	    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WAYLAND_SINK))
#define GST_WAYLAND_SINK_GET_CLASS(inst) \
        (G_TYPE_INSTANCE_GET_CLASS ((inst), GST_TYPE_WAYLAND_SINK, GstWaylandSinkClass))

struct  display
{
  struct wl_display *display;
  struct wl_visual *xrgb_visual;
  struct wl_compositor *compositor;
  struct wl_shell *shell;
  struct wl_shm *shm;
  uint32_t mask;
};

struct window
{
  struct display *display;
  int width, height;
  struct wl_surface *surface;
  struct wl_buffer *buffer;
};

typedef struct _GstWaylandSink GstWaylandSink;
typedef struct _GstWaylandSinkClass GstWaylandSinkClass;

#define GST_TYPE_WLBUFFER (gst_wlbuffer_get_type())
#define GST_IS_WLBUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_WLBUFFER))
#define GST_WLBUFFER (obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_WLBUFFER, GstWlBuffer))

typedef struct _GstWlBuffer GstWlBuffer;

struct _GstWlBuffer {
  GstBuffer buffer; /* Extending GstBuffer */
  
  struct wl_buffer *wbuffer;
  
  GstWaylandSink *wlsink;
};

struct _GstWaylandSink
{

  GstVideoSink parent;

  GstCaps *caps;
  
  struct display *display;
  struct window *window;

  GMutex *pool_lock;
  GSList *buffer_pool;

  GMutex *wayland_lock;

  gint video_width;
  gint video_height;
  guint bpp;

  gboolean render_finish;

};

struct _GstWaylandSinkClass
{
  GstVideoSinkClass parent; 

};

GType gst_wayland_sink_get_type (void) G_GNUC_CONST;
GType gst_dfbsurface_get_type (void);

G_END_DECLS
#endif /* __GST_WAYLAND_VIDEO_SINK_H__ */
