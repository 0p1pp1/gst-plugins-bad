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
#include <gst/gst.h>
#include <tremor/ivorbiscodec.h>
#include <tremor/ivorbisfile.h>
#include <gst/bytestream/bytestream.h>

extern GType vorbisfile_get_type(void);

extern GstElementDetails vorbisfile_details;
extern GstElementDetails vorbisenc_details;

static GstCaps* 	vorbis_type_find 	(GstBuffer *buf, gpointer private);

GstPadTemplate *gst_vorbisdec_src_template, *gst_vorbisdec_sink_template; 

static GstCaps*
vorbis_caps_factory (void)
{
  return
   gst_caps_new (
  	"tremor_tremor",
  	"application/x-ogg",
  	NULL);
}

static GstCaps*
raw_caps_factory (void)
{
  return
   gst_caps_new (
  	"tremor_raw",
  	"audio/raw",
	gst_props_new (
  	  "format",   		GST_PROPS_STRING ("int"),
    	    "law",   		GST_PROPS_INT (0),
    	    "endianness", 	GST_PROPS_INT (G_BYTE_ORDER),
    	    "signed", 		GST_PROPS_BOOLEAN (TRUE),
    	    "width", 		GST_PROPS_INT (16),
    	    "depth",    	GST_PROPS_INT (16),
    	    "rate",     	GST_PROPS_INT_RANGE (11025, 48000),
    	    "channels", 	GST_PROPS_INT_RANGE (1, 2),
	    NULL));
}

static GstCaps*
raw_caps2_factory (void)
{
  return
   gst_caps_new (
  	"tremor_raw_float",
  	"audio/raw",
	gst_props_new (
  	  "format",   		GST_PROPS_STRING ("float"),
    	    "layout",		GST_PROPS_STRING ("IEEE"),
    	    "rate",     	GST_PROPS_INT_RANGE (11025, 48000),
    	    "channels", 	GST_PROPS_INT (2),
	    NULL));
}

static GstTypeDefinition vorbisdefinition = {
  "tremor_audio/x-ogg",
  "application/x-ogg",
  ".ogg",
  vorbis_type_find,
};

static GstCaps* 
vorbis_type_find (GstBuffer *buf, gpointer private) 
{
  gulong head = GULONG_FROM_BE (*((gulong *)GST_BUFFER_DATA (buf)));

  if (head  != 0x4F676753)
    return NULL;

  return gst_caps_new ("vorbis_type_find", "application/x-ogg", NULL);
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *enc, *file;
  GstTypeFactory *type;
  GstCaps *raw_caps, *vorbis_caps, *raw_caps2;

  gst_plugin_set_longname (plugin, "The OGG Vorbis Codec");

  raw_caps = raw_caps_factory ();
  raw_caps2 = raw_caps2_factory ();
  vorbis_caps = vorbis_caps_factory ();

  /* register sink pads */
  gst_vorbisdec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                                      GST_PAD_ALWAYS, 
					              vorbis_caps, NULL);
  raw_caps = gst_caps_prepend (raw_caps, raw_caps2);
  /* register src pads */
  gst_vorbisdec_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                                     GST_PAD_ALWAYS, 
					             raw_caps, NULL);
  /* create an elementfactory for the vorbisfile element */
  file = gst_element_factory_new ("ivorbisfile", vorbisfile_get_type(),
                                  &vorbisfile_details);
  g_return_val_if_fail(file != NULL, FALSE);
  gst_element_factory_set_rank (file, GST_ELEMENT_RANK_PRIMARY);
 
  /* register sink pads */
  gst_element_factory_add_pad_template (file, gst_vorbisdec_sink_template);
  /* register src pads */
  gst_element_factory_add_pad_template (file, gst_vorbisdec_src_template);
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (file));

  /* this filter needs the bytestream package */
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  type = gst_type_factory_new (&vorbisdefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "tremor",
  plugin_init
};
