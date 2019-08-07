/* GStreamer
 * Copyright (C) <2018-2019> Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_CUDA_UTILS_H__
#define __GST_CUDA_UTILS_H__

#include <gst/gst.h>
#include "gstcudaloader.h"
#include "gstcudacontext.h"

G_BEGIN_DECLS

#ifndef GST_DISABLE_GST_DEBUG
static inline gboolean
_gst_cuda_debug(CUresult result, GstDebugCategory * category,
    const gchar * file, const gchar * function, gint line)
{
  const gchar *_error_name, *_error_text;
  if (result != CUDA_SUCCESS) {
    CuGetErrorName (result, &_error_name);
    CuGetErrorString (result, &_error_text);
    gst_debug_log (category, GST_LEVEL_WARNING, file, function, line,
        NULL, "CUDA call failed: %s, %s", _error_name, _error_text);

    return FALSE;
  }

  return TRUE;
}

/**
 * gst_cuda_result:
 * @result: CUDA device API return code #CUresult
 *
 * Returns: %TRUE if CUDA device API call result is CUDA_SUCCESS
 */
#define gst_cuda_result(result) \
  _gst_cuda_debug(result, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__)
#else

static inline gboolean
_gst_cuda_debug(CUresult result, GstDebugCategory * category,
    const gchar * file, const gchar * function, gint line)
{
  return result == CUDA_SUCCESS;
}

/**
 * gst_cuda_result:
 * @result: CUDA device API return code #CUresult
 *
 * Returns: %TRUE if CUDA device API call result is CUDA_SUCCESS
 */
#define gst_cuda_result(result) \
  _gst_cuda_debug(result, NULL, __FILE__, GST_FUNCTION, __LINE__)
#endif

G_GNUC_INTERNAL
gboolean        gst_cuda_ensure_element_context (GstElement * element,
                                                 gint device_id,
                                                 GstCudaContext ** cuda_ctx);

G_GNUC_INTERNAL
gboolean        gst_cuda_handle_set_context     (GstElement * element,
                                                 GstContext * context,
                                                 gint device_id,
                                                 GstCudaContext ** cuda_ctx);

G_GNUC_INTERNAL
gboolean        gst_cuda_handle_context_query   (GstElement * element,
                                                 GstQuery * query,
                                                 GstCudaContext * cuda_ctx);

G_GNUC_INTERNAL
GstContext *    gst_context_new_cuda_context    (GstCudaContext * context);


G_END_DECLS

#endif /* __GST_CUDA_UTILS_H__ */
