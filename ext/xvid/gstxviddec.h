/* GStreamer xvid decoder plugin
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

#ifndef __GST_XVIDDEC_H__
#define __GST_XVIDDEC_H__

#include <gst/gst.h>
#include <xvid.h>
#include "gstxvid.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_XVIDDEC \
  (gst_xviddec_get_type())
#define GST_XVIDDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_XVIDDEC, GstXvidDec))
#define GST_XVIDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_XVIDDEC, GstXvidDec))
#define GST_IS_XVIDDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_XVIDDEC))
#define GST_IS_XVIDDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_XVIDDEC))

typedef struct _GstXvidDec GstXvidDec;
typedef struct _GstXvidDecClass GstXvidDecClass;

struct _GstXvidDec {
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  /* xvid handle */
  void *handle;

  /* video (output) settings */
  int csp, bpp;
  int width, height;
  float fps;
};

struct _GstXvidDecClass {
  GstElementClass parent_class;
};

GType gst_xviddec_get_type(void);

gboolean gst_xviddec_plugin_init (GstPlugin *plugin);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_XVIDDEC_H__ */
