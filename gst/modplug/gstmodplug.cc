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
#include <gst/audio/audio.h>

/* elementfactory information */
GstElementDetails modplug_details = {
  "ModPlug",
  "Codec/Audio/Decoder",
  "LGPL",
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
  ARG_OVERSAMP,
  ARG_METADATA,
  ARG_STREAMINFO
};


GST_PAD_TEMPLATE_FACTORY (modplug_src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "modplug_src",
    "audio/raw",  
      "format",      GST_PROPS_STRING ("int"),
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
    "modplug_sink",
    "audio/x-mod",
    NULL
  ),
  GST_CAPS_NEW (
    "modplug_sink",
    "audio/x-xm",
    NULL
  ),
  GST_CAPS_NEW (
    "modplug_sink",
    "audio/x-s3m",
    NULL
  ),
  GST_CAPS_NEW (
    "modplug_sink",
    "audio/x-it",
    NULL
  )
)

enum {
  MODPLUG_STATE_NEED_TUNE = 1,
  MODPLUG_STATE_LOAD_TUNE = 2,
  MODPLUG_STATE_PLAY_TUNE = 3,
};


static void		gst_modplug_class_init		(GstModPlugClass *klass);
static void		gst_modplug_init		      (GstModPlug *filter);
static void		gst_modplug_set_property 	(GObject *object, guint id, const GValue *value, GParamSpec *pspec );
static void		gst_modplug_get_property	(GObject *object, guint id, GValue *value, GParamSpec *pspec );
static void  	gst_modplug_loop          (GstElement *element);
static void		gst_modplug_setup 		    (GstModPlug *modplug);
static const GstFormat* 
              gst_modplug_get_formats (GstPad *pad);
static const GstQueryType*
              gst_modplug_get_query_types (GstPad *pad);
static gboolean		
              gst_modplug_src_event	(GstPad *pad, GstEvent *event);
static gboolean		
              gst_modplug_src_query (GstPad *pad, GstQueryType type, GstFormat *format, gint64 *value);
static GstElementStateReturn  
              gst_modplug_change_state   (GstElement *element);

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


static GstCaps* 
modplug_type_find (GstBuffer *buf, gpointer priv) 
{  
  if (MOD_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-mod", NULL);
  
  if (Mod_669_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-mod", NULL);
  
  if (Amf_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-mod", NULL);
  
  if (Dsm_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-mod", NULL);
  
  if (Fam_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-mod", NULL);
  
  if (Gdm_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-mod", NULL);
  
  if (Imf_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-mod", NULL);
  
  if (It_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-it", NULL);
  
  if (M15_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-mod", NULL);
  
  /* FIXME
  if ( Med_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/x-mod", NULL);
  */
  
  if (Mtm_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-mod", NULL);
  
  if (Okt_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-mod", NULL);
  
  if (S3m_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-s3m", NULL);
  
  if (Xm_CheckType (buf))
    return gst_caps_new ("modplug_type_find", "audio/x-xm", NULL);
  
  return NULL;
}

static GstTypeDefinition modplug_definitions[] = {
  { "modplug_audio/mod", "audio/x-mod", ".mod .sam .med .stm .mtm .669 .ult .far .amf  .dsm .imf .gdm .stx .okt", modplug_type_find },
  { "modplug_audio/xm", "audio/x-xm", ".xm", modplug_type_find },
  { "modplug_audio/it", "audio/x-it", ".it", modplug_type_find },
  { "modplug_audio/s3m", "audio/x-s3m", ".s3m", modplug_type_find },
  { NULL, NULL, NULL, NULL }
};

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

  g_object_class_install_property (gobject_class, ARG_METADATA,
    g_param_spec_boxed ("metadata", "Metadata", "Metadata",
                        GST_TYPE_CAPS, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, ARG_STREAMINFO,
    g_param_spec_boxed ("streaminfo", "Streaminfo", "Streaminfo",
                        GST_TYPE_CAPS, G_PARAM_READABLE));
		    
  gobject_class->set_property = gst_modplug_set_property;
  gobject_class->get_property = gst_modplug_get_property;

  gstelement_class->change_state = gst_modplug_change_state;
}

static void
gst_modplug_init (GstModPlug *modplug)
{  
  modplug->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (modplug_sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT(modplug), modplug->sinkpad);

  modplug->srcpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (modplug_src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT(modplug), modplug->srcpad);
  
  gst_pad_set_event_function (modplug->srcpad, (GstPadEventFunction)GST_DEBUG_FUNCPTR(gst_modplug_src_event));
  gst_pad_set_query_function (modplug->srcpad, gst_modplug_src_query);
  gst_pad_set_query_type_function (modplug->srcpad,  (GstPadQueryTypeFunction) GST_DEBUG_FUNCPTR (gst_modplug_get_query_types));
  gst_pad_set_formats_function (modplug->srcpad, (GstPadFormatsFunction)GST_DEBUG_FUNCPTR (gst_modplug_get_formats));
  
  gst_element_set_loop_function (GST_ELEMENT (modplug), gst_modplug_loop);      
  
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
}


static void
gst_modplug_setup (GstModPlug *modplug)
{
  if (modplug->_16bit) 
    modplug->mSoundFile->SetWaveConfig (modplug->frequency, 16, modplug->channel);
  else
    modplug->mSoundFile->SetWaveConfig (modplug->frequency, 8,  modplug->channel);
  
  modplug->mSoundFile->SetWaveConfigEx (modplug->surround, !modplug->oversamp, modplug->reverb, true, modplug->megabass, modplug->noise_reduction, true);
  modplug->mSoundFile->SetResamplingMode (SRCMODE_POLYPHASE);

  if (modplug->surround)
    modplug->mSoundFile->SetSurroundParameters (modplug->surround_depth, modplug->surround_delay);

  if (modplug->megabass)
    modplug->mSoundFile->SetXBassParameters (modplug->megabass_amount, modplug->megabass_range);

  if (modplug->reverb)
    modplug->mSoundFile->SetReverbParameters (modplug->reverb_depth, modplug->reverb_delay);

}

static const GstFormat*
gst_modplug_get_formats (GstPad *pad)
{
  static const GstFormat src_formats[] = {
/*    GST_FORMAT_BYTES,
    GST_FORMAT_UNITS,*/
    GST_FORMAT_TIME,
    (GstFormat)0
  };
  static const GstFormat sink_formats[] = {
    /*GST_FORMAT_BYTES,*/
    GST_FORMAT_TIME,
    (GstFormat)0
  };
  
  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}

static const GstQueryType*
gst_modplug_get_query_types (GstPad *pad)
{
  static const GstQueryType gst_modplug_src_query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    (GstQueryType)0
  };
  return gst_modplug_src_query_types;
}


static gboolean
gst_modplug_src_query (GstPad *pad, GstQueryType type,
		                   GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GstModPlug *modplug;
  gfloat tmp;

  modplug = GST_MODPLUG (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
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
    case GST_QUERY_POSITION:
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

      modplug->seek_at = GST_EVENT_SEEK_OFFSET (event);
    }
    default:
      res = FALSE;
      break;
  }
  
  gst_event_unref (event);

  return res;
}

static GstCaps*
gst_modplug_get_streaminfo (GstModPlug *modplug)
{
  GstCaps *caps;
  GstProps *props;
  GstPropsEntry *entry;
 
  props = gst_props_empty_new ();

  entry = gst_props_entry_new ("Patterns", GST_PROPS_INT ((gint)modplug->mSoundFile->GetNumPatterns()));
  gst_props_add_entry (props, (GstPropsEntry *) entry);
  
  caps = gst_caps_new ("mad_streaminfo", "application/x-gst-streaminfo",
		                   props);
  return caps;
}


static void
gst_modplug_update_info (GstModPlug *modplug)
{  
    if (modplug->streaminfo) {
      gst_caps_unref (modplug->streaminfo);
    }

    modplug->streaminfo = gst_modplug_get_streaminfo (modplug);
    g_object_notify (G_OBJECT (modplug), "streaminfo"); 
}

static void 
gst_modplug_update_metadata (GstModPlug *modplug)
{  
  GstProps *props;
  GstPropsEntry *entry;
  const gchar *title;

  props = gst_props_empty_new ();

  title = modplug->mSoundFile->GetTitle();
  entry = gst_props_entry_new ("Title", GST_PROPS_STRING (title));
  gst_props_add_entry (props, entry);

  modplug->metadata = gst_caps_new ("modplug_metadata",
                                    "application/x-gst-metadata",
                                    props);

  g_object_notify (G_OBJECT (modplug), "metadata");
}



static gboolean
modplug_negotiate (GstModPlug *modplug)
{
  modplug->length = 1152 * modplug->channel;
  
  if (modplug->_16bit)
  {
    modplug->length *= 2;
    modplug->bitsPerSample = 16;
  }
  else
    modplug->bitsPerSample = 8;
    
  if (!GST_PAD_CAPS (modplug->srcpad)) {
    if (!gst_pad_try_set_caps (modplug->srcpad, 
      GST_CAPS_NEW (
        "modplug_src",
        "audio/raw",
          "format",       	GST_PROPS_STRING ("int"),
            "law",        	GST_PROPS_INT (0),            
            "endianness",   GST_PROPS_INT (G_BYTE_ORDER),
            "signed",     	GST_PROPS_BOOLEAN (TRUE),
            "width",      	GST_PROPS_INT (modplug->bitsPerSample),
            "depth",      	GST_PROPS_INT (modplug->bitsPerSample),
            "rate",       	GST_PROPS_INT (modplug->frequency),
            "channels",     GST_PROPS_INT (modplug->channel),
           NULL)
       ))
    {
      return FALSE;
    }
  }
  
 gst_modplug_setup (modplug);

  return TRUE;
}

static void
gst_modplug_loop (GstElement *element)
{
  GstModPlug *modplug;  
  GstEvent *event;    

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_MODPLUG (element));
	
  modplug = GST_MODPLUG (element);

  if (modplug->state == MODPLUG_STATE_NEED_TUNE) 
  {            
/*    GstBuffer *buf;*/
    
    modplug->total_samples = 0;
    modplug->seek_at = -1;
    modplug->need_discont = FALSE;
    modplug->eos = FALSE;
/*            
    buf = gst_pad_pull (modplug->sinkpad);
    g_assert (buf != NULL);
      
    if (GST_IS_EVENT (buf)) {
      GstEvent *event = GST_EVENT (buf);

      switch (GST_EVENT_TYPE (buf)) {
        case GST_EVENT_EOS:             
          modplug->state = MODPLUG_STATE_LOAD_TUNE;
          break;
        case GST_EVENT_DISCONTINUOUS:
          break;
        default:
           bail out, we're not going to do anything 
          gst_event_unref (event);
          gst_pad_send_event (modplug->srcpad, gst_event_new (GST_EVENT_EOS));
          gst_element_set_eos (element);
          return;
      }
      gst_event_unref (event);
    }
    else {      
      memcpy (modplug->buffer_in + modplug->song_size, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
      modplug->song_size += GST_BUFFER_SIZE (buf);

      gst_buffer_unref (buf);
    }
*/

    modplug->song_size = gst_bytestream_length (modplug->bs);
  
    gst_bytestream_peek_bytes (modplug->bs, &modplug->buffer_in,  modplug->song_size);
    modplug->state = MODPLUG_STATE_LOAD_TUNE; 
  }  
  
  if (modplug->state == MODPLUG_STATE_LOAD_TUNE) 
  {            
    modplug->mSoundFile = new CSoundFile;
    
    if (!modplug_negotiate (modplug)) {
      gst_element_error (GST_ELEMENT (modplug), "could not negotiate format");
      return;
    }
        
    modplug->mSoundFile->Create (modplug->buffer_in, modplug->song_size);    
      
    modplug->audiobuffer = (guchar *) g_malloc (modplug->length);
    
    gst_modplug_update_metadata (modplug);
    gst_modplug_update_info (modplug);

    modplug->state = MODPLUG_STATE_PLAY_TUNE;
  }
      
  if (modplug->state == MODPLUG_STATE_PLAY_TUNE) 
  {
    if (modplug->seek_at != -1)
    {
      gint seek_to_pos;
      gint64 total;
      gfloat temp;
       
      total = modplug->mSoundFile->GetSongTime () * GST_SECOND;

      temp = (gfloat) total / modplug->seek_at;     
      seek_to_pos = (int) (modplug->mSoundFile->GetMaxPosition () / temp);

      modplug->mSoundFile->SetCurrentPos (seek_to_pos);    
      modplug->need_discont = TRUE;
      modplug->seek_at = -1;
    }
        
    if (modplug->mSoundFile->Read (modplug->audiobuffer, modplug->length) != 0)
    {         
      GstBuffer *buffer_out;
      GstFormat format;
      gint64 value;
 
      format = GST_FORMAT_TIME;
      gst_modplug_src_query (modplug->srcpad, GST_QUERY_POSITION, &format, &value);
      
      if (modplug->need_discont && GST_PAD_IS_USABLE (modplug->srcpad))
      {
        GstEvent *discont;
    
        discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, value, NULL);
        gst_pad_push (modplug->srcpad, GST_BUFFER (discont));        
      
        modplug->need_discont= FALSE;
      }
 	  
      buffer_out = gst_buffer_new ();
      GST_BUFFER_DATA (buffer_out) = (guchar *) g_memdup (modplug->audiobuffer, modplug->length);
      GST_BUFFER_SIZE (buffer_out) = modplug->length;
      GST_BUFFER_TIMESTAMP (buffer_out) = value;
      
      if (GST_PAD_IS_USABLE (modplug->srcpad))
        gst_pad_push (modplug->srcpad, buffer_out);   
    }
    else
      if (GST_PAD_IS_USABLE (modplug->srcpad))
      {        
        event = gst_event_new (GST_EVENT_EOS);
        gst_pad_push (modplug->srcpad, GST_BUFFER (event));	      
        gst_element_set_eos (element);   
      }
  }
}


static GstElementStateReturn
gst_modplug_change_state (GstElement *element)
{
  GstModPlug *modplug;

  modplug = GST_MODPLUG (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:  
      modplug->bs = gst_bytestream_new (modplug->sinkpad);
      modplug->song_size = 0;
      modplug->state = MODPLUG_STATE_NEED_TUNE;
      modplug->metadata = NULL;
      modplug->streaminfo = NULL;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:     
      gst_bytestream_destroy (modplug->bs);          
      modplug->mSoundFile->Destroy ();      
      g_free (modplug->audiobuffer);      
      g_free (modplug->buffer_in);
      modplug->audiobuffer = NULL;
      modplug->buffer_in = NULL;
      modplug->state = MODPLUG_STATE_NEED_TUNE;
      break;
    case GST_STATE_READY_TO_NULL:         
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
    
  return GST_STATE_SUCCESS;
}


static void
gst_modplug_set_property (GObject *object, guint id, const GValue *value, GParamSpec *pspec )
{
  GstModPlug *modplug;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MODPLUG(object));
  modplug = GST_MODPLUG (object);

  switch (id) {
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
      break;
  }
}

static void
gst_modplug_get_property (GObject *object, guint id, GValue *value, GParamSpec *pspec )
{
  GstModPlug *modplug;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MODPLUG(object));
  modplug = GST_MODPLUG (object);
  
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
    case ARG_METADATA:
      g_value_set_boxed (value, modplug->metadata);
      break;
    case ARG_STREAMINFO:
      g_value_set_boxed (value, modplug->streaminfo);
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;  
	guint i;
  
  /* this filter needs the bytestream package */
  if (!gst_library_load ("gstbytestream"))
    return FALSE;
  
  factory = gst_element_factory_new ("modplug", GST_TYPE_MODPLUG, &modplug_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);
 
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (modplug_sink_template_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (modplug_src_template_factory));

  i = 0;
  while (modplug_definitions[i].name) {
    GstTypeFactory *type;

    type = gst_type_factory_new (&modplug_definitions[i]);
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));
    i++;
  }
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));	

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "modplug",
  plugin_init
};
