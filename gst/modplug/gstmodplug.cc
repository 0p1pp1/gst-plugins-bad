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
 
/* 
   Code based on modplugxmms
   XMMS plugin:
     Kenton Varda <temporal@gauge3d.org>
   Sound Engine:
     Olivier Lapicque <olivierl@jps.net>  
*/

#include "libmodplug/stdafx.h"
#include "libmodplug/sndfile.h"

#include "gstmodplug.h"

#include <gst/gst.h>
#include <stdlib.h>

GstElementDetails modplug_details = {
  "ModPlug",
  "Codec/Audio/Decoder",
  "Module decoder based on modplug engine",
  VERSION,
  "Jeremy SIMON <jsimon13@yahoo.fr> "
  "Kenton Varda <temporal@gauge3d.org> "
  "Olivier Lapicque <olivierl@jps.net>",
  "(C) 2001"
};


/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SONGNAME,
  ARG_REVERB,
  ARG_REVERB_DEPTH,
  ARG_REVERB_DELAY,
  ARG_MEGABASS,
  ARG_MEGABASS_AMOUNT,
  ARG_MEGABASS_RANGE,
  ARG_FREQUENCY,
  ARG_NOISE_REDUCTION,
  ARG_SURROUND,
  ARG_SURROUND_DEPTH,
  ARG_SURROUND_DELAY,
  ARG_CHANNEL,
  ARG_16BIT,
  ARG_OVERSAMP
};


GST_PAD_TEMPLATE_FACTORY (modplug_src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "modplug_src",
    "audio/raw",  
      "format",   GST_PROPS_STRING ("int"),
      "law",         GST_PROPS_INT (0),
      "endianness",  GST_PROPS_INT (G_BYTE_ORDER),
      "signed",      GST_PROPS_BOOLEAN (TRUE),
      "width",       GST_PROPS_INT (16),
      "depth",       GST_PROPS_INT (16), 
      "rate",        GST_PROPS_INT_RANGE (11025, 44100),
      "channels",    GST_PROPS_INT_RANGE (1, 2)
  )
)

GST_PAD_TEMPLATE_FACTORY (modplug_sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mad_sink",
    "audio/mod",
    NULL
  )
)


static void		gst_modplug_class_init		(GstModPlugClass *klass);
static void		gst_modplug_init		(GstModPlug *filter);
static void		gst_modplug_set_property 	(GObject *object, guint id, const GValue *value, GParamSpec *pspec );
static void		gst_modplug_get_property	(GObject *object, guint id, GValue *value, GParamSpec *pspec );
static void  		gst_modplug_loop                (GstElement *element);
static void		gst_modplug_setup 		(GstModPlug *modplug);
static gboolean		gst_modplug_src_event		(GstPad *pad, GstEvent *event);
static gboolean		gst_modplug_src_query 		(GstPad *pad, GstPadQueryType type, GstFormat *format, gint64 *value);
static GstElementStateReturn  
			gst_modplug_change_state 	(GstElement *element);

static GstElementClass *parent_class = NULL;

#define GST_TYPE_MODPLUG_MIXFREQ (gst_modplug_mixfreq_get_type())

static GType 
gst_modplug_mixfreq_get_type (void)
{
  static GType modplug_mixfreq_type = 0;
  static GEnumValue modplug_mixfreq[] = {
    { 0, "8000",  "8000 Hz" },
    { 1, "11025", "11025 Hz" },
    { 2, "22100", "22100 Hz" },
    { 3, "44100", "44100 Hz" },
    { 0, NULL, NULL },
  };
  if (! modplug_mixfreq_type ) {
    modplug_mixfreq_type = g_enum_register_static ("GstModPlugmixfreq", modplug_mixfreq);
  }
  return modplug_mixfreq_type;
}


GType
gst_modplug_get_type(void) {
  static GType modplug_type = 0;

  if (!modplug_type) {
    static const GTypeInfo modplug_info = {
      sizeof(GstModPlugClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_modplug_class_init,
      NULL,
      NULL,
      sizeof(GstModPlug),
      0,
      (GInstanceInitFunc)gst_modplug_init,
      NULL
    };
    modplug_type = g_type_register_static(GST_TYPE_ELEMENT, "GstModPlug", &modplug_info, (GTypeFlags)0);
  }
  return modplug_type;
}


static void
gst_modplug_class_init (GstModPlugClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = GST_ELEMENT_CLASS( g_type_class_ref(GST_TYPE_ELEMENT));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SONGNAME,
    g_param_spec_string("songname","Songname","The song name",
                        "", G_PARAM_READABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FREQUENCY,
    g_param_spec_enum("mixfreq", "mixfreq", "mixfreq",
                      GST_TYPE_MODPLUG_MIXFREQ, 3,(GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_16BIT,
    g_param_spec_boolean("use16bit", "use16bit", "use16bit",
                         TRUE, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_REVERB,
    g_param_spec_boolean("reverb", "reverb", "reverb",
                         FALSE, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_REVERB_DEPTH,
    g_param_spec_int("reverb_depth", "reverb_depth", "reverb_depth",
   		     0, 100, 30, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_REVERB_DELAY,
    g_param_spec_int("reverb_delay", "reverb_delay", "reverb_delay",
	 	     0, 200, 100, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MEGABASS,
    g_param_spec_boolean("megabass", "megabass", "megabass",
                         FALSE, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MEGABASS_AMOUNT,
    g_param_spec_int("megabass_amount", "megabass_amount", "megabass_amount",
                     0, 100, 40, (GParamFlags)G_PARAM_READWRITE ));
					
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MEGABASS_RANGE,
    g_param_spec_int("megabass_range", "megabass_range", "megabass_range",
                     0, 100, 30, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SURROUND,
    g_param_spec_boolean("surround", "surround", "surround",
                         TRUE, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SURROUND_DEPTH,
    g_param_spec_int("surround_depth", "surround_depth", "surround_depth",
                     0, 100, 20, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SURROUND_DELAY,
    g_param_spec_int("surround_delay", "surround_delay", "surround_delay",
                     0, 40, 20, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_OVERSAMP,
    g_param_spec_boolean("oversamp", "oversamp", "oversamp",
                         TRUE, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NOISE_REDUCTION,
    g_param_spec_boolean("noise_reduction", "noise_reduction", "noise_reduction",
                         TRUE, (GParamFlags)G_PARAM_READWRITE ));
		    
  gobject_class->set_property = gst_modplug_set_property;
  gobject_class->get_property = gst_modplug_get_property;

  gstelement_class->change_state = gst_modplug_change_state;
}


static void
gst_modplug_init (GstModPlug *modplug)
{  
  modplug->sinkpad = gst_pad_new_from_template( GST_PAD_TEMPLATE_GET (modplug_sink_template_factory), "sink");
  modplug->srcpad = gst_pad_new_from_template( GST_PAD_TEMPLATE_GET (modplug_src_template_factory), "src");

  gst_element_add_pad(GST_ELEMENT(modplug),modplug->sinkpad);
  gst_element_add_pad(GST_ELEMENT(modplug),modplug->srcpad);
  
  gst_pad_set_event_function (modplug->srcpad, (GstPadEventFunction)GST_DEBUG_FUNCPTR(gst_modplug_src_event));
  gst_pad_set_query_function (modplug->srcpad, gst_modplug_src_query);
  gst_element_set_loop_function (GST_ELEMENT (modplug), gst_modplug_loop);
  
  modplug->Buffer = NULL;

  modplug->reverb          = FALSE;
  modplug->reverb_depth    = 30;
  modplug->reverb_delay    = 100;
  modplug->megabass        = FALSE;
  modplug->megabass_amount = 40;
  modplug->megabass_range  = 30;	  
  modplug->surround        = TRUE;
  modplug->surround_depth  = 20;
  modplug->surround_delay  = 20;
  modplug->oversamp        = TRUE;
  modplug->noise_reduction = TRUE;

  modplug->_16bit          = TRUE;
  modplug->channel         = 2;
  modplug->frequency       = 44100;

  modplug->mSoundFile = new CSoundFile;
}


static void
gst_modplug_setup (GstModPlug *modplug)
{
  if ( modplug->_16bit ) 
    modplug->mSoundFile->SetWaveConfig ( modplug->frequency, 16, modplug->channel );
  else
    modplug->mSoundFile->SetWaveConfig ( modplug->frequency, 8,  modplug->channel );
  
  modplug->mSoundFile->SetWaveConfigEx ( modplug->surround, !modplug->oversamp, modplug->reverb, true, modplug->megabass, modplug->noise_reduction, false );
  modplug->mSoundFile->SetResamplingMode ( SRCMODE_POLYPHASE );

  if ( modplug->surround )
    modplug->mSoundFile->SetSurroundParameters( modplug->surround_depth, modplug->surround_delay );

  if ( modplug->megabass )
    modplug->mSoundFile->SetXBassParameters( modplug->megabass_amount, modplug->megabass_range );

  if ( modplug->reverb )
    modplug->mSoundFile->SetReverbParameters( modplug->reverb_depth, modplug->reverb_delay );

}


static gboolean
gst_modplug_src_query (GstPad *pad, GstPadQueryType type,
		                   GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GstModPlug *modplug;
  float tmp;

  modplug = GST_MODPLUG (gst_pad_get_parent (pad));

  switch (type) {
    case GST_PAD_QUERY_TOTAL:
      switch (*format) {
        case GST_FORMAT_DEFAULT:
              *format = GST_FORMAT_TIME;
	case GST_FORMAT_TIME:
          *value=(gint64)modplug->mSoundFile->GetSongTime() * GST_SECOND;
	  break;
	default:
          res = FALSE;
          break;
      }
      break;
    case GST_PAD_QUERY_POSITION:
      switch (*format) {
         case GST_FORMAT_DEFAULT:
           *format = GST_FORMAT_TIME;
	 default:
	   tmp = ((float)( modplug->mSoundFile->GetSongTime() * modplug->mSoundFile->GetCurrentPos() ) / (float)modplug->mSoundFile->GetMaxPosition() );
	   *value=(gint64)(tmp * GST_SECOND);
	   break;
      }
    default:
      break;
  }

  return res;
}    

					


static gboolean
gst_modplug_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstModPlug *modplug; 

  modplug = GST_MODPLUG (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
  /* the all-formats seek logic */
  case GST_EVENT_SEEK:
  {
    gboolean flush;
    GstFormat format;

    format = GST_FORMAT_TIME;

    /* shave off the flush flag, we'll need it later */
    flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;

    /* assume the worst */
    res = FALSE;

    modplug->seek_at = GST_EVENT_SEEK_OFFSET (event);
  }
  default:
    res = FALSE;
    break;
  }
  gst_event_unref (event);

  return res;
}

	

static void
gst_modplug_loop (GstElement *element)
{
  GstModPlug *modplug;
  GstBuffer *buffer_in, *buffer_out;
  gint mode16bits;
  guint64 total_samples, sync_point;
  float temp;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_MODPLUG (element));
	
  modplug = GST_MODPLUG (element);
  srcpad = modplug->srcpad;
  	
  while ((buffer_in = gst_pad_pull( modplug->sinkpad ))) {
    if ( GST_IS_EVENT (buffer_in) ) {
      GstEvent *event = GST_EVENT (buffer_in);
		
      if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
         gst_event_unref (event);
         break;
      }
      gst_event_unref (event);
    }
    else
    {
      if ( modplug->Buffer ) {	 
        GstBuffer *merge;

        merge = gst_buffer_merge( modplug->Buffer, buffer_in );
        gst_buffer_unref( buffer_in );	 	  
        gst_buffer_unref( modplug->Buffer );	 	  

	modplug->Buffer = merge;
      }
      else {
        modplug->Buffer = buffer_in;
      }
    }
  }  

  if ( modplug->Buffer == NULL) {
    gst_pad_push (modplug->srcpad, GST_BUFFER (gst_event_new (GST_EVENT_EOS)));	  
    gst_element_set_eos (GST_ELEMENT (modplug));
    return;
  }
  
  if ( modplug->_16bit )
    mode16bits = 16;
  else
    mode16bits = 8;

  gst_modplug_setup( modplug );

  modplug->mSoundFile = new CSoundFile;
  modplug->mSoundFile->Create( GST_BUFFER_DATA( modplug->Buffer ), GST_BUFFER_SIZE( modplug->Buffer ));
  
  gst_buffer_unref( modplug->Buffer );
  modplug->Buffer = NULL;

  gst_pad_try_set_caps (modplug->srcpad, 
		          GST_CAPS_NEW (
			    "modplug_src",
			    "audio/raw",
			      "format",      GST_PROPS_STRING ("int"),
			      "law",         GST_PROPS_INT (0),
			      "endianness",  GST_PROPS_INT (G_BYTE_ORDER),
			      "signed",      GST_PROPS_BOOLEAN (TRUE),
			      "width",       GST_PROPS_INT (mode16bits),
			      "depth",       GST_PROPS_INT (mode16bits),
			      "rate",        GST_PROPS_INT (modplug->frequency),
			      "channels",    GST_PROPS_INT (modplug->channel)));

  modplug->length = 1152 * modplug->channel;
  
  if ( modplug->_16bit )
    modplug->length *= 2;
    
  modplug->audiobuffer = (guchar *) g_malloc( modplug->length );
  total_samples = 0;
  sync_point = 0;
  			    
  do {
    if ( modplug->seek_at != 0 )
    {
      int seek_to_pos;
      gint64 total;

      total = modplug->mSoundFile->GetSongTime() * GST_SECOND;

      temp = (float) total / modplug->seek_at;     
      seek_to_pos = (int)( modplug->mSoundFile->GetMaxPosition() / temp );

      modplug->mSoundFile->SetCurrentPos( seek_to_pos );
    }
  
    if( modplug->mSoundFile->Read ( modplug->audiobuffer, modplug->length ) != 0 )
    {
      GstClockTime time;

      buffer_out = gst_buffer_new();
      GST_BUFFER_DATA( buffer_out ) = (guchar *) g_memdup( modplug->audiobuffer, modplug->length );
      GST_BUFFER_SIZE( buffer_out ) = modplug->length;

      total_samples+=1152;		

      if ( modplug->seek_at != 0 )
      {
        GstEvent *discont;
        gint64 total;

        modplug->seek_at = 0;

        /* get stream stats */
        total = modplug->mSoundFile->GetSongTime() * GST_SECOND;
        time = (gint64) (total * modplug->mSoundFile->GetCurrentPos() ) / modplug->mSoundFile->GetMaxPosition();

        discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, time, NULL );
        total_samples = time * modplug->frequency / GST_SECOND ;		

        gst_pad_push (modplug->srcpad, GST_BUFFER (discont));
      }
 	  
      GST_BUFFER_TIMESTAMP( buffer_out ) = total_samples * GST_SECOND / modplug->frequency;
 	  	
      gst_pad_push( srcpad, buffer_out );
      gst_element_yield (element);      
    }
    else
    {	    
      free( modplug->audiobuffer );
      gst_pad_push (modplug->srcpad, GST_BUFFER (gst_event_new (GST_EVENT_EOS)));	  
      gst_element_set_eos (GST_ELEMENT (modplug));
      break;
    }
  } 
  while ( 1 );
}


static GstElementStateReturn
gst_modplug_change_state (GstElement *element)
{
  GstModPlug *modplug;

  modplug = GST_MODPLUG( element );

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
//      modplug->mSoundFile = new CSoundFile;
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      modplug->Buffer = NULL;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
//      modplug->mSoundFile->SetCurrentPos( 0 );    
      break;
    case GST_STATE_READY_TO_NULL:
      free( modplug->audiobuffer );
      modplug->mSoundFile->Destroy();
      break;
  }

  parent_class->change_state (element);
  
  return GST_STATE_SUCCESS;
}

static void
gst_modplug_set_property (GObject *object, guint id, const GValue *value, GParamSpec *pspec )
{
  GstModPlug *modplug;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MODPLUG(object));
  modplug = GST_MODPLUG(object);

  switch (id) {
    case ARG_SONGNAME:
      modplug->songname = g_value_get_string (value);
      break;
    case ARG_REVERB:
      modplug->reverb = g_value_get_boolean (value);
      break;
    case ARG_REVERB_DEPTH:
      modplug->reverb_depth = g_value_get_int (value);
      break;
    case ARG_REVERB_DELAY:
      modplug->reverb_delay = g_value_get_int (value);
      break;
    case ARG_MEGABASS:
      modplug->megabass = g_value_get_boolean (value);
      break;
    case ARG_MEGABASS_AMOUNT:
      modplug->megabass_amount = g_value_get_int (value);
      break;
    case ARG_MEGABASS_RANGE:
      modplug->megabass_range = g_value_get_int (value);
      break;
    case ARG_FREQUENCY:
      modplug->frequency = g_value_get_enum (value);
      break;
    case ARG_CHANNEL:
      modplug->channel = g_value_get_int (value);
      break;
    case ARG_NOISE_REDUCTION:
      modplug->noise_reduction = g_value_get_boolean (value);
      break;
    case ARG_SURROUND:
      modplug->surround = g_value_get_boolean (value);
      break;
    case ARG_SURROUND_DEPTH:
      modplug->surround_depth = g_value_get_int (value);
      break;
    case ARG_SURROUND_DELAY:
      modplug->surround_delay = g_value_get_int (value);
      break;
    case ARG_16BIT:
      modplug->_16bit = g_value_get_boolean (value);
      break;
    default:
//      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_modplug_get_property (GObject *object, guint id, GValue *value, GParamSpec *pspec )
{
  GstModPlug *modplug;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MODPLUG(object));
  modplug = GST_MODPLUG(object);
  
  switch (id) {
    case ARG_REVERB:
      g_value_set_boolean (value, modplug->reverb);
      break;
    case ARG_REVERB_DEPTH:
      g_value_set_int (value, modplug->reverb_depth);
      break;
    case ARG_REVERB_DELAY:
      g_value_set_int (value, modplug->reverb_delay);
      break;
    case ARG_MEGABASS:
      g_value_set_boolean (value, modplug->megabass);
      break;
    case ARG_MEGABASS_AMOUNT:
      g_value_set_int (value, modplug->megabass_amount);
      break;
    case ARG_MEGABASS_RANGE:
      g_value_set_int (value, modplug->megabass_range);
      break;
    case ARG_FREQUENCY:
      g_value_set_enum (value, modplug->frequency);
      break;
    case ARG_CHANNEL:
      g_value_set_int (value, modplug->channel);
      break;
    case ARG_16BIT:
      g_value_set_boolean (value, modplug->_16bit);
      break;
    case ARG_SURROUND:
      g_value_set_boolean (value, modplug->surround);
      break;
    case ARG_SURROUND_DEPTH:
      g_value_set_int (value, modplug->surround_depth);
      break;
    case ARG_SURROUND_DELAY:
      g_value_set_int (value, modplug->surround_delay);
      break;
    case ARG_NOISE_REDUCTION:
      g_value_set_boolean (value, modplug->noise_reduction);
      break;
    default:
//      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  
  factory = gst_element_factory_new("modplug",GST_TYPE_MODPLUG,
                                   &modplug_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);
 
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (modplug_sink_template_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (modplug_src_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));	

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "modplug",
  plugin_init
};
