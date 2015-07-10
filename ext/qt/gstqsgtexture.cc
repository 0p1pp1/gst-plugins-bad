/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#include <stdio.h>

#include <gst/video/video.h>
#include <gst/gl/gl.h>
#include "gstqsgtexture.h"

#define GST_CAT_DEFAULT gst_qsg_texture_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

GstQSGTexture::GstQSGTexture ()
{
  static volatile gsize _debug;

  initializeOpenGLFunctions();

  if (g_once_init_enter (&_debug)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "qtqsgtexture", 0,
        "Qt Scenegraph Texture");
    g_once_init_leave (&_debug, 1);
  }

  gst_video_info_init (&this->v_info);
  this->buffer_ = NULL;
}

GstQSGTexture::~GstQSGTexture ()
{
  gst_buffer_replace (&this->buffer_, NULL);
}

/* only called from the streaming thread with scene graph thread blocked */
void
GstQSGTexture::setCaps (GstCaps * caps)
{
  GST_LOG ("%p setCaps %" GST_PTR_FORMAT, this, caps);

  gst_video_info_from_caps (&this->v_info, caps);
}

/* only called from the streaming thread with scene graph thread blocked */
void
GstQSGTexture::setBuffer (GstBuffer * buffer)
{
  GST_LOG ("%p setBuffer %" GST_PTR_FORMAT, this, buffer);
  /* FIXME: update more state here */
  gst_buffer_replace (&this->buffer_, buffer);
}

/* only called from qt's scene graph render thread */
void
GstQSGTexture::bind ()
{
  guint tex_id;

  if (!this->buffer_)
    return;
  if (GST_VIDEO_INFO_FORMAT (&this->v_info) == GST_VIDEO_FORMAT_UNKNOWN)
    return;

  /* FIXME: should really lock the memory to prevent write access */
  if (!gst_video_frame_map (&this->v_frame, &this->v_info, this->buffer_,
        (GstMapFlags) (GST_MAP_READ | GST_MAP_GL))) {
    g_assert_not_reached ();
    return;
  }

  tex_id = *(guint *) this->v_frame.data[0];
  GST_LOG ("%p binding Qt texture %u", this, tex_id);

  glBindTexture (GL_TEXTURE_2D, tex_id);

  gst_video_frame_unmap (&this->v_frame);
}

/* can be called from any thread */
int
GstQSGTexture::textureId () const
{
  int tex_id = 0;

  if (this->buffer_) {
    GstMemory *mem = gst_buffer_peek_memory (this->buffer_, 0);

    tex_id = ((GstGLMemory *) mem)->tex_id;
  }

  GST_LOG ("%p get texture id %u", this, tex_id);

  return tex_id;
}

/* can be called from any thread */
QSize
GstQSGTexture::textureSize () const
{
  if (GST_VIDEO_INFO_FORMAT (&this->v_info) == GST_VIDEO_FORMAT_UNKNOWN)
    return QSize (0, 0);

  GST_TRACE ("%p get texture size %ux%u", this, this->v_info.width,
      this->v_info.height);

  return QSize (this->v_info.width, this->v_info.height);
}

/* can be called from any thread */
bool
GstQSGTexture::hasAlphaChannel () const
{
  /* FIXME: support RGB textures */
  return true;
}

/* can be called from any thread */
bool
GstQSGTexture::hasMipmaps () const
{
  return false;
}
