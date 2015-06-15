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

/**
 * SECTION:gstgtkglsink
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgtkglsink.h"

GST_DEBUG_CATEGORY (gst_debug_gtk_gl_sink);
#define GST_CAT_DEFAULT gst_debug_gtk_gl_sink

#define DEFAULT_FORCE_ASPECT_RATIO  TRUE
#define DEFAULT_PAR_N               0
#define DEFAULT_PAR_D               1

static void gst_gtk_gl_sink_finalize (GObject * object);
static void gst_gtk_gl_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec);
static void gst_gtk_gl_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec);

static gboolean gst_gtk_gl_sink_stop (GstBaseSink * bsink);

static gboolean gst_gtk_gl_sink_query (GstBaseSink * bsink, GstQuery * query);

static GstStateChangeReturn
gst_gtk_gl_sink_change_state (GstElement * element, GstStateChange transition);

static void gst_gtk_gl_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_gtk_gl_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static GstFlowReturn gst_gtk_gl_sink_show_frame (GstVideoSink * bsink,
    GstBuffer * buf);
static gboolean gst_gtk_gl_sink_propose_allocation (GstBaseSink * bsink,
    GstQuery * query);

static GstStaticPadTemplate gst_gtk_gl_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, "RGBA")));

enum
{
  PROP_0,
  PROP_WIDGET,
  PROP_FORCE_ASPECT_RATIO,
  PROP_PIXEL_ASPECT_RATIO,
};

enum
{
  SIGNAL_0,
  LAST_SIGNAL
};

#define gst_gtk_gl_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGtkGLSink, gst_gtk_gl_sink,
    GST_TYPE_VIDEO_SINK, GST_DEBUG_CATEGORY_INIT (gst_debug_gtk_gl_sink,
        "gtkglsink", 0, "Gtk Video Sink"));

static void
gst_gtk_gl_sink_class_init (GstGtkGLSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;

  gobject_class->set_property = gst_gtk_gl_sink_set_property;
  gobject_class->get_property = gst_gtk_gl_sink_get_property;

  gst_element_class_set_metadata (gstelement_class, "Gtk Video Sink",
      "Sink/Video", "A video sink that renders to a GtkWidget",
      "Matthew Waters <matthew@centricular.com>");

  g_object_class_install_property (gobject_class, PROP_WIDGET,
      g_param_spec_object ("widget", "Gtk Widget",
          "The GtkWidget to place in the widget heirachy",
          GTK_TYPE_WIDGET, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PIXEL_ASPECT_RATIO,
      gst_param_spec_fraction ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", DEFAULT_PAR_N, DEFAULT_PAR_D,
          G_MAXINT, 1, 1, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_gtk_gl_sink_template));

  gobject_class->finalize = gst_gtk_gl_sink_finalize;

  gstelement_class->change_state = gst_gtk_gl_sink_change_state;
  gstbasesink_class->query = gst_gtk_gl_sink_query;
  gstbasesink_class->set_caps = gst_gtk_gl_sink_set_caps;
  gstbasesink_class->get_times = gst_gtk_gl_sink_get_times;
  gstbasesink_class->propose_allocation = gst_gtk_gl_sink_propose_allocation;
  gstbasesink_class->stop = gst_gtk_gl_sink_stop;

  gstvideosink_class->show_frame = gst_gtk_gl_sink_show_frame;
}

static void
gst_gtk_gl_sink_init (GstGtkGLSink * gtk_sink)
{
  gtk_sink->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  gtk_sink->par_n = DEFAULT_PAR_N;
  gtk_sink->par_d = DEFAULT_PAR_D;
}

static void
gst_gtk_gl_sink_finalize (GObject * object)
{
  GstGtkGLSink *gtk_sink = GST_GTK_GL_SINK (object);;

  g_object_unref (gtk_sink->widget);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GtkGstGLWidget *
gst_gtk_gl_sink_get_widget (GstGtkGLSink * gtk_sink)
{
  if (gtk_sink->widget != NULL)
    return gtk_sink->widget;

  /* Ensure GTK is initialized, this has no side effect if it was already
   * initialized. Also, we do that lazily, so the application can be first */
  if (!gtk_init_check (NULL, NULL)) {
    GST_ERROR_OBJECT (gtk_sink, "Could not ensure GTK initialization.");
    return NULL;
  }

  gtk_sink->widget = (GtkGstGLWidget *) gtk_gst_gl_widget_new ();
  gtk_sink->bind_force_aspect_ratio =
      g_object_bind_property (gtk_sink, "force-aspect-ratio", gtk_sink->widget,
      "force-aspect-ratio", G_BINDING_BIDIRECTIONAL);
  gtk_sink->bind_pixel_aspect_ratio =
      g_object_bind_property (gtk_sink, "pixel-aspect-ratio", gtk_sink->widget,
      "pixel-aspect-ratio", G_BINDING_BIDIRECTIONAL);

  /* Take the floating ref, otherwise the destruction of the container will
   * make this widget disapear possibly before we are done. */
  gst_object_ref_sink (gtk_sink->widget);

  return gtk_sink->widget;
}

static void
gst_gtk_gl_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGtkGLSink *gtk_sink;

  gtk_sink = GST_GTK_GL_SINK (object);

  switch (prop_id) {
    case PROP_WIDGET:
      g_value_set_object (value, gst_gtk_gl_sink_get_widget (gtk_sink));
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, gtk_sink->force_aspect_ratio);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      gst_value_set_fraction (value, gtk_sink->par_n, gtk_sink->par_d);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gtk_gl_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGtkGLSink *gtk_sink = GST_GTK_GL_SINK (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      gtk_sink->force_aspect_ratio = g_value_get_boolean (value);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      gtk_sink->par_n = gst_value_get_fraction_numerator (value);
      gtk_sink->par_d = gst_value_get_fraction_denominator (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gtk_gl_sink_query (GstBaseSink * bsink, GstQuery * query)
{
  GstGtkGLSink *gtk_sink = GST_GTK_GL_SINK (bsink);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      const gchar *context_type;
      GstContext *context, *old_context;
      gboolean ret;

      ret = gst_gl_handle_context_query ((GstElement *) gtk_sink, query,
          &gtk_sink->display, &gtk_sink->gtk_context);

      if (gtk_sink->display)
        gst_gl_display_filter_gl_api (gtk_sink->display, GST_GL_API_OPENGL3);

      gst_query_parse_context_type (query, &context_type);

      if (g_strcmp0 (context_type, "gst.gl.local_context") == 0) {
        GstStructure *s;

        gst_query_parse_context (query, &old_context);

        if (old_context)
          context = gst_context_copy (old_context);
        else
          context = gst_context_new ("gst.gl.local_context", FALSE);

        s = gst_context_writable_structure (context);
        gst_structure_set (s, "context", GST_GL_TYPE_CONTEXT, gtk_sink->context,
            NULL);
        gst_query_set_context (query, context);
        gst_context_unref (context);

        ret = gtk_sink->context != NULL;
      }
      GST_LOG_OBJECT (gtk_sink, "context query of type %s %i", context_type,
          ret);

      if (ret)
        return ret;
    }
    default:
      res = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
      break;
  }

  return res;
}

static gboolean
gst_gtk_gl_sink_stop (GstBaseSink * bsink)
{
  return TRUE;
}

static GstStateChangeReturn
gst_gtk_gl_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstGtkGLSink *gtk_sink = GST_GTK_GL_SINK (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (gst_gtk_gl_sink_get_widget (gtk_sink) == NULL)
        return GST_STATE_CHANGE_FAILURE;

      /* After this point, gtk_sink->widget will always be set */

      if (!gtk_widget_get_parent (GTK_WIDGET (gtk_sink->widget))) {
        GST_ERROR_OBJECT (gtk_sink,
            "gtkglsink widget needs to be parented to work.");
        return GST_STATE_CHANGE_FAILURE;
      }

      if (!gtk_gst_gl_widget_init_winsys (gtk_sink->widget))
        return GST_STATE_CHANGE_FAILURE;

      gtk_sink->display = gtk_gst_gl_widget_get_display (gtk_sink->widget);
      gtk_sink->context = gtk_gst_gl_widget_get_context (gtk_sink->widget);
      gtk_sink->gtk_context =
          gtk_gst_gl_widget_get_gtk_context (gtk_sink->widget);

      if (!gtk_sink->display || !gtk_sink->context || !gtk_sink->gtk_context)
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gtk_gst_gl_widget_set_buffer (gtk_sink->widget, NULL);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (gtk_sink->display) {
        gst_object_unref (gtk_sink->display);
        gtk_sink->display = NULL;
      }

      if (gtk_sink->context) {
        gst_object_unref (gtk_sink->context);
        gtk_sink->context = NULL;
      }

      if (gtk_sink->gtk_context) {
        gst_object_unref (gtk_sink->gtk_context);
        gtk_sink->gtk_context = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_gtk_gl_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstGtkGLSink *gtk_sink;

  gtk_sink = GST_GTK_GL_SINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      *end = *start + GST_BUFFER_DURATION (buf);
    else {
      if (GST_VIDEO_INFO_FPS_N (&gtk_sink->v_info) > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND,
            GST_VIDEO_INFO_FPS_D (&gtk_sink->v_info),
            GST_VIDEO_INFO_FPS_N (&gtk_sink->v_info));
      }
    }
  }
}

gboolean
gst_gtk_gl_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstGtkGLSink *gtk_sink = GST_GTK_GL_SINK (bsink);

  GST_DEBUG ("set caps with %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&gtk_sink->v_info, caps))
    return FALSE;

  if (!gtk_gst_gl_widget_set_caps (gtk_sink->widget, caps))
    return FALSE;

  return TRUE;
}

static GstFlowReturn
gst_gtk_gl_sink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstGtkGLSink *gtk_sink;

  GST_TRACE ("rendering buffer:%p", buf);

  gtk_sink = GST_GTK_GL_SINK (vsink);

  gtk_gst_gl_widget_set_buffer (gtk_sink->widget, buf);

  return GST_FLOW_OK;
}

static gboolean
gst_gtk_gl_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstGtkGLSink *gtk_sink = GST_GTK_GL_SINK (bsink);
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  guint size;
  gboolean need_pool;

  if (!gtk_sink->display || !gtk_sink->context)
    return FALSE;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if ((pool = gtk_sink->pool))
    gst_object_ref (pool);

  if (pool != NULL) {
    GstCaps *pcaps;

    /* we had a pool, check caps */
    GST_DEBUG_OBJECT (gtk_sink, "check existing pool caps");
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    if (!gst_caps_is_equal (caps, pcaps)) {
      GST_DEBUG_OBJECT (gtk_sink, "pool has different caps");
      /* different caps, we can't use this pool */
      gst_object_unref (pool);
      pool = NULL;
    }
    gst_structure_free (config);
  }

  if (pool == NULL && need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    GST_DEBUG_OBJECT (gtk_sink, "create new pool");
    pool = gst_gl_buffer_pool_new (gtk_sink->context);

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;
  }
  /* we need at least 2 buffer because we hold on to the last one */
  if (pool) {
    gst_query_add_allocation_pool (query, pool, size, 2, 0);
    gst_object_unref (pool);
  }

  /* we also support various metadata */
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, 0);

  if (gtk_sink->context->gl_vtable->FenceSync)
    gst_query_add_allocation_meta (query, GST_GL_SYNC_META_API_TYPE, 0);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (bsink, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_DEBUG_OBJECT (bsink, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_DEBUG_OBJECT (bsink, "failed setting config");
    return FALSE;
  }
}
