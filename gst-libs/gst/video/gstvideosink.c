/*
 *  GStreamer Video sink.
 *
 *  Copyright (C) <2003> Julien Moutte <julien@moutte.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideosink.h"

/* VideoSink signals and args */

enum {
  HAVE_VIDEO_SIZE,
  LAST_SIGNAL
};

static GstElementClass *parent_class = NULL;
static guint gst_videosink_signals[LAST_SIGNAL] = { 0 };

/* Private methods */

static void
gst_videosink_set_clock (GstElement *element, GstClock *clock)
{
  GstVideoSink *videosink;

  videosink = GST_VIDEOSINK (element);
  
  videosink->clock = clock;
}

/* Initing stuff */

static void
gst_videosink_init (GstVideoSink *videosink)
{
  videosink->width = 0;
  videosink->height = 0;
  videosink->clock = NULL;
}

static void
gst_videosink_class_init (GstVideoSinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
                
  gst_videosink_signals[HAVE_VIDEO_SIZE] =
    g_signal_new ("have_video_size",
                  G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstVideoSinkClass, have_video_size),
                  NULL, NULL,
                  gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2,
		  G_TYPE_UINT, G_TYPE_UINT);

  gstelement_class->set_clock = gst_videosink_set_clock;
}

/* Public methods */

/**
 * gst_video_sink_got_video_size:
 * @videosink: a #GstVideoSink which received video geometry.
 * @width: a width as a #gint.
 * @height: a height as a #gint.
 *
 * This will fire an have_size signal and update the internal object's
 * geometry.
 *
 * This function should be used by video sink developpers.
 */
void
gst_video_sink_got_video_size (GstVideoSink *videosink, gint width, gint height)
{
  g_return_if_fail (videosink != NULL);
  g_return_if_fail (GST_IS_VIDEOSINK (videosink));
  
  g_signal_emit (G_OBJECT (videosink), gst_videosink_signals[HAVE_VIDEO_SIZE],
                 0, width, height);
}

GType
gst_videosink_get_type (void)
{
  static GType videosink_type = 0;

  if (!videosink_type)
    {
      static const GTypeInfo videosink_info = {
        sizeof (GstVideoSinkClass),
        NULL,
        NULL,
        (GClassInitFunc) gst_videosink_class_init,
        NULL,
        NULL,
        sizeof (GstVideoSink),
        0,
        (GInstanceInitFunc) gst_videosink_init,
      };
    
      videosink_type = g_type_register_static (GST_TYPE_ELEMENT,
                                               "GstVideoSink",
                                               &videosink_info, 0);
    }
    
  return videosink_type;
}
