/* GStreamer Mixer
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * mixeroptions.c: mixer track options object design
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

#include "mixeroptions.h"

enum
{
  /* FILL ME */
  SIGNAL_OPTION_CHANGED,
  LAST_SIGNAL
};

static void gst_mixer_options_class_init (GstMixerOptionsClass * klass);
static void gst_mixer_options_init (GstMixerOptions * mixer);
static void gst_mixer_options_dispose (GObject * object);

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

GType
gst_mixer_options_get_type (void)
{
  static GType gst_mixer_options_type = 0;

  if (!gst_mixer_options_type) {
    static const GTypeInfo mixer_options_info = {
      sizeof (GstMixerOptionsClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_mixer_options_class_init,
      NULL,
      NULL,
      sizeof (GstMixerOptions),
      0,
      (GInstanceInitFunc) gst_mixer_options_init,
      NULL
    };

    gst_mixer_options_type =
        g_type_register_static (GST_TYPE_MIXER_TRACK,
        "GstMixerOptions", &mixer_options_info, 0);
  }

  return gst_mixer_options_type;
}

static void
gst_mixer_options_class_init (GstMixerOptionsClass * klass)
{
  GObjectClass *object_klass = (GObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_MIXER_TRACK);

  signals[SIGNAL_OPTION_CHANGED] =
      g_signal_new ("option_changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMixerOptionsClass, option_changed),
      NULL, NULL, g_cclosure_marshal_VOID__STRING,
      G_TYPE_NONE, 1, G_TYPE_STRING);

  object_klass->dispose = gst_mixer_options_dispose;
}

static void
gst_mixer_options_init (GstMixerOptions * channel)
{
  channel->values = NULL;
}

static void
gst_mixer_options_dispose (GObject * object)
{
  GstMixerOptions *opts = GST_MIXER_OPTIONS (object);

  g_list_foreach (opts->values, (GFunc) g_free, NULL);
  g_list_free (opts->values);
  opts->values = NULL;

  if (parent_class->dispose)
    parent_class->dispose (object);
}
