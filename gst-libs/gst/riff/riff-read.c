/* GStreamer RIFF I/O
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * riff-read.c: RIFF input file parsing
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

#include "riff-ids.h"
#include "riff-read.h"

enum {
  ARG_0,
  ARG_METADATA
  /* FILL ME */
};

static void     gst_riff_read_class_init   (GstRiffReadClass *klass);
static void     gst_riff_read_init         (GstRiffRead *riff);
static void     gst_riff_read_get_property (GObject     *object,
					    guint        prop_id, 	
					    GValue      *value,
					    GParamSpec  *pspec);

static GstElementStateReturn
		gst_riff_read_change_state (GstElement  *element);

static GstElementClass *parent_class = NULL;

GType
gst_riff_read_get_type (void) 
{
  static GType gst_riff_read_type = 0;

  if (!gst_riff_read_type) {
    static const GTypeInfo gst_riff_read_info = {
      sizeof (GstRiffReadClass),      
      NULL,
      NULL,
      (GClassInitFunc) gst_riff_read_class_init,
      NULL,
      NULL,
      sizeof (GstRiffRead),
      0,
      (GInstanceInitFunc) gst_riff_read_init,
    };

    gst_riff_read_type =
	g_type_register_static (GST_TYPE_ELEMENT, "GstRiffRead",
				&gst_riff_read_info, 0);
  }

  return gst_riff_read_type;
}

static void
gst_riff_read_class_init (GstRiffReadClass *klass) 
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  g_object_class_install_property (gobject_class, ARG_METADATA,
    g_param_spec_boxed ("metadata", "Metadata", "Metadata",
                        GST_TYPE_CAPS, G_PARAM_READABLE));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  
  gobject_class->get_property = gst_riff_read_get_property;

  gstelement_class->change_state = gst_riff_read_change_state;
}

static void
gst_riff_read_init (GstRiffRead *riff)
{
  riff->sinkpad = NULL;
  riff->bs = NULL;
  riff->level = NULL;
  riff->metadata = NULL;
}

static void
gst_riff_read_get_property (GObject    *object,
			    guint       prop_id, 	
			    GValue     *value,
			    GParamSpec *pspec)
{
  GstRiffRead *riff = GST_RIFF_READ (object);

  switch (prop_id) {
    case ARG_METADATA:
      g_value_set_boxed (value, riff->metadata);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_riff_read_change_state (GstElement *element)
{
  GstRiffRead *riff = GST_RIFF_READ (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      if (!riff->sinkpad)
        return GST_STATE_FAILURE;
      riff->bs = gst_bytestream_new (riff->sinkpad);
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_caps_replace (&riff->metadata, NULL);
      gst_bytestream_destroy (riff->bs);
      while (riff->level) {
        GstRiffLevel *level = riff->level->data;

        riff->level = g_list_remove (riff->level, level);
        g_free (level);
      }
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

/*
 * Return: the amount of levels in the hierarchy that the
 * current element lies higher than the previous one.
 * The opposite isn't done - that's auto-done using list
 * element reading.
 */

static guint
gst_riff_read_element_level_up (GstRiffRead *riff)
{
  guint num = 0;
  guint64 pos = gst_bytestream_tell (riff->bs);

  while (riff->level != NULL) {
    GList *last = g_list_last (riff->level);
    GstRiffLevel *level = last->data;

    if (pos >= level->start + level->length) {
      riff->level = g_list_remove (riff->level, level);
      g_free (level);
      num++;
    } else
      break;
  }

  return num;
}

/*
 * Read the next tag plus length (may be NULL). Return
 * TRUE on success or FALSE on failure.
 */

static gboolean
gst_riff_peek_head (GstRiffRead *riff,
		    guint32     *tag,
		    guint32     *length,
		    guint       *level_up)
{
  guint8 *data;

  /* read */
  if (gst_bytestream_peek_bytes (riff->bs, &data, 8) != 8) {
    GstEvent *event = NULL;
    guint32 remaining;

    /* Here, we might encounter EOS */
    gst_bytestream_get_status (riff->bs, &remaining, &event);
    if (event && GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
      gst_pad_event_default (riff->sinkpad, event);
    } else {
      gst_event_unref (event);
      gst_element_error (GST_ELEMENT (riff), "Read error");
    }
    return FALSE;
  }

  /* parse tag + length (if wanted) */
  *tag = GUINT32_FROM_LE (((guint32 *) data)[0]);
  if (length)
    *length = GUINT32_FROM_LE (((guint32 *) data)[1]);

  /* level */
  if (level_up)
    *level_up = gst_riff_read_element_level_up (riff);

  return TRUE;
}

/*
 * Read: the actual data (plus alignment and flush).
 * Return: the data, as a GstBuffer.
 */

static GstBuffer *
gst_riff_read_element_data (GstRiffRead *riff,
			    guint        length)
{
  GstBuffer *buf = NULL;

  if (gst_bytestream_peek (riff->bs, &buf, length) != length) {
    gst_element_error (GST_ELEMENT (riff), "Read error");
    if (buf)
      gst_buffer_unref (buf);
    return NULL;
  }

  /* we need 16-bit alignment */
  if (length & 1)
    length++;

  gst_bytestream_flush (riff->bs, length);

  return buf;
}

/*
 * Seek.
 */

GstEvent *
gst_riff_read_seek (GstRiffRead *riff,
		    guint64      offset)
{
  guint64 length = gst_bytestream_length (riff->bs);
  guint32 remaining;
  GstEvent *event;
  guchar *data;

  /* hack for AVI files with broken idx1 size chunk markers */
  if (offset > length)
    offset = length;

  /* first, flush remaining buffers */
  gst_bytestream_get_status (riff->bs, &remaining, &event);
  if (event) {
    g_warning ("Unexpected event before seek");
    gst_event_unref (event);
  }
  if (remaining)
    gst_bytestream_flush_fast (riff->bs, remaining);

  /* now seek */
  if (!gst_bytestream_seek (riff->bs, offset, GST_SEEK_METHOD_SET)) {
    gst_element_error (GST_ELEMENT (riff), "Seek failed");
    return NULL;
  }

  /* and now, peek a new byte. This will fail because there's a
   * pending event. Then, take the event and return it. */
  if (gst_bytestream_peek_bytes (riff->bs, &data, 1))
    g_warning ("Unexpected data after seek");

  /* get the discont event and return */
  gst_bytestream_get_status (riff->bs, &remaining, &event);
  if (!event || GST_EVENT_TYPE (event) != GST_EVENT_DISCONTINUOUS) {
    gst_element_error (GST_ELEMENT (riff),
		       "No discontinuity event after seek");
    if (event)
      gst_event_unref (event);
    return NULL;
  }

  return event;
}

/*
 * Gives the tag of the next RIFF element.
 */

guint32
gst_riff_peek_tag (GstRiffRead *riff,
		   guint       *level_up)
{
  guint32 tag;

  if (!gst_riff_peek_head (riff, &tag, NULL, level_up))
    return 0;

  return tag;
}

/*
 * Gives the tag of the next LIST/RIFF element.
 */

guint32
gst_riff_peek_list (GstRiffRead *riff)
{
  guint32 lst;
  guint8 *data;

  if (!gst_riff_peek_head (riff, &lst, NULL, NULL))
    return FALSE;
  if (lst != GST_RIFF_TAG_LIST) {
    g_warning ("Not a LIST object");
    return 0;
  }

  if (gst_bytestream_peek_bytes (riff->bs, &data, 12) != 12) {
    gst_element_error (GST_ELEMENT (riff), "Read error");
    return 0;
  }

  return GUINT32_FROM_LE (((guint32 *) data)[2]);
}

/*
 * Don't read data.
 */

gboolean
gst_riff_read_skip (GstRiffRead *riff)
{
  guint32 tag, length;
  GstEvent *event;
  guint32 remaining;

  if (!gst_riff_peek_head (riff, &tag, &length, NULL))
    return FALSE;

  /* 16-bit alignment */
  if (length & 1)
    length++;

  /* header itself */
  length += 8;

  /* see if we have that much data available */
  gst_bytestream_get_status (riff->bs, &remaining, &event);
  if (event) {
    g_warning ("Unexpected event in skip");
    gst_event_unref (event);
  }

  /* yes */
  if (remaining >= length) {
    gst_bytestream_flush_fast (riff->bs, length);
    return TRUE;
  }

  /* no */
  if (!(event = gst_riff_read_seek (riff,
			gst_bytestream_tell (riff->bs) + length)))
    return FALSE;

  gst_event_unref (event);

  return TRUE;
}

/*
 * Read any type of data.
 */

gboolean
gst_riff_read_data (GstRiffRead *riff,
		    guint32     *tag,
		    GstBuffer  **buf)
{
  guint32 length;

  if (!gst_riff_peek_head (riff, tag, &length, NULL))
    return FALSE;
  gst_bytestream_flush_fast (riff->bs, 8);

  return ((*buf = gst_riff_read_element_data (riff, length)) != NULL);
}

/*
 * Read a string.
 */

gboolean
gst_riff_read_ascii (GstRiffRead *riff,
		     guint32     *tag,
		     gchar      **str)
{
  GstBuffer *buf;

  if (!gst_riff_read_data (riff, tag, &buf))
    return FALSE;

  *str = g_malloc (GST_BUFFER_SIZE (buf) + 1);
  memcpy (*str, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  (*str)[GST_BUFFER_SIZE (buf)] = '\0';

  gst_buffer_unref (buf);

  return TRUE;
}

/*
 * Read media structs.
 */

gboolean
gst_riff_read_strh (GstRiffRead    *riff,
		    gst_riff_strh **header)
{
  guint32 tag;
  GstBuffer *buf;
  gst_riff_strh *strh;

  if (!gst_riff_read_data (riff, &tag, &buf))
    return FALSE;

  if (tag != GST_RIFF_TAG_strh) {
    g_warning ("Not a strh chunk");
    gst_buffer_unref (buf);
    return FALSE;
  }
  if (GST_BUFFER_SIZE (buf) < sizeof (gst_riff_strh)) {
    g_warning ("Too small strh (%d available, %d needed)",
	       GST_BUFFER_SIZE (buf), sizeof (gst_riff_strh));
    gst_buffer_unref (buf);
    return FALSE;
  }

  strh = g_memdup (GST_BUFFER_DATA (buf),
		   GST_BUFFER_SIZE (buf));
  gst_buffer_unref (buf);

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
  strh->type        = GUINT32_FROM_LE (strh->type);
  strh->fcc_handler = GUINT32_FROM_LE (strh->fcc_handler);
  strh->flags       = GUINT32_FROM_LE (strh->flags);
  strh->priority    = GUINT32_FROM_LE (strh->priority);
  strh->init_frames = GUINT32_FROM_LE (strh->init_frames);
  strh->scale       = GUINT32_FROM_LE (strh->scale);
  strh->rate        = GUINT32_FROM_LE (strh->rate);
  strh->start       = GUINT32_FROM_LE (strh->start);
  strh->length      = GUINT32_FROM_LE (strh->length);
  strh->bufsize     = GUINT32_FROM_LE (strh->bufsize);
  strh->quality     = GUINT32_FROM_LE (strh->quality);
  strh->samplesize  = GUINT32_FROM_LE (strh->samplesize);
#endif

  /* avoid divisions by zero */
  if (!strh->scale)
    strh->scale = 1;
  if (!strh->rate)
    strh->rate = 1;

  /* debug */
  GST_INFO ("strh tag found");
  GST_INFO (" type        " GST_FOURCC_FORMAT,
	    GST_FOURCC_ARGS (strh->type));
  GST_INFO (" fcc_handler " GST_FOURCC_FORMAT,
	    GST_FOURCC_ARGS (strh->fcc_handler));
  GST_INFO (" flags       0x%08x", strh->flags);
  GST_INFO (" priority    %d",     strh->priority);
  GST_INFO (" init_frames %d",     strh->init_frames);
  GST_INFO (" scale       %d",     strh->scale);
  GST_INFO (" rate        %d",     strh->rate);
  GST_INFO (" start       %d",     strh->start);
  GST_INFO (" length      %d",     strh->length);
  GST_INFO (" bufsize     %d",     strh->bufsize);
  GST_INFO (" quality     %d",     strh->quality);
  GST_INFO (" samplesize  %d",     strh->samplesize);

  *header = strh;

  return TRUE;
}

gboolean
gst_riff_read_strf_vids (GstRiffRead         *riff,
			 gst_riff_strf_vids **header)
{
  guint32 tag;
  GstBuffer *buf;
  gst_riff_strf_vids *strf;

  if (!gst_riff_read_data (riff, &tag, &buf))
    return FALSE;

  if (tag != GST_RIFF_TAG_strf) {
    g_warning ("Not a strf chunk");
    gst_buffer_unref (buf);
    return FALSE;
  }
  if (GST_BUFFER_SIZE (buf) < sizeof (gst_riff_strf_vids)) {
    g_warning ("Too small strf_vids (%d available, %d needed)",
	       GST_BUFFER_SIZE (buf), sizeof (gst_riff_strf_vids));
    gst_buffer_unref (buf);
    return FALSE;
  }

  strf = g_memdup (GST_BUFFER_DATA (buf),
		   GST_BUFFER_SIZE (buf));

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
  strf->size        = GUINT32_FROM_LE (strf->size);
  strf->width       = GUINT32_FROM_LE (strf->width);
  strf->height      = GUINT32_FROM_LE (strf->height);
  strf->planes      = GUINT16_FROM_LE (strf->planes);
  strf->bit_cnt     = GUINT16_FROM_LE (strf->bit_cnt);
  strf->compression = GUINT32_FROM_LE (strf->compression);
  strf->image_size  = GUINT32_FROM_LE (strf->image_size);
  strf->xpels_meter = GUINT32_FROM_LE (strf->xpels_meter);
  strf->ypels_meter = GUINT32_FROM_LE (strf->ypels_meter);
  strf->num_colors  = GUINT32_FROM_LE (strf->num_colors);
  strf->imp_colors  = GUINT32_FROM_LE (strf->imp_colors);
#endif

  /* size checking */
  if (strf->size > GST_BUFFER_SIZE (buf)) {
    g_warning ("strf_vids header gave %d bytes data, only %d available",
	       strf->size, GST_BUFFER_SIZE (buf));
    strf->size = GST_BUFFER_SIZE (buf);
  }

  /* debug */
  GST_INFO ("strf tag found in context vids:");
  GST_INFO (" size        %d", strf->size);
  GST_INFO (" width       %d", strf->width);
  GST_INFO (" height      %d", strf->height);
  GST_INFO (" planes      %d", strf->planes);
  GST_INFO (" bit_cnt     %d", strf->bit_cnt);
  GST_INFO (" compression " GST_FOURCC_FORMAT,
	    GST_FOURCC_ARGS (strf->compression));
  GST_INFO (" image_size  %d", strf->image_size);
  GST_INFO (" xpels_meter %d", strf->xpels_meter);
  GST_INFO (" ypels_meter %d", strf->ypels_meter);
  GST_INFO (" num_colors  %d", strf->num_colors);
  GST_INFO (" imp_colors  %d", strf->imp_colors);

  gst_buffer_unref (buf);

  *header = strf;

  return TRUE;
}

gboolean
gst_riff_read_strf_auds (GstRiffRead         *riff,
			 gst_riff_strf_auds **header)
{
  guint32 tag;
  GstBuffer *buf;
  gst_riff_strf_auds *strf;

  if (!gst_riff_read_data (riff, &tag, &buf))
    return FALSE;

  if (tag != GST_RIFF_TAG_strf) {
    g_warning ("Not a strf chunk");
    gst_buffer_unref (buf);
    return FALSE;
  }
  if (GST_BUFFER_SIZE (buf) < sizeof (gst_riff_strf_auds)) {
    g_warning ("Too small strf_auds (%d available, %d needed)",
	       GST_BUFFER_SIZE (buf), sizeof (gst_riff_strf_auds));
    gst_buffer_unref (buf);
    return FALSE;
  }

  strf = g_memdup (GST_BUFFER_DATA (buf),
		   GST_BUFFER_SIZE (buf));

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
  strf->format     = GUINT16_FROM_LE (strf->format);
  strf->channels   = GUINT16_FROM_LE (strf->channels);
  strf->rate       = GUINT32_FROM_LE (strf->rate);
  strf->av_bps     = GUINT32_FROM_LE (strf->av_bps);
  strf->blockalign = GUINT16_FROM_LE (strf->blockalign);
  strf->size       = GUINT16_FROM_LE (strf->size);
#endif

  /* debug */
  GST_INFO ("strf tag found in context auds:");
  GST_INFO (" format      %d", strf->format);
  GST_INFO (" channels    %d", strf->channels);
  GST_INFO (" rate        %d", strf->rate);
  GST_INFO (" av_bps      %d", strf->av_bps);
  GST_INFO (" blockalign  %d", strf->blockalign);
  GST_INFO (" size        %d", strf->size); /* wordsize, not extrasize! */

  gst_buffer_unref (buf);

  *header = strf;

  return TRUE;
}

gboolean
gst_riff_read_strf_iavs (GstRiffRead         *riff,
			 gst_riff_strf_iavs **header)
{
  guint32 tag;
  GstBuffer *buf;
  gst_riff_strf_iavs *strf;

  if (!gst_riff_read_data (riff, &tag, &buf))
    return FALSE;

  if (tag != GST_RIFF_TAG_strf) {
    g_warning ("Not a strf chunk");
    gst_buffer_unref (buf);
    return FALSE;
  }
  if (GST_BUFFER_SIZE (buf) < sizeof (gst_riff_strf_iavs)) {
    g_warning ("Too small strf_iavs (%d available, %d needed)",
	       GST_BUFFER_SIZE (buf), sizeof (gst_riff_strf_iavs));
    gst_buffer_unref (buf);
    return FALSE;
  }

  strf = g_memdup (GST_BUFFER_DATA (buf),
		   GST_BUFFER_SIZE (buf));
  gst_buffer_unref (buf);

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
  strf->DVAAuxSrc   = GUINT32_FROM_LE (strf->DVAAuxSrc);
  strf->DVAAuxCtl   = GUINT32_FROM_LE (strf->DVAAuxCtl);
  strf->DVAAuxSrc1  = GUINT32_FROM_LE (strf->DVAAuxSrc1);
  strf->DVAAuxCtl1  = GUINT32_FROM_LE (strf->DVAAuxCtl1);
  strf->DVVAuxSrc   = GUINT32_FROM_LE (strf->DVVAuxSrc);
  strf->DVVAuxCtl   = GUINT32_FROM_LE (strf->DVVAuxCtl);
  strf->DVReserved1 = GUINT32_FROM_LE (strf->DVReserved1);
  strf->DVReserved2 = GUINT32_FROM_LE (strf->DVReserved2);
#endif

  /* debug */
  GST_INFO ("strf tag found in context iavs");
  GST_INFO (" DVAAuxSrc   %08x", strf->DVAAuxSrc);
  GST_INFO (" DVAAuxCtl   %08x", strf->DVAAuxCtl);
  GST_INFO (" DVAAuxSrc1  %08x", strf->DVAAuxSrc1);
  GST_INFO (" DVAAuxCtl1  %08x", strf->DVAAuxCtl1);
  GST_INFO (" DVVAuxSrc   %08x", strf->DVVAuxSrc);
  GST_INFO (" DVVAuxCtl   %08x", strf->DVVAuxCtl);
  GST_INFO (" DVReserved1 %08x", strf->DVReserved1);
  GST_INFO (" DVReserved2 %08x", strf->DVReserved2);

  *header = strf;

  return TRUE;
}

/*
 * Read a list.
 */

gboolean
gst_riff_read_list (GstRiffRead *riff,
		    guint32     *tag)
{
  guint32 length, lst;
  GstRiffLevel *level;
  guint8 *data;

  if (!gst_riff_peek_head (riff, &lst, &length, NULL))
    return FALSE;
  if (lst != GST_RIFF_TAG_LIST) {
    g_warning ("Not a LIST object");
    return FALSE;
  }
  gst_bytestream_flush_fast (riff->bs, 8);
  if (gst_bytestream_peek_bytes (riff->bs, &data, 4) != 4) {
    gst_element_error (GST_ELEMENT (riff), "Read error");
    return FALSE;
  }
  gst_bytestream_flush_fast (riff->bs, 4);
  *tag = GUINT32_FROM_LE (* (guint32 *) data);

  /* remember level */
  level = g_new (GstRiffLevel, 1);
  level->start = gst_bytestream_tell (riff->bs);
  level->length = length - 4;
  riff->level = g_list_append (riff->level, level);

  return TRUE;
}

/*
 * Utility function for reading metadata in a RIFF file.
 */

gboolean
gst_riff_read_info (GstRiffRead *riff)
{
  guint32 tag;
  guint64 end;
  GstRiffLevel *level;
  GList *last;
  gchar *name, *type;
  GstCaps *caps;

  /* What we're doing here is ugly (oh no!); we look
   * at our LIST tag size and assure that we do not
   * cross boundaries. This is to maintain the level
   * counter for the client app. */
  last = g_list_last (riff->level);
  level = last->data;
  riff->level = g_list_remove (riff->level, level);
  end = level->start + level->length;
  g_free (level);

  caps = gst_caps_new_simple ("application/x-gst-metadata", NULL);

  while (gst_bytestream_tell (riff->bs) < end) {
    if (!gst_riff_peek_head (riff, &tag, NULL, NULL)) {
      return FALSE;
    }

    /* find out the type of metadata */
    switch (tag) {
      case GST_RIFF_INFO_IARL:
        type = "Location";
        break;
      case GST_RIFF_INFO_IART:
        type = "Artist";
        break;
      case GST_RIFF_INFO_ICMS:
        type = "Commissioner";
        break;
      case GST_RIFF_INFO_ICMT:
        type = "Comment";
        break;
      case GST_RIFF_INFO_ICOP:
        type = "Copyright";
        break;
      case GST_RIFF_INFO_ICRD:
        type = "Creation Date";
        break;
      case GST_RIFF_INFO_ICRP:
        type = "Cropped";
        break;
      case GST_RIFF_INFO_IDIM:
        type = "Dimensions";
        break;
      case GST_RIFF_INFO_IDPI:
        type = "Dots per Inch";
        break;
      case GST_RIFF_INFO_IENG:
        type = "Engineer";
        break;
      case GST_RIFF_INFO_IGNR:
        type = "Genre";
        break;
      case GST_RIFF_INFO_IKEY:
        type = "Keywords";
        break;
      case GST_RIFF_INFO_ILGT:
        type = "Lightness";
        break;
      case GST_RIFF_INFO_IMED:
        type = "Medium";
        break;
      case GST_RIFF_INFO_INAM:
        type = "Title"; /* "Name" */
        break;
      case GST_RIFF_INFO_IPLT:
        type = "Palette";
        break;
      case GST_RIFF_INFO_IPRD:
        type = "Product";
        break;
      case GST_RIFF_INFO_ISBJ:
        type = "Subject";
        break;
      case GST_RIFF_INFO_ISFT:
        type = "Encoder"; /* "Software" */
        break;
      case GST_RIFF_INFO_ISHP:
        type = "Sharpness";
        break;
      case GST_RIFF_INFO_ISRC:
        type = "Source";
        break;
      case GST_RIFF_INFO_ISRF:
        type = "Source Form";
        break;
      case GST_RIFF_INFO_ITCH:
        type = "Technician";
        break;
      default:
        type = NULL;
        GST_WARNING ("Unknown INFO (metadata) tag entry " GST_FOURCC_FORMAT,
		     GST_FOURCC_ARGS (tag));
        break;
    }

    if (type) {
      if (!gst_riff_read_ascii (riff, &tag, &name)) {
        return FALSE;
      }

      gst_caps_set_simple (caps, type, G_TYPE_STRING, name, NULL);
    } else {
      gst_riff_read_skip (riff);
    }
  }

  /* let the world know about this wonderful thing */
  gst_caps_replace (&riff->metadata, caps);
  g_object_notify (G_OBJECT (riff), "metadata");

  return TRUE;
}

/*
 * Read RIFF header and document type.
 */

gboolean
gst_riff_read_header (GstRiffRead *riff,
		      guint32     *doctype)
{
  GstRiffLevel *level;
  guint32 tag, length;
  guint8 *data;

  /* We ignore size for openDML-2.0 support */
  if (!gst_riff_peek_head (riff, &tag, &length, NULL))
    return FALSE;
  if (tag != GST_RIFF_TAG_RIFF) {
    GST_WARNING ("Not a RIFF file");
    return FALSE;
  }
  gst_bytestream_flush_fast (riff->bs, 8);

  /* doctype */
  if (gst_bytestream_peek_bytes (riff->bs, &data, 4) != 4) {
    gst_element_error (GST_ELEMENT (riff), "Read error");
    return FALSE;
  }
  gst_bytestream_flush_fast (riff->bs, 4);
  *doctype = GUINT32_FROM_LE (* (guint32 *) data);

  /* remember level */
  level = g_new (GstRiffLevel, 1);
  level->start = gst_bytestream_tell (riff->bs);
  level->length = length - 4;
  riff->level = g_list_append (riff->level, level);

  return TRUE;
}
