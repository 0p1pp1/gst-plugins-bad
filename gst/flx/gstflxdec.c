/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@temple-baptist.com>
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

#include <string.h>

#include "flx_fmt.h"
#include "gstflxdec.h"

#define JIFFIE  (GST_SECOND/70)

static GstCaps* flxdec_type_find(GstBuffer *buf, gpointer private);

/* flx element information */
static GstElementDetails flxdec_details = {
  "FLX Decoder",
  "Codec/Audio/Decoder",
  "LGPL",
  "FLX decoder",
  VERSION,
  "Sepp Wijnands <mrrazz@garbage-coderz.net>"
  "(C) 2001",
};

static GstTypeDefinition flxdec_definition = {
  "flxdec_video/fli",
  "video/fli",
  ".flc .fli",
  flxdec_type_find,
};

/* Flx signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0
};

/* input */
GST_PAD_TEMPLATE_FACTORY (sink_factory,
  "sink",          
  GST_PAD_SINK, 
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "flxdec_sink",         
    "video/fli",
     NULL
  )
)

/* output */
GST_PAD_TEMPLATE_FACTORY (src_video_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "src_video",
    "video/raw",
      "format",       GST_PROPS_FOURCC (GST_MAKE_FOURCC ('R', 'G', 'B', ' ')),
        "bpp",        GST_PROPS_INT (32),
        "depth",      GST_PROPS_INT (32),
        "endianness", GST_PROPS_INT (G_LITTLE_ENDIAN),
        "red_mask",   GST_PROPS_INT (0x00ff0000),
        "green_mask", GST_PROPS_INT (0x0000ff00),
        "blue_mask",  GST_PROPS_INT (0x000000ff),
        "width",      GST_PROPS_INT_RANGE(320, 1280), 
        "height",     GST_PROPS_INT_RANGE(200, 1024)
  )
)


static void	gst_flxdec_class_init	(GstFlxDecClass *klass);
static void	gst_flxdec_init		(GstFlxDec *flxdec);

static void	gst_flxdec_loop		(GstElement *element);

static GstElementStateReturn 
		gst_flxdec_change_state (GstElement *element);

static void	gst_flxdec_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_flxdec_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);


static void 	flx_decode_color	(GstFlxDec *, guchar *, guchar *, gint);
static void 	flx_decode_brun		(GstFlxDec *, guchar *, guchar *);
static void 	flx_decode_delta_fli	(GstFlxDec *, guchar *, guchar *);
static void 	flx_decode_delta_flc	(GstFlxDec *, guchar *, guchar *);

#define rndalign(off) ((off) + ((off) % 2))

static GstElementClass *parent_class = NULL;

static GstCaps* 
flxdec_type_find (GstBuffer *buf, gpointer private)
{
  guchar *data = GST_BUFFER_DATA(buf);
  GstCaps *new;

  /* check magic */
  if ((data[4] == 0x11 || data[4] == 0x12
       || data[4] == 0x30 || data[4] == 0x44) && data[5] == 0xaf) {
      /* check the frame type of the first frame */
      if ((data[132] == 0x00 || data[132] == 0xfa) && data[133] == 0xf1) {
        g_print("GstFlxDec: found supported flx format\n");
        new = gst_caps_new("flxdec_type_find","video/fli", NULL);
        return new;
      }
  }
  
  return NULL; 
}


GType
gst_flxdec_get_type(void) 
{
  static GType flxdec_type = 0;

  if (!flxdec_type) {
    static const GTypeInfo flxdec_info = {
      sizeof(GstFlxDecClass),      NULL,
      NULL,
      (GClassInitFunc)gst_flxdec_class_init,
      NULL,
      NULL,
      sizeof(GstFlxDec),
      0,
      (GInstanceInitFunc)gst_flxdec_init,
    };
    flxdec_type = g_type_register_static(GST_TYPE_ELEMENT, "GstFlxDec", &flxdec_info, 0);
  }
  return flxdec_type;
}

static void 
gst_flxdec_class_init (GstFlxDecClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_flxdec_set_property;  
  gobject_class->get_property = gst_flxdec_get_property;

  gstelement_class->change_state = gst_flxdec_change_state;
}


  
static void 
gst_flxdec_init(GstFlxDec *flxdec) 
{
  flxdec->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(flxdec),flxdec->sinkpad);
  gst_element_set_loop_function(GST_ELEMENT(flxdec),gst_flxdec_loop);

  flxdec->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_video_factory), "src");
  gst_element_add_pad(GST_ELEMENT(flxdec),flxdec->srcpad);

  flxdec->bs = NULL;
  flxdec->frame = NULL;
  flxdec->delta = NULL;
}

static void
flx_decode_chunks (GstFlxDec *flxdec , gulong count, gchar *data, gchar *dest)
{
  FlxFrameChunk  *hdr;

  g_return_if_fail(data != NULL);

  while (count--) {
    hdr  = (FlxFrameChunk *) data;
    data += FlxFrameChunkSize;

    switch(hdr->id) 
    {
      case FLX_COLOR64:
        flx_decode_color(flxdec, data, dest, 2);
        data += rndalign(hdr->size) - FlxFrameChunkSize;
        break;

      case FLX_COLOR256:
        flx_decode_color(flxdec, data, dest, 0);
        data += rndalign(hdr->size) - FlxFrameChunkSize;
        break;

      case FLX_BRUN:
        flx_decode_brun(flxdec, data, dest);
        data += rndalign(hdr->size) - FlxFrameChunkSize;
        break;

      case FLX_LC:
        flx_decode_delta_fli(flxdec, data, dest);
        data += rndalign(hdr->size) - FlxFrameChunkSize;
        break;

      case FLX_SS2:
        flx_decode_delta_flc(flxdec, data, dest);
        data += rndalign(hdr->size) - FlxFrameChunkSize;
        break;

      case FLX_BLACK:
        memset(dest, 0, flxdec->size);
        break;

      case FLX_MINI:
        data += rndalign(hdr->size) - FlxFrameChunkSize;
        break;

      default:
        g_print("GstFlxDec: Unimplented chunk type: 0x%02x size: %d\n",
                 hdr->id, hdr->size);
        g_print("GstFlxDec: Skipping...\n");
        data += rndalign(hdr->size) - FlxFrameChunkSize;
        break;
    }
  }
}


static void
flx_decode_color(GstFlxDec *flxdec, guchar *data, guchar *dest, gint scale)
{
  guint packs, count, indx;

  g_return_if_fail(flxdec != NULL);

  packs = (data[0] + (data[1] << 8));

  data += 2;
  indx  = 0;

  g_print("GstFlxDec: cmap packs: %d\n", packs);
  while (packs--) {
    /* color map index + skip count */
    indx += *data++;

    /* number of rgb triplets */
    count = *data++ & 0xff;
    if (count == 0)
      count = 256;

    g_print("GstFlxDec: cmap count: %d (indx: %d)\n", count, indx);
    flx_set_palette_vector(flxdec->converter, indx, count, data, scale);

    data += (count * 3);
  }
}

static void 
flx_decode_brun(GstFlxDec *flxdec, guchar *data, guchar *dest)
{
  gulong  count, lines, row;
  guchar  x;
  
  g_return_if_fail(flxdec != NULL);

  lines = flxdec->hdr.height;
  while(lines--) {
    /* packet count.  
     * should not be used anymore, since the flc format can
     * contain more then 255 RLE packets. we use the frame 
     * width instead. 
     */
    data++;

    row = flxdec->hdr.width;
    while(row) {
      count = *data++;

      if (count > 0x7f) { 
        /* literal run */
        count = 0x100 - count;
        row -= count;

        while(count--) 
          *dest++ = *data++;
        
      } else {
        /* replicate run */
        row -= count;
        x = *data++;

        while(count--) 
          *dest++ = x;
      }
    }
  }
}

static void 
flx_decode_delta_fli(GstFlxDec *flxdec, guchar *data, guchar *dest)
{
  gulong  count, packets, lines, start_line, start_l;
  guchar  *start_p,  x;
  
  g_return_if_fail(flxdec != NULL);
  g_return_if_fail(flxdec->delta != NULL);


  /* use last frame for delta */
  memcpy(dest, GST_BUFFER_DATA(flxdec->delta), 
         GST_BUFFER_SIZE(flxdec->delta));

  start_line = (data[0] + (data[1] << 8));
  lines      = (data[2] + (data[3] << 8));
  data    += 4;

  /* start position of delta */
  dest += (flxdec->hdr.width * start_line);
  start_p = dest;
  start_l = lines;

  while(lines--) {
    /* packet count */
    packets = *data++;

    while(packets--) {
      /* skip count */
      dest += *data++;

      /* RLE count */
      count = *data++;

      if (count > 0x7f) { 
        /* literal run */
        count = 0x100 - count;
        x = *data++;

        while (count--)
          *dest++ = x;

      } else {
        /* replicate run */
        while (count--) 
          *dest++ = *data++;
      }
    }
    start_p += flxdec->hdr.width;
    dest = start_p;
  }                  
}

static void 
flx_decode_delta_flc(GstFlxDec *flxdec, guchar *data, guchar *dest)
{
  gulong  count, lines, start_l, opcode;
  guchar  *start_p;
  
  g_return_if_fail(flxdec != NULL);
  g_return_if_fail(flxdec->delta != NULL);


  /* use last frame for delta */
  memcpy(dest, GST_BUFFER_DATA(flxdec->delta), 
         GST_BUFFER_SIZE(flxdec->delta));

  lines      = (data[0] + (data[1] << 8));
  data    += 2;

  start_p    = dest;
  start_l    = lines;

  while (lines) {
    dest = start_p + (flxdec->hdr.width * (start_l - lines));

    /* process opcode(s) */
    while ((opcode = (data[0] + (data[1] << 8))) & 0xc000) {
      data += 2;
      if ((opcode & 0xc000) == 0xc000) {
        /* skip count */
        start_l += (0x10000 - opcode);  
        dest    += flxdec->hdr.width * (0x10000 - opcode);
      } else {
        /* last pixel */
        dest    += flxdec->hdr.width;
        *dest++  = (opcode & 0xff);
      }        
    }
    data += 2;

    /* last opcode is the packet count */
    while(opcode--) {
      /* skip count */
      dest += *data++;
  
      /* RLE count */
      count = *data++;
      
      if (count > 0x7f) {
        /* replicate word run */
        count = 0x100 - count;
        while (count--) {
          *dest++ = data[0];
          *dest++ = data[1];
        }
        data += 2;
      } else {
        /* literal word run */
        while (count--) {
          *dest++ = *data++;
          *dest++ = *data++;
        }
      }
    }
    lines--;
  }
}
          
static GstBuffer*
flx_get_data(GstFlxDec *flxdec, gulong size)
{
  GstBuffer *retbuf;
  guint32 got_bytes;

  g_return_val_if_fail (flxdec != NULL, NULL);

  got_bytes = gst_bytestream_read (flxdec->bs, &retbuf, size);
  if (got_bytes < size) {
    GstEvent *event;
    guint32 remaining;

    gst_bytestream_get_status (flxdec->bs, &remaining, &event);
    gst_pad_event_default (flxdec->sinkpad, event);
  }

  return retbuf;
}


static void 
gst_flxdec_loop (GstElement *element) 
{
  GstBuffer  *buf;  
  GstBuffer  *databuf;
  guchar     *data, *chunk;

  GstFlxDec         *flxdec;  
  FlxHeader      *flxh;
  FlxFrameChunk  *flxfh;
  
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_FLXDEC(element));

  GST_DEBUG (0, "entering loop function");
  
  flxdec = GST_FLXDEC(element);

  if (flxdec->state == GST_FLXDEC_READ_HEADER) {
    databuf = flx_get_data(flxdec, FlxHeaderSize);

    if (!databuf) {
      g_print ("empty buffer\n");
      return;
    }

    data = GST_BUFFER_DATA(databuf);

    memcpy((char *) &flxdec->hdr, data, sizeof(FlxHeader));

    gst_buffer_unref (databuf);

    flxh = &flxdec->hdr;

    /* check header */
    if (flxh->type != FLX_MAGICHDR_FLI &&
      flxh->type != FLX_MAGICHDR_FLC &&
      flxh->type != FLX_MAGICHDR_FLX) {
      gst_element_error (element, "not a flx file (type %d)\n", flxh->type);
      return;
    }
  
  
    g_print("GstFlxDec:       size      :  %d\n", flxh->size);
    g_print("GstFlxDec:       frames    :  %d\n", flxh->frames);
    g_print("GstFlxDec:       width     :  %d\n", flxh->width);
    g_print("GstFlxDec:       height    :  %d\n", flxh->height);
    g_print("GstFlxDec:       depth     :  %d\n", flxh->depth);
    g_print("GstFlxDec:       speed     :  %d\n", flxh->speed);

    flxdec->next_time = 0;

    if (flxh->type == FLX_MAGICHDR_FLI) {
      flxdec->frame_time = JIFFIE * flxh->speed;
    }
    else {
      flxdec->frame_time = flxh->speed * GST_MSECOND;
    }
    
    gst_pad_try_set_caps (flxdec->srcpad,
  		gst_caps_new (
  	  	  "src_video",
	  	  "video/raw",
		  gst_props_new (
		    "format",       GST_PROPS_FOURCC (GST_MAKE_FOURCC ('R', 'G', 'B', ' ')),
		      "bpp",        GST_PROPS_INT (32),
		      "depth",      GST_PROPS_INT (32),
		      "endianness", GST_PROPS_INT (G_LITTLE_ENDIAN),
		      "red_mask",   GST_PROPS_INT (0x00ff0000),
		      "green_mask", GST_PROPS_INT (0x0000ff00),
		      "blue_mask",  GST_PROPS_INT (0x000000ff),
  		      "width",      GST_PROPS_INT (flxh->width), 
  		      "height",     GST_PROPS_INT (flxh->height),
		    NULL)));

    if (flxh->depth <= 8) 
      flxdec->converter = flx_colorspace_converter_new(flxh->width, flxh->height);

    if (flxh->type == FLX_MAGICHDR_FLC || 
        flxh->type == FLX_MAGICHDR_FLX) {
      g_print("GstFlxDec: (FLC) aspect_dx :  %d\n", flxh->aspect_dx);
      g_print("GstFlxDec: (FLC) aspect_dy :  %d\n", flxh->aspect_dy);
      g_print("GstFlxDec: (FLC) oframe1   :  0x%08x\n", flxh->oframe1);
      g_print("GstFlxDec: (FLC) oframe2   :  0x%08x\n", flxh->oframe2);
    }

  
    flxdec->size = (flxh->width * flxh->height);
  
    /* create delta and output frame */
    flxdec->frame = gst_buffer_new();
    flxdec->delta = gst_buffer_new();
    GST_BUFFER_DATA(flxdec->frame) = g_malloc(flxdec->size);
    GST_BUFFER_SIZE(flxdec->frame) = flxdec->size;
    GST_BUFFER_DATA(flxdec->delta) = g_malloc(flxdec->size);
    GST_BUFFER_SIZE(flxdec->delta) = flxdec->size;

    flxdec->state = GST_FLXDEC_PLAYING;
  }
  else if (flxdec->state == GST_FLXDEC_PLAYING) {
    GstBuffer *out;

    databuf = flx_get_data(flxdec, FlxFrameChunkSize);
    if (!databuf)
      return;

    flxfh = (FlxFrameChunk *) GST_BUFFER_DATA(databuf);
    
    switch(flxfh->id) {
      case FLX_FRAME_TYPE:
        buf = flx_get_data(flxdec, flxfh->size-FlxFrameChunkSize);
 
        chunk = GST_BUFFER_DATA(buf);
 
        if (((FlxFrameType *)chunk)->chunks == 0)
          break;

        /* create 32 bits output frame */
        out = gst_buffer_new();
        GST_BUFFER_DATA(out) = g_malloc(flxdec->size * 4);
        GST_BUFFER_SIZE(out) = flxdec->size * 4;

        /* decode chunks */
        flx_decode_chunks(flxdec, 
                         ((FlxFrameType *)chunk)->chunks, 
                         GST_BUFFER_DATA(buf) + FlxFrameTypeSize,
                         GST_BUFFER_DATA(flxdec->frame));
 
        /* destroy input buffer*/
        gst_buffer_unref(buf);
    
        /* save copy of the current frame for possible delta. */
        memcpy(GST_BUFFER_DATA(flxdec->delta), 
               GST_BUFFER_DATA(flxdec->frame), 
               GST_BUFFER_SIZE(flxdec->delta));

        /* convert current frame. */
        flx_colorspace_convert(flxdec->converter,
             GST_BUFFER_DATA(flxdec->frame),
             GST_BUFFER_DATA(out));

	GST_BUFFER_TIMESTAMP (out) = flxdec->next_time;
	flxdec->next_time += flxdec->frame_time;

        gst_pad_push(flxdec->srcpad, out);
        
        break;
    }

    /* destroy header buffer */
    gst_buffer_unref(databuf);
  }
}

static GstElementStateReturn 
gst_flxdec_change_state (GstElement *element)
{
  GstFlxDec *flxdec;

  flxdec = GST_FLXDEC(element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      flxdec->bs = gst_bytestream_new (flxdec->sinkpad);
      flxdec->state = GST_FLXDEC_READ_HEADER;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_buffer_unref (flxdec->frame);
      flxdec->frame = NULL;
      gst_buffer_unref (flxdec->delta);
      flxdec->delta = NULL;
      gst_bytestream_destroy (flxdec->bs);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }
  
  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static void 
gst_flxdec_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstFlxDec *flxdec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_FLXDEC(object));
  flxdec = GST_FLXDEC(object);

  switch (prop_id) {
    default:
      break;
  }
}

static void 
gst_flxdec_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
  GstFlxDec *flxdec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_FLXDEC(object));
  flxdec = GST_FLXDEC(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  /* this filter needs the bytestream package */
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  factory = gst_element_factory_new("flxdec", GST_TYPE_FLXDEC, &flxdec_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_video_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  type = gst_type_factory_new (&flxdec_definition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "flxdec",
  plugin_init
};
