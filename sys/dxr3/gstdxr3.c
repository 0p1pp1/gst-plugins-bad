#include "gstdxr3videosink.h"

/* elementfactory information */
static GstElementDetails dxr3_video_sink_details = {
  "dxr3/Hollywood+ mpeg decoder board plugin",
  "video/mpeg",
  "GPL",
  "Outputs PAL/NTSC video via the dxr3/Hollywood+ mpeg decoder board",
  VERSION,
  "Rehan Khwaja <rehankhwaja@yahoo.com>",
  "(C) 2002",
};


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *video_factory;

  video_factory = gst_element_factory_new("dxr3videosink", GST_TYPE_DXR3_VIDEO_SINK, &dxr3_video_sink_details);
  g_return_val_if_fail(video_factory != NULL, FALSE);

  gst_element_factory_add_pad_template (video_factory, GST_PAD_TEMPLATE_GET (dxr3_video_sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (video_factory));

  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "dxr3",
  plugin_init
};

	
