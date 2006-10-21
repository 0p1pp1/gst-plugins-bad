/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2006 Wim Taymans <wim@fluendo.com>
 *                    2006 David A. Schleef <ds@schleef.org>
 *
 * gstmultifilesink.c:
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
/**
 * SECTION:element-multifilesink
 * @short_description: write buffers to sequentially-named files
 * @see_also: #GstFileSrc
 *
 * Write incoming data to a series of files in the local file system.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <errno.h>
#include "gstmultifilesink.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif


static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_multi_file_sink_debug);
#define GST_CAT_DEFAULT gst_multi_file_sink_debug

static const GstElementDetails gst_multi_file_sink_details =
GST_ELEMENT_DETAILS ("Multi-File Sink",
    "Sink/File",
    "Write stream to a file",
    "David Schleef <ds@schleef.org>");

enum
{
  ARG_0,
  ARG_LOCATION
};

static void gst_multi_file_sink_dispose (GObject * object);

static void gst_multi_file_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multi_file_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

//static gboolean gst_multi_file_sink_open_file (GstMultiFileSink * sink);
//static void gst_multi_file_sink_close_file (GstMultiFileSink * sink);

//static gboolean gst_multi_file_sink_start (GstBaseSink * sink);
//static gboolean gst_multi_file_sink_stop (GstBaseSink * sink);
//static gboolean gst_multi_file_sink_event (GstBaseSink * sink, GstEvent * event);
static GstFlowReturn gst_multi_file_sink_render (GstBaseSink * sink,
    GstBuffer * buffer);

//static gboolean gst_multi_file_sink_do_seek (GstMultiFileSink * filesink,
//    guint64 new_offset);

//static gboolean gst_multi_file_sink_query (GstPad * pad, GstQuery * query);

static void gst_multi_file_sink_uri_handler_init (gpointer g_iface,
    gpointer iface_data);


static void
_do_init (GType filesink_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_multi_file_sink_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (filesink_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);
  GST_DEBUG_CATEGORY_INIT (gst_multi_file_sink_debug, "filesink", 0,
      "filesink element");
}

GST_BOILERPLATE_FULL (GstMultiFileSink, gst_multi_file_sink, GstBaseSink,
    GST_TYPE_BASE_SINK, _do_init);

static void
gst_multi_file_sink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details (gstelement_class,
      &gst_multi_file_sink_details);
}

static void
gst_multi_file_sink_class_init (GstMultiFileSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_multi_file_sink_set_property;
  gobject_class->get_property = gst_multi_file_sink_get_property;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the file to write", NULL, G_PARAM_READWRITE));

  gobject_class->dispose = gst_multi_file_sink_dispose;

  gstbasesink_class->get_times = NULL;
  //gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_multi_file_sink_start);
  //gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_multi_file_sink_stop);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_multi_file_sink_render);
  //gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_multi_file_sink_event);

  if (sizeof (off_t) < 8) {
    GST_LOG ("No large file support, sizeof (off_t) = %u", sizeof (off_t));
  }
}

static void
gst_multi_file_sink_init (GstMultiFileSink * filesink,
    GstMultiFileSinkClass * g_class)
{
  GstPad *pad;

  pad = GST_BASE_SINK_PAD (filesink);

  //gst_pad_set_query_function (pad, GST_DEBUG_FUNCPTR (gst_multi_file_sink_query));

  filesink->filename = g_strdup ("output-%05d");

  GST_BASE_SINK (filesink)->sync = FALSE;
}

static void
gst_multi_file_sink_dispose (GObject * object)
{
  GstMultiFileSink *sink = GST_MULTI_FILE_SINK (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  g_free (sink->uri);
  sink->uri = NULL;
  g_free (sink->filename);
  sink->filename = NULL;
}

static gboolean
gst_multi_file_sink_set_location (GstMultiFileSink * sink,
    const gchar * location)
{
  g_free (sink->filename);
  g_free (sink->uri);
  if (location != NULL) {
    sink->filename = g_strdup (location);
    sink->uri = gst_uri_construct ("file", location);
  } else {
    sink->filename = NULL;
    sink->uri = NULL;
  }

  return TRUE;
}
static void
gst_multi_file_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiFileSink *sink = GST_MULTI_FILE_SINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      gst_multi_file_sink_set_location (sink, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multi_file_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMultiFileSink *sink = GST_MULTI_FILE_SINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, sink->filename);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#if 0
static gboolean
gst_multi_file_sink_open_file (GstMultiFileSink * sink)
{
  /* open the file */
  if (sink->filename == NULL || sink->filename[0] == '\0')
    goto no_filename;

  sink->file = fopen (sink->filename, "wb");
  if (sink->file == NULL)
    goto open_failed;

  sink->data_written = 0;
  /* try to seek in the file to figure out if it is seekable */
  sink->seekable = gst_multi_file_sink_do_seek (sink, 0);

  GST_DEBUG_OBJECT (sink, "opened file %s, seekable %d",
      sink->filename, sink->seekable);

  return TRUE;

  /* ERRORS */
no_filename:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND,
        ("No file name specified for writing."), (NULL));
    return FALSE;
  }
open_failed:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("Could not open file \"%s\" for writing.", sink->filename),
        GST_ERROR_SYSTEM);
    return FALSE;
  }
}
#endif

#if 0
static void
gst_multi_file_sink_close_file (GstMultiFileSink * sink)
{
  if (sink->file) {
    if (fclose (sink->file) != 0)
      goto close_failed;

    GST_DEBUG_OBJECT (sink, "closed file");
  }
  return;

  /* ERRORS */
close_failed:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, CLOSE,
        ("Error closing file \"%s\".", sink->filename), GST_ERROR_SYSTEM);
    return;
  }
}
#endif

#if 0
static gboolean
gst_multi_file_sink_query (GstPad * pad, GstQuery * query)
{
  GstMultiFileSink *self;
  GstFormat format;

  self = GST_MULTI_FILE_SINK (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_DEFAULT:
        case GST_FORMAT_BYTES:
          gst_query_set_position (query, GST_FORMAT_BYTES, self->data_written);
          return TRUE;
        default:
          return FALSE;
      }

    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 2, GST_FORMAT_DEFAULT, GST_FORMAT_BYTES);
      return TRUE;

    default:
      return gst_pad_query_default (pad, query);
  }
}
#endif

#ifdef G_OS_UNIX
# define __GST_STDIO_SEEK_FUNCTION "lseek"
#else
# define __GST_STDIO_SEEK_FUNCTION "fseek"
#endif

#if 0
static gboolean
gst_multi_file_sink_do_seek (GstMultiFileSink * filesink, guint64 new_offset)
{
  GST_DEBUG_OBJECT (filesink, "Seeking to offset %" G_GUINT64_FORMAT
      " using " __GST_STDIO_SEEK_FUNCTION, new_offset);

  if (fflush (filesink->file))
    goto flush_failed;

#ifdef G_OS_UNIX
  if (lseek (fileno (filesink->file), (off_t) new_offset,
          SEEK_SET) == (off_t) - 1)
    goto seek_failed;
#else
  if (fseek (filesink->file, (long) new_offset, SEEK_SET) != 0)
    goto seek_failed;
#endif

  return TRUE;

  /* ERRORS */
flush_failed:
  {
    GST_DEBUG_OBJECT (filesink, "Flush failed: %s", g_strerror (errno));
    return FALSE;
  }
seek_failed:
  {
    GST_DEBUG_OBJECT (filesink, "Seeking failed: %s", g_strerror (errno));
    return FALSE;
  }
}
#endif

#if 0
/* handle events (search) */
static gboolean
gst_multi_file_sink_event (GstBaseSink * sink, GstEvent * event)
{
  GstEventType type;
  GstMultiFileSink *filesink;

  filesink = GST_MULTI_FILE_SINK (sink);

  type = GST_EVENT_TYPE (event);

  switch (type) {
    case GST_EVENT_NEWSEGMENT:
    {
      gint64 soffset, eoffset;
      GstFormat format;

      gst_event_parse_new_segment (event, NULL, NULL, &format, &soffset,
          &eoffset, NULL);

      if (format == GST_FORMAT_BYTES) {
        if (!gst_multi_file_sink_do_seek (filesink, (guint64) soffset))
          goto seek_failed;
      } else {
        GST_DEBUG ("Ignored NEWSEGMENT event of format %u (%s)",
            (guint) format, gst_format_get_name (format));
      }
      break;
    }
    case GST_EVENT_EOS:
      if (fflush (filesink->file))
        goto flush_failed;
      break;
    default:
      break;
  }

  return TRUE;

  /* ERRORS */
seek_failed:
  {
    GST_ELEMENT_ERROR (filesink, RESOURCE, SEEK,
        ("Error while seeking in file \"%s\".", filesink->filename),
        GST_ERROR_SYSTEM);
    return FALSE;
  }
flush_failed:
  {
    GST_ELEMENT_ERROR (filesink, RESOURCE, WRITE,
        ("Error while writing to file \"%s\".", filesink->filename),
        GST_ERROR_SYSTEM);
    return FALSE;
  }
}
#endif

#if 0
static gboolean
gst_multi_file_sink_get_current_offset (GstMultiFileSink * filesink,
    guint64 * p_pos)
{
  off_t ret;

#if HAVE_FTELLO
  ret = ftello (filesink->file);
#elif defined(G_OS_UNIX)
  if (fflush (filesink->file)) {
    GST_DEBUG_OBJECT (filesink, "Flush failed: %s", g_strerror (errno));
    /* ignore and continue */
  }
  ret = lseek (fileno (filesink->file), 0, SEEK_CUR);
#else
  ret = (off_t) ftell (filesink->file);
#endif

  *p_pos = (guint64) ret;

  return (ret != (off_t) - 1);
}
#endif

static gchar *
gst_multi_file_sink_get_filename (GstMultiFileSink * filesink)
{
  gchar *filename;

  filename = g_strdup_printf (filesink->filename, filesink->index);

  return filename;
}

static GstFlowReturn
gst_multi_file_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstMultiFileSink *filesink;
  guint size;
  gchar *filename;
  FILE *file;

  size = GST_BUFFER_SIZE (buffer);

  filesink = GST_MULTI_FILE_SINK (sink);

  filename = gst_multi_file_sink_get_filename (filesink);

  file = fopen (filename, "wb");
  if (!file) {
    goto handle_error;
  }

  g_free (filename);

  if (size > 0 && GST_BUFFER_DATA (buffer) != NULL) {
    if (fwrite (GST_BUFFER_DATA (buffer), size, 1, file) != 1)
      goto handle_error;
  }

  filesink->index++;

  fclose (file);

  return GST_FLOW_OK;

handle_error:
  {
    switch (errno) {
      case ENOSPC:{
        GST_ELEMENT_ERROR (filesink, RESOURCE, NO_SPACE_LEFT, (NULL), (NULL));
        break;
      }
      default:{
        GST_ELEMENT_ERROR (filesink, RESOURCE, WRITE,
            ("Error while writing to file \"%s\".", filesink->filename),
            ("%s", g_strerror (errno)));
      }
    }
    return GST_FLOW_ERROR;
  }
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
gst_multi_file_sink_uri_get_type (void)
{
  return GST_URI_SINK;
}
static gchar **
gst_multi_file_sink_uri_get_protocols (void)
{
  static gchar *protocols[] = { "file", NULL };

  return protocols;
}
static const gchar *
gst_multi_file_sink_uri_get_uri (GstURIHandler * handler)
{
  GstMultiFileSink *sink = GST_MULTI_FILE_SINK (handler);

  return sink->uri;
}

static gboolean
gst_multi_file_sink_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gchar *protocol, *location;
  gboolean ret;
  GstMultiFileSink *sink = GST_MULTI_FILE_SINK (handler);

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "file") != 0) {
    g_free (protocol);
    return FALSE;
  }
  g_free (protocol);
  location = gst_uri_get_location (uri);
  ret = gst_multi_file_sink_set_location (sink, location);
  g_free (location);

  return ret;
}

static void
gst_multi_file_sink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_multi_file_sink_uri_get_type;
  iface->get_protocols = gst_multi_file_sink_uri_get_protocols;
  iface->get_uri = gst_multi_file_sink_uri_get_uri;
  iface->set_uri = gst_multi_file_sink_uri_set_uri;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_multi_file_sink_debug, "multifilesink", 0,
      "multifilesink plugin");

  return gst_element_register (plugin, "multifilesink",
      GST_RANK_NONE, GST_TYPE_MULTI_FILE_SINK);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "multifilesink",
    "Writes buffers to sequentially named files",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
