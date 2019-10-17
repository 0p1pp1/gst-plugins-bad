/* Copyright (C) <2018> Philippe Normand <philn@igalia.com>
 * Copyright (C) <2018> Žan Doberšek <zdobersek@igalia.com>
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
 * SECTION:element-wpesrc
 * @title: wpesrc
 *
 * The wpesrc element is used to produce a video texture representing a web page
 * rendered off-screen by WPE.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 -v wpesrc location="https://gstreamer.freedesktop.org" ! queue ! glimagesink
 * ]|
 * Shows the GStreamer website homepage
 *
 * |[
 * gst-play-1.0 --videosink gtkglsink wpe://https://gstreamer.freedesktop.org
 * ]|
 * Shows the GStreamer website homepage as played with GstPlayer in a GTK+ window.
 *
 * |[
 * gst-launch-1.0  glvideomixer name=m sink_1::zorder=0 ! glimagesink wpesrc location="file:///home/phil/Downloads/plunk/index.html" draw-background=0 ! m. videotestsrc ! queue ! glupload ! glcolorconvert ! m.
 * ]|
 * Composite WPE with a video stream in a single OpenGL scene.
 *
 * |[
 * gst-launch-1.0 glvideomixer name=m sink_1::zorder=0 sink_0::height=818 sink_0::width=1920 ! gtkglsink wpesrc location="file:///home/phil/Downloads/plunk/index.html" draw-background=0 ! m. uridecodebin uri="http://192.168.1.44/Sintel.2010.1080p.mkv" name=d d. ! queue ! glupload ! glcolorconvert ! m.
 * ]|
 * Composite WPE with a video stream, sink_0 pad properties have to match the video dimensions.
 */

/*
 * TODO:
 * - Audio support (requires an AudioSession implementation in WebKit and a WPEBackend-fdo API for it)
 * - DMABuf support (requires changes in WPEBackend-fdo to expose DMABuf planes and fds)
 * - Custom EGLMemory allocator
 * - Better navigation events handling (would require a new GstNavigation API)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwpesrc.h"
#include <gst/gl/gl.h>
#include <gst/gl/egl/gstglmemoryegl.h>
#include <gst/gl/wayland/gstgldisplay_wayland.h>
#include <gst/video/video.h>
#include <xkbcommon/xkbcommon.h>

#include "WPEThreadedView.h"

GST_DEBUG_CATEGORY (wpe_src_debug);
#define GST_CAT_DEFAULT wpe_src_debug

#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
#define DEFAULT_FPS_N 30
#define DEFAULT_FPS_D 1

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_DRAW_BACKGROUND
};

enum
{
  SIGNAL_CONFIGURE_WEB_VIEW,
  SIGNAL_LOAD_BYTES,
  LAST_SIGNAL
};
static guint gst_wpe_src_signals[LAST_SIGNAL] = { 0 };

struct _GstWpeSrc
{
  GstGLBaseSrc parent;

  WPEThreadedView *view;

  /* properties */
  gchar *location;
  gboolean draw_background;

  GBytes *bytes;
};

static void gst_wpe_src_uri_handler_init (gpointer iface, gpointer data);

#define gst_wpe_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWpeSrc, gst_wpe_src, GST_TYPE_GL_BASE_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_wpe_src_uri_handler_init));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(memory:GLMemory), "
        "format = (string) RGBA, "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE ", "
        "framerate = " GST_VIDEO_FPS_RANGE ", "
        "pixel-aspect-ratio = (fraction)1/1," "texture-target = (string)2D")
    );

static gboolean
gst_wpe_src_fill_memory (GstGLBaseSrc * bsrc, GstGLMemory * memory)
{
  GstWpeSrc *src = GST_WPE_SRC (bsrc);
  const GstGLFuncs *gl;
  guint tex_id;
  GstEGLImage *locked_image;

  if (!gst_gl_context_check_feature (GST_GL_CONTEXT (bsrc->context),
          "EGL_KHR_image_base")) {
    GST_ERROR_OBJECT (src, "EGL_KHR_image_base is not supported");
    return FALSE;
  }

  GST_OBJECT_LOCK (src);

  gl = bsrc->context->gl_vtable;
  tex_id = gst_gl_memory_get_texture_id (memory);
  locked_image = src->view->image ();

  if (!locked_image) {
    GST_OBJECT_UNLOCK (src);
    return TRUE;
  }

  gl->ActiveTexture (GL_TEXTURE0 + memory->plane);
  gl->BindTexture (GL_TEXTURE_2D, tex_id);
  gl->EGLImageTargetTexture2D (GL_TEXTURE_2D,
      gst_egl_image_get_image (locked_image));
  gl->Flush ();
  GST_OBJECT_UNLOCK (src);
  return TRUE;
}

static gboolean
gst_wpe_src_gl_start (GstGLBaseSrc * base_src)
{
  GstWpeSrc *src = GST_WPE_SRC (base_src);
  gboolean result = TRUE;

  GST_INFO_OBJECT (src, "Starting up");
  GST_OBJECT_LOCK (src);
  src->view = new WPEThreadedView;
  result = src->view->initialize (src, base_src->context, base_src->display,
      GST_VIDEO_INFO_WIDTH (&base_src->out_info),
      GST_VIDEO_INFO_HEIGHT (&base_src->out_info));

  if (src->bytes != NULL) {
    src->view->loadData (src->bytes);
    g_bytes_unref (src->bytes);
    src->bytes = NULL;
  }

  GST_OBJECT_UNLOCK (src);
  if (!result) {
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
        ("WPEBackend-FDO EGL display initialisation failed"), (NULL));
  }
  return result;
}

static void
gst_wpe_src_gl_stop (GstGLBaseSrc * base_src)
{
  GstWpeSrc *src = GST_WPE_SRC (base_src);
  if (src->view) {
    delete src->view;
    src->view = NULL;
  }
}

static GstCaps *
gst_wpe_src_fixate (GstBaseSrc * base_src, GstCaps * caps)
{
  GstWpeSrc *src = GST_WPE_SRC (base_src);
  GstStructure *structure;
  gint width, height;

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", DEFAULT_WIDTH);
  gst_structure_fixate_field_nearest_int (structure, "height", DEFAULT_HEIGHT);

  if (gst_structure_has_field (structure, "framerate"))
    gst_structure_fixate_field_nearest_fraction (structure, "framerate",
        DEFAULT_FPS_N, DEFAULT_FPS_D);
  else
    gst_structure_set (structure, "framerate", GST_TYPE_FRACTION, DEFAULT_FPS_N,
        DEFAULT_FPS_D, NULL);

  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (base_src, caps);
  GST_INFO_OBJECT (base_src, "Fixated caps to %" GST_PTR_FORMAT, caps);

  if (src->view) {
    gst_structure_get (structure, "width", G_TYPE_INT, &width, "height", G_TYPE_INT, &height, NULL);
    src->view->resize (width, height);
  }
  return caps;
}

void
gst_wpe_src_configure_web_view (GstWpeSrc * src, WebKitWebView * webview)
{
  GValue args[2] = { {0}, {0} };

  g_value_init (&args[0], GST_TYPE_ELEMENT);
  g_value_set_object (&args[0], src);
  g_value_init (&args[1], G_TYPE_OBJECT);
  g_value_set_object (&args[1], webview);

  g_signal_emitv (args, gst_wpe_src_signals[SIGNAL_CONFIGURE_WEB_VIEW], 0,
      NULL);

  g_value_unset (&args[0]);
  g_value_unset (&args[1]);
}

static void
gst_wpe_src_load_bytes (GstWpeSrc * src, GBytes * bytes)
{
  if (src->view && GST_STATE (GST_ELEMENT_CAST (src)) > GST_STATE_NULL)
    src->view->loadData (bytes);
  else
    src->bytes = g_bytes_ref (bytes);
}

static gboolean
gst_wpe_src_set_location (GstWpeSrc * src, const gchar * location,
    GError ** error)
{
  g_free (src->location);
  src->location = g_strdup (location);
  if (src->view)
    src->view->loadUri (src->location);

  return TRUE;
}

static void
gst_wpe_src_set_draw_background (GstWpeSrc * src, gboolean draw_background)
{
  if (src->view)
    src->view->setDrawBackground (draw_background);
  src->draw_background = draw_background;
}

static void
gst_wpe_src_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstWpeSrc *src = GST_WPE_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
    {
      const gchar *location;

      location = g_value_get_string (value);
      if (location == NULL) {
        GST_WARNING_OBJECT (src, "location property cannot be NULL");
        return;
      }

      if (!gst_wpe_src_set_location (src, location, NULL)) {
        GST_WARNING_OBJECT (src, "badly formatted location");
        return;
      }
      break;
    }
    case PROP_DRAW_BACKGROUND:
      gst_wpe_src_set_draw_background (src, g_value_get_boolean (value));
      break;
    default:
      break;
  }
}

static void
gst_wpe_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstWpeSrc *src = GST_WPE_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, src->location);
      break;
    case PROP_DRAW_BACKGROUND:
      g_value_set_boolean (value, src->draw_background);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_wpe_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret = FALSE;
  GstWpeSrc *src = GST_WPE_SRC (parent);

  if (GST_EVENT_TYPE (event) == GST_EVENT_NAVIGATION) {
    const gchar *key;
    gint button;
    gdouble x, y;

    GST_DEBUG_OBJECT (src, "Processing event %" GST_PTR_FORMAT, event);
    if (!src->view) {
      return FALSE;
    }
    switch (gst_navigation_event_get_type (event)) {
      case GST_NAVIGATION_EVENT_KEY_PRESS:
      case GST_NAVIGATION_EVENT_KEY_RELEASE:
        if (gst_navigation_event_parse_key_event (event, &key)) {
          /* FIXME: This is wrong... The GstNavigation API should pass
             hardware-level informations, not high-level keysym strings */
          uint32_t keysym =
              (uint32_t) xkb_keysym_from_name (key, XKB_KEYSYM_NO_FLAGS);
          struct wpe_input_keyboard_event wpe_event;
          wpe_event.key_code = keysym;
          wpe_event.pressed =
              gst_navigation_event_get_type (event) ==
              GST_NAVIGATION_EVENT_KEY_PRESS;
          wpe_view_backend_dispatch_keyboard_event (src->view->backend (),
              &wpe_event);
          ret = TRUE;
        }
        break;
      case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS:
      case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:
        if (gst_navigation_event_parse_mouse_button_event (event, &button, &x,
                &y)) {
          struct wpe_input_pointer_event wpe_event;
          wpe_event.type = wpe_input_pointer_event_type_button;
          wpe_event.x = (int) x;
          wpe_event.y = (int) y;
          if (button == 1) {
            wpe_event.modifiers = wpe_input_pointer_modifier_button1;
          } else if (button == 2) {
            wpe_event.modifiers = wpe_input_pointer_modifier_button2;
          } else if (button == 3) {
            wpe_event.modifiers = wpe_input_pointer_modifier_button3;
          } else if (button == 4) {
            wpe_event.modifiers = wpe_input_pointer_modifier_button4;
          } else if (button == 5) {
            wpe_event.modifiers = wpe_input_pointer_modifier_button5;
          }
          wpe_event.button = button;
          wpe_event.state = 1;
          wpe_event.state =
              gst_navigation_event_get_type (event) ==
              GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS;
          wpe_view_backend_dispatch_pointer_event (src->view->backend (),
              &wpe_event);
          ret = TRUE;
        }
        break;
      case GST_NAVIGATION_EVENT_MOUSE_MOVE:
        if (gst_navigation_event_parse_mouse_move_event (event, &x, &y)) {
          struct wpe_input_pointer_event wpe_event;
          wpe_event.type = wpe_input_pointer_event_type_motion;
          wpe_event.x = (int) x;
          wpe_event.y = (int) y;
          wpe_view_backend_dispatch_pointer_event (src->view->backend (),
              &wpe_event);
          ret = TRUE;
        }
        break;
      default:
        break;
    }
    /* FIXME: No touch events handling support in GstNavigation */
  }

  if (!ret) {
    ret = gst_pad_event_default (pad, parent, event);
  }
  return ret;
}

static void
gst_wpe_src_init (GstWpeSrc * src)
{
  GstPad *pad = gst_element_get_static_pad (GST_ELEMENT_CAST (src), "src");

  gst_pad_set_event_function (pad, gst_wpe_src_event);
  gst_object_unref (pad);

  src->draw_background = TRUE;

  gst_base_src_set_live (GST_BASE_SRC_CAST (src), TRUE);
}

static GstURIType
gst_wpe_src_uri_get_type (GType)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_wpe_src_get_protocols (GType)
{
  static const char *protocols[] = { "wpe", NULL };
  return protocols;
}

static gchar *
gst_wpe_src_get_uri (GstURIHandler * handler)
{
  GstWpeSrc *src = GST_WPE_SRC (handler);
  return g_strdup_printf ("wpe://%s", src->location);
}

static gboolean
gst_wpe_src_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstWpeSrc *src = GST_WPE_SRC (handler);

  return gst_wpe_src_set_location (src, uri + 6, error);
}

static void
gst_wpe_src_uri_handler_init (gpointer iface_ptr, gpointer data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) iface_ptr;

  iface->get_type = gst_wpe_src_uri_get_type;
  iface->get_protocols = gst_wpe_src_get_protocols;
  iface->get_uri = gst_wpe_src_get_uri;
  iface->set_uri = gst_wpe_src_set_uri;
}

static void
gst_wpe_src_class_init (GstWpeSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstGLBaseSrcClass *gl_base_src_class = GST_GL_BASE_SRC_CLASS (klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_wpe_src_set_property;
  gobject_class->get_property = gst_wpe_src_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "location",
          "The URL to display",
          "", (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DRAW_BACKGROUND,
      g_param_spec_boolean ("draw-background", "Draws the background",
          "Whether to draw the WebView background", TRUE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (gstelement_class,
      "WPE source", "Source/Video",
      "Creates a video stream from a WPE browser",
      "Philippe Normand <philn@igalia.com>, Žan Doberšek <zdobersek@igalia.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);

  base_src_class->fixate = GST_DEBUG_FUNCPTR (gst_wpe_src_fixate);

  gl_base_src_class->supported_gl_api =
      static_cast < GstGLAPI >
      (GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2);
  gl_base_src_class->gl_start = GST_DEBUG_FUNCPTR (gst_wpe_src_gl_start);
  gl_base_src_class->gl_stop = GST_DEBUG_FUNCPTR (gst_wpe_src_gl_stop);
  gl_base_src_class->fill_gl_memory =
      GST_DEBUG_FUNCPTR (gst_wpe_src_fill_memory);

  /**
   * GstWpeSrc::configure-web-view:
   * @src: the object which received the signal
   * @webview: the webView
   *
   * Allow application to configure the webView settings.
   */
  gst_wpe_src_signals[SIGNAL_CONFIGURE_WEB_VIEW] =
      g_signal_new ("configure-web-view", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, G_TYPE_OBJECT);

  /**
   * GstWpeSrc::load-bytes:
   * @src: the object which received the signal
   * @bytes: the GBytes data to load
   *
   * Load the specified bytes into the internal webView.
   */
  gst_wpe_src_signals[SIGNAL_LOAD_BYTES] =
      g_signal_new_class_handler ("load-bytes", G_TYPE_FROM_CLASS (klass),
      static_cast < GSignalFlags > (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_CALLBACK (gst_wpe_src_load_bytes), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 1, G_TYPE_BYTES);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (wpe_src_debug, "wpesrc", 0, "WPE Source");

  return gst_element_register (plugin, "wpesrc", GST_RANK_NONE,
      GST_TYPE_WPE_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    wpe, "WPE src plugin", plugin_init, VERSION, GST_LICENSE, PACKAGE,
    GST_PACKAGE_ORIGIN)
