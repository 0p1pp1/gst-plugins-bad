/* GStreamer
 * Copyright (C) <2007> Leandro Melo de Sales <leandroal@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{

  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End-of-stream\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *err;

      gst_message_parse_error (msg, &err, &debug);
      g_free (debug);

      g_print ("Error: %s\n", err->message);
      g_error_free (err);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}


int
main (int argc, char *argv[])
{

  GMainLoop *loop;
  GstBus *bus;
  GstElement *pipeline, *alsasink, *mad, *audioconvert, *dccpclientsrc;

  /* initialize GStreamer */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* check input arguments */
  if (argc != 3) {
    g_print ("%s\n", "see usage: serverHost serverPort");
    return -1;
  }

  /* create elements */
  pipeline = gst_pipeline_new ("audio-sender");
  dccpclientsrc = gst_element_factory_make ("dccpclientsrc", "client-source");
  mad = gst_element_factory_make ("mad", "mad");
  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");
  alsasink = gst_element_factory_make ("alsasink", "alsa-sink");

  if (!pipeline || !alsasink || !mad || !audioconvert || !dccpclientsrc) {
    g_print ("One element could not be created\n");
    return -1;
  }

  g_object_set (G_OBJECT (dccpclientsrc), "host", argv[1], NULL);
  g_object_set (G_OBJECT (dccpclientsrc), "port", atoi (argv[2]), NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  /* put all elements in a bin */
  gst_bin_add_many (GST_BIN (pipeline), dccpclientsrc, mad, audioconvert,
      alsasink, NULL);

  gst_element_link_many (dccpclientsrc, mad, audioconvert, alsasink, NULL);


  /* Now set to playing and iterate. */
  g_print ("Setting to PLAYING\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_print ("Running\n");
  g_main_loop_run (loop);

  /* clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}
