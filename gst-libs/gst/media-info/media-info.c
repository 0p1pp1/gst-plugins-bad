/* GStreamer media-info library
 * Copyright (C) 2003,2004 Thomas Vander Stichele <thomas@apestaart.org>
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

#include <gst/gst.h>
#include <string.h>
#include "media-info.h"
#include "media-info-priv.h"

static void	gst_media_info_class_init	(GstMediaInfoClass *klass);
static void	gst_media_info_instance_init	(GstMediaInfo *info);

static void	gst_media_info_get_property     (GObject *object, guint prop_id,
						 GValue *value,
						 GParamSpec *pspec);


static gboolean _media_info_inited = FALSE;

/* FIXME: this is a lousy hack that needs to go */
#define MAX_METADATA_ITERS 5

/* GObject-y bits */

/* signal stuff */
enum
{
  MEDIA_INFO_SIGNAL,
  LAST_SIGNAL
};

static guint gst_media_info_signals[LAST_SIGNAL] = { 0 };

/*
 * all GError stuff
 */

enum
{
  MEDIA_INFO_ERROR_FILE
};

/* GError quark stuff */
GQuark
gst_media_info_error_quark (void)
{
  static GQuark quark = 0;
  if (quark == 0)
    quark = g_quark_from_static_string ("gst-media-info-error-quark");
  return quark;
}

/*
 * GObject type stuff
 */

enum
{
  PROP_SOURCE
};

static GObjectClass *parent_class = NULL;

GST_DEBUG_CATEGORY (gst_media_info_debug);

/* initialize the media-info library */
void
gst_media_info_init (void)
{
  if (_media_info_inited) return;

  /* register our debugging category */
  GST_DEBUG_CATEGORY_INIT (gst_media_info_debug, "GST_MEDIA_INFO", 0,
                           "GStreamer media-info library");
  GST_DEBUG ("Initialized media-info library");
  _media_info_inited = TRUE;
}

GType
gst_media_info_get_type (void)
{
  static GType gst_media_info_type = 0;
  if (!gst_media_info_type)
  {
    static const GTypeInfo gst_media_info_info = {
      sizeof (GstMediaInfoClass),
      (GBaseInitFunc) NULL,
      (GBaseFinalizeFunc) NULL,
      (GClassInitFunc) gst_media_info_class_init,
      NULL, NULL,
      sizeof (GstMediaInfo),
      0,
      (GInstanceInitFunc) gst_media_info_instance_init,
      NULL
    };
    gst_media_info_type = g_type_register_static (G_TYPE_OBJECT,
		                                  "GstMediaInfo",
		                                  &gst_media_info_info, 0);
  }
  return gst_media_info_type;
}

static void
gst_media_info_class_init (GstMediaInfoClass *klass)
{
  GObjectClass *g_object_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_ref (G_TYPE_OBJECT);
  //parent_class = g_type_class_peek_parent (klass);


  /*
  object_class->finalize = gst_media_info_finalize;
  object_class->dispose  = gst_media_info_dispose;
  */

  /*
  g_object_class->set_property = gst_media_info_set_property;
  */
  g_object_class->get_property = gst_media_info_get_property;

  klass->media_info_signal = NULL;

  gst_media_info_signals [MEDIA_INFO_SIGNAL] =
    g_signal_new ("media-info",
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (GstMediaInfoClass, media_info_signal),
		  NULL, NULL,
		  gst_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
}

static void
gst_media_info_instance_init (GstMediaInfo *info)
{
  GError **error;

  info->priv = g_new0 (GstMediaInfoPriv, 1);
  error = &info->priv->error;

  if (!_media_info_inited) { gst_media_info_init (); }

  gmip_init (info->priv, error);
  gmip_reset (info->priv);
}

/* get/set */
static void
gst_media_info_get_property (GObject *object, guint prop_id,
			     GValue *value, GParamSpec *pspec)
{
  GstMediaInfo *info = GST_MEDIA_INFO (object);

  switch (prop_id)
  {
    case PROP_SOURCE:
      g_value_set_string (value, info->priv->source_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

GstMediaInfo *
gst_media_info_new (GError **error)
{
  GstMediaInfo *info = g_object_new (GST_MEDIA_INFO_TYPE, NULL);

  if (info->priv->error)
  {
    if (error)
    {
      *error = info->priv->error;
      info->priv->error = NULL;
    }
    else
    {
      g_warning ("Error creating GstMediaInfo object.\n%s",
                 info->priv->error->message);
      g_error_free (info->priv->error);
    }
  }
  return info;
}

/**
 * public methods
 */
gboolean
gst_media_info_set_source (GstMediaInfo *info, const char *source, GError **error)
{
  info->priv->source_name = g_strdup (source);
  return TRUE;
}

/* idler-based implementation
 * set up read on a given location
 * FIXME: maybe we should check if the info is cleared when calling this
 * function ? What happens if it gets called again on same info before
 * previous one is done ?
 */
void
gst_media_info_read_with_idler (GstMediaInfo *info, const char *location,
		                guint16 flags, GError **error)
{
  GstMediaInfoPriv *priv = info->priv;

  gmip_reset (info->priv);		/* reset all structs */
  priv->location = g_strdup (location);
  priv->flags = flags;
}

/* an idler which does the work of actually collecting all data
 * this must be called repeatedly, until streamp is set to a non-NULL value
 * returns: TRUE if it was able to idle, FALSE if there was an error
 */
gboolean
gst_media_info_read_idler (GstMediaInfo *info, GstMediaInfoStream **streamp, GError **error)
{
  GstMediaInfoPriv *priv;

  /* if it's NULL then we're sure something went wrong higher up) */
  if (info == NULL) return FALSE;
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  priv = info->priv;

  g_assert (streamp != NULL);
  g_assert (priv);

  switch (priv->state)
  {
    case GST_MEDIA_INFO_STATE_NULL:
      /* make sure we have a source */
      if (!priv->source_name)
      {
        *error = g_error_new (GST_MEDIA_INFO_ERROR, 0,
                              "No source set on media info.");
        return FALSE;
      }

      /* need to find type */
      GST_DEBUG ("idler: NULL, need to find type, priv %p", priv);
      return gmip_find_type_pre (priv, error);

    case GST_MEDIA_INFO_STATE_TYPEFIND:
    {
      gchar *mime;

      GST_LOG ("STATE_TYPEFIND");
      if ((priv->type == NULL) && gst_bin_iterate (GST_BIN (priv->pipeline)))
      {
	GST_DEBUG ("iterating while in STATE_TYPEFIND");
	GMI_DEBUG("?");
        return TRUE;
      }
      if (priv->type == NULL)
      {
        g_warning ("Couldn't find type\n");
	return FALSE;
      }
      /* do the state transition */
      GST_DEBUG ("doing find_type_post");
      gmip_find_type_post (priv);
      GST_DEBUG ("finding out mime type");
      mime = g_strdup (gst_structure_get_name (
	    gst_caps_get_structure(priv->type, 0)));
      GST_DEBUG ("found out mime type: %s", mime);
      if (!gmi_set_mime (info, mime))
      {
        /* FIXME: pop up error */
        GST_DEBUG ("no decoder pipeline found for mime %s", mime);
        return FALSE;
      }
      priv->stream = gmi_stream_new ();
      GST_DEBUG ("new stream: %p", priv->stream);
      priv->stream->mime = mime;
      priv->stream->path = priv->location;

      gmip_find_stream_pre (priv);
    }
    case GST_MEDIA_INFO_STATE_STREAM:
    {
      GST_LOG ("STATE_STREAM");
      if ((priv->format == NULL) && gst_bin_iterate (GST_BIN (priv->pipeline)))
      {
	GMI_DEBUG("?");
        return TRUE;
      }
      if (priv->format == NULL)
      {
        g_warning ("Couldn't find format\n");
	return FALSE;
      }
      /* do state transition; stream -> first track metadata */
      priv->current_track_num = 0;
      gmip_find_stream_post (priv);
      priv->current_track = gmi_track_new ();
      gmip_find_track_metadata_pre (priv);
      return TRUE;
    }
    /* these ones are repeated per track */
    case GST_MEDIA_INFO_STATE_METADATA:
    {
      if ((priv->metadata == NULL) &&
	  gst_bin_iterate (GST_BIN (priv->pipeline)) &&
	  priv->metadata_iters < MAX_METADATA_ITERS)
      {
	GMI_DEBUG("?");
	priv->metadata_iters++;
        return TRUE;
      }
      if (priv->metadata_iters == MAX_METADATA_ITERS)
	      g_print ("iterated a few times, didn't find metadata\n");
      if (priv->metadata == NULL)
      {
	/* this is not a permanent failure */
        GST_DEBUG ("Couldn't find metadata");
      }
      GST_DEBUG ("found metadata of track %ld", priv->current_track_num);
      if (!gmip_find_track_metadata_post (priv)) return FALSE;
      GST_DEBUG ("METADATA: going to STREAMINFO\n");
      priv->state = GST_MEDIA_INFO_STATE_STREAMINFO;
      return gmip_find_track_streaminfo_pre (priv);
    }
    case GST_MEDIA_INFO_STATE_STREAMINFO:
    {
      if ((priv->streaminfo == NULL) &&
	  gst_bin_iterate (GST_BIN (priv->pipeline)))
      {
	      GMI_DEBUG("?");
        return TRUE;
      }
      if (priv->streaminfo == NULL)
      {
	/* this is not a permanent failure */
        GST_DEBUG ("Couldn't find streaminfo");
      }
      else
        GST_DEBUG ("found streaminfo of track %ld", priv->current_track_num);
      if (!gmip_find_track_streaminfo_post (priv)) return FALSE;
      priv->state = GST_MEDIA_INFO_STATE_FORMAT;
      return gmip_find_track_format_pre (priv);
    }
    case GST_MEDIA_INFO_STATE_FORMAT:
    {
      if ((priv->format == NULL) &&
	  gst_bin_iterate (GST_BIN (priv->pipeline)))
      {
	      GMI_DEBUG("?");
        return TRUE;
      }
      if (priv->format == NULL)
      {
        g_warning ("Couldn't find format\n");
	return FALSE;
      }
      GST_DEBUG ("found format of track %ld", priv->current_track_num);
      if (!gmip_find_track_format_post (priv)) return FALSE;
      /* save the track info */
      priv->stream->tracks = g_list_append (priv->stream->tracks,
		                            priv->current_track);
      /* these alloc'd data types have been handed off */
      priv->current_track = NULL;
      priv->location = NULL;
      /* now see if we need to seek to a next track or not */
      priv->current_track_num++;
      if (priv->current_track_num < priv->stream->length_tracks)
      {
        gmi_seek_to_track (info, priv->current_track_num);
        priv->current_track = gmi_track_new ();
	if (!gmip_find_track_metadata_pre (priv))
	{
	  g_free (priv->current_track);
          return FALSE;
	}
	priv->state = GST_MEDIA_INFO_STATE_METADATA;
	return TRUE;
      }
      priv->state = GST_MEDIA_INFO_STATE_DONE;
      gmi_clear_decoder (info);
      GST_DEBUG ("TOTALLY DONE, setting pointer *streamp to %p", *streamp);
      *streamp = priv->stream;
      priv->stream = NULL;
      return TRUE;
    }
    case GST_MEDIA_INFO_STATE_DONE:
      return TRUE;
      default:
      g_warning ("don't know what to do\n");
      return FALSE;
 }
}

/* main function
 * read all possible info from the file pointed to by location
 * use flags to limit the type of information searched for */
GstMediaInfoStream *
gst_media_info_read (GstMediaInfo *info, const char *location, guint16 flags, GError **error)
{
  GstMediaInfoStream *stream = NULL;

  gst_media_info_read_with_idler (info, location, flags, error);
  if (*error) return FALSE;
  while (gst_media_info_read_idler (info, &stream, error) && stream == NULL)
    /* keep looping */;
  if (*error)
    return NULL;

  return stream;
}


/*
 * FIXME: reset ?
gboolean	gst_media_info_write	(GstMediaInfo *media_info,
                                         const char *location,
					 GstCaps *media_info);
					 */
