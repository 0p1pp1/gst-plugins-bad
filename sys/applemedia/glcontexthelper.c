/* GStreamer
 * Copyright (C) 2016 Alessandro Decina <alessandro.d@gmail.com>
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

#include "glcontexthelper.h"

static GstGLContext *
_find_local_gl_context (GstGLContextHelper * ctxh)
{
  GstQuery *query;
  GstContext *context;
  GstGLContext *gl_context = NULL;
  const GstStructure *s;

  g_return_val_if_fail (ctxh != NULL, FALSE);

  query = gst_query_new_context ("gst.gl.local_context");
  if (gst_gl_run_query (ctxh->element, query, GST_PAD_SRC)) {
    gst_query_parse_context (query, &context);
    if (context) {
      s = gst_context_get_structure (context);
      gst_structure_get (s, "context", GST_GL_TYPE_CONTEXT, &gl_context, NULL);
    }
  }
  if (!gl_context && gst_gl_run_query (ctxh->element, query, GST_PAD_SINK)) {
    gst_query_parse_context (query, &context);
    if (context) {
      s = gst_context_get_structure (context);
      gst_structure_get (s, "context", GST_GL_TYPE_CONTEXT, &gl_context, NULL);
    }
  }

  GST_DEBUG_OBJECT (ctxh->element, "found local context %p", gl_context);

  gst_query_unref (query);

  return gl_context;
}

GstGLContextHelper *
gst_gl_context_helper_new (GstElement * element)
{
  GstGLContextHelper *ctxh = g_new0 (GstGLContextHelper, 1);
  ctxh->element = gst_object_ref (element);

  return ctxh;
}

void
gst_gl_context_helper_free (GstGLContextHelper * ctxh)
{
  g_return_if_fail (ctxh != NULL);

  gst_object_unref (ctxh->element);

  if (ctxh->display)
    gst_object_unref (ctxh->display);

  if (ctxh->context)
    gst_object_unref (ctxh->context);

  if (ctxh->other_context)
    gst_object_unref (ctxh->other_context);

  g_free (ctxh);
}

void
gst_gl_context_helper_ensure_context (GstGLContextHelper * ctxh)
{
  GError *error = NULL;
  GstGLContext *context;

  g_return_if_fail (ctxh != NULL);

  if (!ctxh->display)
    gst_gl_ensure_element_data (ctxh->element, &ctxh->display,
        &ctxh->other_context);

  context = _find_local_gl_context (ctxh);
  if (context) {
    GST_INFO_OBJECT (ctxh->element, "found local context %p, old context %p",
        context, ctxh->context);
    if (ctxh->context)
      gst_object_unref (ctxh->context);
    ctxh->context = context;
  }

  if (!ctxh->context) {
    GST_OBJECT_LOCK (ctxh->display);
    do {
      if (ctxh->context)
        gst_object_unref (ctxh->context);
      ctxh->context =
          gst_gl_display_get_gl_context_for_thread (ctxh->display, NULL);
      if (!ctxh->context) {
        if (!gst_gl_display_create_context (ctxh->display,
                ctxh->other_context, &ctxh->context, &error)) {
          GST_OBJECT_UNLOCK (ctxh->display);
          goto context_error;
        }
      }
    } while (!gst_gl_display_add_context (ctxh->display, ctxh->context));
    GST_OBJECT_UNLOCK (ctxh->display);
  }

  return;

context_error:
  {
    GST_ELEMENT_ERROR (ctxh->element, RESOURCE, NOT_FOUND, ("%s",
            error->message), (NULL));
    g_clear_error (&error);

    return;
  }
}
