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

#include "gstvkdisplay.h"

#if GST_VULKAN_HAVE_WINDOW_XCB
#include "xcb/gstvkdisplay_xcb.h"
#endif
#if GST_VULKAN_HAVE_WINDOW_WAYLAND
#include "wayland/gstvkdisplay_wayland.h"
#endif
#if GST_VULKAN_HAVE_WINDOW_COCOA
#include "cocoa/gstvkdisplay_cocoa.h"
#endif
#if GST_VULKAN_HAVE_WINDOW_IOS
#include "ios/gstvkdisplay_ios.h"
#endif

/**
 * SECTION:vkdisplay
 * @title: GstVulkanDisplay
 * @short_description: window system display
 * @see_also: #GstVulkanInstance, #GstVulkanWindow
 *
 * A #GstVulkanDisplay represents a connection to a display server on the platform
 */

GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);
#define GST_CAT_DEFAULT gst_vulkan_display_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void
_init_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkandisplay", 0,
        "Vulkan display");
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");
    g_once_init_leave (&_init, 1);
  }
}

enum
{
  SIGNAL_0,
  CREATE_CONTEXT,
  LAST_SIGNAL
};

/* static guint gst_vulkan_display_signals[LAST_SIGNAL] = { 0 }; */

static void gst_vulkan_display_finalize (GObject * object);
static gpointer gst_vulkan_display_default_get_handle (GstVulkanDisplay *
    display);
static GstVulkanWindow
    * gst_vulkan_display_default_create_window (GstVulkanDisplay * display);

struct _GstVulkanDisplayPrivate
{
  GThread *event_thread;

  GMutex thread_lock;
  GCond thread_cond;
};

G_DEFINE_TYPE_WITH_CODE (GstVulkanDisplay, gst_vulkan_display, GST_TYPE_OBJECT,
    G_ADD_PRIVATE (GstVulkanDisplay) _init_debug ());

static gpointer
_event_thread_main (GstVulkanDisplay * display)
{
  g_mutex_lock (&display->priv->thread_lock);

  display->main_context = g_main_context_new ();
  display->main_loop = g_main_loop_new (display->main_context, FALSE);

  g_cond_broadcast (&display->priv->thread_cond);
  g_mutex_unlock (&display->priv->thread_lock);

  g_main_loop_run (display->main_loop);

  g_mutex_lock (&display->priv->thread_lock);

  g_main_loop_unref (display->main_loop);
  g_main_context_unref (display->main_context);

  display->main_loop = NULL;
  display->main_context = NULL;

  g_cond_broadcast (&display->priv->thread_cond);
  g_mutex_unlock (&display->priv->thread_lock);

  return NULL;
}

static void
gst_vulkan_display_class_init (GstVulkanDisplayClass * klass)
{
  klass->get_handle = gst_vulkan_display_default_get_handle;
  klass->create_window = gst_vulkan_display_default_create_window;

  G_OBJECT_CLASS (klass)->finalize = gst_vulkan_display_finalize;
}

static void
gst_vulkan_display_init (GstVulkanDisplay * display)
{
  display->priv = gst_vulkan_display_get_instance_private (display);
  display->type = GST_VULKAN_DISPLAY_TYPE_ANY;

  g_mutex_init (&display->priv->thread_lock);
  g_cond_init (&display->priv->thread_cond);

  display->priv->event_thread = g_thread_new ("vkdisplay-event",
      (GThreadFunc) _event_thread_main, display);

  g_mutex_lock (&display->priv->thread_lock);
  while (!display->main_loop)
    g_cond_wait (&display->priv->thread_cond, &display->priv->thread_lock);
  g_mutex_unlock (&display->priv->thread_lock);
}

static void
gst_vulkan_display_finalize (GObject * object)
{
  GstVulkanDisplay *display = GST_VULKAN_DISPLAY (object);

  g_mutex_lock (&display->priv->thread_lock);

  if (display->main_loop)
    g_main_loop_quit (display->main_loop);

  while (display->main_loop)
    g_cond_wait (&display->priv->thread_cond, &display->priv->thread_lock);

  if (display->priv->event_thread)
    g_thread_unref (display->priv->event_thread);
  display->priv->event_thread = NULL;
  g_mutex_unlock (&display->priv->thread_lock);

  if (display->main_context && display->event_source) {
    g_source_destroy (display->event_source);
    g_source_unref (display->event_source);
  }
  display->event_source = NULL;

  if (display->instance) {
    gst_object_unref (display->instance);
  }

  G_OBJECT_CLASS (gst_vulkan_display_parent_class)->finalize (object);
}

/**
 * gst_vulkan_display_new_with_type:
 * @instance: a #GstVulkanInstance
 * @type: the #GstVulkanDisplayType to create
 *
 * Returns: (transfer full): a new #GstVulkanDisplay or %NULL if e.g. @type is
 * unsupported
 *
 * Since: 1.18
 */
GstVulkanDisplay *
gst_vulkan_display_new_with_type (GstVulkanInstance * instance,
    GstVulkanDisplayType type)
{
  GstVulkanDisplay *display = NULL;

  _init_debug ();

#if GST_VULKAN_HAVE_WINDOW_XCB
  if (!display && type & GST_VULKAN_DISPLAY_TYPE_XCB) {
    display = GST_VULKAN_DISPLAY (gst_vulkan_display_xcb_new (NULL));
  }
#endif
#if GST_VULKAN_HAVE_WINDOW_WAYLAND
  if (!display && type & GST_VULKAN_DISPLAY_TYPE_WAYLAND) {
    display = GST_VULKAN_DISPLAY (gst_vulkan_display_wayland_new (NULL));
  }
#endif

  if (display)
    display->instance = gst_object_ref (instance);

  return display;
}

/**
 * gst_vulkan_display_new:
 *
 * Returns: (transfer full): a new #GstVulkanDisplay
 *
 * Since: 1.18
 */
GstVulkanDisplay *
gst_vulkan_display_new (GstVulkanInstance * instance)
{
  GstVulkanDisplayType type;
  GstVulkanDisplay *display = NULL;

  type = gst_vulkan_display_choose_type (instance);
  display = gst_vulkan_display_new_with_type (instance, type);

  if (!display) {
    /* subclass returned a NULL display */
    GST_FIXME ("creating dummy display");

    display = g_object_new (GST_TYPE_VULKAN_DISPLAY, NULL);
    gst_object_ref_sink (display);
    display->instance = gst_object_ref (instance);
  }

  return display;
}

/**
 * gst_vulkan_display_get_handle:
 * @display: a #GstVulkanDisplay
 *
 * Returns: the winsys specific handle of @display
 *
 * Since: 1.18
 */
gpointer
gst_vulkan_display_get_handle (GstVulkanDisplay * display)
{
  GstVulkanDisplayClass *klass;

  g_return_val_if_fail (GST_IS_VULKAN_DISPLAY (display), NULL);
  klass = GST_VULKAN_DISPLAY_GET_CLASS (display);
  g_return_val_if_fail (klass->get_handle != NULL, NULL);

  return klass->get_handle (display);
}

static gpointer
gst_vulkan_display_default_get_handle (GstVulkanDisplay * display)
{
  return 0;
}

/**
 * gst_vulkan_display_get_handle_type:
 * @display: a #GstVulkanDisplay
 *
 * Returns: the #GstVulkanDisplayType of @display
 *
 * Since: 1.18
 */
GstVulkanDisplayType
gst_vulkan_display_get_handle_type (GstVulkanDisplay * display)
{
  g_return_val_if_fail (GST_IS_VULKAN_DISPLAY (display),
      GST_VULKAN_DISPLAY_TYPE_NONE);

  return display->type;
}

/**
 * gst_vulkan_display_create_window:
 * @display: a #GstVulkanDisplay
 *
 * Returns: (transfer full): a new #GstVulkanWindow for @display or %NULL.
 *
 * Since: 1.18
 */
GstVulkanWindow *
gst_vulkan_display_create_window (GstVulkanDisplay * display)
{
  GstVulkanDisplayClass *klass;
  GstVulkanWindow *window;

  g_return_val_if_fail (GST_IS_VULKAN_DISPLAY (display), NULL);
  klass = GST_VULKAN_DISPLAY_GET_CLASS (display);
  g_return_val_if_fail (klass->create_window != NULL, NULL);

  window = klass->create_window (display);

  if (window) {
    GWeakRef *ref = g_new0 (GWeakRef, 1);

    g_weak_ref_set (ref, window);

    GST_OBJECT_LOCK (display);
    display->windows = g_list_prepend (display->windows, ref);
    GST_OBJECT_UNLOCK (display);
  }

  return window;
}

static GstVulkanWindow *
gst_vulkan_display_default_create_window (GstVulkanDisplay * display)
{
  return gst_vulkan_window_new (display);
}

static gint
_compare_vulkan_window (GWeakRef * ref, GstVulkanWindow * window)
{
  GstVulkanWindow *other = g_weak_ref_get (ref);
  gboolean equal = window == other;

  gst_object_unref (other);

  return !equal;
}

static GList *
_find_window_list_item (GstVulkanDisplay * display, GstVulkanWindow * window)
{
  GList *l;

  if (!window)
    return NULL;

  l = g_list_find_custom (display->windows, window,
      (GCompareFunc) _compare_vulkan_window);

  return l;
}

/**
 * gst_vulkan_display_remove_window:
 * @display: a #GstVUlkanDisplay:
 * @window: the #GstVulkanWindow to remove
 *
 * Returns: whether the window was successfully removed
 *
 * Since: 1.18
 */
gboolean
gst_vulkan_display_remove_window (GstVulkanDisplay * display,
    GstVulkanWindow * window)
{
  gboolean ret = FALSE;
  GList *l;

  GST_OBJECT_LOCK (display);
  l = _find_window_list_item (display, window);
  if (l) {
    display->windows = g_list_delete_link (display->windows, l);
    g_weak_ref_clear (l->data);
    g_free (l->data);
    ret = TRUE;
  }
  GST_OBJECT_UNLOCK (display);

  return ret;
}

/**
 * gst_context_set_vulkan_display:
 * @context: a #GstContext
 * @display: a #GstVulkanDisplay
 *
 * Sets @display on @context
 *
 * Since: 1.18
 */
void
gst_context_set_vulkan_display (GstContext * context,
    GstVulkanDisplay * display)
{
  GstStructure *s;

  g_return_if_fail (context != NULL);
  g_return_if_fail (gst_context_is_writable (context));

  if (display)
    GST_CAT_LOG (GST_CAT_CONTEXT,
        "setting GstVulkanDisplay(%" GST_PTR_FORMAT ") on context(%"
        GST_PTR_FORMAT ")", display, context);

  s = gst_context_writable_structure (context);
  gst_structure_set (s, GST_VULKAN_DISPLAY_CONTEXT_TYPE_STR,
      GST_TYPE_VULKAN_DISPLAY, display, NULL);
}

/**
 * gst_context_get_vulkan_display:
 * @context: a #GstContext
 * @display: resulting #GstVulkanDisplay
 *
 * Returns: Whether @display was in @context
 *
 * Since: 1.18
 */
gboolean
gst_context_get_vulkan_display (GstContext * context,
    GstVulkanDisplay ** display)
{
  const GstStructure *s;
  gboolean ret;

  g_return_val_if_fail (display != NULL, FALSE);
  g_return_val_if_fail (context != NULL, FALSE);

  s = gst_context_get_structure (context);
  ret = gst_structure_get (s, GST_VULKAN_DISPLAY_CONTEXT_TYPE_STR,
      GST_TYPE_VULKAN_DISPLAY, display, NULL);

  GST_CAT_LOG (GST_CAT_CONTEXT, "got GstVulkanDisplay(%" GST_PTR_FORMAT
      ") from context(%" GST_PTR_FORMAT ")", *display, context);

  return ret;
}

/**
 * gst_vulkan_display_choose_type:
 * @instance: a #GstVulkanInstance
 *
 * This function will read the %GST_VULKAN_WINDOW environment variable for
 * a user choice or choose the first supported implementation.
 *
 * Returns: the default #GstVulkanDisplayType #GstVulkanInstance will choose
 *          on creation
 *
 * Since: 1.18
 */
GstVulkanDisplayType
gst_vulkan_display_choose_type (GstVulkanInstance * instance)
{
  const gchar *window_str;
  GstVulkanDisplayType type = GST_VULKAN_DISPLAY_TYPE_NONE;
  GstVulkanDisplayType first_supported = GST_VULKAN_DISPLAY_TYPE_NONE;

  window_str = g_getenv ("GST_VULKAN_WINDOW");

  /* FIXME: enumerate instance extensions for the supported winsys' */

#define CHOOSE_WINSYS(lname,uname) \
  G_STMT_START { \
    if (!type && g_strcmp0 (window_str, G_STRINGIFY (lname)) == 0) { \
      type = G_PASTE(GST_VULKAN_DISPLAY_TYPE_,uname); \
    } \
    if (!first_supported) \
      first_supported = G_PASTE(GST_VULKAN_DISPLAY_TYPE_,uname); \
  } G_STMT_END

#if GST_VULKAN_HAVE_WINDOW_XCB
  CHOOSE_WINSYS (xcb, XCB);
#endif
#if GST_VULKAN_HAVE_WINDOW_WAYLAND
  CHOOSE_WINSYS (wayland, WAYLAND);
#endif
#if GST_VULKAN_HAVE_WINDOW_COCOA
  CHOOSE_WINSYS (cocoa, COCOA);
#endif
#if GST_VULKAN_HAVE_WINDOW_IOS
  CHOOSE_WINSYS (ios, IOS);
#endif

#undef CHOOSE_WINSYS

#if GST_VULKAN_HAVE_WINDOW_WIN32
  /* CHOOSE_WINSYS macro doesn't work with "WIN32" */
  if (!type && g_strcmp0 (window_str, "win32") == 0) {
    type = GST_VULKAN_DISPLAY_TYPE_WIN32;
  }

  if (!first_supported)
    first_supported = GST_VULKAN_DISPLAY_TYPE_WIN32;
#endif

  if (type)
    return type;

  if (first_supported)
    return first_supported;

  return GST_VULKAN_DISPLAY_TYPE_NONE;
}

/**
 * gst_vulkan_display_type_to_extension_string:
 * @type: a #GstVulkanDisplayType
 *
 * Returns: the Vulkan extension string required for creating a VkSurfaceKHR
 * using a window system handle or %NULL
 *
 * Since: 1.18
 */
const gchar *
gst_vulkan_display_type_to_extension_string (GstVulkanDisplayType type)
{
  if (type == GST_VULKAN_DISPLAY_TYPE_NONE)
    return NULL;

#if GST_VULKAN_HAVE_WINDOW_XCB
  if (type & GST_VULKAN_DISPLAY_TYPE_XCB)
    return VK_KHR_XCB_SURFACE_EXTENSION_NAME;
#endif

#if GST_VULKAN_HAVE_WINDOW_WAYLAND
  if (type & GST_VULKAN_DISPLAY_TYPE_WAYLAND)
    return VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
#endif
#if GST_VULKAN_HAVE_WINDOW_COCOA
  if (type & GST_VULKAN_DISPLAY_TYPE_COCOA)
    return VK_MVK_MACOS_SURFACE_EXTENSION_NAME;
#endif
#if GST_VULKAN_HAVE_WINDOW_IOS
  if (type & GST_VULKAN_DISPLAY_TYPE_IOS)
    return VK_MVK_IOS_SURFACE_EXTENSION_NAME;
#endif
#if GST_VULKAN_HAVE_WINDOW_WIN32
  if (type & GST_VULKAN_DISPLAY_TYPE_WIN32)
    return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#endif

  return NULL;
}

/**
 * gst_vulkan_display_handle_context_query:
 * @element: a #GstElement
 * @query: a #GstQuery of type #GST_QUERY_CONTEXT
 * @display: the #GstVulkanDisplay
 *
 * If a #GstVulkanDisplay is requested in @query, sets @device as the reply.
 *
 * Intended for use with element query handlers to respond to #GST_QUERY_CONTEXT
 * for a #GstVulkanDisplay.
 *
 * Returns: whether @query was responded to with @display
 *
 * Since: 1.18
 */
gboolean
gst_vulkan_display_handle_context_query (GstElement * element, GstQuery * query,
    GstVulkanDisplay ** display)
{
  gboolean res = FALSE;
  const gchar *context_type;
  GstContext *context, *old_context;

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (query != NULL, FALSE);
  g_return_val_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT, FALSE);
  g_return_val_if_fail (display != NULL, FALSE);

  gst_query_parse_context_type (query, &context_type);

  if (g_strcmp0 (context_type, GST_VULKAN_DISPLAY_CONTEXT_TYPE_STR) == 0) {
    gst_query_parse_context (query, &old_context);

    if (old_context)
      context = gst_context_copy (old_context);
    else
      context = gst_context_new (GST_VULKAN_DISPLAY_CONTEXT_TYPE_STR, TRUE);

    gst_context_set_vulkan_display (context, *display);
    gst_query_set_context (query, context);
    gst_context_unref (context);

    res = *display != NULL;
  }

  return res;
}

/**
 * gst_vulkan_display_run_context_query:
 * @element: a #GstElement
 * @display: (inout): a #GstVulkanDisplay
 *
 * Attempt to retrieve a #GstVulkanDisplay using #GST_QUERY_CONTEXT from the
 * surrounding elements of @element.
 *
 * Returns: whether @display contains a valid #GstVulkanDisplay
 *
 * Since: 1.18
 */
gboolean
gst_vulkan_display_run_context_query (GstElement * element,
    GstVulkanDisplay ** display)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (display != NULL, FALSE);

  if (*display && GST_IS_VULKAN_DISPLAY (*display))
    return TRUE;

  gst_vulkan_global_context_query (element,
      GST_VULKAN_DISPLAY_CONTEXT_TYPE_STR);

  GST_DEBUG_OBJECT (element, "found display %p", *display);

  if (*display)
    return TRUE;

  return FALSE;
}
