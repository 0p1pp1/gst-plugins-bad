/*
 * this library handles interaction with GConf
 */

#include "gconf.h"

#define GST_GCONF_DIR "/system/gstreamer"

static GConfClient *_gst_gconf_client = NULL; /* GConf connection */


/* internal functions */

static GConfClient *
gst_gconf_get_client (void)
{
  if (!_gst_gconf_client)
    _gst_gconf_client = gconf_client_get_default ();

  return _gst_gconf_client;
}

/* go through a bin, finding the one pad that is unconnected in the given
 *  * direction, and return that pad */
static GstPad *
gst_bin_find_unconnected_pad (GstBin *bin, GstPadDirection direction)
{
  GstPad *pad = NULL;
  GList *elements = NULL;
  const GList *pads = NULL;
  GstElement *element = NULL;

  elements = (GList *) gst_bin_get_list (bin);
  /* traverse all elements looking for unconnected pads */
  while (elements && pad == NULL)
  {
    element = GST_ELEMENT (elements->data);
    pads = gst_element_get_pad_list (element);
    while (pads)
    {
      /* check if the direction matches */
      if (GST_PAD_DIRECTION (GST_PAD (pads->data)) == direction)
      {
        if (GST_PAD_PEER (GST_PAD (pads->data)) == NULL)
        {
          /* found it ! */
	  pad = GST_PAD (pads->data);
	}
      }
      if (pad) break; /* found one already */
      pads = g_list_next (pads);
    }
    elements = g_list_next (elements);
  }
  return pad;
}

/* external functions */

/**
 * gst_gconf_get_string:
 * @key: a #gchar corresponding to the key you want to get.
 *
 * Get GConf key @key's string value.
 *
 * Returns: a #gchar string containing @key's value.
 */
gchar *
gst_gconf_get_string (const gchar *key)
{
  GError *error = NULL;
  gchar *value = NULL;
  gchar *full_key = g_strdup_printf ("%s/%s", GST_GCONF_DIR, key);


  value = gconf_client_get_string (gst_gconf_get_client (), full_key, &error);
  g_free (full_key);

  if (error)
  {
    g_warning ("gst_gconf_get_string: error: %s\n", error->message);
    g_error_free (error);
  }
  /* FIXME: decide if we want to strdup this value; if we do, check for NULL */
  return value;
}

/**
 * gst_gconf_set_string:
 * @key: a #gchar corresponding to the key you want to set.
 * @value: a #gchar containing key value.
 *
 * Set GConf key @key to string value @value.
 */
void
gst_gconf_set_string (const gchar *key, const gchar *value)
{
  GError *error = NULL;
  gchar *full_key = g_strdup_printf ("%s/%s", GST_GCONF_DIR, key);

  gconf_client_set_string (gst_gconf_get_client (), full_key, value, &error);
  if (error)
  {
    g_warning ("gst_gconf_set_string: error: %s\n", error->message);
    g_error_free (error);
  }
  g_free (full_key);
}

/**
 * gst_gconf_render_bin_from_description:
 * @description: a #gchar string describing the bin.
 *
 * Render bin from description @description.
 *
 * Returns: a #GstElement containing the rendered bin.
 */
GstElement *
gst_gconf_render_bin_from_description (const gchar *description)
{
  GstElement *bin = NULL;
  GstPad *pad = NULL;
  GError *error = NULL;
  gchar *desc = NULL;

  /* parse the pipeline to a bin */
  desc = g_strdup_printf ("bin.( %s )", description);
  bin = GST_ELEMENT (gst_parse_launch (desc, &error));
  g_free (desc);
  if (error)
  {
    g_print ("DEBUG: gstgconf: error parsing pipeline %s\n%s\n",
	     description, error->message);
    g_error_free (error);
    return NULL;
  }

  /* find pads and ghost them if necessary */
  if ((pad = gst_bin_find_unconnected_pad (GST_BIN (bin), GST_PAD_SRC))){
    gst_element_add_ghost_pad (bin, pad, "src");
  }
  if ((pad = gst_bin_find_unconnected_pad (GST_BIN (bin), GST_PAD_SINK))){
    gst_element_add_ghost_pad (bin, pad, "sink");
  }
  return bin;
}

/**
 * gst_gconf_render_bin_from_key:
 * @key: a #gchar string corresponding to a GConf key.
 *
 * Render bin from GConf key @key.
 *
 * Returns: a #GstElement containing the rendered bin.
 */
GstElement *
gst_gconf_render_bin_from_key (const gchar *key)
{
  GstElement *bin = NULL;
  gchar *value;
  
  value = gst_gconf_get_string (key);
  if (value)
    bin = gst_gconf_render_bin_from_description (value);
  return bin;
}

/**
 * gst_gconf_get_default_audio_sink:
 *
 * Render audio output bin from GStreamer GConf key : "default/audiosink".
 * If key is invalid osssink is used as default output plugin.
 *
 * Returns: a #GstElement containing the audio output bin, or NULL if
 * everything failed.
 */
GstElement *
gst_gconf_get_default_audio_sink (void)
{
  GstElement *ret = gst_gconf_render_bin_from_key ("default/audiosink");
  
  if (!ret) {
    ret = gst_element_factory_make ("osssink", NULL);
  
    if (!ret)
      g_warning ("No GConf default audio sink key and osssink doesn't work");
    else
      g_warning ("GConf audio sink not found, using osssink");
  }

  return ret;
}

/**
 * gst_gconf_get_default_video_sink:
 *
 * Render video output bin from GStreamer GConf key : "default/videosink".
 * If key is invalid xvideosink is used as default output plugin.
 *
 * Returns: a #GstElement containing the video output bin, or NULL if
 * everything failed.
 */
GstElement *
gst_gconf_get_default_video_sink (void)
{
  GstElement *ret = gst_gconf_render_bin_from_key ("default/videosink");
  
  if (!ret) {
    ret = gst_element_factory_make ("xvideosink", NULL);
  
    if (!ret)
      g_warning ("No GConf default video sink key and xvideosink doesn't work");
    else
      g_warning ("GConf video sink not found, using xvideosink");
  }

  return ret;
}

/**
 * gst_gconf_get_default_audio_src:
 *
 * Render audio acquisition bin from GStreamer GConf key : "default/audiosrc".
 * If key is invalid osssrc is used as default source.
 *
 * Returns: a #GstElement containing the audio source bin, or NULL if
 * everything failed.
 */
GstElement *
gst_gconf_get_default_audio_src (void)
{
  GstElement *ret = gst_gconf_render_bin_from_key ("default/audiosrc");
  
  if (!ret) {
    ret = gst_element_factory_make ("osssrc", NULL);
  
    if (!ret)
      g_warning ("No GConf default audio src key and osssrc doesn't work");
    else
      g_warning ("GConf audio src not found, using osssrc");
  }

  return ret;
}

/**
 * gst_gconf_get_default_video_src:
 *
 * Render video acquisition bin from GStreamer GConf key :
 * "default/videosrc". If key is invalid videotestsrc
 * is used as default source.
 *
 * Returns: a #GstElement containing the video source bin, or NULL if
 * everything failed.
 */
GstElement *
gst_gconf_get_default_video_src (void)
{
  GstElement *ret = gst_gconf_render_bin_from_key ("default/videosrc");
  
  if (!ret) {
    ret = gst_element_factory_make ("videotestsrc", NULL);
  
    if (!ret)
      g_warning ("No GConf default video src key and videotestrc doesn't work");
    else
      g_warning ("GConf video src not found, using videotestrc");
  }

  return ret;
}

/**
 * gst_gconf_get_default_visualisation_element:
 *
 * Render visualisation bin from GStreamer GConf key : "default/visualisation".
 * If key is invalid goom is used as default visualisation element.
 *
 * Returns: a #GstElement containing the visualisation bin, or NULL if
 * everything failed.
 */
GstElement *
gst_gconf_get_default_visualisation_element (void)
{
  GstElement *ret = gst_gconf_render_bin_from_key ("default/visualisation");
  
  if (!ret) {
    ret = gst_element_factory_make ("goom", NULL);
  
    if (!ret)
      g_warning ("No GConf default visualisation plugin key and goom doesn't work");
    else
      g_warning ("GConf visualisation plugin not found, using goom");
  }

  return ret;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
    gst_plugin_set_longname (plugin, 
	                     "Convenience routines for GConf interaction");
      return TRUE;
}

GstPluginDesc plugin_desc = {
    GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
        "gstgconf",
	  plugin_init
};

