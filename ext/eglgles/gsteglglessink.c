/*
 * GStreamer EGL/GLES Sink
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-eglglessink
 *
 * EglGlesSink renders video frames on a EGL surface it sets up
 * from a window it either creates (on X11) or gets a handle to
 * through it's xOverlay interface. All the display/surface logic
 * in this sink uses EGL to interact with the native window system.
 * The rendering logic, in turn, uses OpenGL ES v2.
 *
 * This sink has been tested to work on X11/Mesa and on Android
 * (From Gingerbread on to Jelly Bean) and while it's currently
 * using an slow copy-over rendering path it has proven to be fast
 * enough on the devices we have tried it on. 
 *
 * <refsect2>
 * <title>Supported EGL/OpenGL ES versions</title>
 * <para>
 * This Sink uses EGLv1 and GLESv2
 * </para>
 * </refsect2>
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m videotestsrc ! eglglessink
 * ]|
 * </refsect2>
 *
 * <refsect2>
 * <title>Example launch line with internal window creation disabled</title>
 * <para>
 * By setting the can_create_window property to FALSE you can force the
 * sink to wait for a window handle through it's xOverlay interface even
 * if internal window creation is supported by the platform. Window creation
 * is only supported in X11 right now but it should be trivial to add support
 * for different platforms.
 * </para>
 * |[
 * gst-launch -v -m videotestsrc ! eglglessink can_create_window=FALSE
 * ]|
 * </refsect2>
 *
 * <refsect2>
 * <title>Scaling</title>
 * <para>
 * The sink will try it's best to consider the incoming frame's and display's
 * pixel aspect ratio and fill the corresponding surface without altering the
 * decoded frame's geometry when scaling. You can disable this logic by setting
 * the force_aspect_ratio property to FALSE, in which case the sink will just
 * fill the entire surface it has access to regardles of the PAR/DAR relationship.
 * </para>
 * <para>
 * Querying the display aspect ratio is only supported with EGL versions >= 1.2.
 * The sink will just assume the DAR to be 1/1 if it can't get access to this
 * information.
 * </para>
 * <para>
 * Here is an example launch line with the PAR/DAR aware scaling disabled:
 * </para>
 * |[
 * gst-launch -v -m videotestsrc ! eglglessink force_aspect_ratio=FALSE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES

#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/video-frame.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include <gst/video/videooverlay.h>
#include <gst/egl/egl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef USE_EGL_RPI
#include <bcm_host.h>
#endif

#include "video_platform_wrapper.h"

#include "gsteglglessink.h"

/* Some EGL implementations are reporting wrong
 * values for the display's EGL_PIXEL_ASPECT_RATIO.
 * They are required by the khronos specs to report
 * this value as w/h * EGL_DISPLAY_SCALING (Which is
 * a constant with value 10000) but at least the
 * Galaxy SIII (Android) is reporting just 1 when
 * w = h. We use these two to bound returned values to
 * sanity.
 */
#define EGL_SANE_DAR_MIN ((EGL_DISPLAY_SCALING)/10)
#define EGL_SANE_DAR_MAX ((EGL_DISPLAY_SCALING)*10)

GST_DEBUG_CATEGORY_STATIC (gst_eglglessink_debug);
#define GST_CAT_DEFAULT gst_eglglessink_debug

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);

/* GLESv2 GLSL Shaders
 *
 * OpenGL ES Standard does not mandate YUV support. This is
 * why most of these shaders deal with Packed/Planar YUV->RGB
 * conversion.
 */

/* *INDENT-OFF* */
/* Direct vertex copy */
static const char *vert_COPY_prog = {
      "attribute vec3 position;"
      "attribute vec2 texpos;"
      "varying vec2 opos;"
      "void main(void)"
      "{"
      " opos = texpos;"
      " gl_Position = vec4(position, 1.0);"
      "}"
};

static const char *vert_COPY_prog_no_tex = {
      "attribute vec3 position;"
      "void main(void)"
      "{"
      " gl_Position = vec4(position, 1.0);"
      "}"
};

/* Paint all black */
static const char *frag_BLACK_prog = {
  "precision mediump float;"
      "void main(void)"
      "{"
      " gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);"
      "}"
};

/* Direct fragments copy */
static const char *frag_COPY_prog = {
  "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "void main(void)"
      "{"
      " vec4 t = texture2D(tex, opos / tex_scale0);"
      " gl_FragColor = vec4(t.rgb, 1.0);"
      "}"
};

/* Channel reordering for XYZ <-> ZYX conversion */
static const char *frag_REORDER_prog = {
  "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "void main(void)"
      "{"
      " vec4 t = texture2D(tex, opos / tex_scale0);"
      " gl_FragColor = vec4(t.%c, t.%c, t.%c, 1.0);"
      "}"
};

/* Packed YUV converters */

/** AYUV to RGB conversion */
static const char *frag_AYUV_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv  = texture2D(tex,opos / tex_scale0).gba;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};

/* Planar YUV converters */

/** YUV to RGB conversion */
static const char *frag_PLANAR_YUV_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D Ytex,Utex,Vtex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv.x=texture2D(Ytex,opos / tex_scale0).r;"
      "  yuv.y=texture2D(Utex,opos / tex_scale1).r;"
      "  yuv.z=texture2D(Vtex,opos / tex_scale2).r;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};

/** NV12/NV21 to RGB conversion */
static const char *frag_NV12_NV21_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D Ytex,UVtex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv.x=texture2D(Ytex,opos / tex_scale0).r;"
      "  yuv.yz=texture2D(UVtex,opos / tex_scale1).%c%c;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};
/* *INDENT-ON* */

static const EGLint eglglessink_RGBA8888_attribs[] = {
  EGL_RED_SIZE, 8,
  EGL_GREEN_SIZE, 8,
  EGL_BLUE_SIZE, 8,
  EGL_ALPHA_SIZE, 8,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};

/* Input capabilities. */
static GstStaticPadTemplate gst_eglglessink_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ "
            "RGBA, BGRA, ARGB, ABGR, "
            "RGBx, BGRx, xRGB, xBGR, "
            "AYUV, Y444, I420, YV12, "
            "NV12, NV21, Y42B, Y41B, " "RGB, BGR, RGB16 }")));

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CREATE_WINDOW,
  PROP_FORCE_ASPECT_RATIO,
};

static void gst_eglglessink_finalize (GObject * object);
static void gst_eglglessink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_eglglessink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_eglglessink_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_eglglessink_prepare (GstBaseSink * bsink,
    GstBuffer * buf);
static GstFlowReturn gst_eglglessink_show_frame (GstVideoSink * vsink,
    GstBuffer * buf);
static gboolean gst_eglglessink_setcaps (GstBaseSink * bsink, GstCaps * caps);
static GstCaps *gst_eglglessink_getcaps (GstBaseSink * bsink, GstCaps * filter);
static gboolean gst_eglglessink_propose_allocation (GstBaseSink * bsink,
    GstQuery * query);

/* VideoOverlay interface cruft */
static void gst_eglglessink_videooverlay_init (GstVideoOverlayInterface *
    iface);

/* Actual VideoOverlay interface funcs */
static void gst_eglglessink_expose (GstVideoOverlay * overlay);
static void gst_eglglessink_set_window_handle (GstVideoOverlay * overlay,
    guintptr id);
static void gst_eglglessink_set_render_rectangle (GstVideoOverlay * overlay,
    gint x, gint y, gint width, gint height);

/* Utility */
static EGLNativeWindowType gst_eglglessink_create_window (GstEglGlesSink *
    eglglessink, gint width, gint height);
static gboolean gst_eglglessink_fill_supported_fbuffer_configs (GstEglGlesSink *
    eglglessink);
static gboolean gst_eglglessink_init_egl_display (GstEglGlesSink * eglglessink);
static gboolean gst_eglglessink_choose_config (GstEglGlesSink * eglglessink);
static gboolean gst_eglglessink_init_egl_surface (GstEglGlesSink * eglglessink);
static void gst_eglglessink_init_egl_exts (GstEglGlesSink * eglglessink);
static gboolean gst_eglglessink_setup_vbo (GstEglGlesSink * eglglessink,
    gboolean reset);
static gboolean
gst_eglglessink_configure_caps (GstEglGlesSink * eglglessink, GstCaps * caps);
static GstFlowReturn gst_eglglessink_upload (GstEglGlesSink * sink,
    GstBuffer * buf);
static GstFlowReturn gst_eglglessink_render (GstEglGlesSink * sink);
static GstFlowReturn gst_eglglessink_queue_object (GstEglGlesSink * sink,
    GstMiniObject * obj);
static inline gboolean got_gl_error (const char *wtf);
static inline gboolean got_egl_error (const char *wtf);
static inline gboolean egl_init (GstEglGlesSink * eglglessink);
static gboolean gst_eglglessink_context_make_current (GstEglGlesSink *
    eglglessink, gboolean bind);
static void gst_eglglessink_wipe_eglglesctx (GstEglGlesSink * eglglessink);
static GstBuffer *gst_eglglessink_allocate_eglimage (GstEglGlesSink *
    eglglessink, GstVideoFormat format, gint width, gint height);
static GstBufferPool *gst_egl_image_buffer_pool_new (GstEglGlesSink *
    eglglessink, GstEGLDisplay * display);

#define parent_class gst_eglglessink_parent_class
G_DEFINE_TYPE_WITH_CODE (GstEglGlesSink, gst_eglglessink, GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_eglglessink_videooverlay_init));


static GstCaps *
_gst_video_format_new_template_caps (GstVideoFormat format)
{
  return gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, gst_video_format_to_string (format),
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
}

static gboolean
gst_eglglessink_fill_supported_fbuffer_configs (GstEglGlesSink * eglglessink)
{
  gboolean ret = FALSE;
  EGLint cfg_number;
  GstCaps *caps;

  GST_DEBUG_OBJECT (eglglessink,
      "Building initial list of wanted eglattribs per format");

  /* Init supported format/caps list */
  caps = gst_caps_new_empty ();

  if (eglChooseConfig (gst_egl_display_get (eglglessink->eglglesctx.display),
          eglglessink_RGBA8888_attribs, NULL, 1, &cfg_number) != EGL_FALSE) {
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBA));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRA));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ARGB));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ABGR));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBx));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRx));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xRGB));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xBGR));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_AYUV));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y444));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGR));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_I420));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_YV12));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV12));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV21));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y42B));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y41B));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB16));
    ret = TRUE;
  } else {
    GST_INFO_OBJECT (eglglessink,
        "EGL display doesn't support RGBA8888 config");
  }

  GST_OBJECT_LOCK (eglglessink);
  gst_caps_replace (&eglglessink->sinkcaps, caps);
  GST_OBJECT_UNLOCK (eglglessink);
  gst_caps_unref (caps);

  return ret;
}

static inline gboolean
egl_init (GstEglGlesSink * eglglessink)
{
  if (!platform_wrapper_init ()) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't init EGL platform wrapper");
    goto HANDLE_ERROR;
  }

  if (!gst_eglglessink_init_egl_display (eglglessink)) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't init EGL display");
    goto HANDLE_ERROR;
  }

  gst_eglglessink_init_egl_exts (eglglessink);

  if (!gst_eglglessink_fill_supported_fbuffer_configs (eglglessink)) {
    GST_ERROR_OBJECT (eglglessink, "Display support NONE of our configs");
    goto HANDLE_ERROR;
  }

  eglglessink->egl_started = TRUE;

  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Failed to perform EGL init");
  return FALSE;
}

static gpointer
render_thread_func (GstEglGlesSink * eglglessink)
{
  GstMessage *message;
  GValue val = { 0 };
  GstDataQueueItem *item = NULL;
  GstFlowReturn last_flow = GST_FLOW_OK;

  g_value_init (&val, GST_TYPE_G_THREAD);
  g_value_set_boxed (&val, g_thread_self ());
  message = gst_message_new_stream_status (GST_OBJECT_CAST (eglglessink),
      GST_STREAM_STATUS_TYPE_ENTER, GST_ELEMENT_CAST (eglglessink));
  gst_message_set_stream_status_object (message, &val);
  GST_DEBUG_OBJECT (eglglessink, "posting ENTER stream status");
  gst_element_post_message (GST_ELEMENT_CAST (eglglessink), message);
  g_value_unset (&val);

  eglBindAPI (EGL_OPENGL_ES_API);

  while (gst_data_queue_pop (eglglessink->queue, &item)) {
    GstMiniObject *object = item->object;

    GST_DEBUG_OBJECT (eglglessink, "Handling object %" GST_PTR_FORMAT, object);

    if (GST_IS_CAPS (object)) {
      GstCaps *caps = GST_CAPS_CAST (object);

      if (caps != eglglessink->configured_caps) {
        if (!gst_eglglessink_configure_caps (eglglessink, caps)) {
          last_flow = GST_FLOW_NOT_NEGOTIATED;
        }
      }
    } else if (GST_IS_QUERY (object)) {
      GstQuery *query = GST_QUERY_CAST (object);
      GstStructure *s = (GstStructure *) gst_query_get_structure (query);

      if (gst_structure_has_name (s, "eglglessink-allocate-eglimage")) {
        GstBuffer *buffer;
        GstVideoFormat format;
        gint width, height;
        GValue v = { 0, };

        if (!gst_structure_get_enum (s, "format", GST_TYPE_VIDEO_FORMAT,
                (gint *) & format)
            || !gst_structure_get_int (s, "width", &width)
            || !gst_structure_get_int (s, "height", &height)) {
          g_assert_not_reached ();
        }

        buffer =
            gst_eglglessink_allocate_eglimage (eglglessink, format, width,
            height);
        g_value_init (&v, G_TYPE_POINTER);
        g_value_set_pointer (&v, buffer);
        gst_structure_set_value (s, "buffer", &v);
        g_value_unset (&v);
      } else {
        g_assert_not_reached ();
      }
      last_flow = GST_FLOW_OK;
    } else if (GST_IS_BUFFER (object)) {
      GstBuffer *buf = GST_BUFFER_CAST (item->object);

      if (eglglessink->configured_caps) {
        last_flow = gst_eglglessink_upload (eglglessink, buf);
      } else {
        last_flow = GST_FLOW_OK;
        GST_DEBUG_OBJECT (eglglessink,
            "No caps configured yet, not drawing anything");
      }
    } else if (!object) {
      if (eglglessink->configured_caps) {
        last_flow = gst_eglglessink_render (eglglessink);
      } else {
        last_flow = GST_FLOW_OK;
        GST_DEBUG_OBJECT (eglglessink,
            "No caps configured yet, not drawing anything");
      }
    } else {
      g_assert_not_reached ();
    }

    item->destroy (item);
    g_mutex_lock (&eglglessink->render_lock);
    eglglessink->last_flow = last_flow;
    eglglessink->dequeued_object = object;
    g_cond_broadcast (&eglglessink->render_cond);
    g_mutex_unlock (&eglglessink->render_lock);

    if (last_flow != GST_FLOW_OK)
      break;
    GST_DEBUG_OBJECT (eglglessink, "Successfully handled object");
  }

  if (last_flow == GST_FLOW_OK) {
    g_mutex_lock (&eglglessink->render_lock);
    eglglessink->last_flow = GST_FLOW_FLUSHING;
    eglglessink->dequeued_object = NULL;
    g_cond_broadcast (&eglglessink->render_cond);
    g_mutex_unlock (&eglglessink->render_lock);
  }

  GST_DEBUG_OBJECT (eglglessink, "Shutting down thread");

  /* EGL/GLES cleanup */
  gst_eglglessink_wipe_eglglesctx (eglglessink);

  if (eglglessink->configured_caps) {
    gst_caps_unref (eglglessink->configured_caps);
    eglglessink->configured_caps = NULL;
  }

  g_value_init (&val, GST_TYPE_G_THREAD);
  g_value_set_boxed (&val, g_thread_self ());
  message = gst_message_new_stream_status (GST_OBJECT_CAST (eglglessink),
      GST_STREAM_STATUS_TYPE_LEAVE, GST_ELEMENT_CAST (eglglessink));
  gst_message_set_stream_status_object (message, &val);
  GST_DEBUG_OBJECT (eglglessink, "posting LEAVE stream status");
  gst_element_post_message (GST_ELEMENT_CAST (eglglessink), message);
  g_value_unset (&val);

  return NULL;
}

static void
gst_eglglessink_wipe_eglglesctx (GstEglGlesSink * eglglessink)
{
  gint i;

  glUseProgram (0);

  if (eglglessink->have_vbo) {
    glDeleteBuffers (1, &eglglessink->eglglesctx.position_buffer);
    glDeleteBuffers (1, &eglglessink->eglglesctx.index_buffer);
    eglglessink->have_vbo = FALSE;
  }

  if (eglglessink->have_texture) {
    glDeleteTextures (eglglessink->eglglesctx.n_textures,
        eglglessink->eglglesctx.texture);
    eglglessink->have_texture = FALSE;
    eglglessink->eglglesctx.n_textures = 0;
  }

  for (i = 0; i < 2; i++) {
    if (eglglessink->eglglesctx.glslprogram[i]) {
      glDetachShader (eglglessink->eglglesctx.glslprogram[i],
          eglglessink->eglglesctx.fragshader[i]);
      glDetachShader (eglglessink->eglglesctx.glslprogram[i],
          eglglessink->eglglesctx.vertshader[i]);
      glDeleteProgram (eglglessink->eglglesctx.glslprogram[i]);
      glDeleteShader (eglglessink->eglglesctx.fragshader[i]);
      glDeleteShader (eglglessink->eglglesctx.vertshader[i]);
      eglglessink->eglglesctx.glslprogram[i] = 0;
      eglglessink->eglglesctx.fragshader[i] = 0;
      eglglessink->eglglesctx.vertshader[i] = 0;
    }
  }

  gst_eglglessink_context_make_current (eglglessink, FALSE);

  if (eglglessink->eglglesctx.surface) {
    eglDestroySurface (gst_egl_display_get (eglglessink->eglglesctx.display),
        eglglessink->eglglesctx.surface);
    eglglessink->eglglesctx.surface = NULL;
    eglglessink->have_surface = FALSE;
  }

  if (eglglessink->eglglesctx.eglcontext) {
    eglDestroyContext (gst_egl_display_get (eglglessink->eglglesctx.display),
        eglglessink->eglglesctx.eglcontext);
    eglglessink->eglglesctx.eglcontext = NULL;
  }
}

static gboolean
gst_eglglessink_start (GstEglGlesSink * eglglessink)
{
  GError *error = NULL;

  GST_DEBUG_OBJECT (eglglessink, "Starting");

  if (!eglglessink->egl_started) {
    GST_ERROR_OBJECT (eglglessink, "EGL uninitialized. Bailing out");
    goto HANDLE_ERROR;
  }

  /* Ask for a window to render to */
  if (!eglglessink->have_window)
    gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (eglglessink));

  if (!eglglessink->have_window && !eglglessink->create_window) {
    GST_ERROR_OBJECT (eglglessink, "Window handle unavailable and we "
        "were instructed not to create an internal one. Bailing out.");
    goto HANDLE_ERROR;
  }

  eglglessink->last_flow = GST_FLOW_OK;
  eglglessink->display_region.w = 0;
  eglglessink->display_region.h = 0;

  gst_data_queue_set_flushing (eglglessink->queue, FALSE);

#if !GLIB_CHECK_VERSION (2, 31, 0)
  eglglessink->thread =
      g_thread_create ((GThreadFunc) render_thread_func, eglglessink, TRUE,
      &error);
#else
  eglglessink->thread = g_thread_try_new ("eglglessink-render",
      (GThreadFunc) render_thread_func, eglglessink, &error);
#endif

  if (!eglglessink->thread || error != NULL)
    goto HANDLE_ERROR;

  GST_DEBUG_OBJECT (eglglessink, "Started");

  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Couldn't start");
  g_clear_error (&error);
  return FALSE;
}

static gboolean
gst_eglglessink_stop (GstEglGlesSink * eglglessink)
{
  GST_DEBUG_OBJECT (eglglessink, "Stopping");

  gst_data_queue_set_flushing (eglglessink->queue, TRUE);
  g_mutex_lock (&eglglessink->render_lock);
  g_cond_broadcast (&eglglessink->render_cond);
  g_mutex_unlock (&eglglessink->render_lock);

  if (eglglessink->thread) {
    g_thread_join (eglglessink->thread);
    eglglessink->thread = NULL;
  }
  eglglessink->last_flow = GST_FLOW_FLUSHING;

  gst_buffer_replace (&eglglessink->last_buffer, NULL);

  if (eglglessink->using_own_window) {
    platform_destroy_native_window (gst_egl_display_get
        (eglglessink->eglglesctx.display), eglglessink->eglglesctx.used_window,
        &eglglessink->own_window_data);
    eglglessink->eglglesctx.used_window = 0;
    eglglessink->have_window = FALSE;
  }
  eglglessink->eglglesctx.used_window = 0;
  if (eglglessink->current_caps) {
    gst_caps_unref (eglglessink->current_caps);
    eglglessink->current_caps = NULL;
  }

  GST_DEBUG_OBJECT (eglglessink, "Stopped");

  return TRUE;
}

static void
gst_eglglessink_videooverlay_init (GstVideoOverlayInterface * iface)
{
  iface->set_window_handle = gst_eglglessink_set_window_handle;
  iface->expose = gst_eglglessink_expose;
  iface->set_render_rectangle = gst_eglglessink_set_render_rectangle;
}

static inline gboolean
got_gl_error (const char *wtf)
{
  GLuint error = GL_NO_ERROR;

  if ((error = glGetError ()) != GL_NO_ERROR) {
    GST_CAT_ERROR (GST_CAT_DEFAULT, "GL ERROR: %s returned 0x%04x", wtf, error);
    return TRUE;
  }
  return FALSE;
}

static inline gboolean
got_egl_error (const char *wtf)
{
  EGLint error;

  if ((error = eglGetError ()) != EGL_SUCCESS) {
    GST_CAT_DEBUG (GST_CAT_DEFAULT, "EGL ERROR: %s returned 0x%04x", wtf,
        error);
    return TRUE;
  }

  return FALSE;
}

static EGLNativeWindowType
gst_eglglessink_create_window (GstEglGlesSink * eglglessink, gint width,
    gint height)
{
  EGLNativeWindowType window = 0;

  if (!eglglessink->create_window) {
    GST_ERROR_OBJECT (eglglessink, "This sink can't create a window by itself");
    return window;
  } else
    GST_INFO_OBJECT (eglglessink, "Attempting internal window creation");

  window =
      platform_create_native_window (width, height,
      &eglglessink->own_window_data);
  if (!window) {
    GST_ERROR_OBJECT (eglglessink, "Could not create window");
    return window;
  }
  return window;
}

static void
gst_eglglessink_expose (GstVideoOverlay * overlay)
{
  GstEglGlesSink *eglglessink;
  GstFlowReturn ret;

  eglglessink = GST_EGLGLESSINK (overlay);
  GST_DEBUG_OBJECT (eglglessink, "Expose catched, redisplay");

  /* Render from last seen buffer */
  ret = gst_eglglessink_queue_object (eglglessink, NULL);
  if (ret == GST_FLOW_ERROR)
    GST_ERROR_OBJECT (eglglessink, "Redisplay failed");
}

/* Prints available EGL/GLES extensions 
 * If another rendering path is implemented this is the place
 * where you want to check for the availability of its supporting
 * EGL/GLES extensions.
 */
static void
gst_eglglessink_init_egl_exts (GstEglGlesSink * eglglessink)
{
  const char *eglexts;
  unsigned const char *glexts;

  eglexts =
      eglQueryString (gst_egl_display_get (eglglessink->eglglesctx.display),
      EGL_EXTENSIONS);
  glexts = glGetString (GL_EXTENSIONS);

  GST_DEBUG_OBJECT (eglglessink, "Available EGL extensions: %s\n",
      GST_STR_NULL (eglexts));
  GST_DEBUG_OBJECT (eglglessink, "Available GLES extensions: %s\n",
      GST_STR_NULL ((const char *) glexts));

  return;
}

static gboolean
gst_eglglessink_setup_vbo (GstEglGlesSink * eglglessink, gboolean reset)
{
  gdouble render_width, render_height;
  gdouble texture_width, texture_height;
  gdouble x1, x2, y1, y2;
  gdouble tx1, tx2, ty1, ty2;

  GST_INFO_OBJECT (eglglessink, "VBO setup. have_vbo:%d, should reset %d",
      eglglessink->have_vbo, reset);

  if (eglglessink->have_vbo && reset) {
    glDeleteBuffers (1, &eglglessink->eglglesctx.position_buffer);
    glDeleteBuffers (1, &eglglessink->eglglesctx.index_buffer);
    eglglessink->have_vbo = FALSE;
  }

  render_width = eglglessink->render_region.w;
  render_height = eglglessink->render_region.h;

  texture_width = eglglessink->configured_info.width;
  texture_height = eglglessink->configured_info.height;

  GST_DEBUG_OBJECT (eglglessink, "Performing VBO setup");

  x1 = (eglglessink->display_region.x / render_width) * 2.0 - 1;
  y1 = (eglglessink->display_region.y / render_height) * 2.0 - 1;
  x2 = ((eglglessink->display_region.x +
          eglglessink->display_region.w) / render_width) * 2.0 - 1;
  y2 = ((eglglessink->display_region.y +
          eglglessink->display_region.h) / render_height) * 2.0 - 1;

  tx1 = (eglglessink->crop.x / texture_width);
  tx2 = ((eglglessink->crop.x + eglglessink->crop.w) / texture_width);
  ty1 = (eglglessink->crop.y / texture_height);
  ty2 = ((eglglessink->crop.y + eglglessink->crop.h) / texture_height);

  eglglessink->eglglesctx.position_array[0].x = x2;
  eglglessink->eglglesctx.position_array[0].y = y2;
  eglglessink->eglglesctx.position_array[0].z = 0;
  eglglessink->eglglesctx.position_array[0].a = tx2;
  eglglessink->eglglesctx.position_array[0].b = ty1;

  eglglessink->eglglesctx.position_array[1].x = x2;
  eglglessink->eglglesctx.position_array[1].y = y1;
  eglglessink->eglglesctx.position_array[1].z = 0;
  eglglessink->eglglesctx.position_array[1].a = tx2;
  eglglessink->eglglesctx.position_array[1].b = ty2;

  eglglessink->eglglesctx.position_array[2].x = x1;
  eglglessink->eglglesctx.position_array[2].y = y2;
  eglglessink->eglglesctx.position_array[2].z = 0;
  eglglessink->eglglesctx.position_array[2].a = tx1;
  eglglessink->eglglesctx.position_array[2].b = ty1;

  eglglessink->eglglesctx.position_array[3].x = x1;
  eglglessink->eglglesctx.position_array[3].y = y1;
  eglglessink->eglglesctx.position_array[3].z = 0;
  eglglessink->eglglesctx.position_array[3].a = tx1;
  eglglessink->eglglesctx.position_array[3].b = ty2;

  if (eglglessink->display_region.x == 0) {
    /* Borders top/bottom */

    eglglessink->eglglesctx.position_array[4 + 0].x = 1;
    eglglessink->eglglesctx.position_array[4 + 0].y = 1;
    eglglessink->eglglesctx.position_array[4 + 0].z = 0;

    eglglessink->eglglesctx.position_array[4 + 1].x = x2;
    eglglessink->eglglesctx.position_array[4 + 1].y = y2;
    eglglessink->eglglesctx.position_array[4 + 1].z = 0;

    eglglessink->eglglesctx.position_array[4 + 2].x = -1;
    eglglessink->eglglesctx.position_array[4 + 2].y = 1;
    eglglessink->eglglesctx.position_array[4 + 2].z = 0;

    eglglessink->eglglesctx.position_array[4 + 3].x = x1;
    eglglessink->eglglesctx.position_array[4 + 3].y = y2;
    eglglessink->eglglesctx.position_array[4 + 3].z = 0;

    eglglessink->eglglesctx.position_array[8 + 0].x = 1;
    eglglessink->eglglesctx.position_array[8 + 0].y = y1;
    eglglessink->eglglesctx.position_array[8 + 0].z = 0;

    eglglessink->eglglesctx.position_array[8 + 1].x = 1;
    eglglessink->eglglesctx.position_array[8 + 1].y = -1;
    eglglessink->eglglesctx.position_array[8 + 1].z = 0;

    eglglessink->eglglesctx.position_array[8 + 2].x = x1;
    eglglessink->eglglesctx.position_array[8 + 2].y = y1;
    eglglessink->eglglesctx.position_array[8 + 2].z = 0;

    eglglessink->eglglesctx.position_array[8 + 3].x = -1;
    eglglessink->eglglesctx.position_array[8 + 3].y = -1;
    eglglessink->eglglesctx.position_array[8 + 3].z = 0;
  } else {
    /* Borders left/right */

    eglglessink->eglglesctx.position_array[4 + 0].x = x1;
    eglglessink->eglglesctx.position_array[4 + 0].y = 1;
    eglglessink->eglglesctx.position_array[4 + 0].z = 0;

    eglglessink->eglglesctx.position_array[4 + 1].x = x1;
    eglglessink->eglglesctx.position_array[4 + 1].y = -1;
    eglglessink->eglglesctx.position_array[4 + 1].z = 0;

    eglglessink->eglglesctx.position_array[4 + 2].x = -1;
    eglglessink->eglglesctx.position_array[4 + 2].y = 1;
    eglglessink->eglglesctx.position_array[4 + 2].z = 0;

    eglglessink->eglglesctx.position_array[4 + 3].x = -1;
    eglglessink->eglglesctx.position_array[4 + 3].y = -1;
    eglglessink->eglglesctx.position_array[4 + 3].z = 0;

    eglglessink->eglglesctx.position_array[8 + 0].x = 1;
    eglglessink->eglglesctx.position_array[8 + 0].y = 1;
    eglglessink->eglglesctx.position_array[8 + 0].z = 0;

    eglglessink->eglglesctx.position_array[8 + 1].x = 1;
    eglglessink->eglglesctx.position_array[8 + 1].y = -1;
    eglglessink->eglglesctx.position_array[8 + 1].z = 0;

    eglglessink->eglglesctx.position_array[8 + 2].x = x2;
    eglglessink->eglglesctx.position_array[8 + 2].y = y2;
    eglglessink->eglglesctx.position_array[8 + 2].z = 0;

    eglglessink->eglglesctx.position_array[8 + 3].x = x2;
    eglglessink->eglglesctx.position_array[8 + 3].y = -1;
    eglglessink->eglglesctx.position_array[8 + 3].z = 0;
  }

  eglglessink->eglglesctx.index_array[0] = 0;
  eglglessink->eglglesctx.index_array[1] = 1;
  eglglessink->eglglesctx.index_array[2] = 2;
  eglglessink->eglglesctx.index_array[3] = 3;

  glGenBuffers (1, &eglglessink->eglglesctx.position_buffer);
  glGenBuffers (1, &eglglessink->eglglesctx.index_buffer);
  if (got_gl_error ("glGenBuffers"))
    goto HANDLE_ERROR_LOCKED;

  glBindBuffer (GL_ARRAY_BUFFER, eglglessink->eglglesctx.position_buffer);
  if (got_gl_error ("glBindBuffer position_buffer"))
    goto HANDLE_ERROR_LOCKED;

  glBufferData (GL_ARRAY_BUFFER,
      sizeof (eglglessink->eglglesctx.position_array),
      eglglessink->eglglesctx.position_array, GL_STATIC_DRAW);
  if (got_gl_error ("glBufferData position_buffer"))
    goto HANDLE_ERROR_LOCKED;

  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, eglglessink->eglglesctx.index_buffer);
  if (got_gl_error ("glBindBuffer index_buffer"))
    goto HANDLE_ERROR_LOCKED;

  glBufferData (GL_ELEMENT_ARRAY_BUFFER,
      sizeof (eglglessink->eglglesctx.index_array),
      eglglessink->eglglesctx.index_array, GL_STATIC_DRAW);
  if (got_gl_error ("glBufferData index_buffer"))
    goto HANDLE_ERROR_LOCKED;

  eglglessink->have_vbo = TRUE;
  GST_DEBUG_OBJECT (eglglessink, "VBO setup done");

  return TRUE;

HANDLE_ERROR_LOCKED:
  GST_ERROR_OBJECT (eglglessink, "Unable to perform VBO setup");
  return FALSE;
}

/* XXX: Lock eglgles context? */
static gboolean
gst_eglglessink_update_surface_dimensions (GstEglGlesSink * eglglessink)
{
  gint width, height;

  /* Save surface dims */
  eglQuerySurface (gst_egl_display_get (eglglessink->eglglesctx.display),
      eglglessink->eglglesctx.surface, EGL_WIDTH, &width);
  eglQuerySurface (gst_egl_display_get (eglglessink->eglglesctx.display),
      eglglessink->eglglesctx.surface, EGL_HEIGHT, &height);

  if (width != eglglessink->eglglesctx.surface_width ||
      height != eglglessink->eglglesctx.surface_height) {
    eglglessink->eglglesctx.surface_width = width;
    eglglessink->eglglesctx.surface_height = height;
    GST_INFO_OBJECT (eglglessink, "Got surface of %dx%d pixels", width, height);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_eglglessink_context_make_current (GstEglGlesSink * eglglessink,
    gboolean bind)
{
  g_assert (eglglessink->eglglesctx.display != NULL);

  if (bind && eglglessink->eglglesctx.surface &&
      eglglessink->eglglesctx.eglcontext) {
    EGLContext *ctx = eglGetCurrentContext ();

    if (ctx == eglglessink->eglglesctx.eglcontext) {
      GST_DEBUG_OBJECT (eglglessink,
          "Already attached the context to thread %p", g_thread_self ());
      return TRUE;
    }

    GST_DEBUG_OBJECT (eglglessink, "Attaching context to thread %p",
        g_thread_self ());
    if (!eglMakeCurrent (gst_egl_display_get (eglglessink->eglglesctx.display),
            eglglessink->eglglesctx.surface, eglglessink->eglglesctx.surface,
            eglglessink->eglglesctx.eglcontext)) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (eglglessink, "Couldn't bind context");
      return FALSE;
    }
  } else {
    GST_DEBUG_OBJECT (eglglessink, "Detaching context from thread %p",
        g_thread_self ());
    if (!eglMakeCurrent (gst_egl_display_get (eglglessink->eglglesctx.display),
            EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (eglglessink, "Couldn't unbind context");
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
create_shader_program (GstEglGlesSink * eglglessink, GLuint * prog,
    GLuint * vert, GLuint * frag, const gchar * vert_text,
    const gchar * frag_text)
{
  GLint test;
  GLchar *info_log;

  /* Build shader program for video texture rendering */
  *vert = glCreateShader (GL_VERTEX_SHADER);
  GST_DEBUG_OBJECT (eglglessink, "Sending %s to handle %d", vert_text, *vert);
  glShaderSource (*vert, 1, &vert_text, NULL);
  if (got_gl_error ("glShaderSource vertex"))
    goto HANDLE_ERROR;

  glCompileShader (*vert);
  if (got_gl_error ("glCompileShader vertex"))
    goto HANDLE_ERROR;

  glGetShaderiv (*vert, GL_COMPILE_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (eglglessink, "Successfully compiled vertex shader");
  else {
    GST_ERROR_OBJECT (eglglessink, "Couldn't compile vertex shader");
    glGetShaderiv (*vert, GL_INFO_LOG_LENGTH, &test);
    info_log = g_new0 (GLchar, test);
    glGetShaderInfoLog (*vert, test, NULL, info_log);
    GST_INFO_OBJECT (eglglessink, "Compilation info log:\n%s", info_log);
    g_free (info_log);
    goto HANDLE_ERROR;
  }

  *frag = glCreateShader (GL_FRAGMENT_SHADER);
  GST_DEBUG_OBJECT (eglglessink, "Sending %s to handle %d", frag_text, *frag);
  glShaderSource (*frag, 1, &frag_text, NULL);
  if (got_gl_error ("glShaderSource fragment"))
    goto HANDLE_ERROR;

  glCompileShader (*frag);
  if (got_gl_error ("glCompileShader fragment"))
    goto HANDLE_ERROR;

  glGetShaderiv (*frag, GL_COMPILE_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (eglglessink, "Successfully compiled fragment shader");
  else {
    GST_ERROR_OBJECT (eglglessink, "Couldn't compile fragment shader");
    glGetShaderiv (*frag, GL_INFO_LOG_LENGTH, &test);
    info_log = g_new0 (GLchar, test);
    glGetShaderInfoLog (*frag, test, NULL, info_log);
    GST_INFO_OBJECT (eglglessink, "Compilation info log:\n%s", info_log);
    g_free (info_log);
    goto HANDLE_ERROR;
  }

  *prog = glCreateProgram ();
  if (got_gl_error ("glCreateProgram"))
    goto HANDLE_ERROR;
  glAttachShader (*prog, *vert);
  if (got_gl_error ("glAttachShader vertices"))
    goto HANDLE_ERROR;
  glAttachShader (*prog, *frag);
  if (got_gl_error ("glAttachShader fragments"))
    goto HANDLE_ERROR;
  glLinkProgram (*prog);
  glGetProgramiv (*prog, GL_LINK_STATUS, &test);
  if (test != GL_FALSE) {
    GST_DEBUG_OBJECT (eglglessink, "GLES: Successfully linked program");
  } else {
    GST_ERROR_OBJECT (eglglessink, "Couldn't link program");
    goto HANDLE_ERROR;
  }

  return TRUE;

HANDLE_ERROR:
  {
    if (*frag && *prog)
      glDetachShader (*prog, *frag);
    if (*vert && *prog)
      glDetachShader (*prog, *vert);
    if (*prog)
      glDeleteProgram (*prog);
    if (*frag)
      glDeleteShader (*frag);
    if (*vert)
      glDeleteShader (*vert);
    *prog = 0;
    *frag = 0;
    *vert = 0;

    return FALSE;
  }
}

static gboolean
gst_eglglessink_init_egl_surface (GstEglGlesSink * eglglessink)
{
  GLboolean ret;
  EGLint display_par;
  const gchar *texnames[3] = { NULL, };
  gchar *frag_prog = NULL;
  gboolean free_frag_prog = FALSE;
  EGLint swap_behavior;
  gint i;

  GST_DEBUG_OBJECT (eglglessink, "Enter EGL surface setup");

  eglglessink->eglglesctx.surface =
      eglCreateWindowSurface (gst_egl_display_get (eglglessink->
          eglglesctx.display), eglglessink->eglglesctx.config,
      eglglessink->eglglesctx.used_window, NULL);

  if (eglglessink->eglglesctx.surface == EGL_NO_SURFACE) {
    got_egl_error ("eglCreateWindowSurface");
    GST_ERROR_OBJECT (eglglessink, "Can't create surface");
    goto HANDLE_EGL_ERROR_LOCKED;
  }

  eglglessink->eglglesctx.buffer_preserved = FALSE;
  if (eglQuerySurface (gst_egl_display_get (eglglessink->eglglesctx.display),
          eglglessink->eglglesctx.surface, EGL_SWAP_BEHAVIOR, &swap_behavior)) {
    GST_DEBUG_OBJECT (eglglessink, "Buffer swap behavior %x", swap_behavior);
    eglglessink->eglglesctx.buffer_preserved =
        swap_behavior == EGL_BUFFER_PRESERVED;
  } else {
    GST_DEBUG_OBJECT (eglglessink, "Can't query buffer swap behavior");
  }

  if (!gst_eglglessink_context_make_current (eglglessink, TRUE))
    goto HANDLE_EGL_ERROR_LOCKED;

  /* Save display's pixel aspect ratio
   *
   * DAR is reported as w/h * EGL_DISPLAY_SCALING wich is
   * a constant with value 10000. This attribute is only
   * supported if the EGL version is >= 1.2
   * XXX: Setup this as a property.
   * or some other one time check. Right now it's being called once
   * per frame.
   */
  if (eglglessink->eglglesctx.egl_major == 1 &&
      eglglessink->eglglesctx.egl_minor < 2) {
    GST_DEBUG_OBJECT (eglglessink, "Can't query PAR. Using default: %dx%d",
        EGL_DISPLAY_SCALING, EGL_DISPLAY_SCALING);
    eglglessink->eglglesctx.pixel_aspect_ratio = EGL_DISPLAY_SCALING;
  } else {
    eglQuerySurface (gst_egl_display_get (eglglessink->eglglesctx.display),
        eglglessink->eglglesctx.surface, EGL_PIXEL_ASPECT_RATIO, &display_par);
    /* Fix for outbound DAR reporting on some implementations not
     * honoring the 'should return w/h * EGL_DISPLAY_SCALING' spec
     * requirement
     */
    if (display_par == EGL_UNKNOWN || display_par < EGL_SANE_DAR_MIN ||
        display_par > EGL_SANE_DAR_MAX) {
      GST_DEBUG_OBJECT (eglglessink, "Nonsensical PAR value returned: %d. "
          "Bad EGL implementation? "
          "Will use default: %d/%d", eglglessink->eglglesctx.pixel_aspect_ratio,
          EGL_DISPLAY_SCALING, EGL_DISPLAY_SCALING);
      eglglessink->eglglesctx.pixel_aspect_ratio = EGL_DISPLAY_SCALING;
    } else {
      eglglessink->eglglesctx.pixel_aspect_ratio = display_par;
    }
  }

  /* Save surface dims */
  gst_eglglessink_update_surface_dimensions (eglglessink);

  /* We have a surface! */
  eglglessink->have_surface = TRUE;

  /* Init vertex and fragment GLSL shaders. 
   * Note: Shader compiler support is optional but we currently rely on it.
   */

  glGetBooleanv (GL_SHADER_COMPILER, &ret);
  if (ret == GL_FALSE) {
    GST_ERROR_OBJECT (eglglessink, "Shader compiler support is unavailable!");
    goto HANDLE_ERROR;
  }

  /* Build shader program for video texture rendering */

  switch (eglglessink->configured_info.finfo->format) {
    case GST_VIDEO_FORMAT_AYUV:
      frag_prog = (gchar *) frag_AYUV_prog;
      free_frag_prog = FALSE;
      eglglessink->eglglesctx.n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      frag_prog = (gchar *) frag_PLANAR_YUV_prog;
      free_frag_prog = FALSE;
      eglglessink->eglglesctx.n_textures = 3;
      texnames[0] = "Ytex";
      texnames[1] = "Utex";
      texnames[2] = "Vtex";
      break;
    case GST_VIDEO_FORMAT_NV12:
      frag_prog = g_strdup_printf (frag_NV12_NV21_prog, 'r', 'a');
      free_frag_prog = TRUE;
      eglglessink->eglglesctx.n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_NV21:
      frag_prog = g_strdup_printf (frag_NV12_NV21_prog, 'a', 'r');
      free_frag_prog = TRUE;
      eglglessink->eglglesctx.n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      frag_prog = g_strdup_printf (frag_REORDER_prog, 'b', 'g', 'r');
      free_frag_prog = TRUE;
      eglglessink->eglglesctx.n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
      frag_prog = g_strdup_printf (frag_REORDER_prog, 'g', 'b', 'a');
      free_frag_prog = TRUE;
      eglglessink->eglglesctx.n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      frag_prog = g_strdup_printf (frag_REORDER_prog, 'a', 'b', 'g');
      free_frag_prog = TRUE;
      eglglessink->eglglesctx.n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGB16:
      frag_prog = (gchar *) frag_COPY_prog;
      free_frag_prog = FALSE;
      eglglessink->eglglesctx.n_textures = 1;
      texnames[0] = "tex";
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (!create_shader_program (eglglessink,
          &eglglessink->eglglesctx.glslprogram[0],
          &eglglessink->eglglesctx.vertshader[0],
          &eglglessink->eglglesctx.fragshader[0], vert_COPY_prog, frag_prog)) {
    if (free_frag_prog)
      g_free (frag_prog);
    frag_prog = NULL;
    goto HANDLE_ERROR;
  }
  if (free_frag_prog)
    g_free (frag_prog);
  frag_prog = NULL;

  eglglessink->eglglesctx.position_loc[0] =
      glGetAttribLocation (eglglessink->eglglesctx.glslprogram[0], "position");
  eglglessink->eglglesctx.texpos_loc[0] =
      glGetAttribLocation (eglglessink->eglglesctx.glslprogram[0], "texpos");
  eglglessink->eglglesctx.tex_scale_loc[0][0] =
      glGetUniformLocation (eglglessink->eglglesctx.glslprogram[0],
      "tex_scale0");
  eglglessink->eglglesctx.tex_scale_loc[0][1] =
      glGetUniformLocation (eglglessink->eglglesctx.glslprogram[0],
      "tex_scale1");
  eglglessink->eglglesctx.tex_scale_loc[0][2] =
      glGetUniformLocation (eglglessink->eglglesctx.glslprogram[0],
      "tex_scale2");

  glEnableVertexAttribArray (eglglessink->eglglesctx.position_loc[0]);
  if (got_gl_error ("glEnableVertexAttribArray"))
    goto HANDLE_ERROR;

  glEnableVertexAttribArray (eglglessink->eglglesctx.texpos_loc[0]);
  if (got_gl_error ("glEnableVertexAttribArray"))
    goto HANDLE_ERROR;

  for (i = 0; i < eglglessink->eglglesctx.n_textures; i++) {
    eglglessink->eglglesctx.tex_loc[0][i] =
        glGetUniformLocation (eglglessink->eglglesctx.glslprogram[0],
        texnames[i]);
  }

  if (!eglglessink->eglglesctx.buffer_preserved) {
    /* Build shader program for black borders */
    if (!create_shader_program (eglglessink,
            &eglglessink->eglglesctx.glslprogram[1],
            &eglglessink->eglglesctx.vertshader[1],
            &eglglessink->eglglesctx.fragshader[1], vert_COPY_prog_no_tex,
            frag_BLACK_prog))
      goto HANDLE_ERROR;

    eglglessink->eglglesctx.position_loc[1] =
        glGetAttribLocation (eglglessink->eglglesctx.glslprogram[1],
        "position");

    glEnableVertexAttribArray (eglglessink->eglglesctx.position_loc[1]);
    if (got_gl_error ("glEnableVertexAttribArray"))
      goto HANDLE_ERROR;
  }

  /* Generate textures */
  if (!eglglessink->have_texture) {
    GST_INFO_OBJECT (eglglessink, "Performing initial texture setup");

    glGenTextures (eglglessink->eglglesctx.n_textures,
        eglglessink->eglglesctx.texture);
    if (got_gl_error ("glGenTextures"))
      goto HANDLE_ERROR_LOCKED;

    for (i = 0; i < eglglessink->eglglesctx.n_textures; i++) {
      glBindTexture (GL_TEXTURE_2D, eglglessink->eglglesctx.texture[i]);
      if (got_gl_error ("glBindTexture"))
        goto HANDLE_ERROR;

      /* Set 2D resizing params */
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      /* If these are not set the texture image unit will return
       * (R, G, B, A) = black on glTexImage2D for non-POT width/height
       * frames. For a deeper explanation take a look at the OpenGL ES
       * documentation for glTexParameter */
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (got_gl_error ("glTexParameteri"))
        goto HANDLE_ERROR_LOCKED;
    }

    eglglessink->have_texture = TRUE;
  }

  glUseProgram (0);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR_LOCKED:
  GST_ERROR_OBJECT (eglglessink, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR_LOCKED:
HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Couldn't setup EGL surface");
  return FALSE;
}

static gboolean
gst_eglglessink_init_egl_display (GstEglGlesSink * eglglessink)
{
  EGLDisplay display;
  GST_DEBUG_OBJECT (eglglessink, "Enter EGL initial configuration");

#ifdef USE_EGL_RPI
  /* See https://github.com/raspberrypi/firmware/issues/99 */
  if (!eglMakeCurrent ((EGLDisplay) 1, EGL_NO_SURFACE, EGL_NO_SURFACE,
          EGL_NO_CONTEXT)) {
    got_egl_error ("eglMakeCurrent");
    GST_ERROR_OBJECT (eglglessink, "Couldn't unbind context");
    return FALSE;
  }
#endif

  display = eglGetDisplay (EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY) {
    GST_ERROR_OBJECT (eglglessink, "Could not get EGL display connection");
    goto HANDLE_ERROR;          /* No EGL error is set by eglGetDisplay() */
  }
  eglglessink->eglglesctx.display = gst_egl_display_new (display);

  if (!eglInitialize (gst_egl_display_get (eglglessink->eglglesctx.display),
          &eglglessink->eglglesctx.egl_major,
          &eglglessink->eglglesctx.egl_minor)) {
    got_egl_error ("eglInitialize");
    GST_ERROR_OBJECT (eglglessink, "Could not init EGL display connection");
    goto HANDLE_EGL_ERROR;
  }

  /* Check against required EGL version
   * XXX: Need to review the version requirement in terms of the needed API
   */
  if (eglglessink->eglglesctx.egl_major < GST_EGLGLESSINK_EGL_MIN_VERSION) {
    GST_ERROR_OBJECT (eglglessink, "EGL v%d needed, but you only have v%d.%d",
        GST_EGLGLESSINK_EGL_MIN_VERSION, eglglessink->eglglesctx.egl_major,
        eglglessink->eglglesctx.egl_minor);
    goto HANDLE_ERROR;
  }

  GST_INFO_OBJECT (eglglessink, "System reports supported EGL version v%d.%d",
      eglglessink->eglglesctx.egl_major, eglglessink->eglglesctx.egl_minor);

  eglBindAPI (EGL_OPENGL_ES_API);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR:
  GST_ERROR_OBJECT (eglglessink, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Couldn't setup window/surface from handle");
  return FALSE;
}

static gboolean
gst_eglglessink_choose_config (GstEglGlesSink * eglglessink)
{
  EGLint con_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
  GLint egl_configs;

  if ((eglChooseConfig (gst_egl_display_get (eglglessink->eglglesctx.display),
              eglglessink_RGBA8888_attribs,
              &eglglessink->eglglesctx.config, 1, &egl_configs)) == EGL_FALSE) {
    got_egl_error ("eglChooseConfig");
    GST_ERROR_OBJECT (eglglessink, "eglChooseConfig failed");
    goto HANDLE_EGL_ERROR;
  }

  if (egl_configs < 1) {
    GST_ERROR_OBJECT (eglglessink,
        "Could not find matching framebuffer config");
    goto HANDLE_ERROR;
  }

  eglglessink->eglglesctx.eglcontext =
      eglCreateContext (gst_egl_display_get (eglglessink->eglglesctx.display),
      eglglessink->eglglesctx.config, EGL_NO_CONTEXT, con_attribs);

  if (eglglessink->eglglesctx.eglcontext == EGL_NO_CONTEXT) {
    GST_ERROR_OBJECT (eglglessink, "Error getting context, eglCreateContext");
    goto HANDLE_EGL_ERROR;
  }

  GST_DEBUG_OBJECT (eglglessink, "EGL Context: %p",
      eglglessink->eglglesctx.eglcontext);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR:
  GST_ERROR_OBJECT (eglglessink, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Couldn't choose an usable config");
  return FALSE;
}

static void
gst_eglglessink_set_window_handle (GstVideoOverlay * overlay, guintptr id)
{
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (overlay);

  g_return_if_fail (GST_IS_EGLGLESSINK (eglglessink));
  GST_DEBUG_OBJECT (eglglessink, "We got a window handle: %p", (gpointer) id);

  /* OK, we have a new window */
  GST_OBJECT_LOCK (eglglessink);
  eglglessink->eglglesctx.window = (EGLNativeWindowType) id;
  eglglessink->have_window = ((gpointer) id != NULL);
  GST_OBJECT_UNLOCK (eglglessink);

  return;
}

static void
gst_eglglessink_set_render_rectangle (GstVideoOverlay * overlay, gint x, gint y,
    gint width, gint height)
{
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (overlay);

  g_return_if_fail (GST_IS_EGLGLESSINK (eglglessink));

  GST_OBJECT_LOCK (eglglessink);
  eglglessink->render_region.x = x;
  eglglessink->render_region.y = y;
  eglglessink->render_region.w = width;
  eglglessink->render_region.h = height;
  eglglessink->render_region_changed = TRUE;
  eglglessink->render_region_user = (width != -1 && height != -1);
  GST_OBJECT_UNLOCK (eglglessink);

  return;
}

static void
queue_item_destroy (GstDataQueueItem * item)
{
  if (item->object && !GST_IS_QUERY (item->object))
    gst_mini_object_unref (item->object);
  g_slice_free (GstDataQueueItem, item);
}

static GstFlowReturn
gst_eglglessink_queue_object (GstEglGlesSink * eglglessink, GstMiniObject * obj)
{
  GstDataQueueItem *item;
  GstFlowReturn last_flow;

  g_mutex_lock (&eglglessink->render_lock);
  last_flow = eglglessink->last_flow;
  g_mutex_unlock (&eglglessink->render_lock);

  if (last_flow != GST_FLOW_OK)
    return last_flow;

  item = g_slice_new0 (GstDataQueueItem);

  if (obj == NULL)
    item->object = NULL;
  else if (GST_IS_QUERY (obj))
    item->object = obj;
  else
    item->object = gst_mini_object_ref (obj);
  item->size = 0;
  item->duration = GST_CLOCK_TIME_NONE;
  item->visible = TRUE;
  item->destroy = (GDestroyNotify) queue_item_destroy;

  GST_DEBUG_OBJECT (eglglessink, "Queueing object %" GST_PTR_FORMAT, obj);

  g_mutex_lock (&eglglessink->render_lock);
  if (!gst_data_queue_push (eglglessink->queue, item)) {
    item->destroy (item);
    g_mutex_unlock (&eglglessink->render_lock);
    GST_DEBUG_OBJECT (eglglessink, "Flushing");
    return GST_FLOW_FLUSHING;
  }

  GST_DEBUG_OBJECT (eglglessink, "Waiting for object to be handled");
  do {
    g_cond_wait (&eglglessink->render_cond, &eglglessink->render_lock);
  } while (eglglessink->dequeued_object != obj
      && eglglessink->last_flow != GST_FLOW_FLUSHING);
  GST_DEBUG_OBJECT (eglglessink, "Object handled: %s",
      gst_flow_get_name (eglglessink->last_flow));
  last_flow = eglglessink->last_flow;
  g_mutex_unlock (&eglglessink->render_lock);

  return (obj ? last_flow : GST_FLOW_OK);
}

static gboolean
gst_eglglessink_crop_changed (GstEglGlesSink * eglglessink,
    GstVideoCropMeta * crop)
{
  if (crop) {
    return (crop->x != eglglessink->crop.x ||
        crop->y != eglglessink->crop.y ||
        crop->width != eglglessink->crop.w ||
        crop->height != eglglessink->crop.h);
  }

  return (eglglessink->crop.x != 0 || eglglessink->crop.y != 0 ||
      eglglessink->crop.w != eglglessink->configured_info.width ||
      eglglessink->crop.h != eglglessink->configured_info.height);
}

static gboolean
gst_eglglessink_fill_texture (GstEglGlesSink * eglglessink, GstBuffer * buf)
{
  GstVideoFrame vframe;
  gint w, h;

  memset (&vframe, 0, sizeof (vframe));

  if (!gst_video_frame_map (&vframe, &eglglessink->configured_info, buf,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't map frame");
    goto HANDLE_ERROR;
  }

  w = GST_VIDEO_FRAME_WIDTH (&vframe);
  h = GST_VIDEO_FRAME_HEIGHT (&vframe);

  GST_DEBUG_OBJECT (eglglessink,
      "Got buffer %p: %dx%d size %d", buf, w, h, gst_buffer_get_size (buf));

  switch (eglglessink->configured_info.finfo->format) {
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w * 3 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width * 3 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->eglglesctx.texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, stride_width, h, 0, GL_RGB,
          GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_RGB16:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (c_w * 2 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (stride_width * 2 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->eglglesctx.texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, stride_width, h, 0, GL_RGB,
          GL_UNSIGNED_SHORT_5_6_5, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 4) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (c_w * 4 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (stride_width * 4 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->eglglesctx.texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, stride_width, h, 0,
          GL_RGBA, GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_AYUV:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 4) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (c_w * 4 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (stride_width * 4 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->eglglesctx.texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, stride_width, h, 0,
          GL_RGBA, GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 0);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->eglglesctx.texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 0),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 0));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 1);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 1);

      glActiveTexture (GL_TEXTURE1);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[1] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->eglglesctx.texture[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 1),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 1));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 2);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 2);

      glActiveTexture (GL_TEXTURE2);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[2] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->eglglesctx.texture[2]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 2),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 2));
      break;
    }
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 0);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->eglglesctx.texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 0),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 1);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 1);

      glActiveTexture (GL_TEXTURE1);

      if (GST_ROUND_UP_8 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (c_w * 2 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else {
        stride_width = stride / 2;

        if (GST_ROUND_UP_8 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (stride_width * 2 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[1] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->eglglesctx.texture[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 1),
          0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_PLANE_DATA (&vframe, 1));
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  if (got_gl_error ("glTexImage2D"))
    goto HANDLE_ERROR;

  gst_video_frame_unmap (&vframe);

  return TRUE;

HANDLE_ERROR:
  {
    if (vframe.buffer)
      gst_video_frame_unmap (&vframe);
    return FALSE;
  }
}

/* Rendering and display */
static GstFlowReturn
gst_eglglessink_upload (GstEglGlesSink * eglglessink, GstBuffer * buf)
{
  GstVideoCropMeta *crop = NULL;

  if (!buf) {
    GST_DEBUG_OBJECT (eglglessink, "Rendering previous buffer again");
  } else if (buf) {
    GstMemory *mem;

    crop = gst_buffer_get_video_crop_meta (buf);

    if (gst_eglglessink_crop_changed (eglglessink, crop)) {
      if (crop) {
        eglglessink->crop.x = crop->x;
        eglglessink->crop.y = crop->y;
        eglglessink->crop.w = crop->width;
        eglglessink->crop.h = crop->height;
      } else {
        eglglessink->crop.x = 0;
        eglglessink->crop.y = 0;
        eglglessink->crop.w = eglglessink->configured_info.width;
        eglglessink->crop.h = eglglessink->configured_info.height;
      }
      eglglessink->crop_changed = TRUE;
    }

    if (gst_buffer_n_memory (buf) >= 1 &&
        (mem = gst_buffer_peek_memory (buf, 0))
        && gst_is_egl_image_memory (mem)) {
      guint n, i;

      n = gst_buffer_n_memory (buf);

      for (i = 0; i < n; i++) {
        mem = gst_buffer_peek_memory (buf, i);

        g_assert (gst_is_egl_image_memory (mem));

        if (i == 0)
          glActiveTexture (GL_TEXTURE0);
        else if (i == 1)
          glActiveTexture (GL_TEXTURE1);
        else if (i == 2)
          glActiveTexture (GL_TEXTURE2);

        glBindTexture (GL_TEXTURE_2D, eglglessink->eglglesctx.texture[i]);
        glEGLImageTargetTexture2DOES (GL_TEXTURE_2D,
            gst_egl_image_memory_get_image (mem));
        if (got_gl_error ("glEGLImageTargetTexture2DOES"))
          goto HANDLE_ERROR;
      }
      gst_buffer_replace (&eglglessink->last_buffer, buf);
      eglglessink->stride[0] = 1;
      eglglessink->stride[1] = 1;
      eglglessink->stride[2] = 1;
    } else {
      if (!gst_eglglessink_fill_texture (eglglessink, buf))
        goto HANDLE_ERROR;
    }
  }

  return GST_FLOW_OK;

HANDLE_ERROR:
  {
    GST_ERROR_OBJECT (eglglessink, "Failed to upload texture");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_eglglessink_render (GstEglGlesSink * eglglessink)
{
  guint dar_n, dar_d;
  gint i;

  /* If no one has set a display rectangle on us initialize
   * a sane default. According to the docs on the xOverlay
   * interface we are supposed to fill the overlay 100%. We
   * do this trying to take PAR/DAR into account unless the
   * calling party explicitly ask us not to by setting
   * force_aspect_ratio to FALSE.
   */
  if (gst_eglglessink_update_surface_dimensions (eglglessink) ||
      eglglessink->render_region_changed ||
      !eglglessink->display_region.w || !eglglessink->display_region.h ||
      eglglessink->crop_changed) {
    GST_OBJECT_LOCK (eglglessink);

    if (!eglglessink->render_region_user) {
      eglglessink->render_region.x = 0;
      eglglessink->render_region.y = 0;
      eglglessink->render_region.w = eglglessink->eglglesctx.surface_width;
      eglglessink->render_region.h = eglglessink->eglglesctx.surface_height;
    }
    eglglessink->render_region_changed = FALSE;
    eglglessink->crop_changed = FALSE;

    if (!eglglessink->force_aspect_ratio) {
      eglglessink->display_region.x = 0;
      eglglessink->display_region.y = 0;
      eglglessink->display_region.w = eglglessink->render_region.w;
      eglglessink->display_region.h = eglglessink->render_region.h;
    } else {
      GstVideoRectangle frame;

      frame.x = 0;
      frame.y = 0;

      if (!gst_video_calculate_display_ratio (&dar_n, &dar_d,
              eglglessink->crop.w, eglglessink->crop.h,
              eglglessink->configured_info.par_n,
              eglglessink->configured_info.par_d,
              eglglessink->eglglesctx.pixel_aspect_ratio,
              EGL_DISPLAY_SCALING)) {
        GST_WARNING_OBJECT (eglglessink, "Could not compute resulting DAR");
        frame.w = eglglessink->crop.w;
        frame.h = eglglessink->crop.h;
      } else {
        /* Find suitable matching new size acording to dar & par
         * rationale for prefering leaving the height untouched
         * comes from interlacing considerations.
         * XXX: Move this to gstutils?
         */
        if (eglglessink->crop.h % dar_d == 0) {
          frame.w =
              gst_util_uint64_scale_int (eglglessink->crop.h, dar_n, dar_d);
          frame.h = eglglessink->crop.h;
        } else if (eglglessink->crop.w % dar_n == 0) {
          frame.h =
              gst_util_uint64_scale_int (eglglessink->crop.w, dar_d, dar_n);
          frame.w = eglglessink->crop.w;
        } else {
          /* Neither width nor height can be precisely scaled.
           * Prefer to leave height untouched. See comment above.
           */
          frame.w =
              gst_util_uint64_scale_int (eglglessink->crop.h, dar_n, dar_d);
          frame.h = eglglessink->crop.h;
        }
      }

      gst_video_sink_center_rect (frame, eglglessink->render_region,
          &eglglessink->display_region, TRUE);
    }

    glViewport (eglglessink->render_region.x,
        eglglessink->eglglesctx.surface_height -
        eglglessink->render_region.y -
        eglglessink->render_region.h,
        eglglessink->render_region.w, eglglessink->render_region.h);

    /* Clear the surface once if its content is preserved */
    if (eglglessink->eglglesctx.buffer_preserved) {
      glClearColor (0.0, 0.0, 0.0, 1.0);
      glClear (GL_COLOR_BUFFER_BIT);
    }

    if (!gst_eglglessink_setup_vbo (eglglessink, FALSE)) {
      GST_OBJECT_UNLOCK (eglglessink);
      GST_ERROR_OBJECT (eglglessink, "VBO setup failed");
      goto HANDLE_ERROR;
    }
    GST_OBJECT_UNLOCK (eglglessink);
  }

  if (!eglglessink->eglglesctx.buffer_preserved) {
    /* Draw black borders */
    GST_DEBUG_OBJECT (eglglessink, "Drawing black border 1");
    glUseProgram (eglglessink->eglglesctx.glslprogram[1]);

    glVertexAttribPointer (eglglessink->eglglesctx.position_loc[1], 3,
        GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (4 * sizeof (coord5)));
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR;

    glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
    if (got_gl_error ("glDrawElements"))
      goto HANDLE_ERROR;

    GST_DEBUG_OBJECT (eglglessink, "Drawing black border 2");

    glVertexAttribPointer (eglglessink->eglglesctx.position_loc[1], 3,
        GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (8 * sizeof (coord5)));
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR;

    glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
    if (got_gl_error ("glDrawElements"))
      goto HANDLE_ERROR;
  }

  /* Draw video frame */
  GST_DEBUG_OBJECT (eglglessink, "Drawing video frame");
  glUseProgram (eglglessink->eglglesctx.glslprogram[0]);

  glUniform2f (eglglessink->eglglesctx.tex_scale_loc[0][0],
      eglglessink->stride[0], 1);
  glUniform2f (eglglessink->eglglesctx.tex_scale_loc[0][1],
      eglglessink->stride[1], 1);
  glUniform2f (eglglessink->eglglesctx.tex_scale_loc[0][2],
      eglglessink->stride[2], 1);

  for (i = 0; i < eglglessink->eglglesctx.n_textures; i++) {
    glUniform1i (eglglessink->eglglesctx.tex_loc[0][i], i);
    if (got_gl_error ("glUniform1i"))
      goto HANDLE_ERROR;
  }

  glVertexAttribPointer (eglglessink->eglglesctx.position_loc[0], 3,
      GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (0 * sizeof (coord5)));
  if (got_gl_error ("glVertexAttribPointer"))
    goto HANDLE_ERROR;

  glVertexAttribPointer (eglglessink->eglglesctx.texpos_loc[0], 2,
      GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (3 * sizeof (gfloat)));
  if (got_gl_error ("glVertexAttribPointer"))
    goto HANDLE_ERROR;

  glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
  if (got_gl_error ("glDrawElements"))
    goto HANDLE_ERROR;

  if ((eglSwapBuffers (gst_egl_display_get (eglglessink->eglglesctx.display),
              eglglessink->eglglesctx.surface))
      == EGL_FALSE) {
    got_egl_error ("eglSwapBuffers");
    goto HANDLE_ERROR;
  }


  GST_DEBUG_OBJECT (eglglessink, "Succesfully rendered 1 frame");
  return GST_FLOW_OK;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Rendering disabled for this frame");

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_eglglessink_prepare (GstBaseSink * bsink, GstBuffer * buf)
{
  GstEglGlesSink *eglglessink;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  eglglessink = GST_EGLGLESSINK (bsink);
  GST_DEBUG_OBJECT (eglglessink, "Got buffer: %p", buf);

  return gst_eglglessink_queue_object (eglglessink, GST_MINI_OBJECT_CAST (buf));
}

static GstFlowReturn
gst_eglglessink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstEglGlesSink *eglglessink;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  eglglessink = GST_EGLGLESSINK (vsink);
  GST_DEBUG_OBJECT (eglglessink, "Got buffer: %p", buf);

  return gst_eglglessink_queue_object (eglglessink, NULL);
}

static GstCaps *
gst_eglglessink_getcaps (GstBaseSink * bsink, GstCaps * filter)
{
  GstEglGlesSink *eglglessink;
  GstCaps *ret = NULL;

  eglglessink = GST_EGLGLESSINK (bsink);

  GST_OBJECT_LOCK (eglglessink);
  if (eglglessink->sinkcaps) {
    ret = gst_caps_ref (eglglessink->sinkcaps);
  } else {
    ret =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
            (bsink)));
  }
  GST_OBJECT_UNLOCK (eglglessink);

  if (filter) {
    GstCaps *tmp =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (ret);
    ret = tmp;
  }

  return ret;
}

static gboolean
gst_eglglessink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstEglGlesSink *eglglessink;
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  gboolean need_pool;
  guint size;
  GstAllocator *allocator;
  GstAllocationParams params;

  eglglessink = GST_EGLGLESSINK (bsink);

  gst_allocation_params_init (&params);

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps) {
    GST_ERROR_OBJECT (eglglessink, "allocation query without caps");
    return FALSE;
  }

  GST_OBJECT_LOCK (eglglessink);
  pool = eglglessink->pool ? gst_object_ref (eglglessink->pool) : NULL;
  GST_OBJECT_UNLOCK (eglglessink);

  if (pool) {
    GstCaps *pcaps;

    /* we had a pool, check caps */
    GST_DEBUG_OBJECT (eglglessink, "check existing pool caps");
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    if (!gst_caps_is_equal (caps, pcaps)) {
      GST_DEBUG_OBJECT (eglglessink, "pool has different caps");
      /* different caps, we can't use this pool */
      gst_object_unref (pool);
      pool = NULL;
    }
    gst_structure_free (config);
  }

  if (pool == NULL && need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps)) {
      GST_ERROR_OBJECT (eglglessink, "allocation query has invalid caps %"
          GST_PTR_FORMAT, caps);
      return FALSE;
    }

    GST_DEBUG_OBJECT (eglglessink, "create new pool");
    pool =
        gst_egl_image_buffer_pool_new (eglglessink,
        eglglessink->eglglesctx.display);

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    /* we need at least 2 buffer because we hold on to the last one */
    gst_buffer_pool_config_set_params (config, caps, size, 2, 0);
    gst_buffer_pool_config_set_allocator (config, NULL, &params);
    if (!gst_buffer_pool_set_config (pool, config)) {
      gst_object_unref (pool);
      GST_ERROR_OBJECT (eglglessink, "failed to set pool configuration");
      return FALSE;
    }
  }

  if (pool) {
    /* we need at least 2 buffer because we hold on to the last one */
    gst_query_add_allocation_pool (query, pool, size, 2, 0);
    gst_object_unref (pool);
  }

  /* First the default allocator */
  if (!gst_egl_image_memory_is_mappable ()) {
    allocator = gst_allocator_find (NULL);
    gst_query_add_allocation_param (query, allocator, &params);
    gst_object_unref (allocator);
  }

  allocator = gst_egl_image_allocator_obtain ();
  if (!gst_egl_image_memory_is_mappable ())
    params.flags |= GST_MEMORY_FLAG_NOT_MAPPABLE;
  gst_query_add_allocation_param (query, allocator, &params);
  gst_object_unref (allocator);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_eglglessink_configure_caps (GstEglGlesSink * eglglessink, GstCaps * caps)
{
  gboolean ret = TRUE;
  GstVideoInfo info;

  gst_video_info_init (&info);
  if (!(ret = gst_video_info_from_caps (&info, caps))) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't parse caps");
    goto HANDLE_ERROR;
  }

  eglglessink->configured_info = info;
  GST_VIDEO_SINK_WIDTH (eglglessink) = info.width;
  GST_VIDEO_SINK_HEIGHT (eglglessink) = info.height;

  if (eglglessink->configured_caps) {
    GST_DEBUG_OBJECT (eglglessink, "Caps were already set");
    if (gst_caps_can_intersect (caps, eglglessink->configured_caps)) {
      GST_DEBUG_OBJECT (eglglessink, "Caps are compatible anyway");
      goto SUCCEED;
    }

    GST_DEBUG_OBJECT (eglglessink, "Caps are not compatible, reconfiguring");

    /* EGL/GLES cleanup */
    gst_eglglessink_wipe_eglglesctx (eglglessink);

    gst_caps_unref (eglglessink->configured_caps);
    eglglessink->configured_caps = NULL;
  }

  if (!gst_eglglessink_choose_config (eglglessink)) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't choose EGL config");
    goto HANDLE_ERROR;
  }

  gst_caps_replace (&eglglessink->configured_caps, caps);

  /* By now the application should have set a window
   * if it meant to do so
   */
  GST_OBJECT_LOCK (eglglessink);
  if (!eglglessink->have_window) {
    EGLNativeWindowType window;

    GST_INFO_OBJECT (eglglessink,
        "No window. Will attempt internal window creation");
    if (!(window =
            gst_eglglessink_create_window (eglglessink, info.width,
                info.height))) {
      GST_ERROR_OBJECT (eglglessink, "Internal window creation failed!");
      GST_OBJECT_UNLOCK (eglglessink);
      goto HANDLE_ERROR;
    }
    eglglessink->using_own_window = TRUE;
    eglglessink->eglglesctx.window = window;
    eglglessink->have_window = TRUE;
  }
  GST_DEBUG_OBJECT (eglglessink, "Using window handle %p",
      eglglessink->eglglesctx.window);
  eglglessink->eglglesctx.used_window = eglglessink->eglglesctx.window;
  GST_OBJECT_UNLOCK (eglglessink);
  gst_video_overlay_got_window_handle (GST_VIDEO_OVERLAY (eglglessink),
      (guintptr) eglglessink->eglglesctx.used_window);

  if (!eglglessink->have_surface) {
    if (!gst_eglglessink_init_egl_surface (eglglessink)) {
      GST_ERROR_OBJECT (eglglessink, "Couldn't init EGL surface from window");
      goto HANDLE_ERROR;
    }
  }

SUCCEED:
  GST_INFO_OBJECT (eglglessink, "Configured caps successfully");
  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Configuring caps failed");
  return FALSE;
}

static gboolean
gst_eglglessink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstEglGlesSink *eglglessink;
  GstVideoInfo info;
  GstBufferPool *newpool, *oldpool;
  GstStructure *config;
  GstAllocationParams params = { 0, };

  eglglessink = GST_EGLGLESSINK (bsink);

  GST_DEBUG_OBJECT (eglglessink,
      "Current caps %" GST_PTR_FORMAT ", setting caps %"
      GST_PTR_FORMAT, eglglessink->current_caps, caps);

  if (gst_eglglessink_queue_object (eglglessink,
          GST_MINI_OBJECT_CAST (caps)) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (eglglessink, "Failed to configure caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (eglglessink, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  newpool =
      gst_egl_image_buffer_pool_new (eglglessink,
      eglglessink->eglglesctx.display);
  config = gst_buffer_pool_get_config (newpool);
  /* we need at least 2 buffer because we hold on to the last one */
  gst_buffer_pool_config_set_params (config, caps, info.size, 2, 0);
  gst_buffer_pool_config_set_allocator (config, NULL, &params);
  if (!gst_buffer_pool_set_config (newpool, config)) {
    gst_object_unref (newpool);
    GST_ERROR_OBJECT (eglglessink, "Failed to set buffer pool configuration");
    return FALSE;
  }

  GST_OBJECT_LOCK (eglglessink);
  oldpool = eglglessink->pool;
  eglglessink->pool = newpool;
  GST_OBJECT_UNLOCK (eglglessink);

  if (oldpool)
    gst_object_unref (oldpool);

  gst_caps_replace (&eglglessink->current_caps, caps);

  return TRUE;
}

static gboolean
gst_eglglessink_open (GstEglGlesSink * eglglessink)
{
  if (!egl_init (eglglessink)) {
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_eglglessink_close (GstEglGlesSink * eglglessink)
{
  if (eglglessink->eglglesctx.display) {
    gst_egl_display_unref (eglglessink->eglglesctx.display);
    eglglessink->eglglesctx.display = NULL;
  }

  gst_caps_unref (eglglessink->sinkcaps);
  eglglessink->sinkcaps = NULL;
  eglglessink->egl_started = FALSE;

  GST_OBJECT_LOCK (eglglessink);
  if (eglglessink->pool)
    gst_object_unref (eglglessink->pool);
  eglglessink->pool = NULL;
  GST_OBJECT_UNLOCK (eglglessink);

  return TRUE;
}

static GstStateChangeReturn
gst_eglglessink_change_state (GstElement * element, GstStateChange transition)
{
  GstEglGlesSink *eglglessink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  eglglessink = GST_EGLGLESSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_eglglessink_open (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_eglglessink_start (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_eglglessink_close (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_eglglessink_stop (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    default:
      break;
  }

done:
  return ret;
}

static void
gst_eglglessink_finalize (GObject * object)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  if (eglglessink->queue)
    g_object_unref (eglglessink->queue);
  eglglessink->queue = NULL;

  g_cond_clear (&eglglessink->render_cond);
  g_mutex_clear (&eglglessink->render_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_eglglessink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  switch (prop_id) {
    case PROP_CREATE_WINDOW:
      eglglessink->create_window = g_value_get_boolean (value);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      eglglessink->force_aspect_ratio = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_eglglessink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  switch (prop_id) {
    case PROP_CREATE_WINDOW:
      g_value_set_boolean (value, eglglessink->create_window);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, eglglessink->force_aspect_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* initialize the eglglessink's class */
static void
gst_eglglessink_class_init (GstEglGlesSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;

  gobject_class->set_property = gst_eglglessink_set_property;
  gobject_class->get_property = gst_eglglessink_get_property;
  gobject_class->finalize = gst_eglglessink_finalize;

  gstelement_class->change_state = gst_eglglessink_change_state;

  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_eglglessink_setcaps);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_eglglessink_getcaps);
  gstbasesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_eglglessink_propose_allocation);
  gstbasesink_class->prepare = GST_DEBUG_FUNCPTR (gst_eglglessink_prepare);

  gstvideosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_eglglessink_show_frame);

  g_object_class_install_property (gobject_class, PROP_CREATE_WINDOW,
      g_param_spec_boolean ("create-window", "Create Window",
          "If set to true, the sink will attempt to create it's own window to "
          "render to if none is provided. This is currently only supported "
          "when the sink is used under X11",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Respect aspect ratio when scaling",
          "If set to true, the sink will attempt to preserve the incoming "
          "frame's geometry while scaling, taking both the storage's and "
          "display's pixel aspect ratio into account",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "EGL/GLES vout Sink",
      "Sink/Video",
      "An EGL/GLES Video Output Sink Implementing the VideoOverlay interface",
      "Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_eglglessink_sink_template_factory));
}

static gboolean
queue_check_full_func (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  return visible != 0;
}

static void
gst_eglglessink_init (GstEglGlesSink * eglglessink)
{
  /* Init defaults */

  /** Flags */
  eglglessink->have_window = FALSE;
  eglglessink->have_surface = FALSE;
  eglglessink->have_vbo = FALSE;
  eglglessink->have_texture = FALSE;
  eglglessink->egl_started = FALSE;
  eglglessink->using_own_window = FALSE;

  /** Props */
  eglglessink->create_window = TRUE;
  eglglessink->force_aspect_ratio = TRUE;

  g_mutex_init (&eglglessink->render_lock);
  g_cond_init (&eglglessink->render_cond);
  eglglessink->queue =
      gst_data_queue_new (queue_check_full_func, NULL, NULL, NULL);
  eglglessink->last_flow = GST_FLOW_FLUSHING;

  eglglessink->render_region.x = 0;
  eglglessink->render_region.y = 0;
  eglglessink->render_region.w = -1;
  eglglessink->render_region.h = -1;
  eglglessink->render_region_changed = TRUE;
  eglglessink->render_region_user = FALSE;
}

/* EGLImage memory, buffer pool, etc */
typedef struct
{
  GstVideoBufferPool parent;

  GstEglGlesSink *sink;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstVideoInfo info;
  gboolean add_metavideo;
  gboolean want_eglimage;
  GstEGLDisplay *display;
} GstEGLImageBufferPool;

typedef GstVideoBufferPoolClass GstEGLImageBufferPoolClass;

#define GST_EGL_IMAGE_BUFFER_POOL(p) ((GstEGLImageBufferPool*)(p))

GType gst_egl_image_buffer_pool_get_type (void);

typedef struct
{
  GLuint texture;
} GstEGLGLESImageData;

static void
gst_egl_gles_image_data_free (GstEGLGLESImageData * data)
{
  glDeleteTextures (1, &data->texture);
  g_slice_free (GstEGLGLESImageData, data);
}

static GstBuffer *
gst_eglglessink_allocate_eglimage (GstEglGlesSink * eglglessink,
    GstVideoFormat format, gint width, gint height)
{
  GstEGLGLESImageData *data = NULL;
  GstBuffer *buffer;
  GstVideoInfo info;
  gint i;
  gint stride[3];
  gsize offset[3];
  GstMemory *mem[3] = { NULL, NULL, NULL };
  guint n_mem;
  GstMemoryFlags flags = 0;

  memset (stride, 0, sizeof (stride));
  memset (offset, 0, sizeof (offset));

  if (!gst_egl_image_memory_is_mappable ())
    flags |= GST_MEMORY_FLAG_NOT_MAPPABLE;
  /* See https://bugzilla.gnome.org/show_bug.cgi?id=695203 */
  flags |= GST_MEMORY_FLAG_NO_SHARE;

  gst_video_info_set_format (&info, format, width, height);

  switch (format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:{
      gsize size;
      EGLImageKHR image;

      mem[0] =
          gst_egl_image_allocator_alloc (GST_EGL_IMAGE_BUFFER_POOL
          (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
          GST_EGL_IMAGE_MEMORY_TYPE_RGB, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        data = g_slice_new0 (GstEGLGLESImageData);

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 3);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        image =
            eglCreateImageKHR (gst_egl_display_get (eglglessink->
                eglglesctx.display), eglglessink->eglglesctx.eglcontext,
            EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer) (guintptr) data->texture,
            NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (GST_EGL_IMAGE_BUFFER_POOL
            (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
            image, GST_EGL_IMAGE_MEMORY_TYPE_RGB,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);
        n_mem = 1;
      }
      break;
    }
    case GST_VIDEO_FORMAT_RGB16:{
      EGLImageKHR image;
      gsize size;

      mem[0] =
          gst_egl_image_allocator_alloc (GST_EGL_IMAGE_BUFFER_POOL
          (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
          GST_EGL_IMAGE_MEMORY_TYPE_RGB, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        data = g_slice_new0 (GstEGLGLESImageData);

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 2);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
            NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        image =
            eglCreateImageKHR (gst_egl_display_get (eglglessink->
                eglglesctx.display), eglglessink->eglglesctx.eglcontext,
            EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer) (guintptr) data->texture,
            NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (GST_EGL_IMAGE_BUFFER_POOL
            (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
            image, GST_EGL_IMAGE_MEMORY_TYPE_RGB,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);
        n_mem = 1;
      }
      break;
    }
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:{
      EGLImageKHR image;
      gsize size[2];

      mem[0] =
          gst_egl_image_allocator_alloc (GST_EGL_IMAGE_BUFFER_POOL
          (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
          GST_EGL_IMAGE_MEMORY_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              0), GST_VIDEO_INFO_COMP_HEIGHT (&info, 0), &size[0]);
      mem[1] =
          gst_egl_image_allocator_alloc (GST_EGL_IMAGE_BUFFER_POOL
          (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
          GST_EGL_IMAGE_MEMORY_TYPE_LUMINANCE_ALPHA,
          GST_VIDEO_INFO_COMP_WIDTH (&info, 1),
          GST_VIDEO_INFO_COMP_HEIGHT (&info, 1), &size[1]);

      if (mem[0] && mem[1]) {
        stride[0] = size[0] / GST_VIDEO_INFO_HEIGHT (&info);
        offset[1] = size[0];
        stride[1] = size[1] / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 2;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[1], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        if (mem[0])
          gst_memory_unref (mem[0]);
        if (mem[1])
          gst_memory_unref (mem[1]);
        mem[0] = mem[1] = NULL;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 0));
        stride[1] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 1) * 2);
        offset[1] = stride[0] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 0);
        size[0] = offset[1];
        size[1] = stride[1] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 1);

        for (i = 0; i < 2; i++) {
          data = g_slice_new0 (GstEGLGLESImageData);

          glGenTextures (1, &data->texture);
          if (got_gl_error ("glGenTextures"))
            goto mem_error;

          glBindTexture (GL_TEXTURE_2D, data->texture);
          if (got_gl_error ("glBindTexture"))
            goto mem_error;

          /* Set 2D resizing params */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

          /* If these are not set the texture image unit will return
           * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
           * * frames. For a deeper explanation take a look at the OpenGL ES
           * * documentation for glTexParameter */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          if (got_gl_error ("glTexParameteri"))
            goto mem_error;

          if (i == 0)
            glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
                GST_VIDEO_INFO_COMP_WIDTH (&info, i),
                GST_VIDEO_INFO_COMP_HEIGHT (&info, i), 0, GL_LUMINANCE,
                GL_UNSIGNED_BYTE, NULL);
          else
            glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
                GST_VIDEO_INFO_COMP_WIDTH (&info, i),
                GST_VIDEO_INFO_COMP_HEIGHT (&info, i), 0, GL_LUMINANCE_ALPHA,
                GL_UNSIGNED_BYTE, NULL);

          if (got_gl_error ("glTexImage2D"))
            goto mem_error;

          image =
              eglCreateImageKHR (eglglessink->eglglesctx.display,
              eglglessink->eglglesctx.eglcontext, EGL_GL_TEXTURE_2D_KHR,
              (EGLClientBuffer) (guintptr) data->texture, NULL);
          if (got_egl_error ("eglCreateImageKHR"))
            goto mem_error;

          mem[i] =
              gst_egl_image_allocator_wrap (GST_EGL_IMAGE_BUFFER_POOL
              (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
              image,
              (i ==
                  0 ? GST_EGL_IMAGE_MEMORY_TYPE_LUMINANCE :
                  GST_EGL_IMAGE_MEMORY_TYPE_LUMINANCE_ALPHA),
              flags, size[i], data,
              (GDestroyNotify) gst_egl_gles_image_data_free);
        }

        n_mem = 2;
      }
      break;
    }
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{
      EGLImageKHR image;
      gsize size[3];

      mem[0] =
          gst_egl_image_allocator_alloc (GST_EGL_IMAGE_BUFFER_POOL
          (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
          GST_EGL_IMAGE_MEMORY_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              0), GST_VIDEO_INFO_COMP_HEIGHT (&info, 0), &size[0]);
      mem[1] =
          gst_egl_image_allocator_alloc (GST_EGL_IMAGE_BUFFER_POOL
          (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
          GST_EGL_IMAGE_MEMORY_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              1), GST_VIDEO_INFO_COMP_HEIGHT (&info, 1), &size[1]);
      mem[2] =
          gst_egl_image_allocator_alloc (GST_EGL_IMAGE_BUFFER_POOL
          (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
          GST_EGL_IMAGE_MEMORY_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              2), GST_VIDEO_INFO_COMP_HEIGHT (&info, 2), &size[2]);

      if (mem[0] && mem[1] && mem[2]) {
        stride[0] = size[0] / GST_VIDEO_INFO_HEIGHT (&info);
        offset[1] = size[0];
        stride[1] = size[1] / GST_VIDEO_INFO_HEIGHT (&info);
        offset[2] = size[1];
        stride[2] = size[2] / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 3;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[1], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[2], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        if (mem[0])
          gst_memory_unref (mem[0]);
        if (mem[1])
          gst_memory_unref (mem[1]);
        if (mem[2])
          gst_memory_unref (mem[2]);
        mem[0] = mem[1] = mem[2] = NULL;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 0));
        stride[1] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 1));
        stride[2] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 2));
        size[0] = stride[0] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 0);
        size[1] = stride[1] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 1);
        size[2] = stride[2] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 2);
        offset[0] = 0;
        offset[1] = size[0];
        offset[2] = offset[1] + size[1];

        for (i = 0; i < 3; i++) {
          data = g_slice_new0 (GstEGLGLESImageData);

          glGenTextures (1, &data->texture);
          if (got_gl_error ("glGenTextures"))
            goto mem_error;

          glBindTexture (GL_TEXTURE_2D, data->texture);
          if (got_gl_error ("glBindTexture"))
            goto mem_error;

          /* Set 2D resizing params */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

          /* If these are not set the texture image unit will return
           * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
           * * frames. For a deeper explanation take a look at the OpenGL ES
           * * documentation for glTexParameter */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          if (got_gl_error ("glTexParameteri"))
            goto mem_error;

          glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
              GST_VIDEO_INFO_COMP_WIDTH (&info, i),
              GST_VIDEO_INFO_COMP_HEIGHT (&info, i), 0, GL_LUMINANCE,
              GL_UNSIGNED_BYTE, NULL);

          if (got_gl_error ("glTexImage2D"))
            goto mem_error;

          image =
              eglCreateImageKHR (eglglessink->eglglesctx.display,
              eglglessink->eglglesctx.eglcontext, EGL_GL_TEXTURE_2D_KHR,
              (EGLClientBuffer) (guintptr) data->texture, NULL);
          if (got_egl_error ("eglCreateImageKHR"))
            goto mem_error;

          mem[i] =
              gst_egl_image_allocator_wrap (GST_EGL_IMAGE_BUFFER_POOL
              (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
              image, GST_EGL_IMAGE_MEMORY_TYPE_LUMINANCE,
              flags, size[i], data,
              (GDestroyNotify) gst_egl_gles_image_data_free);
        }

        n_mem = 3;
      }
      break;
    }
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_AYUV:{
      gsize size;
      EGLImageKHR image;

      mem[0] =
          gst_egl_image_allocator_alloc (GST_EGL_IMAGE_BUFFER_POOL
          (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
          GST_EGL_IMAGE_MEMORY_TYPE_RGBA, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        data = g_slice_new0 (GstEGLGLESImageData);

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 4);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        image =
            eglCreateImageKHR (gst_egl_display_get (eglglessink->
                eglglesctx.display), eglglessink->eglglesctx.eglcontext,
            EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer) (guintptr) data->texture,
            NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (GST_EGL_IMAGE_BUFFER_POOL
            (eglglessink->pool)->allocator, eglglessink->eglglesctx.display,
            image, GST_EGL_IMAGE_MEMORY_TYPE_RGBA,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);

        n_mem = 1;
      }
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  buffer = gst_buffer_new ();
  gst_buffer_add_video_meta_full (buffer, 0, format, width, height,
      GST_VIDEO_INFO_N_PLANES (&info), offset, stride);

  for (i = 0; i < n_mem; i++)
    gst_buffer_append_memory (buffer, mem[i]);

  return buffer;

mem_error:
  {
    GST_ERROR_OBJECT (eglglessink, "Failed to create EGLImage");

    if (data)
      gst_egl_gles_image_data_free (data);

    if (mem[0])
      gst_memory_unref (mem[0]);
    if (mem[1])
      gst_memory_unref (mem[1]);
    if (mem[2])
      gst_memory_unref (mem[2]);

    return NULL;
  }
}

G_DEFINE_TYPE (GstEGLImageBufferPool, gst_egl_image_buffer_pool,
    GST_TYPE_VIDEO_BUFFER_POOL);

static const gchar **
gst_egl_image_buffer_pool_get_options (GstBufferPool * bpool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL
  };

  return options;
}

static gboolean
gst_egl_image_buffer_pool_set_config (GstBufferPool * bpool,
    GstStructure * config)
{
  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);
  GstCaps *caps;
  GstVideoInfo info;

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  if (!GST_BUFFER_POOL_CLASS
      (gst_egl_image_buffer_pool_parent_class)->set_config (bpool, config))
    return FALSE;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL)
      || !caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (!gst_buffer_pool_config_get_allocator (config, &pool->allocator,
          &pool->params))
    return FALSE;
  if (pool->allocator)
    gst_object_ref (pool->allocator);

  pool->add_metavideo =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  pool->want_eglimage = (pool->allocator
      && g_strcmp0 (pool->allocator->mem_type, GST_EGL_IMAGE_MEMORY_TYPE) == 0);

  pool->info = info;

  return TRUE;
}

static GstFlowReturn
gst_egl_image_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);

  *buffer = NULL;

  if (!pool->add_metavideo || !pool->want_eglimage)
    return
        GST_BUFFER_POOL_CLASS
        (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
        buffer, params);

  if (!pool->allocator)
    return GST_FLOW_NOT_NEGOTIATED;

  switch (pool->info.finfo->format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{
      GstFlowReturn ret;
      GstQuery *query;
      GstStructure *s;
      const GValue *v;

      s = gst_structure_new ("eglglessink-allocate-eglimage",
          "format", GST_TYPE_VIDEO_FORMAT, pool->info.finfo->format,
          "width", G_TYPE_INT, pool->info.width,
          "height", G_TYPE_INT, pool->info.height, NULL);
      query = gst_query_new_custom (GST_QUERY_CUSTOM, s);

      ret =
          gst_eglglessink_queue_object (pool->sink,
          GST_MINI_OBJECT_CAST (query));

      if (ret != GST_FLOW_OK || !gst_structure_has_field (s, "buffer")) {
        GST_WARNING ("Fallback memory allocation");
        gst_query_unref (query);
        return
            GST_BUFFER_POOL_CLASS
            (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
            buffer, params);
      }

      v = gst_structure_get_value (s, "buffer");
      *buffer = GST_BUFFER_CAST (g_value_get_pointer (v));
      gst_query_unref (query);

      if (!*buffer) {
        GST_WARNING ("Fallback memory allocation");
        return
            GST_BUFFER_POOL_CLASS
            (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
            buffer, params);
      }

      return GST_FLOW_OK;
      break;
    }
    default:
      return
          GST_BUFFER_POOL_CLASS
          (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
          buffer, params);
      break;
  }

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_egl_image_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret;
  GstEGLImageBufferPool *pool;

  ret =
      GST_BUFFER_POOL_CLASS
      (gst_egl_image_buffer_pool_parent_class)->acquire_buffer (bpool,
      buffer, params);
  if (ret != GST_FLOW_OK || !*buffer)
    return ret;

  pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);

  /* XXX: Don't return the memory we just rendered, glEGLImageTargetTexture2DOES()
   * keeps the EGLImage unmappable until the next one is uploaded
   */
  if (*buffer && *buffer == pool->sink->last_buffer) {
    GstBuffer *oldbuf = *buffer;

    ret =
        GST_BUFFER_POOL_CLASS
        (gst_egl_image_buffer_pool_parent_class)->acquire_buffer (bpool,
        buffer, params);
    gst_object_replace ((GstObject **) &oldbuf->pool, (GstObject *) pool);
    gst_buffer_unref (oldbuf);
  }

  return ret;
}

static void
gst_egl_image_buffer_pool_finalize (GObject * object)
{
  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (object);

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  if (pool->sink)
    gst_object_unref (pool->sink);
  pool->sink = NULL;

  if (pool->display)
    gst_egl_display_unref (pool->display);
  pool->display = NULL;

  G_OBJECT_CLASS (gst_egl_image_buffer_pool_parent_class)->finalize (object);
}

static void
gst_egl_image_buffer_pool_class_init (GstEGLImageBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_egl_image_buffer_pool_finalize;
  gstbufferpool_class->get_options = gst_egl_image_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_egl_image_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_egl_image_buffer_pool_alloc_buffer;
  gstbufferpool_class->acquire_buffer =
      gst_egl_image_buffer_pool_acquire_buffer;
}

static void
gst_egl_image_buffer_pool_init (GstEGLImageBufferPool * pool)
{
}

static GstBufferPool *
gst_egl_image_buffer_pool_new (GstEglGlesSink *
    eglglessink, GstEGLDisplay * display)
{
  GstEGLImageBufferPool *pool;

  pool = g_object_new (gst_egl_image_buffer_pool_get_type (), NULL);
  pool->display = gst_egl_display_ref (display);
  pool->sink = gst_object_ref (eglglessink);

  return (GstBufferPool *) pool;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
eglglessink_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_eglglessink_debug, "eglglessink",
      0, "Simple EGL/GLES Sink");

#ifdef USE_EGL_RPI
  GST_DEBUG ("Initialize BCM host");
  bcm_host_init ();
#endif

  return gst_element_register (plugin, "eglglessink", GST_RANK_PRIMARY,
      GST_TYPE_EGLGLESSINK);
}

/* gstreamer looks for this structure to register eglglessinks */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    eglglessink,
    "EGL/GLES sink",
    eglglessink_plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
