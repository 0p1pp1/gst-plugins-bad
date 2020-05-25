/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#include <gst/video/video.h>
#include "gstmfsourcereader.h"
#include <string.h>
#include <wrl.h>

using namespace Microsoft::WRL;

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_mf_source_object_debug);
#define GST_CAT_DEFAULT gst_mf_source_object_debug

G_END_DECLS

typedef struct _GstMFStreamMediaType
{
  IMFMediaType *media_type;

  /* the stream index of media type */
  guint stream_index;

  /* the media index in the stream index */
  guint media_type_index;

  GstCaps *caps;
} GstMFStreamMediaType;

struct _GstMFSourceReader
{
  GstMFSourceObject parent;

  GMutex lock;

  /* protected by lock */
  GQueue *queue;

  IMFMediaSource *source;
  IMFSourceReader *reader;

  GstCaps *supported_caps;
  GList *media_types;
  GstMFStreamMediaType *cur_type;
  GstVideoInfo info;

  gboolean flushing;
};

static void gst_mf_source_reader_finalize (GObject * object);

static gboolean gst_mf_source_reader_open (GstMFSourceObject * object,
    IMFActivate * activate);
static gboolean gst_mf_source_reader_start (GstMFSourceObject * object);
static gboolean gst_mf_source_reader_stop  (GstMFSourceObject * object);
static gboolean gst_mf_source_reader_close (GstMFSourceObject * object);
static GstFlowReturn gst_mf_source_reader_fill (GstMFSourceObject * object,
    GstBuffer * buffer);
static gboolean gst_mf_source_reader_unlock (GstMFSourceObject * object);
static gboolean gst_mf_source_reader_unlock_stop (GstMFSourceObject * object);
static GstCaps * gst_mf_source_reader_get_caps (GstMFSourceObject * object);
static gboolean gst_mf_source_reader_set_caps (GstMFSourceObject * object,
    GstCaps * caps);

#define gst_mf_source_reader_parent_class parent_class
G_DEFINE_TYPE (GstMFSourceReader, gst_mf_source_reader,
    GST_TYPE_MF_SOURCE_OBJECT);

static void
gst_mf_source_reader_class_init (GstMFSourceReaderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstMFSourceObjectClass *source_class = GST_MF_SOURCE_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_mf_source_reader_finalize;

  source_class->open = GST_DEBUG_FUNCPTR (gst_mf_source_reader_open);
  source_class->start = GST_DEBUG_FUNCPTR (gst_mf_source_reader_start);
  source_class->stop = GST_DEBUG_FUNCPTR (gst_mf_source_reader_stop);
  source_class->close = GST_DEBUG_FUNCPTR (gst_mf_source_reader_close);
  source_class->fill = GST_DEBUG_FUNCPTR (gst_mf_source_reader_fill);
  source_class->unlock = GST_DEBUG_FUNCPTR (gst_mf_source_reader_unlock);
  source_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_mf_source_reader_unlock_stop);
  source_class->get_caps = GST_DEBUG_FUNCPTR (gst_mf_source_reader_get_caps);
  source_class->set_caps = GST_DEBUG_FUNCPTR (gst_mf_source_reader_set_caps);
}

static void
gst_mf_source_reader_init (GstMFSourceReader * self)
{
  self->queue = g_queue_new ();
  g_mutex_init (&self->lock);
}

static gboolean
gst_mf_enum_media_type_from_source_reader (IMFSourceReader * source_reader,
    GList ** media_types)
{
  gint i, j;
  HRESULT hr;
  GList *list = NULL;

  g_return_val_if_fail (source_reader != NULL, FALSE);
  g_return_val_if_fail (media_types != NULL, FALSE);

  for (i = 0;; i++) {
    for (j = 0;; j++) {
      ComPtr<IMFMediaType> media_type;

      hr = source_reader->GetNativeMediaType (i, j, &media_type);

      if (SUCCEEDED (hr)) {
        GstMFStreamMediaType *mtype;
        GstCaps *caps = NULL;

        caps = gst_mf_media_type_to_caps (media_type.Get ());

        /* unknown format */
        if (!caps)
          continue;

        mtype = g_new0 (GstMFStreamMediaType, 1);

        mtype->media_type = media_type.Detach ();
        mtype->stream_index = i;
        mtype->media_type_index = j;
        mtype->caps = caps;

        GST_DEBUG ("StreamIndex %d, MediaTypeIndex %d, %" GST_PTR_FORMAT,
            i, j, caps);

        list = g_list_prepend (list, mtype);
      } else if (hr == MF_E_NO_MORE_TYPES) {
        /* no more media type in this stream index, try next stream index */
        break;
      } else if (hr == MF_E_INVALIDSTREAMNUMBER) {
        /* no more streams and media types */
        goto done;
      } else {
        /* undefined return */
        goto done;
      }
    }
  }

done:
  list = g_list_reverse (list);
  *media_types = list;

  return ! !list;
}

static void
gst_mf_stream_media_type_free (GstMFStreamMediaType * media_type)
{
  g_return_if_fail (media_type != NULL);

  if (media_type->media_type)
    media_type->media_type->Release ();

  if (media_type->caps)
    gst_caps_unref (media_type->caps);

  g_free (media_type);
}

static gboolean
gst_mf_source_reader_open (GstMFSourceObject * object, IMFActivate * activate)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);
  GList *iter;
  HRESULT hr;
  ComPtr<IMFSourceReader> reader;
  ComPtr<IMFMediaSource> source;
  ComPtr<IMFAttributes> attr;

  hr = activate->ActivateObject (IID_IMFMediaSource, (void **) &source);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = MFCreateAttributes (&attr, 2);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = attr->SetUINT32 (MF_READWRITE_DISABLE_CONVERTERS, TRUE);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = MFCreateSourceReaderFromMediaSource (source.Get (),
      attr.Get (), &reader);
  if (!gst_mf_result (hr))
    return FALSE;

  if (!gst_mf_enum_media_type_from_source_reader (reader.Get (),
          &self->media_types)) {
    GST_ERROR_OBJECT (self, "No available media types");
    source->Shutdown ();
    return FALSE;
  }

  self->source = source.Detach ();
  self->reader = reader.Detach ();

  for (iter = self->media_types; iter; iter = g_list_next (iter)) {
    GstMFStreamMediaType *mtype = (GstMFStreamMediaType *) iter->data;
    if (!self->supported_caps)
      self->supported_caps = gst_caps_ref (mtype->caps);
    else
      self->supported_caps =
          gst_caps_merge (self->supported_caps, gst_caps_ref (mtype->caps));
  }

  GST_DEBUG_OBJECT (self, "Available output caps %" GST_PTR_FORMAT,
      self->supported_caps);

  return TRUE;
}

static gboolean
gst_mf_source_reader_close (GstMFSourceObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);

  gst_clear_caps (&self->supported_caps);

  if (self->media_types) {
    g_list_free_full (self->media_types,
        (GDestroyNotify) gst_mf_stream_media_type_free);
    self->media_types = NULL;
  }

  if (self->reader) {
    self->reader->Release ();
    self->reader = NULL;
  }

  if (self->source) {
    self->source->Shutdown ();
    self->source->Release ();
    self->source = NULL;
  }

  return TRUE;
}

static void
gst_mf_source_reader_finalize (GObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);

  g_queue_free (self->queue);
  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_mf_source_reader_start (GstMFSourceObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);
  HRESULT hr;
  GstMFStreamMediaType *type;

  if (!self->cur_type) {
    GST_ERROR_OBJECT (self, "MediaType wasn't specified");
    return FALSE;
  }

  type = self->cur_type;

  hr = type->media_type->SetUINT32 (MF_MT_DEFAULT_STRIDE,
      GST_VIDEO_INFO_PLANE_STRIDE (&self->info, 0));
  if (!gst_mf_result (hr))
    return FALSE;

  hr = self->reader->SetStreamSelection (type->stream_index, TRUE);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = self->reader->SetCurrentMediaType (type->stream_index,
      NULL, type->media_type);
  if (!gst_mf_result (hr))
    return FALSE;

  return TRUE;
}

static gboolean
gst_mf_source_reader_stop (GstMFSourceObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);

  while (!g_queue_is_empty (self->queue)) {
    IMFMediaBuffer *buffer = (IMFMediaBuffer *) g_queue_pop_head (self->queue);
    buffer->Release ();
  }

  return TRUE;
}

static GstFlowReturn
gst_mf_source_reader_read_sample (GstMFSourceReader * self)
{
  HRESULT hr;
  DWORD count = 0, i;
  DWORD stream_flags = 0;
  GstMFStreamMediaType *type = self->cur_type;
  ComPtr<IMFSample> sample;

  hr = self->reader->ReadSample (type->stream_index, 0, NULL, &stream_flags,
    NULL, &sample);

  if (!gst_mf_result (hr))
    return GST_FLOW_ERROR;

  if ((stream_flags & MF_SOURCE_READERF_ERROR) == MF_SOURCE_READERF_ERROR)
    return GST_FLOW_ERROR;

  if (!sample)
    return GST_FLOW_OK;

  hr = sample->GetBufferCount (&count);
  if (!gst_mf_result (hr) || !count)
    return GST_FLOW_OK;

  for (i = 0; i < count; i++) {
    IMFMediaBuffer *buffer = NULL;

    hr = sample->GetBufferByIndex (i, &buffer);
    if (!gst_mf_result (hr) || !buffer)
      continue;

    g_queue_push_tail (self->queue, buffer);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mf_source_reader_fill (GstMFSourceObject * object, GstBuffer * buffer)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);
  GstFlowReturn ret = GST_FLOW_OK;
  HRESULT hr;
  GstVideoFrame frame;
  BYTE *data;
  gint i, j;
  ComPtr<IMFMediaBuffer> media_buffer;

  while (g_queue_is_empty (self->queue)) {
    ret = gst_mf_source_reader_read_sample (self);
    if (ret != GST_FLOW_OK)
      return ret;

    g_mutex_lock (&self->lock);
    if (self->flushing) {
      g_mutex_unlock (&self->lock);
      return GST_FLOW_FLUSHING;
    }
    g_mutex_unlock (&self->lock);
  }

  media_buffer.Attach ((IMFMediaBuffer *) g_queue_pop_head (self->queue));

  hr = media_buffer->Lock (&data, NULL, NULL);
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self, "Failed to lock media buffer");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&frame, &self->info, buffer, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Failed to map buffer");
    media_buffer->Unlock ();
    return GST_FLOW_ERROR;
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&self->info); i++) {
    guint8 *src, *dst;
    gint src_stride, dst_stride;
    gint width;

    src = data + GST_VIDEO_INFO_PLANE_OFFSET (&self->info, i);
    dst = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame, i);

    src_stride = GST_VIDEO_INFO_PLANE_STRIDE (&self->info, i);
    dst_stride = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, i);
    width = GST_VIDEO_INFO_COMP_WIDTH (&self->info, i)
        * GST_VIDEO_INFO_COMP_PSTRIDE (&self->info, i);

    for (j = 0; j < GST_VIDEO_INFO_COMP_HEIGHT (&self->info, i); j++) {
      memcpy (dst, src, width);
      src += src_stride;
      dst += dst_stride;
    }
  }

  gst_video_frame_unmap (&frame);
  media_buffer->Unlock ();

  return ret;
}

static gboolean
gst_mf_source_reader_unlock (GstMFSourceObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);

  g_mutex_lock (&self->lock);
  self->flushing = TRUE;
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static gboolean
gst_mf_source_reader_unlock_stop (GstMFSourceObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);

  g_mutex_lock (&self->lock);
  self->flushing = FALSE;
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static GstCaps *
gst_mf_source_reader_get_caps (GstMFSourceObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);

  if (self->supported_caps)
    return gst_caps_ref (self->supported_caps);

  return NULL;
}

static gboolean
gst_mf_source_reader_set_caps (GstMFSourceObject * object, GstCaps * caps)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);
  GList *iter;
  GstMFStreamMediaType *best_type = NULL;

  for (iter = self->media_types; iter; iter = g_list_next (iter)) {
    GstMFStreamMediaType *minfo = (GstMFStreamMediaType *) iter->data;
    if (gst_caps_is_subset (minfo->caps, caps)) {
      best_type = minfo;
      break;
    }
  }

  if (!best_type) {
    GST_ERROR_OBJECT (self,
        "Could not determine target media type with given caps %"
        GST_PTR_FORMAT, caps);

    return FALSE;
  }

  self->cur_type = best_type;
  gst_video_info_from_caps (&self->info, best_type->caps);

  return TRUE;
}

GstMFSourceObject *
gst_mf_source_reader_new (GstMFSourceType type, gint device_index,
    const gchar * device_name, const gchar * device_path)
{
  GstMFSourceObject *self;
  gchar *name;
  gchar *path;

  /* TODO: add more type */
  g_return_val_if_fail (type == GST_MF_SOURCE_TYPE_VIDEO, NULL);

  name = device_name ? g_strdup (device_name) : g_strdup ("");
  path = device_path ? g_strdup (device_path) : g_strdup ("");

  self = (GstMFSourceObject *) g_object_new (GST_TYPE_MF_SOURCE_READER,
      "source-type", type, "device-index", device_index, "device-name", name,
      "device-path", path, NULL);

  gst_object_ref_sink (self);
  g_free (name);
  g_free (path);

  if (!self->opened) {
    GST_WARNING_OBJECT (self, "Couldn't open device");
    gst_object_unref (self);
    return NULL;
  }

  return self;
}