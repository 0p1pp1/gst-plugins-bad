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

#include "gstshout.h"

/* elementfactory information */
static GstElementDetails icecastsend_details = {
  "An Icecast plugin",
  "Sink/Network/Icecast",
  "GPL",
  "Sends data to an icecast server using libshout",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

static char *SHOUT_ERRORS[] = {
  "ok",
  "insane",
  "could not connect",
  "could not login",
  "socket error",
  "could not allocate memory",
  "metadata error",
};

/* IcecastSend signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_IP,          /* the ip of the server */
  ARG_PORT,        /* the encoder port number on the server */
  ARG_PASSWORD,    /* the encoder password on the server */
  ARG_PUBLIC,      /* is this stream public? */
  ARG_NAME,        /* Name of the stream */
  ARG_DESCRIPTION, /* Description of the stream */
  ARG_GENRE,       /* Genre of the stream */
  ARG_MOUNT,       /* mountpoint of stream (icecast only) */
  ARG_DUMPFILE,    /* Dumpfile on the server for this stream (icecast only) */
  ARG_ICY,         /* use icy headers for login? (for use with shoutcast) */
  ARG_AIM,         /* AIM number (shoutcast only) */
  ARG_ICQ,         /* ICQ number (shoutcast only) */
  ARG_IRC,         /* IRC server (shoutcast only) */
};

static GstPadTemplate*
sink_template_factory (void)
{
  static GstPadTemplate *template = NULL;
  
  if (!template) {
    template = gst_pad_template_new (
      "sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "icecastsend_sink",
        "audio/x-mp3",
	NULL),
      NULL);
  }

  return template;
}

static void		gst_icecastsend_class_init	(GstIcecastSendClass *klass);
static void		gst_icecastsend_init		(GstIcecastSend *icecastsend);

static void		gst_icecastsend_chain		(GstPad *pad, GstBuffer *buf);

static void		gst_icecastsend_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_icecastsend_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static 
  GstElementStateReturn	gst_icecastsend_change_state	(GstElement *element);

static GstElementClass *parent_class = NULL;
/*static guint gst_icecastsend_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_icecastsend_get_type (void)
{
  static GType icecastsend_type = 0;

  if (!icecastsend_type) {
    static const GTypeInfo icecastsend_info = {
      sizeof (GstIcecastSendClass), NULL, NULL,
      (GClassInitFunc) gst_icecastsend_class_init, NULL, NULL,
      sizeof (GstIcecastSend), 0,
      (GInstanceInitFunc) gst_icecastsend_init,
    };
    icecastsend_type = g_type_register_static(GST_TYPE_ELEMENT, "GstIcecastSend", &icecastsend_info, 0);
  }
  return icecastsend_type;
}

static void
gst_icecastsend_class_init (GstIcecastSendClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_IP,
    g_param_spec_string("ip","ip","ip",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PORT,
    g_param_spec_int("port","port","port",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PASSWORD,
    g_param_spec_string("password","password","password",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PUBLIC,
    g_param_spec_boolean("public","public","public",
                        TRUE, G_PARAM_READWRITE)); /* CHECKME */

  /* metadata */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NAME,
    g_param_spec_string("name","name","name",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DESCRIPTION,
    g_param_spec_string("description","description","description",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_GENRE,
    g_param_spec_string("genre","genre","genre",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  /* icecast only */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MOUNT,
    g_param_spec_string("mount","mount","mount",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DUMPFILE,
    g_param_spec_string("dumpfile","dumpfile","dumpfile",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  /* shoutcast only */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ICY,
    g_param_spec_boolean("icy","icy","icy",
                        FALSE, G_PARAM_READWRITE)); /* CHECKME */

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_AIM,
    g_param_spec_string("aim","aim","aim",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ICQ,
    g_param_spec_string("icq","icq","icq",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_IRC,
    g_param_spec_string("irc","irc","irc",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  
  gobject_class->set_property = gst_icecastsend_set_property;
  gobject_class->get_property = gst_icecastsend_get_property;

  gstelement_class->change_state = gst_icecastsend_change_state;
}

static void
gst_icecastsend_init (GstIcecastSend *icecastsend)
{
  icecastsend->sinkpad = gst_pad_new_from_template (sink_template_factory (), "sink");
  gst_element_add_pad(GST_ELEMENT(icecastsend),icecastsend->sinkpad);
  gst_pad_set_chain_function(icecastsend->sinkpad,gst_icecastsend_chain);

  icecastsend->ip = g_strdup ("127.0.0.1");
  icecastsend->port = 8000;
  icecastsend->password = g_strdup ("hackme");
  icecastsend->public = TRUE;
  icecastsend->name = g_strdup ("");
  icecastsend->description = g_strdup ("");
  icecastsend->genre = g_strdup ("");
  icecastsend->mount = g_strdup ("");
  icecastsend->dumpfile = g_strdup ("");
  icecastsend->icy = FALSE;
  icecastsend->aim = g_strdup ("");
  icecastsend->icq = g_strdup ("");
  icecastsend->irc = g_strdup ("");
}

static void
gst_icecastsend_chain (GstPad *pad, GstBuffer *buf)
{
  GstIcecastSend *icecastsend;
  glong ret;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  icecastsend = GST_ICECASTSEND (GST_OBJECT_PARENT (pad));

  g_return_if_fail (icecastsend != NULL);
  g_return_if_fail (GST_IS_ICECASTSEND (icecastsend));

  ret = shout_send_data (&icecastsend->conn, GST_BUFFER_DATA (buf),
		                             GST_BUFFER_SIZE (buf));
  if (!ret) {
    g_warning ("send error: %i...\n", icecastsend->conn.error);
  }

  shout_sleep (&icecastsend->conn);

  gst_buffer_unref (buf);
}

static void
gst_icecastsend_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstIcecastSend *icecastsend;

  g_return_if_fail(GST_IS_ICECASTSEND(object));
  icecastsend = GST_ICECASTSEND(object);

  switch (prop_id) {
    case ARG_IP:
      if (icecastsend->ip)
	g_free (icecastsend->ip);
      icecastsend->ip = g_strdup (g_value_get_string (value));
     break;
    case ARG_PORT:
      icecastsend->port = g_value_get_int (value);
      break;
    case ARG_PASSWORD:
      if (icecastsend->password)
	g_free (icecastsend->password);
      icecastsend->password = g_strdup (g_value_get_string (value));
      break;

    case ARG_PUBLIC:      /* is this stream public? */
      icecastsend->public=g_value_get_boolean (value);
      break;

    case ARG_NAME:         /* Name of the stream */
      if (icecastsend->name)
	g_free (icecastsend->name);
      icecastsend->name = g_strdup (g_value_get_string (value));
      break;

    case ARG_DESCRIPTION: /* Description of the stream */
      if (icecastsend->description)
	g_free (icecastsend->description);
      icecastsend->description = g_strdup (g_value_get_string (value));
      break;

    case ARG_GENRE:       /* Genre of the stream */
      if (icecastsend->genre)
	g_free (icecastsend->genre);
      icecastsend->genre = g_strdup (g_value_get_string (value));
      break;

    case ARG_MOUNT:       /* mountpoint of stream (icecast only) */
      if (icecastsend->mount)
	g_free (icecastsend->mount);
      icecastsend->mount = g_strdup (g_value_get_string (value));
      break;

    case ARG_DUMPFILE:    /* Dumpfile on the server for this stream (icecast only) */
      if (icecastsend->dumpfile)
	g_free (icecastsend->dumpfile);
      icecastsend->dumpfile = g_strdup (g_value_get_string (value));
      break;


    case ARG_ICY:         /* use icy headers for login? (for use with shoutcast) */
      icecastsend->icy = g_value_get_boolean (value);
      break;

    case ARG_AIM:         /* AIM number (shoutcast only) */
      if (icecastsend->aim)
	g_free (icecastsend->aim);
      icecastsend->aim = g_strdup (g_value_get_string (value));
      break;

    case ARG_ICQ:         /* ICQ number (shoutcast only) */
      if (icecastsend->icq)
	g_free (icecastsend->icq);
      icecastsend->icq = g_strdup (g_value_get_string (value));
      break;

    case ARG_IRC:         /* IRC server (shoutcast only) */
      if (icecastsend->irc)
	g_free (icecastsend->irc);
      icecastsend->irc = g_strdup (g_value_get_string (value));
      break;

    default:
      break;
  }
}

static void
gst_icecastsend_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstIcecastSend *icecastsend;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ICECASTSEND(object));
  icecastsend = GST_ICECASTSEND(object);

  switch (prop_id) {
    case ARG_IP:
      g_value_set_string (value, icecastsend->ip);
      break;
    case ARG_PORT:
      g_value_set_int (value, icecastsend->port);
      break;
    case ARG_PASSWORD:
      g_value_set_string (value, icecastsend->password);
      break;
    case ARG_PUBLIC:      /* is this stream public? */
      g_value_set_boolean (value, icecastsend->public);
      break;

    case ARG_NAME:         /* Name of the stream */
      g_value_set_string (value, icecastsend->name);
      break;

    case ARG_DESCRIPTION: /* Description of the stream */
      g_value_set_string (value, icecastsend->description);
      break;

    case ARG_GENRE:       /* Genre of the stream */
      g_value_set_string (value, icecastsend->genre);
      break;

    case ARG_MOUNT:       /* mountpoint of stream (icecast only) */
      g_value_set_string (value, icecastsend->mount);
      break;

    case ARG_DUMPFILE:    /* Dumpfile on the server for this stream (icecast only) */
      g_value_set_string (value, icecastsend->dumpfile);
      break;

    case ARG_ICY:         /* use icy headers for login? (for use with shoutcast) */
      g_value_set_boolean (value, icecastsend->icy);
      break;

    case ARG_AIM:         /* AIM number (shoutcast only) */
      g_value_set_string (value, icecastsend->aim);
      break;

    case ARG_ICQ:         /* ICQ number (shoutcast only) */
      g_value_set_string (value, icecastsend->icq);
      break;

    case ARG_IRC:         /* IRC server (shoutcast only) */
      g_value_set_string (value, icecastsend->irc);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_icecastsend_change_state (GstElement *element)
{
  GstIcecastSend *icecastsend;

  g_return_val_if_fail (GST_IS_ICECASTSEND (element), GST_STATE_FAILURE);

  icecastsend = GST_ICECASTSEND(element);

  GST_DEBUG (0,"state pending %d", GST_STATE_PENDING (element));

  /* if going down into NULL state, close the file if it's open */
  switch (GST_STATE_TRANSITION (element)) {
   case GST_STATE_NULL_TO_READY:
     shout_init_connection (&icecastsend->conn);

     /* --- FIXME: shout requires an ip, and fails if it is given a host. */
     /* may want to put convert_to_ip(icecastsend->ip) here */
     icecastsend->conn.ip = icecastsend->ip; 
     /* --- */

     icecastsend->conn.port = icecastsend->port;
     icecastsend->conn.password = icecastsend->password;
     icecastsend->conn.ispublic = icecastsend->public;
     icecastsend->conn.name = icecastsend->name;
     icecastsend->conn.description = icecastsend->description;
     icecastsend->conn.genre = icecastsend->genre;
     icecastsend->conn.mount = icecastsend->mount;
     icecastsend->conn.dumpfile = icecastsend->dumpfile;
     icecastsend->conn.icy_compat = icecastsend->icy;
     /* --- FIXME: libshout 1.0.5 doesn't have the two next fields */
     /* icecastsend->conn.aim = icecastsend->aim;
        icecastsend->conn.irc = icecastsend->irc;*/

     if (shout_connect (&icecastsend->conn)) {
       g_print ("connected to server...\n");
     }
     else {
       /* changed from g_warning, and included result code lookup. */
       g_warning ("couldn't connect to server... (%i: %s)\n", icecastsend->conn.error, SHOUT_ERRORS[icecastsend->conn.error]);
       shout_disconnect (&icecastsend->conn);
       return GST_STATE_FAILURE;
     }
     break;
   case GST_STATE_READY_TO_NULL:
     shout_disconnect (&icecastsend->conn);
     break;
   default:
     break;
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new("icecastsend", GST_TYPE_ICECASTSEND, &icecastsend_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, sink_template_factory ());

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "icecastsend",
  plugin_init
};

