/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2002,2003> David A. Schleef <ds@schleef.org>
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
#include "gstswfdec.h"
#include <string.h>

/* elementfactory information */
static GstElementDetails gst_swfdec_details = {
  "SWF video decoder",
  "Codec/Video/Decoder",
  "LGPL",
  "Uses libswfdec to decode Flash video streams",
  VERSION,
  "David Schleef <ds@schleef.org>",
  "(C) 2002",
};

/* Swfdec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (video_template_factory,
  "video_00",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "swfdec_videosrc",
    "video/x-raw-rgb",
      "width",		GST_PROPS_INT_RANGE (16, 4096),
      "height",		GST_PROPS_INT_RANGE (16, 4096),
      "bpp",		GST_PROPS_INT (24),
      "depth",		GST_PROPS_INT (24),
      "endianness",	GST_PROPS_INT (G_BIG_ENDIAN),
      "red_mask",	GST_PROPS_INT (0xff0000),
      "green_mask",	GST_PROPS_INT (0x00ff00),
      "blue_mask",	GST_PROPS_INT (0x0000ff),
      "framerate",	GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT)
  )
);

GST_PAD_TEMPLATE_FACTORY (audio_template_factory,
  "audio_00",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "swfdec_audiosrc",
    "audio/x-raw-int",
      "endianness",	GST_PROPS_INT(G_BYTE_ORDER),
      "signed",		GST_PROPS_BOOLEAN(TRUE),
      "width",		GST_PROPS_INT(16),
      "depth",		GST_PROPS_INT(16),
      "rate",		GST_PROPS_INT(44100),
      "channels",	GST_PROPS_INT(2)
  )
);

GST_PAD_TEMPLATE_FACTORY (sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "swfdec_sink",
    "application/x-shockwave-flash",
    NULL
  )
);

static void	gst_swfdec_class_init		(GstSwfdecClass *klass);
static void	gst_swfdec_init		(GstSwfdec *swfdec);

static void	gst_swfdec_dispose		(GObject *object);

static void	gst_swfdec_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void	gst_swfdec_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

#if 0
static gboolean gst_swfdec_src_event       	(GstPad *pad, GstEvent *event);
#endif
static gboolean gst_swfdec_src_query 		(GstPad *pad, GstQueryType type,
		       				 GstFormat *format, gint64 *value);

#if 0
static gboolean gst_swfdec_convert_sink 	(GstPad *pad, GstFormat src_format, gint64 src_value,
		         			 GstFormat *dest_format, gint64 *dest_value);
static gboolean gst_swfdec_convert_src 	(GstPad *pad, GstFormat src_format, gint64 src_value,
		        	 		 GstFormat *dest_format, gint64 *dest_value);
#endif

static GstElementStateReturn gst_swfdec_change_state	(GstElement *element);


static GstElementClass *parent_class = NULL;

GType
gst_swfdec_get_type (void)
{
  static GType swfdec_type = 0;

  if (!swfdec_type) {
    static const GTypeInfo swfdec_info = {
      sizeof(GstSwfdecClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_swfdec_class_init,
      NULL,
      NULL,
      sizeof(GstSwfdec),
      0,
      (GInstanceInitFunc)gst_swfdec_init,
    };
    swfdec_type = g_type_register_static(GST_TYPE_ELEMENT, "GstSwfdec", &swfdec_info, 0);
  }
  return swfdec_type;
}

static void
gst_swfdec_class_init(GstSwfdecClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property 	= gst_swfdec_set_property;
  gobject_class->get_property 	= gst_swfdec_get_property;
  gobject_class->dispose 	= gst_swfdec_dispose;

  gstelement_class->change_state = gst_swfdec_change_state;
}

static GstCaps *gst_swfdec_videosrc_getcaps(GstPad *pad, GstCaps *caps)
{
  GstSwfdec *swfdec;
  GstCaps *c;

  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  c = GST_CAPS_NEW (
    "swfdec_videosrc",
    "video/x-raw-rgb",
      "width",		GST_PROPS_INT_RANGE (16, 4096),
      "height",		GST_PROPS_INT_RANGE (16, 4096),
      "bpp",		GST_PROPS_INT (24),
      "depth",		GST_PROPS_INT (24),
      "endianness",	GST_PROPS_INT (G_BIG_ENDIAN),
      "red_mask",	GST_PROPS_INT (0xff0000),
      "green_mask",	GST_PROPS_INT (0x00ff00),
      "blue_mask",	GST_PROPS_INT (0x0000ff),
      "framerate",	GST_PROPS_FLOAT (swfdec->frame_rate)
  );

  if(swfdec->height){
    gst_caps_set(c,"height",GST_PROPS_INT(swfdec->height));
    gst_caps_set(c,"width",GST_PROPS_INT(swfdec->width));
  }

  return c;
}

static void
gst_swfdec_loop(GstElement *element)
{
	GstSwfdec *swfdec;
	GstBuffer *buf = NULL;
	int ret;

	g_return_if_fail(element != NULL);
	g_return_if_fail(GST_IS_SWFDEC(element));

	swfdec = GST_SWFDEC(element);

	if(!swfdec->videopad){
	}

	ret = swfdec_decoder_parse(swfdec->state);
	if(ret==SWF_NEEDBITS){
		buf = GST_BUFFER (gst_pad_pull(swfdec->sinkpad));
		if(GST_IS_EVENT(buf)){
			switch (GST_EVENT_TYPE (buf)) {
			case GST_EVENT_EOS:
				printf("got eos\n");
				break;
			default:
				printf("got event\n");
				break;
			}

		}else{
			if(!GST_BUFFER_DATA(buf)){
				printf("expected non-null buffer\n");
			}
			ret = swfdec_decoder_addbits(swfdec->state,
				GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
		}
	}

	if(ret==SWF_CHANGE){
		swfdec_decoder_get_image_size(swfdec->state,
			&swfdec->width, &swfdec->height);
		swfdec_decoder_get_rate(swfdec->state, &swfdec->rate);
		swfdec->interval = GST_SECOND / swfdec->rate;

		gst_caps_debug(gst_swfdec_videosrc_getcaps(swfdec->videopad,NULL),"ack");
		gst_pad_try_set_caps(swfdec->videopad,
			gst_swfdec_videosrc_getcaps(swfdec->videopad,NULL));
		return;
	}

	if(ret==SWF_IMAGE){
		GstBuffer *newbuf = NULL;
		unsigned char *data;
		int len;

		/* video stuff */
		newbuf = gst_buffer_new();
		GST_BUFFER_SIZE(newbuf) = swfdec->width * swfdec->height * 3;

		swfdec_decoder_get_image(swfdec->state, &data);
		GST_BUFFER_DATA(newbuf) = data;

		swfdec->timestamp += swfdec->interval;
		GST_BUFFER_TIMESTAMP(newbuf) = swfdec->timestamp;

		gst_pad_push(swfdec->videopad, GST_DATA (newbuf));

		/* audio stuff */

		data = swfdec_decoder_get_sound_chunk(swfdec->state, &len);
		while(data){
			newbuf = gst_buffer_new();

			GST_BUFFER_SIZE(newbuf) = len;
			GST_BUFFER_DATA(newbuf) = data;
			GST_BUFFER_TIMESTAMP(newbuf) = swfdec->timestamp;

			gst_pad_push(swfdec->audiopad, GST_DATA (newbuf));

			data = swfdec_decoder_get_sound_chunk(swfdec->state, &len);
		}
	}

	if(ret==SWF_EOF){
		gst_pad_push(swfdec->videopad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
		gst_pad_push(swfdec->audiopad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
	}
}

static void
gst_swfdec_init (GstSwfdec *swfdec)
{
  /* create the sink and src pads */
  swfdec->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (swfdec), swfdec->sinkpad);

  swfdec->videopad = gst_pad_new_from_template(
		GST_PAD_TEMPLATE_GET(video_template_factory),
		"video_00");
  gst_pad_set_query_function (swfdec->videopad,
		GST_DEBUG_FUNCPTR (gst_swfdec_src_query));
  gst_pad_set_getcaps_function (swfdec->videopad,
		GST_DEBUG_FUNCPTR (gst_swfdec_videosrc_getcaps));
  gst_element_add_pad(GST_ELEMENT(swfdec), swfdec->videopad);

  swfdec->audiopad = gst_pad_new_from_template(
		GST_PAD_TEMPLATE_GET(audio_template_factory),
		"audio_00");
  gst_pad_set_query_function (swfdec->audiopad,
		GST_DEBUG_FUNCPTR (gst_swfdec_src_query));

  gst_element_add_pad(GST_ELEMENT(swfdec), swfdec->audiopad);
  
  gst_element_set_loop_function(GST_ELEMENT(swfdec), gst_swfdec_loop);

  /* initialize the swfdec decoder state */
  swfdec->state = swfdec_decoder_new();
  g_return_if_fail(swfdec->state != NULL);

  swfdec_decoder_set_colorspace(swfdec->state, SWF_COLORSPACE_RGB888);

  GST_FLAG_SET (GST_ELEMENT (swfdec), GST_ELEMENT_EVENT_AWARE);

  swfdec->frame_rate = 0.;
}

static void
gst_swfdec_dispose (GObject *object)
{
  //GstSwfdec *swfdec = GST_SWFDEC (object);

  /* FIXME */
  //swfdec_decoder_free(swfdec->state);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

#if 0
static gboolean
gst_swfdec_convert_sink (GstPad *pad, GstFormat src_format, gint64 src_value,
		           GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  GstSwfdec *swfdec;
	      
  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}
#endif

#if 0
static gboolean
gst_swfdec_convert_src (GstPad *pad, GstFormat src_format, gint64 src_value,
		          GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  GstSwfdec *swfdec;
	      
  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * 6 * (swfdec->width * swfdec->height >> 2) *  
		  video_rates[swfdec->decoder->frame_rate_code] / GST_SECOND;
	  break;
        case GST_FORMAT_DEFAULT:
	  *dest_value = src_value * video_rates[swfdec->decoder->frame_rate_code] / GST_SECOND;
	  break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
	  if (video_rates[swfdec->decoder->frame_rate_code] != 0.0) {
	    *dest_value = src_value * GST_SECOND /
	      video_rates[swfdec->decoder->frame_rate_code];
	  }
	  else
	    res = FALSE;
	  break;
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * 6 * (swfdec->width * swfdec->height >> 2);
	  break;
        case GST_FORMAT_DEFAULT:
	  *dest_value = src_value;
	  break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}
#endif

static gboolean 
gst_swfdec_src_query (GstPad *pad, GstQueryType type,
		        GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GstSwfdec *swfdec;

  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
    {
      switch (*format) {
        case GST_FORMAT_TIME:
	{
	  int n_frames;
	  int ret;

          res = FALSE;
	  ret = swfdec_decoder_get_n_frames(swfdec->state, &n_frames);
	  if(ret == SWF_OK){
	    *value = n_frames * swfdec->interval;
            res = TRUE;
	  }
          break;
	}
        default:
	  res = FALSE;
          break;
      }
      break;
    }
    case GST_QUERY_POSITION:
    {
      switch (*format) {
        default:
          res = FALSE;
          break;
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }

  return res;
}

#if 0
static gboolean 
gst_swfdec_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstSwfdec *swfdec;
  static const GstFormat formats[] = { GST_FORMAT_TIME, GST_FORMAT_BYTES };
#define MAX_SEEK_FORMATS 1 /* we can only do time seeking for now */
  gint i;

  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    /* the all-formats seek logic */
    case GST_EVENT_SEEK:
    {
      gint64 src_offset;
      gboolean flush;
      GstFormat format;
			                
      format = GST_FORMAT_TIME;

      /* first bring the src_format to TIME */
      if (!gst_pad_convert (pad,
                GST_EVENT_SEEK_FORMAT (event), GST_EVENT_SEEK_OFFSET (event),
                &format, &src_offset))
      {
        /* didn't work, probably unsupported seek format then */
        res = FALSE;
        break;
      }

      /* shave off the flush flag, we'll need it later */
      flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;

      /* assume the worst */
      res = FALSE;

      /* while we did not exhaust our seek formats without result */
      for (i = 0; i < MAX_SEEK_FORMATS && !res; i++) {
        gint64 desired_offset;

        format = formats[i];

        /* try to convert requested format to one we can seek with on the sinkpad */
        if (gst_pad_convert (swfdec->sinkpad, GST_FORMAT_TIME, src_offset, &format, &desired_offset))
        {
          GstEvent *seek_event;

          /* conversion succeeded, create the seek */
          seek_event = gst_event_new_seek (formats[i] | GST_SEEK_METHOD_SET | flush, desired_offset);
          /* do the seekk */
          if (gst_pad_send_event (GST_PAD_PEER (swfdec->sinkpad), seek_event)) {
            /* seek worked, we're done, loop will exit */
            res = TRUE;
          }
        }
        /* at this point, either the seek worked or res == FALSE */
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }
  gst_event_unref (event);
  return res;
}
#endif

static GstElementStateReturn
gst_swfdec_change_state (GstElement *element)
{
  GstSwfdec *swfdec = GST_SWFDEC (element);

  switch (GST_STATE_TRANSITION (element)) { 
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
    {
      //gst_swfdec_vo_open (swfdec);
      //swfdec_decoder_new (swfdec->decoder, swfdec->accel, swfdec->vo);

      //swfdec->decoder->is_sequence_needed = 1;
      //swfdec->decoder->frame_rate_code = 0;
      swfdec->timestamp = 0;
      swfdec->pool = NULL;
      swfdec->closed = FALSE;

      /* reset the initial video state */
      swfdec->format = -1;
      swfdec->width = -1;
      swfdec->height = -1;
      swfdec->first = TRUE;
      break;
    }
    case GST_STATE_PAUSED_TO_PLAYING:
      /* try to get a bufferpool */
#if 0
      swfdec->pool = gst_pad_get_bufferpool (swfdec->videopad);
      if (swfdec->pool)
        GST_INFO ( "got pool %p", swfdec->pool);
#endif
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      /* need to clear things we get from other plugins, since we could be reconnected */
      if (swfdec->pool) {
	gst_buffer_pool_unref (swfdec->pool);
	swfdec->pool = NULL;
      }
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* if we are not closed by an EOS event do so now, this cen send a few frames but
       * we are prepared to not really send them (see above) */
      if (!swfdec->closed) {
        /*swf_close (swfdec->decoder); */
	swfdec->closed = TRUE;
      }
      //gst_swfdec_vo_destroy (swfdec);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_swfdec_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSwfdec *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SWFDEC (object));
  src = GST_SWFDEC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_swfdec_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSwfdec *swfdec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SWFDEC (object));
  swfdec = GST_SWFDEC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static GstCaps *
swf_type_find(GstByteStream *bs, gpointer private)
{
        GstBuffer *buf;
	gchar *data;
        
        gst_bytestream_peek (bs, &buf, 4);
        data = GST_BUFFER_DATA(buf);

	if (GST_BUFFER_SIZE (buf) < 4)
	  return NULL;

	if((data[0] != 'F' && data[0] != 'C') ||
	    data[1] != 'W' || data[2] != 'S')return NULL;

	return gst_caps_new("swf_type_find","application/x-shockwave-flash",
		NULL);
}

static GstTypeDefinition swftype_definition = 
	{ "swfdecode/x-shockwave-flash", "application/x-shockwave-flash",
		".swf .swfl", swf_type_find };

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  /* create an elementfactory for the swfdec element */
  factory = gst_element_factory_new("swfdec",GST_TYPE_SWFDEC,
                                   &gst_swfdec_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);

  gst_element_factory_add_pad_template (factory,
		  GST_PAD_TEMPLATE_GET (video_template_factory));
  gst_element_factory_add_pad_template (factory,
		  GST_PAD_TEMPLATE_GET (audio_template_factory));
  gst_element_factory_add_pad_template (factory,
		  GST_PAD_TEMPLATE_GET (sink_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  type = gst_type_factory_new(&swftype_definition);
  gst_plugin_add_feature(plugin, GST_PLUGIN_FEATURE(type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "swfdec",
  plugin_init
};
