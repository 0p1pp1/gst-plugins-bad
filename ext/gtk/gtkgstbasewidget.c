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

#include <stdio.h>

#include "gtkgstbasewidget.h"

#define DEFAULT_FORCE_ASPECT_RATIO  TRUE
#define DEFAULT_PAR_N               0
#define DEFAULT_PAR_D               1
#define DEFAULT_IGNORE_ALPHA        TRUE

enum
{
  PROP_0,
  PROP_FORCE_ASPECT_RATIO,
  PROP_PIXEL_ASPECT_RATIO,
  PROP_IGNORE_ALPHA,
};

static void
gtk_gst_base_widget_get_preferred_width (GtkWidget * widget, gint * min,
    gint * natural)
{
  GtkGstBaseWidget *gst_widget = (GtkGstBaseWidget *) widget;
  gint video_width = gst_widget->display_width;

  if (!gst_widget->negotiated)
    video_width = 10;

  if (min)
    *min = 1;
  if (natural)
    *natural = video_width;
}

static void
gtk_gst_base_widget_get_preferred_height (GtkWidget * widget, gint * min,
    gint * natural)
{
  GtkGstBaseWidget *gst_widget = (GtkGstBaseWidget *) widget;
  gint video_height = gst_widget->display_height;

  if (!gst_widget->negotiated)
    video_height = 10;

  if (min)
    *min = 1;
  if (natural)
    *natural = video_height;
}

static void
gtk_gst_base_widget_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GtkGstBaseWidget *gtk_widget = GTK_GST_BASE_WIDGET (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      gtk_widget->force_aspect_ratio = g_value_get_boolean (value);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      gtk_widget->par_n = gst_value_get_fraction_numerator (value);
      gtk_widget->par_d = gst_value_get_fraction_denominator (value);
      break;
    case PROP_IGNORE_ALPHA:
      gtk_widget->ignore_alpha = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gtk_gst_base_widget_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GtkGstBaseWidget *gtk_widget = GTK_GST_BASE_WIDGET (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, gtk_widget->force_aspect_ratio);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      gst_value_set_fraction (value, gtk_widget->par_n, gtk_widget->par_d);
      break;
    case PROP_IGNORE_ALPHA:
      g_value_set_boolean (value, gtk_widget->ignore_alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
_calculate_par (GtkGstBaseWidget * widget, GstVideoInfo * info)
{
  gboolean ok;
  gint width, height;
  gint par_n, par_d;
  gint display_par_n, display_par_d;

  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);

  par_n = GST_VIDEO_INFO_PAR_N (info);
  par_d = GST_VIDEO_INFO_PAR_D (info);

  if (!par_n)
    par_n = 1;

  /* get display's PAR */
  if (widget->par_n != 0 && widget->par_d != 0) {
    display_par_n = widget->par_n;
    display_par_d = widget->par_d;
  } else {
    display_par_n = 1;
    display_par_d = 1;
  }


  ok = gst_video_calculate_display_ratio (&widget->display_ratio_num,
      &widget->display_ratio_den, width, height, par_n, par_d, display_par_n,
      display_par_d);

  if (ok) {
    GST_LOG ("PAR: %u/%u DAR:%u/%u", par_n, par_d, display_par_n,
        display_par_d);
    return TRUE;
  }

  return FALSE;
}

static void
_apply_par (GtkGstBaseWidget * widget)
{
  guint display_ratio_num, display_ratio_den;
  gint width, height;

  width = GST_VIDEO_INFO_WIDTH (&widget->v_info);
  height = GST_VIDEO_INFO_HEIGHT (&widget->v_info);

  display_ratio_num = widget->display_ratio_num;
  display_ratio_den = widget->display_ratio_den;

  if (height % display_ratio_den == 0) {
    GST_DEBUG ("keeping video height");
    widget->display_width = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    widget->display_height = height;
  } else if (width % display_ratio_num == 0) {
    GST_DEBUG ("keeping video width");
    widget->display_width = width;
    widget->display_height = (guint)
        gst_util_uint64_scale_int (width, display_ratio_den, display_ratio_num);
  } else {
    GST_DEBUG ("approximating while keeping video height");
    widget->display_width = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    widget->display_height = height;
  }

  GST_DEBUG ("scaling to %dx%d", widget->display_width, widget->display_height);
}

/* note, buffer is not refrence, it's only passed for pointer comparision */
static gboolean
_queue_draw (GtkGstBaseWidget * widget)
{
  GTK_GST_BASE_WIDGET_LOCK (widget);
  widget->draw_id = 0;

  if (widget->pending_resize) {
    widget->pending_resize = FALSE;

    if (widget->reset)
      widget->reset (widget);

    widget->v_info = widget->pending_v_info;
    widget->negotiated = TRUE;
    widget->new_buffer = TRUE;

    _apply_par (widget);

    gtk_widget_queue_resize (GTK_WIDGET (widget));
  } else {
    gtk_widget_queue_draw (GTK_WIDGET (widget));
  }

  GTK_GST_BASE_WIDGET_UNLOCK (widget);

  return G_SOURCE_REMOVE;
}

static const gchar *
_gdk_key_to_navigation_string (guint keyval)
{
  /* TODO: expand */
  switch (keyval) {
#define KEY(key) case GDK_KEY_ ## key: return G_STRINGIFY(key)
      KEY (Up);
      KEY (Down);
      KEY (Left);
      KEY (Right);
      KEY (Home);
      KEY (End);
#undef KEY
    default:
      return NULL;
  }
}

static gboolean
gtk_gst_base_widget_key_event (GtkWidget * widget, GdkEventKey * event)
{
  GtkGstBaseWidget *base_widget = GTK_GST_BASE_WIDGET (widget);
  GstElement *element;

  if ((element = g_weak_ref_get (&base_widget->element))) {
    if (GST_IS_NAVIGATION (element)) {
      const gchar *str = _gdk_key_to_navigation_string (event->keyval);
      const gchar *key_type =
          event->type == GDK_KEY_PRESS ? "key-press" : "key-release";

      if (!str)
        str = event->string;

      gst_navigation_send_key_event (GST_NAVIGATION (element), key_type, str);
    }
    g_object_unref (element);
  }

  return TRUE;
}

static gboolean
gtk_gst_base_widget_button_event (GtkWidget * widget, GdkEventButton * event)
{
  GtkGstBaseWidget *base_widget = GTK_GST_BASE_WIDGET (widget);
  GstElement *element;

  if ((element = g_weak_ref_get (&base_widget->element))) {
    if (GST_IS_NAVIGATION (element)) {
      const gchar *key_type =
          event->type ==
          GDK_BUTTON_PRESS ? "mouse-button-press" : "mouse-button-release";

      gst_navigation_send_mouse_event (GST_NAVIGATION (element), key_type,
          event->button, event->x, event->y);
    }
    g_object_unref (element);
  }

  return TRUE;
}

static gboolean
gtk_gst_base_widget_motion_event (GtkWidget * widget, GdkEventMotion * event)
{
  GtkGstBaseWidget *base_widget = GTK_GST_BASE_WIDGET (widget);
  GstElement *element;

  if ((element = g_weak_ref_get (&base_widget->element))) {
    if (GST_IS_NAVIGATION (element)) {
      gst_navigation_send_mouse_event (GST_NAVIGATION (element), "motion-move",
          0, event->x, event->y);
    }
    g_object_unref (element);
  }

  return TRUE;
}

void
gtk_gst_base_widget_class_init (GtkGstBaseWidgetClass * klass)
{
  GObjectClass *gobject_klass = (GObjectClass *) klass;
  GtkWidgetClass *widget_klass = (GtkWidgetClass *) klass;

  gobject_klass->set_property = gtk_gst_base_widget_set_property;
  gobject_klass->get_property = gtk_gst_base_widget_get_property;

  g_object_class_install_property (gobject_klass, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_PIXEL_ASPECT_RATIO,
      gst_param_spec_fraction ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", DEFAULT_PAR_N, DEFAULT_PAR_D,
          G_MAXINT, 1, 1, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_IGNORE_ALPHA,
      g_param_spec_boolean ("ignore-alpha", "Ignore Alpha",
          "When enabled, alpha will be ignored and converted to black",
          DEFAULT_IGNORE_ALPHA, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  widget_klass->get_preferred_width = gtk_gst_base_widget_get_preferred_width;
  widget_klass->get_preferred_height = gtk_gst_base_widget_get_preferred_height;
  widget_klass->key_press_event = gtk_gst_base_widget_key_event;
  widget_klass->key_release_event = gtk_gst_base_widget_key_event;
  widget_klass->button_press_event = gtk_gst_base_widget_button_event;
  widget_klass->button_release_event = gtk_gst_base_widget_button_event;
  widget_klass->motion_notify_event = gtk_gst_base_widget_motion_event;
}

void
gtk_gst_base_widget_init (GtkGstBaseWidget * widget)
{
  int event_mask;

  widget->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  widget->par_n = DEFAULT_PAR_N;
  widget->par_d = DEFAULT_PAR_D;
  widget->ignore_alpha = DEFAULT_IGNORE_ALPHA;

  gst_video_info_init (&widget->v_info);
  gst_video_info_init (&widget->pending_v_info);

  g_weak_ref_init (&widget->element, NULL);
  g_mutex_init (&widget->lock);

  gtk_widget_set_can_focus (GTK_WIDGET (widget), TRUE);
  event_mask = gtk_widget_get_events (GTK_WIDGET (widget));
  event_mask |= GDK_KEY_PRESS_MASK
      | GDK_KEY_RELEASE_MASK
      | GDK_BUTTON_PRESS_MASK
      | GDK_BUTTON_RELEASE_MASK
      | GDK_POINTER_MOTION_MASK | GDK_BUTTON_MOTION_MASK;
  gtk_widget_set_events (GTK_WIDGET (widget), event_mask);
}

void
gtk_gst_base_widget_finalize (GObject * object)
{
  GtkGstBaseWidget *widget = GTK_GST_BASE_WIDGET (object);

  gst_buffer_replace (&widget->buffer, NULL);
  g_mutex_clear (&widget->lock);
  g_weak_ref_clear (&widget->element);

  if (widget->draw_id)
    g_source_remove (widget->draw_id);
}

void
gtk_gst_base_widget_set_element (GtkGstBaseWidget * widget,
    GstElement * element)
{
  g_weak_ref_set (&widget->element, element);
}

gboolean
gtk_gst_base_widget_set_format (GtkGstBaseWidget * widget,
    GstVideoInfo * v_info)
{
  GTK_GST_BASE_WIDGET_LOCK (widget);

  if (gst_video_info_is_equal (&widget->v_info, v_info)) {
    GTK_GST_BASE_WIDGET_UNLOCK (widget);
    return TRUE;
  }

  if (!_calculate_par (widget, v_info)) {
    GTK_GST_BASE_WIDGET_UNLOCK (widget);
    return FALSE;
  }

  widget->pending_resize = TRUE;
  widget->pending_v_info = *v_info;

  GTK_GST_BASE_WIDGET_UNLOCK (widget);

  return TRUE;
}

void
gtk_gst_base_widget_set_buffer (GtkGstBaseWidget * widget, GstBuffer * buffer)
{
  /* As we have no type, this is better then no check */
  g_return_if_fail (GTK_IS_WIDGET (widget));

  GTK_GST_BASE_WIDGET_LOCK (widget);

  gst_buffer_replace (&widget->buffer, buffer);
  widget->new_buffer = TRUE;

  if (!widget->draw_id) {
    widget->draw_id = g_idle_add_full (G_PRIORITY_DEFAULT,
        (GSourceFunc) _queue_draw, widget, NULL);
  }

  GTK_GST_BASE_WIDGET_UNLOCK (widget);
}
