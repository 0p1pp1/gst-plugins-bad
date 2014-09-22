/*
 * GStreamer
 * Copyright (C) 2008-2009 Julien Isorce <julien.isorce@gmail.com>
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

#include <gst/gst.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include "../gstgtk.h"


static GstBusSyncReply create_window (GstBus* bus, GstMessage* message, GtkWidget* widget)
{
  /* ignore anything but 'prepare-window-handle' element messages */
  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
      return GST_BUS_PASS;

  if (!gst_is_video_overlay_prepare_window_handle_message (message))
      return GST_BUS_PASS;

  g_print ("setting window handle\n");

  /* do not call gdk_window_ensure_native for the first time here because
   * we are in a different thread than the main thread */
  gst_video_overlay_set_gtk_window (GST_VIDEO_OVERLAY (GST_MESSAGE_SRC (message)), widget);

  gst_message_unref (message);

  return GST_BUS_DROP;
}


static void end_stream_cb(GstBus* bus, GstMessage* message, GstElement* pipeline)
{
  switch (GST_MESSAGE_TYPE (message))
  {
    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");

      gst_element_set_state (pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
      gtk_main_quit ();
      break;
    case GST_MESSAGE_ERROR:
    {
      gchar *debug = NULL;
      GError *err = NULL;

      gst_message_parse_error (message, &err, &debug);

      g_print ("Error: %s\n", err->message);
      g_error_free (err);

      if (debug)
      {
        g_print ("Debug details: %s\n", debug);
        g_free (debug);
      }

      gst_element_set_state (pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
      gtk_main_quit ();
      break;
    }
    default:
      break;
  }
}

static gboolean draw_cb(GtkWidget* widget, cairo_t *cr, GstElement* videosink)
{
    g_print ("draw_cb\n");
    gst_video_overlay_expose (GST_VIDEO_OVERLAY (videosink));
    return FALSE;
}


static void destroy_cb(GtkWidget* widget, GdkEvent* event, GstElement* pipeline)
{
    g_print("Close\n");

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    gtk_main_quit();
}


static void button_state_null_cb(GtkWidget* widget, GstElement* pipeline)
{
    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_print ("GST_STATE_NULL\n");
}


static void button_state_ready_cb(GtkWidget* widget, GstElement* pipeline)
{
    gst_element_set_state (pipeline, GST_STATE_READY);
    g_print ("GST_STATE_READY\n");
}


static void button_state_paused_cb(GtkWidget* widget, GstElement* pipeline)
{
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    g_print ("GST_STATE_PAUSED\n");
}


static void button_state_playing_cb(GtkWidget* widget, GstElement* pipeline)
{
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("GST_STATE_PLAYING\n");
}


static gchar* slider_fps_cb (GtkScale* scale, gdouble value, GstElement* pipeline)
{
    //change the video frame rate dynamically
    return g_strdup_printf ("video framerate: %0.*g", gtk_scale_get_digits (scale), value);
}



gint main (gint argc, gchar *argv[])
{
    GtkWidget *area;
    gst_init (&argc, &argv);
    gtk_init (&argc, &argv);

    GstElement* pipeline = gst_pipeline_new ("pipeline");
    GstElement* videosrc  = gst_element_factory_make ("videotestsrc", "videotestsrc");
    GstElement* videosink = gst_element_factory_make ("glimagesink", "glimagesink");

    gst_bin_add_many (GST_BIN (pipeline), videosrc, videosink, NULL);

    gboolean link_ok = gst_element_link_many(videosrc, videosink, NULL) ;
    if(!link_ok)
    {
        g_warning("Failed to link an element!\n") ;
        return -1;
    }

    //set window id on this event
    GstBus* bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    gst_bus_add_signal_watch (bus);
    g_signal_connect(bus, "message::error", G_CALLBACK(end_stream_cb), pipeline);
    g_signal_connect(bus, "message::warning", G_CALLBACK(end_stream_cb), pipeline);
    g_signal_connect(bus, "message::eos", G_CALLBACK(end_stream_cb), pipeline);

    gst_element_set_state(pipeline, GST_STATE_READY);

    area = gtk_drawing_area_new();
    gst_bus_set_sync_handler (bus, (GstBusSyncHandler) create_window, area, NULL);
    gst_object_unref (bus);

    //window that contains an area where the video is drawn
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_size_request (window, 640, 480);
    gtk_window_move (GTK_WINDOW (window), 300, 10);
    gtk_window_set_title (GTK_WINDOW (window), "glimagesink implement the gstvideooverlay interface");
    GdkGeometry geometry;
    geometry.min_width = 1;
    geometry.min_height = 1;
    geometry.max_width = -1;
    geometry.max_height = -1;
    gtk_window_set_geometry_hints (GTK_WINDOW (window), window, &geometry, GDK_HINT_MIN_SIZE);

    //window to control the states
    GtkWidget* window_control = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    geometry.min_width = 1;
    geometry.min_height = 1;
    geometry.max_width = -1;
    geometry.max_height = -1;
    gtk_window_set_geometry_hints (GTK_WINDOW (window_control), window_control, &geometry, GDK_HINT_MIN_SIZE);
    gtk_window_set_resizable (GTK_WINDOW (window_control), FALSE);
    gtk_window_move (GTK_WINDOW (window_control), 10, 10);
    GtkWidget* table = gtk_grid_new ();
    gtk_container_add (GTK_CONTAINER (window_control), table);

    //control state null
    GtkWidget* button_state_null = gtk_button_new_with_label ("GST_STATE_NULL");
    g_signal_connect (G_OBJECT (button_state_null), "clicked",
        G_CALLBACK (button_state_null_cb), pipeline);
    gtk_grid_attach (GTK_GRID (table), button_state_null, 0, 0, 1, 1);
    gtk_widget_show (button_state_null);

    //control state ready
    GtkWidget* button_state_ready = gtk_button_new_with_label ("GST_STATE_READY");
    g_signal_connect (G_OBJECT (button_state_ready), "clicked",
        G_CALLBACK (button_state_ready_cb), pipeline);
    gtk_grid_attach (GTK_GRID (table), button_state_ready, 0, 1, 1, 1);
    gtk_widget_show (button_state_ready);

    //control state paused
    GtkWidget* button_state_paused = gtk_button_new_with_label ("GST_STATE_PAUSED");
    g_signal_connect (G_OBJECT (button_state_paused), "clicked",
        G_CALLBACK (button_state_paused_cb), pipeline);
    gtk_grid_attach (GTK_GRID (table), button_state_paused, 0, 2, 1, 1);
    gtk_widget_show (button_state_paused);

    //control state playing
    GtkWidget* button_state_playing = gtk_button_new_with_label ("GST_STATE_PLAYING");
    g_signal_connect (G_OBJECT (button_state_playing), "clicked",
        G_CALLBACK (button_state_playing_cb), pipeline);
    gtk_grid_attach (GTK_GRID (table), button_state_playing, 0, 3, 1, 1);
    gtk_widget_show (button_state_playing);

    //change framerate
    GtkWidget* slider_fps = gtk_scale_new_with_range (GTK_ORIENTATION_VERTICAL,
        1, 30, 2);
    g_signal_connect (G_OBJECT (slider_fps), "format-value",
        G_CALLBACK (slider_fps_cb), pipeline);
    gtk_grid_attach (GTK_GRID (table), slider_fps, 1, 0, 1, 4);
    gtk_widget_show (slider_fps);

    gtk_widget_show (table);
    gtk_widget_show (window_control);

    //configure the pipeline
    g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(destroy_cb), pipeline);

    //area where the video is drawn
    gtk_container_add (GTK_CONTAINER (window), area);

    gtk_widget_realize(area);

    //needed when being in GST_STATE_READY, GST_STATE_PAUSED
    //or resizing/obscuring the window
    g_signal_connect(area, "draw", G_CALLBACK(draw_cb), videosink);

    gtk_widget_show_all (window);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    gtk_main();

    return 0;
}

