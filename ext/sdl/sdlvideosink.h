/* GStreamer SDL plugin
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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


#ifndef __GST_SDLVIDEOSINK_H__
#define __GST_SDLVIDEOSINK_H__

#include <gst/gst.h>

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_SDLVIDEOSINK \
  (gst_sdlvideosink_get_type())
#define GST_SDLVIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SDLVIDEOSINK,GstSDLVideoSink))
#define GST_SDLVIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SDLVIDEOSINK,GstSDLVideoSink))
#define GST_IS_SDLVIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SDLVIDEOSINK))
#define GST_IS_SDLVIDEOSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SDLVIDEOSINK))

typedef enum {
  GST_SDLVIDEOSINK_OPEN              = GST_ELEMENT_FLAG_LAST,

  GST_SDLVIDEOSINK_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2,
} GstSDLVideoSinkFlags;

typedef struct _GstSDLVideoSink GstSDLVideoSink;
typedef struct _GstSDLVideoSinkClass GstSDLVideoSinkClass;

struct _GstSDLVideoSink {
  GstElement element;

  GstPad *sinkpad;

  gulong format;
  gint window_width, window_height; /* the size of the SDL window */
  gint image_width, image_height;   /* the size of the incoming YUV stream */
  gint window_id;

  gint frames_displayed;
  guint64 frame_time;

  GstClock *clock;

  GstCaps *capslist;

  unsigned char *yuv[3];
  SDL_Surface *screen;
  SDL_Overlay *yuv_overlay;
  SDL_Rect rect;
};

struct _GstSDLVideoSinkClass {
  GstElementClass parent_class;

  /* signals */
  void (*frame_displayed) (GstElement *element);
  void (*have_size) 	  (GstElement *element, guint width, guint height);
};

GType gst_sdlsink_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_SDLVIDEOSINK_H__ */
