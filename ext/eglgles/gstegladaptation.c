/*
 * GStreamer EGL/GLES Sink Adaptation
 * Copyright (C) 2013 Collabora Ltd.
 *   @author: Thiago Santos <thiago.sousa.santos@collabora.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/video/video.h>
#include "gstegladaptation.h"

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
      "void main(void)"
      "{"
      " vec4 t = texture2D(tex, opos);"
      " gl_FragColor = vec4(t.rgb, 1.0);"
      "}"
};

/* Channel reordering for XYZ <-> ZYX conversion */
static const char *frag_REORDER_prog = {
  "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "void main(void)"
      "{"
      " vec4 t = texture2D(tex, opos);"
      " gl_FragColor = vec4(t.%c, t.%c, t.%c, 1.0);"
      "}"
};

/* Packed YUV converters */

/** AYUV to RGB conversion */
static const char *frag_AYUV_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv  = texture2D(tex,opos).gba;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};

/** YUY2/YVYU/UYVY to RGB conversion */
static const char *frag_YUY2_YVYU_UYVY_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D Ytex, UVtex;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r, g, b;"
      "  vec3 yuv;"
      "  yuv.x = texture2D(Ytex,opos).%c;"
      "  yuv.yz = texture2D(UVtex,opos).%c%c;"
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
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv.x=texture2D(Ytex,opos).r;"
      "  yuv.y=texture2D(Utex,opos).r;"
      "  yuv.z=texture2D(Vtex,opos).r;"
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
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv.x=texture2D(Ytex,opos).r;"
      "  yuv.yz=texture2D(UVtex,opos).%c%c;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};
/* *INDENT-ON* */

/* will probably move elsewhere */
static const EGLint eglglessink_RGBA8888_attribs[] = {
  EGL_RED_SIZE, 8,
  EGL_GREEN_SIZE, 8,
  EGL_BLUE_SIZE, 8,
  EGL_ALPHA_SIZE, 8,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};

static const EGLint eglglessink_RGB888_attribs[] = {
  EGL_RED_SIZE, 8,
  EGL_GREEN_SIZE, 8,
  EGL_BLUE_SIZE, 8,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};

static const EGLint eglglessink_RGB565_attribs[] = {
  EGL_RED_SIZE, 5,
  EGL_GREEN_SIZE, 6,
  EGL_BLUE_SIZE, 5,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};

static gboolean
create_shader_program (GstEglAdaptationContext * ctx, GLuint * prog,
    GLuint * vert, GLuint * frag, const gchar * vert_text,
    const gchar * frag_text)
{
  GLint test;
  GLchar *info_log;

  /* Build shader program for video texture rendering */
  *vert = glCreateShader (GL_VERTEX_SHADER);
  GST_DEBUG_OBJECT (ctx->element, "Sending %s to handle %d", vert_text, *vert);
  glShaderSource (*vert, 1, &vert_text, NULL);
  if (got_gl_error ("glShaderSource vertex"))
    goto HANDLE_ERROR;

  glCompileShader (*vert);
  if (got_gl_error ("glCompileShader vertex"))
    goto HANDLE_ERROR;

  glGetShaderiv (*vert, GL_COMPILE_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (ctx->element, "Successfully compiled vertex shader");
  else {
    GST_ERROR_OBJECT (ctx->element, "Couldn't compile vertex shader");
    glGetShaderiv (*vert, GL_INFO_LOG_LENGTH, &test);
    info_log = g_new0 (GLchar, test);
    glGetShaderInfoLog (*vert, test, NULL, info_log);
    GST_INFO_OBJECT (ctx->element, "Compilation info log:\n%s", info_log);
    g_free (info_log);
    goto HANDLE_ERROR;
  }

  *frag = glCreateShader (GL_FRAGMENT_SHADER);
  GST_DEBUG_OBJECT (ctx->element, "Sending %s to handle %d", frag_text, *frag);
  glShaderSource (*frag, 1, &frag_text, NULL);
  if (got_gl_error ("glShaderSource fragment"))
    goto HANDLE_ERROR;

  glCompileShader (*frag);
  if (got_gl_error ("glCompileShader fragment"))
    goto HANDLE_ERROR;

  glGetShaderiv (*frag, GL_COMPILE_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (ctx->element, "Successfully compiled fragment shader");
  else {
    GST_ERROR_OBJECT (ctx->element, "Couldn't compile fragment shader");
    glGetShaderiv (*frag, GL_INFO_LOG_LENGTH, &test);
    info_log = g_new0 (GLchar, test);
    glGetShaderInfoLog (*frag, test, NULL, info_log);
    GST_INFO_OBJECT (ctx->element, "Compilation info log:\n%s", info_log);
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
    GST_DEBUG_OBJECT (ctx->element, "GLES: Successfully linked program");
  } else {
    GST_ERROR_OBJECT (ctx->element, "Couldn't link program");
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


gboolean
gst_egl_adaptation_init_display (GstEglAdaptationContext * ctx)
{
  EGLDisplay display;
  GST_DEBUG_OBJECT (ctx->element, "Enter EGL initial configuration");

#ifdef USE_EGL_RPI
  /* See https://github.com/raspberrypi/firmware/issues/99 */
  if (!eglMakeCurrent ((EGLDisplay) 1, EGL_NO_SURFACE, EGL_NO_SURFACE,
          EGL_NO_CONTEXT)) {
    got_egl_error ("eglMakeCurrent");
    GST_ERROR_OBJECT (ctx->element, "Couldn't unbind context");
    return FALSE;
  }
#endif

  display = eglGetDisplay (EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY) {
    GST_ERROR_OBJECT (ctx->element, "Could not get EGL display connection");
    goto HANDLE_ERROR;          /* No EGL error is set by eglGetDisplay() */
  }
  ctx->eglglesctx.display = display;

  if (!eglInitialize (display,
          &ctx->eglglesctx.egl_major,
          &ctx->eglglesctx.egl_minor)) {
    got_egl_error ("eglInitialize");
    GST_ERROR_OBJECT (ctx->element, "Could not init EGL display connection");
    goto HANDLE_EGL_ERROR;
  }

  /* Check against required EGL version
   * XXX: Need to review the version requirement in terms of the needed API
   */
  if (ctx->eglglesctx.egl_major < GST_EGLGLESSINK_EGL_MIN_VERSION) {
    GST_ERROR_OBJECT (ctx->element, "EGL v%d needed, but you only have v%d.%d",
        GST_EGLGLESSINK_EGL_MIN_VERSION, ctx->eglglesctx.egl_major,
        ctx->eglglesctx.egl_minor);
    goto HANDLE_ERROR;
  }

  GST_INFO_OBJECT (ctx->element, "System reports supported EGL version v%d.%d",
      ctx->eglglesctx.egl_major, ctx->eglglesctx.egl_minor);

  eglBindAPI (EGL_OPENGL_ES_API);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR:
  GST_ERROR_OBJECT (ctx->element, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR:
  GST_ERROR_OBJECT (ctx->element, "Couldn't setup window/surface from handle");
  return FALSE;
}

GstEglAdaptationContext *
gst_egl_adaptation_context_new (GstElement * element)
{
  GstEglAdaptationContext *ctx = g_new0 (GstEglAdaptationContext, 1);

  ctx->element = gst_object_ref (element);

  return ctx;
}

void
gst_egl_adaptation_context_free (GstEglAdaptationContext * ctx)
{
  gst_object_unref (ctx->element);
  g_free (ctx);
}

gboolean
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

gboolean
gst_egl_adaptation_choose_config (GstEglAdaptationContext * ctx)
{
  EGLint con_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
  GLint egl_configs;

  if ((eglChooseConfig (ctx->eglglesctx.display,
              eglglessink_RGBA8888_attribs,
              &ctx->eglglesctx.config, 1, &egl_configs)) == EGL_FALSE) {
    got_egl_error ("eglChooseConfig");
    GST_ERROR_OBJECT (ctx->element, "eglChooseConfig failed");
    goto HANDLE_EGL_ERROR;
  }

  if (egl_configs < 1) {
    GST_ERROR_OBJECT (ctx->element,
        "Could not find matching framebuffer config");
    goto HANDLE_ERROR;
  }

  ctx->eglglesctx.eglcontext =
      eglCreateContext (ctx->eglglesctx.display,
      ctx->eglglesctx.config, EGL_NO_CONTEXT, con_attribs);

  if (ctx->eglglesctx.eglcontext == EGL_NO_CONTEXT) {
    GST_ERROR_OBJECT (ctx->element, "Error getting context, eglCreateContext");
    goto HANDLE_EGL_ERROR;
  }

  GST_DEBUG_OBJECT (ctx->element, "EGL Context: %p",
      ctx->eglglesctx.eglcontext);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR:
  GST_ERROR_OBJECT (ctx->element, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR:
  GST_ERROR_OBJECT (ctx->element, "Couldn't choose an usable config");
  return FALSE;
}

gint gst_egl_adaptation_context_fill_supported_fbuffer_configs
    (GstEglAdaptationContext * ctx, GstCaps ** ret_caps)
{
  gboolean ret = FALSE;
  EGLint cfg_number;
  GstCaps *caps;

  GST_DEBUG_OBJECT (ctx->element,
      "Building initial list of wanted eglattribs per format");

  /* Init supported format/caps list */
  caps = gst_caps_new_empty ();

  if (eglChooseConfig (ctx->eglglesctx.display,
          eglglessink_RGBA8888_attribs, NULL, 1, &cfg_number) != EGL_FALSE) {
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBA));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRA));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ARGB));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ABGR));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBx));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRx));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xRGB));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xBGR));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_AYUV));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y444));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_I420));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_YV12));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV12));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV21));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_YUY2));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_YVYU));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_UYVY));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y42B));
    gst_caps_append (caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y41B));
    ret = TRUE;
  } else {
    GST_INFO_OBJECT (ctx->element,
        "EGL display doesn't support RGBA8888 config");
  }

  GST_OBJECT_LOCK (ctx->element);
  gst_caps_replace (ret_caps, caps);
  GST_OBJECT_UNLOCK (ctx->element);
  gst_caps_unref (caps);

  return ret;
}

gboolean
gst_egl_adaptation_context_make_current (GstEglAdaptationContext * ctx,
    gboolean bind)
{
  g_assert (ctx->eglglesctx.display != NULL);

  if (bind && ctx->eglglesctx.surface &&
      ctx->eglglesctx.eglcontext) {
    EGLContext *cur_ctx = eglGetCurrentContext ();

    if (cur_ctx == ctx->eglglesctx.eglcontext) {
      GST_DEBUG_OBJECT (ctx->element,
          "Already attached the context to thread %p", g_thread_self ());
      return TRUE;
    }

    GST_DEBUG_OBJECT (ctx->element, "Attaching context to thread %p",
        g_thread_self ());
    if (!eglMakeCurrent (ctx->eglglesctx.display,
            ctx->eglglesctx.surface, ctx->eglglesctx.surface,
            ctx->eglglesctx.eglcontext)) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (ctx->element, "Couldn't bind context");
      return FALSE;
    }
  } else {
    GST_DEBUG_OBJECT (ctx->element, "Detaching context from thread %p",
        g_thread_self ());
    if (!eglMakeCurrent (ctx->eglglesctx.display, EGL_NO_SURFACE,
            EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (ctx->element, "Couldn't unbind context");
      return FALSE;
    }
  }

  return TRUE;
}

void
gst_egl_adaptation_context_cleanup (GstEglAdaptationContext * ctx)
{
  gint i;

  glUseProgram (0);

  if (ctx->have_vbo) {
    glDeleteBuffers (1, &ctx->eglglesctx.position_buffer);
    glDeleteBuffers (1, &ctx->eglglesctx.index_buffer);
    ctx->have_vbo = FALSE;
  }

  if (ctx->have_texture) {
    glDeleteTextures (ctx->eglglesctx.n_textures,
        ctx->eglglesctx.texture);
    ctx->have_texture = FALSE;
    ctx->eglglesctx.n_textures = 0;
  }

  for (i = 0; i < 2; i++) {
    if (ctx->eglglesctx.glslprogram[i]) {
      glDetachShader (ctx->eglglesctx.glslprogram[i],
          ctx->eglglesctx.fragshader[i]);
      glDetachShader (ctx->eglglesctx.glslprogram[i],
          ctx->eglglesctx.vertshader[i]);
      glDeleteProgram (ctx->eglglesctx.glslprogram[i]);
      glDeleteShader (ctx->eglglesctx.fragshader[i]);
      glDeleteShader (ctx->eglglesctx.vertshader[i]);
      ctx->eglglesctx.glslprogram[i] = 0;
      ctx->eglglesctx.fragshader[i] = 0;
      ctx->eglglesctx.vertshader[i] = 0;
    }
  }

  gst_egl_adaptation_context_make_current (ctx, FALSE);

  if (ctx->eglglesctx.surface) {
    eglDestroySurface (ctx->eglglesctx.display,
        ctx->eglglesctx.surface);
    ctx->eglglesctx.surface = NULL;
    ctx->have_surface = FALSE;
  }

  if (ctx->eglglesctx.eglcontext) {
    eglDestroyContext (ctx->eglglesctx.display,
        ctx->eglglesctx.eglcontext);
    ctx->eglglesctx.eglcontext = NULL;
  }
}

/* XXX: Lock eglgles context? */
gboolean
gst_egl_adaptation_context_update_surface_dimensions (GstEglAdaptationContext *
    ctx)
{
  gint width, height;

  /* Save surface dims */
  eglQuerySurface (ctx->eglglesctx.display,
      ctx->eglglesctx.surface, EGL_WIDTH, &width);
  eglQuerySurface (ctx->eglglesctx.display,
      ctx->eglglesctx.surface, EGL_HEIGHT, &height);

  if (width != ctx->eglglesctx.surface_width ||
      height != ctx->eglglesctx.surface_height) {
    ctx->eglglesctx.surface_width = width;
    ctx->eglglesctx.surface_height = height;
    GST_INFO_OBJECT (ctx->element, "Got surface of %dx%d pixels", width, height);
    return TRUE;
  }

  return FALSE;
}


gboolean
gst_egl_adaptation_init_egl_surface (GstEglAdaptationContext * ctx,
    GstVideoFormat format)
{
  GLboolean ret;
  EGLint display_par;
  const gchar *texnames[3] = { NULL, };
  gchar *frag_prog = NULL;
  gboolean free_frag_prog = FALSE;
  EGLint swap_behavior;
  gint i;

  GST_DEBUG_OBJECT (ctx->element, "Enter EGL surface setup");

  ctx->eglglesctx.surface =
      eglCreateWindowSurface (ctx->eglglesctx.display,
      ctx->eglglesctx.config, ctx->eglglesctx.used_window,
      NULL);

  if (ctx->eglglesctx.surface == EGL_NO_SURFACE) {
    got_egl_error ("eglCreateWindowSurface");
    GST_ERROR_OBJECT (ctx->element, "Can't create surface");
    goto HANDLE_EGL_ERROR_LOCKED;
  }

  ctx->eglglesctx.buffer_preserved = FALSE;
  if (eglQuerySurface (ctx->eglglesctx.display,
          ctx->eglglesctx.surface, EGL_SWAP_BEHAVIOR, &swap_behavior)) {
    GST_DEBUG_OBJECT (ctx->element, "Buffer swap behavior %x", swap_behavior);
    ctx->eglglesctx.buffer_preserved =
        swap_behavior == EGL_BUFFER_PRESERVED;
  } else {
    GST_DEBUG_OBJECT (ctx->element, "Can't query buffer swap behavior");
  }

  if (!gst_egl_adaptation_context_make_current (ctx, TRUE))
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
  if (ctx->eglglesctx.egl_major == 1 &&
      ctx->eglglesctx.egl_minor < 2) {
    GST_DEBUG_OBJECT (ctx->element, "Can't query PAR. Using default: %dx%d",
        EGL_DISPLAY_SCALING, EGL_DISPLAY_SCALING);
    ctx->eglglesctx.pixel_aspect_ratio = EGL_DISPLAY_SCALING;
  } else {
    eglQuerySurface (ctx->eglglesctx.display,
        ctx->eglglesctx.surface, EGL_PIXEL_ASPECT_RATIO, &display_par);
    /* Fix for outbound DAR reporting on some implementations not
     * honoring the 'should return w/h * EGL_DISPLAY_SCALING' spec
     * requirement
     */
    if (display_par == EGL_UNKNOWN || display_par < EGL_SANE_DAR_MIN ||
        display_par > EGL_SANE_DAR_MAX) {
      GST_DEBUG_OBJECT (ctx->element, "Nonsensical PAR value returned: %d. "
          "Bad EGL implementation? "
          "Will use default: %d/%d", ctx->eglglesctx.pixel_aspect_ratio,
          EGL_DISPLAY_SCALING, EGL_DISPLAY_SCALING);
      ctx->eglglesctx.pixel_aspect_ratio = EGL_DISPLAY_SCALING;
    } else {
      ctx->eglglesctx.pixel_aspect_ratio = display_par;
    }
  }

  /* Save surface dims */
  gst_egl_adaptation_context_update_surface_dimensions (ctx);

  /* We have a surface! */
  ctx->have_surface = TRUE;

  /* Init vertex and fragment GLSL shaders. 
   * Note: Shader compiler support is optional but we currently rely on it.
   */

  glGetBooleanv (GL_SHADER_COMPILER, &ret);
  if (ret == GL_FALSE) {
    GST_ERROR_OBJECT (ctx->element, "Shader compiler support is unavailable!");
    goto HANDLE_ERROR;
  }

  /* Build shader program for video texture rendering */

  switch (format) {
    case GST_VIDEO_FORMAT_AYUV:
      frag_prog = (gchar *) frag_AYUV_prog;
      free_frag_prog = FALSE;
      ctx->eglglesctx.n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      frag_prog = (gchar *) frag_PLANAR_YUV_prog;
      free_frag_prog = FALSE;
      ctx->eglglesctx.n_textures = 3;
      texnames[0] = "Ytex";
      texnames[1] = "Utex";
      texnames[2] = "Vtex";
      break;
    case GST_VIDEO_FORMAT_YUY2:
      frag_prog = g_strdup_printf (frag_YUY2_YVYU_UYVY_prog, 'r', 'g', 'a');
      free_frag_prog = TRUE;
      ctx->eglglesctx.n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_YVYU:
      frag_prog = g_strdup_printf (frag_YUY2_YVYU_UYVY_prog, 'r', 'a', 'g');
      free_frag_prog = TRUE;
      ctx->eglglesctx.n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_UYVY:
      frag_prog = g_strdup_printf (frag_YUY2_YVYU_UYVY_prog, 'a', 'r', 'b');
      free_frag_prog = TRUE;
      ctx->eglglesctx.n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_NV12:
      frag_prog = g_strdup_printf (frag_NV12_NV21_prog, 'r', 'a');
      free_frag_prog = TRUE;
      ctx->eglglesctx.n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_NV21:
      frag_prog = g_strdup_printf (frag_NV12_NV21_prog, 'a', 'r');
      free_frag_prog = TRUE;
      ctx->eglglesctx.n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      frag_prog = g_strdup_printf (frag_REORDER_prog, 'b', 'g', 'r');
      free_frag_prog = TRUE;
      ctx->eglglesctx.n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
      frag_prog = g_strdup_printf (frag_REORDER_prog, 'g', 'b', 'a');
      free_frag_prog = TRUE;
      ctx->eglglesctx.n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      frag_prog = g_strdup_printf (frag_REORDER_prog, 'a', 'b', 'g');
      free_frag_prog = TRUE;
      ctx->eglglesctx.n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGB16:
      frag_prog = (gchar *) frag_COPY_prog;
      free_frag_prog = FALSE;
      ctx->eglglesctx.n_textures = 1;
      texnames[0] = "tex";
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (!create_shader_program (ctx,
          &ctx->eglglesctx.glslprogram[0],
          &ctx->eglglesctx.vertshader[0],
          &ctx->eglglesctx.fragshader[0], vert_COPY_prog, frag_prog)) {
    if (free_frag_prog)
      g_free (frag_prog);
    frag_prog = NULL;
    goto HANDLE_ERROR;
  }
  if (free_frag_prog)
    g_free (frag_prog);
  frag_prog = NULL;

  ctx->eglglesctx.position_loc[0] =
      glGetAttribLocation (ctx->eglglesctx.glslprogram[0], "position");
  ctx->eglglesctx.texpos_loc[0] =
      glGetAttribLocation (ctx->eglglesctx.glslprogram[0], "texpos");

  glEnableVertexAttribArray (ctx->eglglesctx.position_loc[0]);
  if (got_gl_error ("glEnableVertexAttribArray"))
    goto HANDLE_ERROR;

  glEnableVertexAttribArray (ctx->eglglesctx.texpos_loc[0]);
  if (got_gl_error ("glEnableVertexAttribArray"))
    goto HANDLE_ERROR;

  for (i = 0; i < ctx->eglglesctx.n_textures; i++) {
    ctx->eglglesctx.tex_loc[0][i] =
        glGetUniformLocation (ctx->eglglesctx.glslprogram[0],
        texnames[i]);
  }

  if (!ctx->eglglesctx.buffer_preserved) {
    /* Build shader program for black borders */
    if (!create_shader_program (ctx,
            &ctx->eglglesctx.glslprogram[1],
            &ctx->eglglesctx.vertshader[1],
            &ctx->eglglesctx.fragshader[1], vert_COPY_prog_no_tex,
            frag_BLACK_prog))
      goto HANDLE_ERROR;

    ctx->eglglesctx.position_loc[1] =
        glGetAttribLocation (ctx->eglglesctx.glslprogram[1],
        "position");

    glEnableVertexAttribArray (ctx->eglglesctx.position_loc[1]);
    if (got_gl_error ("glEnableVertexAttribArray"))
      goto HANDLE_ERROR;
  }

  /* Generate textures */
  if (!ctx->have_texture) {
    GST_INFO_OBJECT (ctx->element, "Performing initial texture setup");

    glGenTextures (ctx->eglglesctx.n_textures,
        ctx->eglglesctx.texture);
    if (got_gl_error ("glGenTextures"))
      goto HANDLE_ERROR_LOCKED;

    for (i = 0; i < ctx->eglglesctx.n_textures; i++) {
      glBindTexture (GL_TEXTURE_2D, ctx->eglglesctx.texture[i]);
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

    ctx->have_texture = TRUE;
  }

  glUseProgram (0);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR_LOCKED:
  GST_ERROR_OBJECT (ctx->element, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR_LOCKED:
HANDLE_ERROR:
  GST_ERROR_OBJECT (ctx->element, "Couldn't setup EGL surface");
  return FALSE;
}

gboolean
got_gl_error (const char *wtf)
{
  GLuint error = GL_NO_ERROR;

  if ((error = glGetError ()) != GL_NO_ERROR) {
    GST_CAT_ERROR (GST_CAT_DEFAULT, "GL ERROR: %s returned 0x%04x", wtf, error);
    return TRUE;
  }
  return FALSE;
}


void
gst_egl_adaptation_context_terminate_display (GstEglAdaptationContext * ctx)
{
  if (ctx->eglglesctx.display) {
    eglTerminate (ctx->eglglesctx.display);
    ctx->eglglesctx.display = NULL;
  }
}

void
gst_egl_adaptation_context_bind_API (GstEglAdaptationContext * ctx)
{
  eglBindAPI (EGL_OPENGL_ES_API);
}

gboolean
gst_egl_adaptation_context_swap_buffers (GstEglAdaptationContext * ctx)
{
  gboolean ret =
      eglSwapBuffers (ctx->eglglesctx.display, ctx->eglglesctx.surface);
  if (ret == EGL_FALSE) {
    got_egl_error ("eglSwapBuffers");
  }
  return ret;
}

/* Prints avilable EGL/GLES extensions 
 * If another rendering path is implemented this is the place
 * where you want to check for the availability of its supporting
 * EGL/GLES extensions.
 */
void
gst_egl_adaptation_context_init_egl_exts (GstEglAdaptationContext * ctx)
{
  const char *eglexts;
  unsigned const char *glexts;

  eglexts = eglQueryString (ctx->eglglesctx.display, EGL_EXTENSIONS);
  glexts = glGetString (GL_EXTENSIONS);

  GST_DEBUG_OBJECT (ctx->element, "Available EGL extensions: %s\n",
      GST_STR_NULL (eglexts));
  GST_DEBUG_OBJECT (ctx->element, "Available GLES extensions: %s\n",
      GST_STR_NULL ((const char *) glexts));

  return;
}
