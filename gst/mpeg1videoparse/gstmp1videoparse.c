/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

/*#define GST_DEBUG_ENABLED */
#include "gstmp1videoparse.h"

/* Start codes. */
#define SEQ_START_CODE 0x000001b3
#define GOP_START_CODE 0x000001b8
#define PICTURE_START_CODE 0x00000100
#define SLICE_MIN_START_CODE 0x00000101
#define SLICE_MAX_START_CODE 0x000001af
#define EXT_START_CODE 0x000001b5
#define USER_START_CODE 0x000001b2
#define SEQUENCE_ERROR_CODE 0x000001b4
#define SEQ_END_CODE 0x000001b7

/* elementfactory information */
static GstElementDetails mp1videoparse_details = {
  "MPEG 1 Video Parser",
  "Codec/Parser",
  "LGPL",
  "Parses and frames MPEG 1 video streams, provides seek",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

GST_PAD_TEMPLATE_FACTORY (src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mp1videoparse_src",
    "video/mpeg",
      "mpegversion",   GST_PROPS_INT (1),
      "systemstream",  GST_PROPS_BOOLEAN (FALSE),
      "width",         GST_PROPS_INT_RANGE (16, 4096),
      "height",        GST_PROPS_INT_RANGE (16, 4096),
      "pixel_width",   GST_PROPS_INT_RANGE (1, 255),
      "pixel_height",  GST_PROPS_INT_RANGE (1, 255),
      "framerate",     GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT)
  )
);

GST_PAD_TEMPLATE_FACTORY (sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mp1videoparse_sink",
    "video/mpeg",
      "mpegversion",   GST_PROPS_INT (1),
      "systemstream",  GST_PROPS_BOOLEAN (FALSE)
  )
);

/* Mp1VideoParse signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void	gst_mp1videoparse_class_init	(Mp1VideoParseClass *klass);
static void	gst_mp1videoparse_init		(Mp1VideoParse *mp1videoparse);

static void	gst_mp1videoparse_chain		(GstPad *pad, GstBuffer *buf);
static void	gst_mp1videoparse_real_chain	(Mp1VideoParse *mp1videoparse, GstBuffer *buf, GstPad *outpad);
static void	gst_mp1videoparse_flush		(Mp1VideoParse *mp1videoparse);
static GstElementStateReturn
		gst_mp1videoparse_change_state	(GstElement *element);

static GstElementClass *parent_class = NULL;
/*static guint gst_mp1videoparse_signals[LAST_SIGNAL] = { 0 }; */

GType
mp1videoparse_get_type (void)
{
  static GType mp1videoparse_type = 0;

  if (!mp1videoparse_type) {
    static const GTypeInfo mp1videoparse_info = {
      sizeof(Mp1VideoParseClass),      NULL,
      NULL,
      (GClassInitFunc)gst_mp1videoparse_class_init,
      NULL,
      NULL,
      sizeof(Mp1VideoParse),
      0,
      (GInstanceInitFunc)gst_mp1videoparse_init,
    };
    mp1videoparse_type = g_type_register_static(GST_TYPE_ELEMENT, "Mp1VideoParse", &mp1videoparse_info, 0);
  }
  return mp1videoparse_type;
}

static void
gst_mp1videoparse_class_init (Mp1VideoParseClass *klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_mp1videoparse_change_state;
}

static void
gst_mp1videoparse_init (Mp1VideoParse *mp1videoparse)
{
  mp1videoparse->sinkpad = gst_pad_new_from_template (
	GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(mp1videoparse),mp1videoparse->sinkpad);
  gst_pad_set_chain_function(mp1videoparse->sinkpad,gst_mp1videoparse_chain);

  mp1videoparse->srcpad = gst_pad_new_from_template (
	GST_PAD_TEMPLATE_GET (src_factory), "src");
  gst_element_add_pad(GST_ELEMENT(mp1videoparse),mp1videoparse->srcpad);

  mp1videoparse->partialbuf = NULL;
  mp1videoparse->need_resync = FALSE;
  mp1videoparse->last_pts = 0;
  mp1videoparse->picture_in_buffer = 0;
  mp1videoparse->width = mp1videoparse->height = -1;
  mp1videoparse->fps = mp1videoparse->asr = 0.;
}

static void
mp1videoparse_parse_seq (Mp1VideoParse *mp1videoparse, GstBuffer *buf)
{
  gint width, height, asr_idx, fps_idx;
  gfloat asr_table[] = { 0., 1.,
			 0.6735, 0.7031, 0.7615, 0.8055, 0.8437, 
			 0.8935, 0.9157, 0.9815, 1.0255, 1.0695,
			 1.0950, 1.1575, 1.2015 };
  gfloat fps_table[] = { 0., 24./1.001, 24., 25.,
			 30./1.001, 30.,
			 50., 60./1.001, 60. };
  guint32 n = GUINT32_FROM_BE (*(guint32 *) GST_BUFFER_DATA (buf));

  width   = (n & 0xfff00000) >> 20;
  height  = (n & 0x000fff00) >>  8;
  asr_idx = (n & 0x000000f0) >>  4;
  fps_idx = (n & 0x0000000f) >>  0;

  if (fps_idx >= 9 || fps_idx <= 0)
    fps_idx = 3; /* well, we need a default */
  if (asr_idx >= 15 || asr_idx <= 0)
    asr_idx = 1; /* no aspect ratio */

  if (asr_table[asr_idx] != mp1videoparse->asr    ||
      fps_table[fps_idx] != mp1videoparse->fps    ||
      width              != mp1videoparse->width  ||
      height             != mp1videoparse->height) {
    GstCaps *caps;
    gint p_w, p_h;

    mp1videoparse->asr    = asr_table[asr_idx];
    mp1videoparse->fps    = fps_table[fps_idx];
    mp1videoparse->width  = width;
    mp1videoparse->height = height;

    p_w = (asr_table[asr_idx] < 1.0) ? (100 / asr_table[asr_idx]) : 1;
    p_h = (asr_table[asr_idx] > 1.0) ? (100 * asr_table[asr_idx]) : 1;

    caps = GST_CAPS_NEW ("mp1videoparse_src",
                         "video/mpeg",
                           "systemstream", GST_PROPS_BOOLEAN (FALSE),
                           "mpegversion",  GST_PROPS_INT (1),
                           "width",        GST_PROPS_INT (width),
                           "height",       GST_PROPS_INT (height),
                           "framerate",    GST_PROPS_FLOAT (fps_table[fps_idx]),
                           "pixel_width",  GST_PROPS_INT (p_w),
                           "pixel_height", GST_PROPS_INT (p_h));

    gst_caps_debug (caps, "New mpeg1videoparse caps");

    if (gst_pad_try_set_caps (mp1videoparse->srcpad, caps) <= 0) {
      gst_element_error (GST_ELEMENT (mp1videoparse),
                         "mp1videoparse: failed to negotiate a new format");
      return; 
    }
  }
}

static gboolean
mp1videoparse_valid_sync (Mp1VideoParse *mp1videoparse, guint32 head, GstBuffer *buf)
{
  switch (head) {
    case SEQ_START_CODE: {
      GstBuffer *subbuf = gst_buffer_create_sub (buf, 4,
		      				 GST_BUFFER_SIZE (buf) - 4);
      mp1videoparse_parse_seq(mp1videoparse, subbuf);
      gst_buffer_unref (subbuf);
      return TRUE;
    }
    case GOP_START_CODE:
    case PICTURE_START_CODE:
    case USER_START_CODE:
    case EXT_START_CODE:
      return TRUE;
    default:
      if (head >= SLICE_MIN_START_CODE &&
	  head <= SLICE_MAX_START_CODE)
        return TRUE;
  }

  return FALSE;
}

static gint
mp1videoparse_find_next_gop (Mp1VideoParse *mp1videoparse, GstBuffer *buf)
{
  guchar *data = GST_BUFFER_DATA(buf);
  gulong size = GST_BUFFER_SIZE(buf);
  gulong offset = 0;
  gint sync_zeros = 0;
  gboolean have_sync = FALSE;

  while (offset < size) {
    guchar byte = *(data+offset);
    offset++;
    if (byte == 0) {
      sync_zeros++;
    }
    else if (byte == 1 && sync_zeros >=2 ) {
      sync_zeros = 0;
      have_sync = TRUE;
    }
    else if (have_sync) {
      if (byte == (SEQ_START_CODE & 0xff) || byte == (GOP_START_CODE & 0xff)) return offset-4;
      else {
        sync_zeros = 0;
	have_sync = FALSE;
      }
    }
    else {
      sync_zeros = 0;
    }
  }

  return -1;
}
static void
gst_mp1videoparse_flush (Mp1VideoParse *mp1videoparse)
{
  GST_DEBUG ("mp1videoparse: flushing");
  if (mp1videoparse->partialbuf) {
    gst_buffer_unref(mp1videoparse->partialbuf);
    mp1videoparse->partialbuf= NULL;
  }
  mp1videoparse->need_resync = TRUE;
  mp1videoparse->in_flush = TRUE;
  mp1videoparse->picture_in_buffer = 0;
}

static void
gst_mp1videoparse_chain (GstPad *pad,GstBuffer *buf)
{
  Mp1VideoParse *mp1videoparse;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  mp1videoparse = GST_MP1VIDEOPARSE (GST_OBJECT_PARENT (pad));

  gst_mp1videoparse_real_chain(mp1videoparse, buf, mp1videoparse->srcpad);
}

static void
gst_mp1videoparse_real_chain (Mp1VideoParse *mp1videoparse, GstBuffer *buf, GstPad *outpad)
{
  guchar *data;
  gulong size, offset = 0;
  GstBuffer *outbuf;
  gint sync_state;
  gboolean have_sync;
  guchar sync_byte;
  guint32 head;
  gint sync_pos;
  guint64 time_stamp;
  GstBuffer *temp;

  time_stamp = GST_BUFFER_TIMESTAMP(buf);

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_FLUSH:
      case GST_EVENT_DISCONTINUOUS:
        gst_mp1videoparse_flush(mp1videoparse);
        break;
      case GST_EVENT_EOS:
        gst_mp1videoparse_flush(mp1videoparse);
        gst_event_ref(event);
        gst_pad_push(outpad, GST_BUFFER (event));
        gst_element_set_eos (GST_ELEMENT (mp1videoparse));
        break;
      default:
        GST_DEBUG ("Unhandled event type %d",
		   GST_EVENT_TYPE (event));
        break;
    }
 
    gst_event_unref (event);
    return;
  }
 

  if (mp1videoparse->partialbuf) {
    GstBuffer *merge;

    offset = GST_BUFFER_SIZE(mp1videoparse->partialbuf);
    merge = gst_buffer_merge(mp1videoparse->partialbuf, buf);

    gst_buffer_unref(mp1videoparse->partialbuf);
    gst_buffer_unref(buf);

    mp1videoparse->partialbuf = merge;
  }
  else {
    mp1videoparse->partialbuf = buf;
    offset = 0;
  }

  data = GST_BUFFER_DATA(mp1videoparse->partialbuf);
  size = GST_BUFFER_SIZE(mp1videoparse->partialbuf);

  GST_DEBUG ("mp1videoparse: received buffer of %ld bytes %" G_GINT64_FORMAT,size, GST_BUFFER_TIMESTAMP(buf));

  head = GUINT32_FROM_BE(*((guint32 *)data));

  GST_DEBUG ("mp1videoparse: head is %08x", (unsigned int)head);

  if (!mp1videoparse_valid_sync(mp1videoparse, head,
				mp1videoparse->partialbuf) ||
      mp1videoparse->need_resync) {
    sync_pos = mp1videoparse_find_next_gop(mp1videoparse, mp1videoparse->partialbuf);
    if (sync_pos != -1) {
      mp1videoparse->need_resync = FALSE;
      GST_DEBUG ("mp1videoparse: found new gop at %d", sync_pos);

      if (sync_pos != 0) {
        temp = gst_buffer_create_sub(mp1videoparse->partialbuf, sync_pos, size-sync_pos);
	g_assert(temp != NULL);
	gst_buffer_unref(mp1videoparse->partialbuf);
	mp1videoparse->partialbuf = temp;
        data = GST_BUFFER_DATA(mp1videoparse->partialbuf);
        size = GST_BUFFER_SIZE(mp1videoparse->partialbuf);
	offset = 0;
      }
    }
    else {
      GST_DEBUG ("mp1videoparse: could not sync");
      gst_buffer_unref(mp1videoparse->partialbuf);
      mp1videoparse->partialbuf = NULL;
      return;
    }
  }

  if (mp1videoparse->picture_in_buffer == 1) {
    mp1videoparse->last_pts = time_stamp;
  }

  sync_state = 0;
  have_sync = FALSE;

  GST_DEBUG ("mp1videoparse: searching sync");

  while (offset < size-1) {
    sync_byte = *(data + offset);
    /*printf(" %d %02x\n", offset, sync_byte); */
    if (sync_byte == 0) {
      sync_state++;
    }
    else if ((sync_byte == 1) && (sync_state >=2)) {
      GST_DEBUG ("mp1videoparse: code 0x000001%02x",data[offset+1]);
      if (data[offset+1] == (PICTURE_START_CODE & 0xff)) {
	mp1videoparse->picture_in_buffer++;
	if (mp1videoparse->picture_in_buffer == 1) {
          mp1videoparse->last_pts = time_stamp;
	  sync_state = 0;
	}
	else if (mp1videoparse->picture_in_buffer == 2) {
          have_sync = TRUE;
          break;
	}
	else {
          GST_DEBUG ("mp1videoparse: %d in buffer", mp1videoparse->picture_in_buffer);
          g_assert_not_reached();
	}
      }
      else sync_state = 0;
    }
    /* something else... */
    else sync_state = 0;
    /* go down the buffer */
    offset++;
  }

  if (have_sync) {
    offset -= 2;
    GST_DEBUG ("mp1videoparse: synced at %ld code 0x000001%02x",offset,data[offset+3]);

    outbuf = gst_buffer_create_sub(mp1videoparse->partialbuf, 0, offset+4);
    g_assert(outbuf != NULL);
    GST_BUFFER_TIMESTAMP(outbuf) = mp1videoparse->last_pts;
    GST_BUFFER_DURATION(outbuf) = GST_SECOND / mp1videoparse->fps;

    if (mp1videoparse->in_flush) {
      /* FIXME, send a flush event here */
      mp1videoparse->in_flush = FALSE;
    }

    if (GST_PAD_CAPS (outpad) != NULL) {
      GST_DEBUG ("mp1videoparse: pushing  %d bytes %" G_GUINT64_FORMAT, GST_BUFFER_SIZE(outbuf), GST_BUFFER_TIMESTAMP(outbuf));
      gst_pad_push(outpad, outbuf);
      GST_DEBUG ("mp1videoparse: pushing  done");
    } else {
      GST_DEBUG ("No capsnego yet, delaying buffer push");
      gst_buffer_unref (outbuf);
    }
    mp1videoparse->picture_in_buffer = 0;

    temp = gst_buffer_create_sub(mp1videoparse->partialbuf, offset, size-offset);
    gst_buffer_unref(mp1videoparse->partialbuf);
    mp1videoparse->partialbuf = temp;
  }
  else {
    mp1videoparse->last_pts = time_stamp;
  }
}

static GstElementStateReturn 
gst_mp1videoparse_change_state (GstElement *element) 
{
  Mp1VideoParse *mp1videoparse;
  g_return_val_if_fail(GST_IS_MP1VIDEOPARSE(element),GST_STATE_FAILURE);

  mp1videoparse = GST_MP1VIDEOPARSE(element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      gst_mp1videoparse_flush(mp1videoparse);
      mp1videoparse->width = mp1videoparse->height = -1;
      mp1videoparse->fps   = mp1videoparse->asr = 0.;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the mp1videoparse element */
  factory = gst_element_factory_new("mp1videoparse",GST_TYPE_MP1VIDEOPARSE,
                                   &mp1videoparse_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory,
	GST_PAD_TEMPLATE_GET (src_factory));
  gst_element_factory_add_pad_template (factory,
	GST_PAD_TEMPLATE_GET (sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mp1videoparse",
  plugin_init
};
