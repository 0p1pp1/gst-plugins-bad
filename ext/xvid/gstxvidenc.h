/* GStreamer xvid encoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_XVIDENC_H__
#define __GST_XVIDENC_H__

#include <gst/gst.h>
#include "gstxvid.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_XVIDENC \
  (gst_xvidenc_get_type())
#define GST_XVIDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_XVIDENC, GstXvidEnc))
#define GST_XVIDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_XVIDENC, GstXvidEnc))
#define GST_IS_XVIDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_XVIDENC))
#define GST_IS_XVIDENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_XVIDENC))

typedef struct _GstXvidEnc GstXvidEnc;
typedef struct _GstXvidEncClass GstXvidEncClass;

struct _GstXvidEnc {
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  /* quality of encoded JPEG image */
  gulong bitrate;

  /* size of the JPEG buffers */
  gulong buffer_size;

  /* max key interval */
  gint max_key_interval;

  /* xvid handle */
  void *handle;
  int csp;
  int width, height;
  float fps;
};

struct _GstXvidEncClass {
  GstElementClass parent_class;

  /* signals */
  void (*frame_encoded) (GstElement *element);
};

GType gst_xvidenc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_XVIDENC_H__ */
