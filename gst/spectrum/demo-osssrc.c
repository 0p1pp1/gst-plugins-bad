/* GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gtk/gtk.h>

#define DEFAULT_AUDIOSRC "alsasrc"
#define SPECT_BANDS 256

static GtkWidget *drawingarea = NULL;

static void
on_window_destroy (GtkObject * object, gpointer user_data)
{
  drawingarea = NULL;
  gtk_main_quit ();
}

/* draw frequency spectrum as a bunch of bars */
static void
draw_spectrum (guchar * data)
{
  gint i;
  GdkRectangle rect = { 0, 0, SPECT_BANDS, 50 };

  if (!drawingarea)
    return;

  gdk_window_begin_paint_rect (drawingarea->window, &rect);
  gdk_draw_rectangle (drawingarea->window, drawingarea->style->black_gc,
      TRUE, 0, 0, SPECT_BANDS, 50);
  for (i = 0; i < SPECT_BANDS; i++) {
    gdk_draw_rectangle (drawingarea->window, drawingarea->style->white_gc,
        TRUE, i, 64 - data[i], 1, data[i]);
  }
  gdk_window_end_paint (drawingarea->window);
}

/* receive spectral data from element message */
gboolean
message_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  if (message->type == GST_MESSAGE_ELEMENT) {
    const GstStructure *s = gst_message_get_structure (message);
    const gchar *name = gst_structure_get_name (s);

    if (strcmp (name, "spectrum") == 0) {
      guchar spect[SPECT_BANDS];
      const GValue *list;
      const GValue *value;
      guint i;

      list = gst_structure_get_value (s, "spectrum");
      for (i = 0; i < SPECT_BANDS; ++i) {
        value = gst_value_list_get_value (list, i);
        spect[i] = g_value_get_uchar (value);
      }
      draw_spectrum (spect);
    }
  }
  /* we handled the message we want, and ignored the ones we didn't want.
   * so the core can unref the message for us */
  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *bin;
  GstElement *src, *spectrum, *sink;
  GstBus *bus;
  GtkWidget *appwindow;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  bin = gst_pipeline_new ("bin");

  src = gst_element_factory_make (DEFAULT_AUDIOSRC, "src");

  spectrum = gst_element_factory_make ("spectrum", "spectrum");
  g_object_set (G_OBJECT (spectrum), "bands", SPECT_BANDS, "threshold", -80,
      "message", TRUE, NULL);

  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add_many (GST_BIN (bin), src, spectrum, sink, NULL);
  if (!gst_element_link_many (src, spectrum, sink, NULL)) {
    fprintf (stderr, "cant link elements\n");
    exit (1);
  }

  bus = gst_element_get_bus (bin);
  gst_bus_add_watch (bus, message_handler, NULL);
  gst_object_unref (bus);

  appwindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (G_OBJECT (appwindow), "destroy",
      G_CALLBACK (on_window_destroy), NULL);

  drawingarea = gtk_drawing_area_new ();
  gtk_drawing_area_size (GTK_DRAWING_AREA (drawingarea), SPECT_BANDS, 64);
  gtk_container_add (GTK_CONTAINER (appwindow), drawingarea);
  gtk_widget_show_all (appwindow);

  gst_element_set_state (bin, GST_STATE_PLAYING);
  gtk_main ();
  gst_element_set_state (bin, GST_STATE_NULL);

  gst_object_unref (bin);

  return 0;
}
