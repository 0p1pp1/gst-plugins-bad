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
#include <gstsmooth.h>

/* elementfactory information */
static GstElementDetails smooth_details = {
  "Smooth effect",
  "Filter/Video",
  "LGPL",
  "Apply a smooth filter to an image",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};


/* Smooth signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_ACTIVE,
  ARG_TOLERANCE,
  ARG_FILTERSIZE,
  ARG_LUM_ONLY
};

GST_PAD_TEMPLATE_FACTORY (smooth_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
   "smooth_src",
   "video/raw",
     "format",   GST_PROPS_FOURCC (GST_STR_FOURCC ("I420"))
  )
)

GST_PAD_TEMPLATE_FACTORY (smooth_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
   "smooth_src",
   "video/raw",
     "format",   GST_PROPS_FOURCC (GST_STR_FOURCC ("I420"))
  )
)

static void	gst_smooth_class_init	(GstSmoothClass *klass);
static void	gst_smooth_init		(GstSmooth *smooth);

static void	gst_smooth_chain	(GstPad *pad, GstBuffer *buf);
static void	smooth_filter		(unsigned char* dest, unsigned char* src,
					 int width, int height, int tolerance, int filtersize);

static void	gst_smooth_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_smooth_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
/*static guint gst_smooth_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_smooth_get_type (void)
{
  static GType smooth_type = 0;

  if (!smooth_type) {
    static const GTypeInfo smooth_info = {
      sizeof(GstSmoothClass),      NULL,
      NULL,
      (GClassInitFunc)gst_smooth_class_init,
      NULL,
      NULL,
      sizeof(GstSmooth),
      0,
      (GInstanceInitFunc)gst_smooth_init,
    };
    smooth_type = g_type_register_static(GST_TYPE_ELEMENT, "GstSmooth", &smooth_info, 0);
  }
  return smooth_type;
}

static void
gst_smooth_class_init (GstSmoothClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ACTIVE,
    g_param_spec_boolean("active","active","active",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_TOLERANCE,
    g_param_spec_int("tolerance","tolerance","tolerance",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FILTERSIZE,
    g_param_spec_int("filtersize","filtersize","filtersize",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */

  gobject_class->set_property = gst_smooth_set_property;
  gobject_class->get_property = gst_smooth_get_property;

}

static GstPadLinkReturn
gst_smooth_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstSmooth *filter;

  filter = GST_SMOOTH (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  gst_caps_get_int (caps, "width", &filter->width);
  gst_caps_get_int (caps, "height", &filter->height);

  return GST_PAD_LINK_OK;
}

static void
gst_smooth_init (GstSmooth *smooth)
{
  smooth->sinkpad = gst_pad_new_from_template (
                  GST_PAD_TEMPLATE_GET (smooth_sink_factory), "sink");
  gst_pad_set_link_function (smooth->sinkpad, gst_smooth_sinkconnect);
  gst_pad_set_chain_function (smooth->sinkpad, gst_smooth_chain);
  gst_element_add_pad (GST_ELEMENT (smooth), smooth->sinkpad);

  smooth->srcpad = gst_pad_new_from_template (
                  GST_PAD_TEMPLATE_GET (smooth_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (smooth), smooth->srcpad);

  smooth->active = TRUE;
  smooth->tolerance = 8;
  smooth->filtersize = 3;
  smooth->lum_only = TRUE;
}

static void
smooth_filter (unsigned char* dest, unsigned char* src, int width, int height, int tolerance, int filtersize)
{
  int refval, aktval, upperval, lowerval, numvalues, sum;
  int x, y, fx, fy, fy1, fy2, fx1, fx2;
  unsigned char *srcp = src;

  fy1 = 0;
  fy2 = MIN(filtersize+1, height) * width;

  for(y = 0; y < height; y++)
  {
    if (y>(filtersize+1)) fy1 += width;
    if (y<height-(filtersize+1)) fy2 += width;

    for(x = 0; x < width; x++)
    {
      refval    = *src;
      upperval  = refval + tolerance;
      lowerval  = refval - tolerance;

      numvalues = 1;
      sum       = refval;

      fx1      = MAX(x-filtersize,   0)     + fy1;
      fx2      = MIN(x+filtersize+1, width) + fy1;

      for (fy = fy1; fy<fy2; fy+=width)
      {
        for (fx = fx1; fx<fx2; fx++)
        {
          aktval = srcp[fx];
          if ((lowerval-aktval)*(upperval-aktval)<0)
          {
            numvalues ++;
            sum += aktval;
          }
        } /*for fx */
        fx1 += width;
        fx2 += width;
      } /*for fy */

      src++;
      *dest++ = sum/numvalues;
    }
  }
}

static void
gst_smooth_chain (GstPad *pad, GstBuffer *buf)
{
  GstSmooth *smooth;
  guchar *data;
  gulong size;
  GstBuffer *outbuf;
  gint lumsize, chromsize;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  smooth = GST_SMOOTH (GST_OBJECT_PARENT (pad));

  if (!smooth->active) {
    gst_pad_push(smooth->srcpad,buf);
    return;
  }

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_DEBUG (0,"smooth: have buffer of %d", GST_BUFFER_SIZE (buf));

  outbuf = gst_buffer_new();
  GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (buf));
  GST_BUFFER_SIZE (outbuf) = GST_BUFFER_SIZE (buf);

  lumsize = smooth->width*smooth->height;
  chromsize = lumsize/4;

  smooth_filter (GST_BUFFER_DATA (outbuf), data, smooth->width, smooth->height, 
		  smooth->tolerance, smooth->filtersize);
  if (!smooth->lum_only) {
    smooth_filter (GST_BUFFER_DATA (outbuf)+lumsize, data+lumsize, smooth->width/2, smooth->height/2, 
		  smooth->tolerance, smooth->filtersize/2);
    smooth_filter (GST_BUFFER_DATA (outbuf)+lumsize+chromsize, data+lumsize+chromsize, smooth->width/2, 
		  smooth->height/2, smooth->tolerance, smooth->filtersize/2);
  }
  else {
    memcpy (GST_BUFFER_DATA (outbuf)+lumsize, data+lumsize, chromsize*2);
  }

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

  gst_buffer_unref (buf);

  gst_pad_push (smooth->srcpad, outbuf);
}

static void
gst_smooth_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSmooth *smooth;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SMOOTH(object));
  smooth = GST_SMOOTH(object);

  switch (prop_id) {
    case ARG_ACTIVE:
      smooth->active = g_value_get_boolean (value);
      break;
    case ARG_TOLERANCE:
      smooth->tolerance = g_value_get_int (value);
      break;
    case ARG_FILTERSIZE:
      smooth->filtersize = g_value_get_int (value);
      break;
    case ARG_LUM_ONLY:
      smooth->lum_only = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
gst_smooth_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSmooth *smooth;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SMOOTH(object));
  smooth = GST_SMOOTH(object);

  switch (prop_id) {
    case ARG_ACTIVE:
      g_value_set_boolean (value, smooth->active);
      break;
    case ARG_TOLERANCE:
      g_value_set_int (value, smooth->tolerance);
      break;
    case ARG_FILTERSIZE:
      g_value_set_int (value, smooth->filtersize);
      break;
    case ARG_LUM_ONLY:
      g_value_set_boolean (value, smooth->lum_only);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new("smooth",GST_TYPE_SMOOTH,
                                   &smooth_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (smooth_sink_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (smooth_src_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "smooth",
  plugin_init
};

