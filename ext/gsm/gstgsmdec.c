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


#include <string.h>

#include "gstgsmdec.h"

extern GstPadTemplate *gsmdec_src_template, *gsmdec_sink_template;

/* elementfactory information */
GstElementDetails gst_gsmdec_details = {
  "gsm audio decoder",
  "Codec/Audio/Decoder",
  "LGPL",
  ".gsm",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

/* GSMDec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void			gst_gsmdec_class_init	(GstGSMDec *klass);
static void			gst_gsmdec_init		(GstGSMDec *gsmdec);

static void			gst_gsmdec_chain	(GstPad *pad, GstBuffer *buf);
static GstPadLinkReturn	gst_gsmdec_sinkconnect 	(GstPad *pad, GstCaps *caps);

static GstElementClass *parent_class = NULL;
/*static guint gst_gsmdec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_gsmdec_get_type(void) {
  static GType gsmdec_type = 0;

  if (!gsmdec_type) {
    static const GTypeInfo gsmdec_info = {
      sizeof(GstGSMDecClass),      NULL,
      NULL,
      (GClassInitFunc)gst_gsmdec_class_init,
      NULL,
      NULL,
      sizeof(GstGSMDec),
      0,
      (GInstanceInitFunc)gst_gsmdec_init,
    };
    gsmdec_type = g_type_register_static(GST_TYPE_ELEMENT, "GstGSMDec", &gsmdec_info, 0);
  }
  return gsmdec_type;
}

static void
gst_gsmdec_class_init (GstGSMDec *klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}

static void
gst_gsmdec_init (GstGSMDec *gsmdec)
{
  GST_DEBUG (0,"gst_gsmdec_init: initializing");

  /* create the sink and src pads */
  gsmdec->sinkpad = gst_pad_new_from_template (gsmdec_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (gsmdec), gsmdec->sinkpad);
  gst_pad_set_chain_function (gsmdec->sinkpad, gst_gsmdec_chain);
  gst_pad_set_link_function (gsmdec->sinkpad, gst_gsmdec_sinkconnect);

  gsmdec->srcpad = gst_pad_new_from_template (gsmdec_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (gsmdec), gsmdec->srcpad);

  gsmdec->state = gsm_create ();
  gsmdec->bufsize = 0;
}

static GstPadLinkReturn
gst_gsmdec_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstGSMDec *gsmdec;
  gint rate;
  
  gsmdec = GST_GSMDEC (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;
  
  gst_caps_get_int (caps, "rate", &rate);

  if (gst_pad_try_set_caps (gsmdec->srcpad, 
		      GST_CAPS_NEW (
	  		"gsm_raw",
			"audio/raw",
			  "format",       GST_PROPS_STRING ("int"),
			    "law",        GST_PROPS_INT (0),
			    "endianness", GST_PROPS_INT (G_BYTE_ORDER),
			    "signed",     GST_PROPS_BOOLEAN (TRUE),
			    "width",      GST_PROPS_INT (16),
			    "depth",      GST_PROPS_INT (16),
			    "rate",       GST_PROPS_INT (rate),
			    "channels",   GST_PROPS_INT (1)
			   )) > 0)
  {
    return GST_PAD_LINK_OK;
  }
  return GST_PAD_LINK_REFUSED;
}

static void
gst_gsmdec_chain (GstPad *pad, GstBuffer *buf)
{
  GstGSMDec *gsmdec;
  gsm_byte *data;
  guint size;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
  /*g_return_if_fail(GST_IS_BUFFER(buf)); */

  gsmdec = GST_GSMDEC (gst_pad_get_parent (pad));

  data = (gsm_byte*) GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  if (gsmdec->bufsize && (gsmdec->bufsize + size >= 33)) {
    GstBuffer *outbuf;

    memcpy (gsmdec->buffer + gsmdec->bufsize, data, (33 - gsmdec->bufsize) * sizeof (gsm_byte));

    outbuf = gst_buffer_new ();
    GST_BUFFER_DATA (outbuf) = g_malloc (160 * sizeof (gsm_signal));
    GST_BUFFER_SIZE (outbuf) = 160 * sizeof (gsm_signal);

    gsm_decode (gsmdec->state, gsmdec->buffer, (gsm_signal *) GST_BUFFER_DATA (outbuf));

    gst_pad_push (gsmdec->srcpad, outbuf);

    size -= (33 - gsmdec->bufsize);
    data += (33 - gsmdec->bufsize);
    gsmdec->bufsize = 0;
  }

  while (size >= 33) {
    GstBuffer *outbuf;

    outbuf = gst_buffer_new ();
    GST_BUFFER_DATA (outbuf) = g_malloc (160 * sizeof (gsm_signal));
    GST_BUFFER_SIZE (outbuf) = 160 * sizeof (gsm_signal);
    
    gsm_decode (gsmdec->state, data, (gsm_signal *)GST_BUFFER_DATA (outbuf));

    gst_pad_push (gsmdec->srcpad, outbuf);

    size -= 33;
    data += 33;
  }

  if (size) {
    memcpy (gsmdec->buffer + gsmdec->bufsize, data, size * sizeof (gsm_byte));
    gsmdec->bufsize += size;
  }

  gst_buffer_unref(buf);
}

