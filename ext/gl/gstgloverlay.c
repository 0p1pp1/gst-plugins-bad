/*
 * GStreamer
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
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
 * SECTION:element-gloverlay
 *
 * Overlay GL video texture with a PNG image
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-1.0 videotestsrc ! gloverlay location=image.jpg ! glimagesink
 * ]|
 * FBO (Frame Buffer Object) is required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gl/gstglconfig.h>

#include "gstgloverlay.h"
#include "effects/gstgleffectssources.h"

#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>
#include <png.h>

#if PNG_LIBPNG_VER >= 10400
#define int_p_NULL         NULL
#define png_infopp_NULL    NULL
#endif

#define GST_CAT_DEFAULT gst_gl_overlay_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_overlay_debug, "gloverlay", 0, "gloverlay element");

G_DEFINE_TYPE_WITH_CODE (GstGLOverlay, gst_gl_overlay, GST_TYPE_GL_FILTER,
    DEBUG_INIT);

static gboolean gst_gl_overlay_set_caps (GstGLFilter * filter,
    GstCaps * incaps, GstCaps * outcaps);

static void gst_gl_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_overlay_reset_resources (GstGLFilter * filter);

static void gst_gl_overlay_before_transform (GstBaseTransform * trans,
    GstBuffer * outbuf);
static gboolean gst_gl_overlay_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);

static gint gst_gl_overlay_load_png (GstGLFilter * filter);
static gint gst_gl_overlay_load_jpeg (GstGLFilter * filter);

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_OFFSET_X,
  PROP_OFFSET_Y,
  PROP_RELATIVE_X,
  PROP_RELATIVE_Y,
  PROP_OVERLAY_WIDTH,
  PROP_OVERLAY_HEIGHT,
  PROP_ALPHA
};

/* *INDENT-OFF* */
/* vertex source */
static const gchar *overlay_v_src =
    "attribute vec4 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = a_position;\n"
    "   v_texcoord = a_texcoord;\n"
    "}";

/* fragment source */
static const gchar *overlay_f_src =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform sampler2D texture;\n"
    "uniform float alpha;\n"
    "varying vec2 v_texcoord;\n"
    "void main()\n"
    "{\n"
    "  vec4 rgba = texture2D( texture, v_texcoord );\n"
    "  gl_FragColor = vec4(rgba.rgb, rgba.a * alpha);\n"
    "}\n";
/* *INDENT-ON* */

/* init resources that need a gl context */
static gboolean
gst_gl_overlay_init_gl_resources (GstGLFilter * filter)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (filter);

  if (overlay->shader)
    gst_gl_context_del_shader (filter->context, overlay->shader);

  return gst_gl_context_gen_shader (filter->context, overlay_v_src,
      overlay_f_src, &overlay->shader);
}

/* free resources that need a gl context */
static void
gst_gl_overlay_reset_gl_resources (GstGLFilter * filter)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (filter);
  const GstGLFuncs *gl = filter->context->gl_vtable;

  if (overlay->shader) {
    gst_gl_context_del_shader (filter->context, overlay->shader);
    overlay->shader = NULL;
  }

  if (overlay->image_memory) {
    gst_memory_unref ((GstMemory *) overlay->image_memory);
    overlay->image_memory = NULL;
  }

  if (overlay->vao) {
    gl->DeleteVertexArrays (1, &overlay->vao);
    overlay->vao = 0;
  }

  if (overlay->vbo) {
    gl->DeleteBuffers (1, &overlay->vbo);
    overlay->vbo = 0;
  }

  if (overlay->overlay_vao) {
    gl->DeleteVertexArrays (1, &overlay->overlay_vao);
    overlay->overlay_vao = 0;
  }

  if (overlay->overlay_vbo) {
    gl->DeleteBuffers (1, &overlay->overlay_vbo);
    overlay->overlay_vbo = 0;
  }
}

static void
gst_gl_overlay_class_init (GstGLOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_overlay_set_property;
  gobject_class->get_property = gst_gl_overlay_get_property;

  GST_GL_FILTER_CLASS (klass)->set_caps = gst_gl_overlay_set_caps;
  GST_GL_FILTER_CLASS (klass)->filter_texture = gst_gl_overlay_filter_texture;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb =
      gst_gl_overlay_reset_gl_resources;
  GST_GL_FILTER_CLASS (klass)->onStop = gst_gl_overlay_reset_resources;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_overlay_init_gl_resources;

  GST_BASE_TRANSFORM_CLASS (klass)->before_transform =
      GST_DEBUG_FUNCPTR (gst_gl_overlay_before_transform);

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "location",
          "Location of image file to overlay", NULL, GST_PARAM_CONTROLLABLE
          | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OFFSET_X,
      g_param_spec_int ("offset-x", "X Offset",
          "For positive value, horizontal offset of overlay image in pixels from"
          " left of video image. For negative value, horizontal offset of overlay"
          " image in pixels from right of video image", G_MININT, G_MAXINT, 0,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OFFSET_Y,
      g_param_spec_int ("offset-y", "Y Offset",
          "For positive value, vertical offset of overlay image in pixels from"
          " top of video image. For negative value, vertical offset of overlay"
          " image in pixels from bottom of video image", G_MININT, G_MAXINT, 0,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RELATIVE_X,
      g_param_spec_double ("relative-x", "Relative X Offset",
          "Horizontal offset of overlay image in fractions of video image "
          "width, from top-left corner of video image", 0.0, 1.0, 0.0,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RELATIVE_Y,
      g_param_spec_double ("relative-y", "Relative Y Offset",
          "Vertical offset of overlay image in fractions of video image "
          "height, from top-left corner of video image", 0.0, 1.0, 0.0,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OVERLAY_WIDTH,
      g_param_spec_int ("overlay-width", "Overlay Width",
          "Width of overlay image in pixels (0 = same as overlay image)", 0,
          G_MAXINT, 0,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OVERLAY_HEIGHT,
      g_param_spec_int ("overlay-height", "Overlay Height",
          "Height of overlay image in pixels (0 = same as overlay image)", 0,
          G_MAXINT, 0,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Global alpha of overlay image",
          0.0, 1.0, 1.0, GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING
          | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class,
      "Gstreamer OpenGL Overlay", "Filter/Effect/Video",
      "Overlay GL video texture with a JPEG/PNG image",
      "Filippo Argiolas <filippo.argiolas@gmail.com>, "
      "Matthew Waters <matthew@centricular.com>");
}

static void
gst_gl_overlay_init (GstGLOverlay * overlay)
{
  overlay->offset_x = 0;
  overlay->offset_y = 0;

  overlay->relative_x = 0.0;
  overlay->relative_y = 0.0;

  overlay->overlay_width = 0;
  overlay->overlay_height = 0;

  overlay->alpha = 1.0;
}

static void
gst_gl_overlay_reset_resources (GstGLFilter * filter)
{
}

static void
gst_gl_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (object);

  switch (prop_id) {
    case PROP_LOCATION:
      if (overlay->location != NULL)
        g_free (overlay->location);
      overlay->location_has_changed = TRUE;
      overlay->location = g_value_dup_string (value);
      break;
    case PROP_OFFSET_X:
      overlay->offset_x = g_value_get_int (value);
      overlay->geometry_change = TRUE;
      break;
    case PROP_OFFSET_Y:
      overlay->offset_y = g_value_get_int (value);
      overlay->geometry_change = TRUE;
      break;
    case PROP_RELATIVE_X:
      overlay->relative_x = g_value_get_double (value);
      overlay->geometry_change = TRUE;
      break;
    case PROP_RELATIVE_Y:
      overlay->relative_y = g_value_get_double (value);
      overlay->geometry_change = TRUE;
      break;
    case PROP_OVERLAY_WIDTH:
      overlay->overlay_width = g_value_get_int (value);
      overlay->geometry_change = TRUE;
      break;
    case PROP_OVERLAY_HEIGHT:
      overlay->overlay_height = g_value_get_int (value);
      overlay->geometry_change = TRUE;
      break;
    case PROP_ALPHA:
      overlay->alpha = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, overlay->location);
      break;
    case PROP_OFFSET_X:
      g_value_set_int (value, overlay->offset_x);
      break;
    case PROP_OFFSET_Y:
      g_value_set_int (value, overlay->offset_y);
      break;
    case PROP_RELATIVE_X:
      g_value_set_double (value, overlay->relative_x);
      break;
    case PROP_RELATIVE_Y:
      g_value_set_double (value, overlay->relative_y);
      break;
    case PROP_OVERLAY_WIDTH:
      g_value_set_int (value, overlay->overlay_width);
      break;
    case PROP_OVERLAY_HEIGHT:
      g_value_set_int (value, overlay->overlay_height);
      break;
    case PROP_ALPHA:
      g_value_set_double (value, overlay->alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_overlay_set_caps (GstGLFilter * filter, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (filter);
  GstStructure *s = gst_caps_get_structure (incaps, 0);
  gint width = 0;
  gint height = 0;

  gst_structure_get_int (s, "width", &width);
  gst_structure_get_int (s, "height", &height);

  overlay->window_width = width;
  overlay->window_height = height;

  return TRUE;
}

static void
_unbind_buffer (GstGLOverlay * overlay)
{
  const GstGLFuncs *gl = GST_GL_FILTER (overlay)->context->gl_vtable;

  gl->BindBuffer (GL_ARRAY_BUFFER, 0);

  gl->DisableVertexAttribArray (overlay->attr_position);
  gl->DisableVertexAttribArray (overlay->attr_texture);
}

static void
_bind_buffer (GstGLOverlay * overlay, GLuint vbo)
{
  const GstGLFuncs *gl = GST_GL_FILTER (overlay)->context->gl_vtable;

  gl->BindBuffer (GL_ARRAY_BUFFER, vbo);

  gl->EnableVertexAttribArray (overlay->attr_position);
  gl->EnableVertexAttribArray (overlay->attr_texture);

  gl->VertexAttribPointer (overlay->attr_position, 3, GL_FLOAT,
      GL_FALSE, 5 * sizeof (GLfloat), (void *) 0);
  gl->VertexAttribPointer (overlay->attr_texture, 2, GL_FLOAT,
      GL_FALSE, 5 * sizeof (GLfloat), (void *) (3 * sizeof (GLfloat)));
}

/* *INDENT-OFF* */
float v_vertices[] = {
/*|      Vertex     | TexCoord  |*/
  -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
   1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
   1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
  -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
};
/* *INDENT-ON* */

static void
gst_gl_overlay_callback (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (stuff);
  GstGLFilter *filter = GST_GL_FILTER (overlay);
  GstMapInfo map_info;
  guint image_tex;
  gboolean memory_mapped = FALSE;
  const GstGLFuncs *gl = filter->context->gl_vtable;

  GLushort indices[] = {
    0, 1, 2,
    0, 2, 3,
  };

#if GST_GL_HAVE_OPENGL
  if (gst_gl_context_get_gl_api (filter->context) & GST_GL_API_OPENGL) {

    gl->MatrixMode (GL_PROJECTION);
    gl->LoadIdentity ();
  }
#endif

  if (gst_gl_context_get_gl_api (filter->context) & GST_GL_API_OPENGL)
    gl->Enable (GL_TEXTURE_2D);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_use (overlay->shader);

  gst_gl_shader_set_uniform_1f (overlay->shader, "alpha", 1.0f);
  gst_gl_shader_set_uniform_1i (overlay->shader, "texture", 0);

  overlay->attr_position =
      gst_gl_shader_get_attribute_location (overlay->shader, "a_position");
  overlay->attr_texture =
      gst_gl_shader_get_attribute_location (overlay->shader, "a_texcoord");

  if (!overlay->vbo) {
    if (gl->GenVertexArrays) {
      gl->GenVertexArrays (1, &overlay->vao);
      gl->BindVertexArray (overlay->vao);
    }

    gl->GenBuffers (1, &overlay->vbo);
    gl->BindBuffer (GL_ARRAY_BUFFER, overlay->vbo);
    gl->BufferData (GL_ARRAY_BUFFER, 4 * 5 * sizeof (GLfloat), v_vertices,
        GL_STATIC_DRAW);

    if (gl->GenVertexArrays)
      _bind_buffer (overlay, overlay->vbo);
  }

  if (gl->GenVertexArrays)
    gl->BindVertexArray (overlay->vao);
  else
    _bind_buffer (overlay, overlay->vbo);

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

  if (!overlay->image_memory)
    goto out;

  if (!gst_memory_map ((GstMemory *) overlay->image_memory, &map_info,
          GST_MAP_READ | GST_MAP_GL) || map_info.data == NULL)
    goto out;

  memory_mapped = TRUE;
  image_tex = *(guint *) map_info.data;

  if (!overlay->overlay_vbo) {
    if (gl->GenVertexArrays) {
      gl->GenVertexArrays (1, &overlay->overlay_vao);
      gl->BindVertexArray (overlay->overlay_vao);
    }

    gl->GenBuffers (1, &overlay->overlay_vbo);
    gl->BindBuffer (GL_ARRAY_BUFFER, overlay->overlay_vbo);
    overlay->geometry_change = TRUE;
  }

  if (overlay->geometry_change) {
    gint render_width, render_height;
    gfloat x, y, image_width, image_height;

    /* *INDENT-OFF* */
    float vertices[] = {
     -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
      1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
      1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
     -1.0f,  1.0f, 0.0f, 0.0,  1.0f,
    };
    /* *INDENT-ON* */

    /* scale from [0, 1] -> [-1, 1] */
    x = ((gfloat) overlay->offset_x / (gfloat) overlay->window_width +
        overlay->relative_x) * 2.0f - 1.0;
    y = ((gfloat) overlay->offset_y / (gfloat) overlay->window_height +
        overlay->relative_y) * 2.0f - 1.0;
    /* scale from [0, 1] -> [0, 2] */
    render_width =
        overlay->overlay_width >
        0 ? overlay->overlay_width : overlay->image_width;
    render_height =
        overlay->overlay_height >
        0 ? overlay->overlay_height : overlay->image_height;
    image_width =
        ((gfloat) render_width / (gfloat) overlay->window_width) * 2.0f;
    image_height =
        ((gfloat) render_height / (gfloat) overlay->window_height) * 2.0f;

    vertices[0] = vertices[15] = x;
    vertices[5] = vertices[10] = x + image_width;
    vertices[1] = vertices[6] = y;
    vertices[11] = vertices[16] = y + image_height;

    gl->BufferData (GL_ARRAY_BUFFER, 4 * 5 * sizeof (GLfloat), vertices,
        GL_STATIC_DRAW);
  }

  if (gl->GenVertexArrays) {
    if (overlay->geometry_change)
      _bind_buffer (overlay, overlay->overlay_vbo);
    gl->BindVertexArray (overlay->overlay_vao);
  } else {
    _bind_buffer (overlay, overlay->overlay_vbo);
  }

  gl->BindTexture (GL_TEXTURE_2D, image_tex);
  gst_gl_shader_set_uniform_1f (overlay->shader, "alpha", overlay->alpha);

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

out:
  if (gl->GenVertexArrays) {
    gl->BindVertexArray (0);
  } else {
    _unbind_buffer (overlay);
  }

  gst_gl_context_clear_shader (filter->context);

  if (memory_mapped)
    gst_memory_unmap ((GstMemory *) overlay->image_memory, &map_info);

  overlay->geometry_change = FALSE;
}

static gboolean
gst_gl_overlay_filter_texture (GstGLFilter * filter, guint in_tex,
    guint out_tex)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (filter);

  if (overlay->location_has_changed) {
    if (overlay->image_memory) {
      gst_memory_unref ((GstMemory *) overlay->image_memory);
      overlay->image_memory = NULL;
    }
    if (overlay->location != NULL) {
      if ((overlay->type_file = gst_gl_overlay_load_png (filter)) == 0) {
        if ((overlay->type_file = gst_gl_overlay_load_jpeg (filter)) == 0) {
          return FALSE;
        }
      }
    }

    overlay->location_has_changed = FALSE;
  }

  gst_gl_filter_render_to_target (filter, TRUE, in_tex, out_tex,
      gst_gl_overlay_callback, overlay);

  return TRUE;
}

static void
gst_gl_overlay_before_transform (GstBaseTransform * trans, GstBuffer * outbuf)
{
  GstClockTime stream_time;

  stream_time = gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (outbuf));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT (trans), stream_time);
}

static void
user_warning_fn (png_structp png_ptr, png_const_charp warning_msg)
{
  g_warning ("%s\n", warning_msg);
}

#define LOAD_ERROR(msg) { GST_WARNING ("unable to load %s: %s", overlay->location, msg); return FALSE; }

static gint
gst_gl_overlay_load_jpeg (GstGLFilter * filter)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (filter);
  GstVideoInfo v_info;
  GstMapInfo map_info;
  FILE *fp = NULL;
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;
  JSAMPROW j;
  int i;

  fp = fopen (overlay->location, "rb");
  if (!fp) {
    g_error ("error: couldn't open file!\n");
    return 0;
  }
  jpeg_create_decompress (&cinfo);
  cinfo.err = jpeg_std_error (&jerr);
  jpeg_stdio_src (&cinfo, fp);
  jpeg_read_header (&cinfo, TRUE);
  jpeg_start_decompress (&cinfo);
  overlay->image_width = cinfo.image_width;
  overlay->image_height = cinfo.image_height;

  if (cinfo.num_components == 1)
    gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_Y444,
        overlay->image_width, overlay->image_height);
  else
    gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGB,
        overlay->image_width, overlay->image_height);

  overlay->image_memory =
      (GstGLMemory *) gst_gl_memory_alloc (filter->context, &v_info, 0);

  if (!gst_memory_map ((GstMemory *) overlay->image_memory, &map_info,
          GST_MAP_WRITE)) {
    LOAD_ERROR ("failed to map memory");
  }

  for (i = 0; i < overlay->image_height; ++i) {
    j = (map_info.data +
        (((int) overlay->image_height - (i +
                    1)) * (int) overlay->image_width * cinfo.num_components));
    jpeg_read_scanlines (&cinfo, &j, 1);
  }
  jpeg_finish_decompress (&cinfo);
  jpeg_destroy_decompress (&cinfo);
  gst_memory_unmap ((GstMemory *) overlay->image_memory, &map_info);
  fclose (fp);
  return 2;
}

static gint
gst_gl_overlay_load_png (GstGLFilter * filter)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (filter);
  GstVideoInfo v_info;
  GstMapInfo map_info;

  png_structp png_ptr;
  png_infop info_ptr;
  png_uint_32 width = 0;
  png_uint_32 height = 0;
  gint bit_depth = 0;
  gint color_type = 0;
  gint interlace_type = 0;
  png_FILE_p fp = NULL;
  guint y = 0;
  guchar **rows = NULL;
  gint filler;
  png_byte magic[8];
  gint n_read;

  if (!filter->context)
    return 1;

  if ((fp = fopen (overlay->location, "rb")) == NULL)
    LOAD_ERROR ("file not found");

  /* Read magic number */
  n_read = fread (magic, 1, sizeof (magic), fp);
  if (n_read != sizeof (magic)) {
    fclose (fp);
    LOAD_ERROR ("can't read PNG magic number");
  }

  /* Check for valid magic number */
  if (png_sig_cmp (magic, 0, sizeof (magic))) {
    fclose (fp);
    LOAD_ERROR ("not a valid PNG image");
  }

  png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if (png_ptr == NULL) {
    fclose (fp);
    LOAD_ERROR ("failed to initialize the png_struct");
  }

  png_set_error_fn (png_ptr, NULL, NULL, user_warning_fn);

  info_ptr = png_create_info_struct (png_ptr);
  if (info_ptr == NULL) {
    fclose (fp);
    png_destroy_read_struct (&png_ptr, png_infopp_NULL, png_infopp_NULL);
    LOAD_ERROR ("failed to initialize the memory for image information");
  }

  png_init_io (png_ptr, fp);

  png_set_sig_bytes (png_ptr, sizeof (magic));

  png_read_info (png_ptr, info_ptr);

  png_get_IHDR (png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
      &interlace_type, int_p_NULL, int_p_NULL);

  if (color_type == PNG_COLOR_TYPE_RGB) {
    filler = 0xff;
    png_set_filler (png_ptr, filler, PNG_FILLER_AFTER);
    color_type = PNG_COLOR_TYPE_RGB_ALPHA;
  }

  if (color_type != PNG_COLOR_TYPE_RGB_ALPHA) {
    fclose (fp);
    png_destroy_read_struct (&png_ptr, png_infopp_NULL, png_infopp_NULL);
    LOAD_ERROR ("color type is not rgb");
  }

  overlay->image_width = width;
  overlay->image_height = height;

  gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, width, height);
  overlay->image_memory =
      (GstGLMemory *) gst_gl_memory_alloc (filter->context, &v_info, 0);

  if (!gst_memory_map ((GstMemory *) overlay->image_memory, &map_info,
          GST_MAP_WRITE)) {
    LOAD_ERROR ("failed to map memory");
  }
  rows = (guchar **) malloc (sizeof (guchar *) * height);

  for (y = 0; y < height; ++y)
    rows[y] = (guchar *) (map_info.data + y * width * 4);

  png_read_image (png_ptr, rows);

  free (rows);
  gst_memory_unmap ((GstMemory *) overlay->image_memory, &map_info);

  png_read_end (png_ptr, info_ptr);
  png_destroy_read_struct (&png_ptr, &info_ptr, png_infopp_NULL);
  fclose (fp);

  return 1;
}
