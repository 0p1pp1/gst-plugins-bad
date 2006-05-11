/* G-Streamer Video4linux2 video-capture plugin - system calls
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2006 Edgard Lima <edgard.lima@indt.org.br>
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

#ifndef __V4L2SRC_CALLS_H__
#define __V4L2SRC_CALLS_H__

#include "gstv4l2src.h"
#include "v4l2_calls.h"


gboolean gst_v4l2src_get_capture (GstV4l2Src * v4l2src);
gboolean gst_v4l2src_set_capture (GstV4l2Src * v4l2src,
                                  struct v4l2_fmtdesc *fmt,
                                  gint width, gint height);
gboolean gst_v4l2src_capture_init (GstV4l2Src * v4l2src);
gboolean gst_v4l2src_capture_start (GstV4l2Src * v4l2src);
gint gst_v4l2src_grab_frame (GstV4l2Src * v4l2src);

gboolean gst_v4l2src_queue_frame (GstV4l2Src * v4l2src, guint i);
gboolean gst_v4l2src_capture_stop (GstV4l2Src * v4l2src);
gboolean gst_v4l2src_capture_deinit (GstV4l2Src * v4l2src);

gboolean gst_v4l2src_fill_format_list (GstV4l2Src * v4l2src);
gboolean gst_v4l2src_clear_format_list (GstV4l2Src * v4l2src);

/* hacky */
gboolean gst_v4l2src_get_size_limits (GstV4l2Src * v4l2src,
                                      struct v4l2_fmtdesc *fmt,
                                      gint * min_w, gint * max_w,
                                      gint * min_h, gint * max_h);

void gst_v4l2src_free_buffer (GstBuffer * buffer);

extern gboolean
gst_v4l2src_update_fps (GstV4l2Object * v4l2object);

extern gboolean
gst_v4l2src_get_fps (GstV4l2Src * v4l2src, guint * fps_n, guint * fps_d);

GValue *gst_v4l2src_get_fps_list (GstV4l2Src * v4l2src);

GstBuffer *gst_v4l2src_buffer_new (GstV4l2Src * v4l2src,
                                   guint size, guint8 * data,
                                   GstV4l2Buffer * srcbuf);

#endif /* __V4L2SRC_CALLS_H__ */
