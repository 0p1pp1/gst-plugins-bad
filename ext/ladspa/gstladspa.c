/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2001> Steve Baker <stevebaker_org@yahoo.co.uk>
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
#include <math.h>
#include <gst/control/control.h>
#include <gst/audio/audio.h>

#include "gstladspa.h"
#include <ladspa.h>     /* main ladspa sdk include file */
#include "utils.h"      /* ladspa sdk utility functions */


/* takes ownership of the name */
static GstPadTemplate*
ladspa_sink_factory (gchar *name)
{
  return GST_PAD_TEMPLATE_NEW (
  name,
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  gst_caps_new (
    "ladspa_sink",
    "audio/x-raw-float",
    GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_PROPS
    )
  );
}

/* takes ownership of the name */
static GstPadTemplate*
ladspa_src_factory (gchar *name)
{
  return GST_PAD_TEMPLATE_NEW (
  name,
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  gst_caps_new (
    "ladspa_src",
    "audio/x-raw-float",
    GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_PROPS
    )
  );
}

static void			gst_ladspa_class_init		(GstLADSPAClass *klass);
static void			gst_ladspa_init			(GstLADSPA *ladspa);

static void			gst_ladspa_update_int		(const GValue *value, gpointer data);
static GstPadLinkReturn		gst_ladspa_link			(GstPad *pad, GstCaps *caps);
static void			gst_ladspa_force_src_caps	(GstLADSPA *ladspa, GstPad *pad);

static void			gst_ladspa_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void			gst_ladspa_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean			gst_ladspa_instantiate		(GstLADSPA *ladspa);
static void			gst_ladspa_activate		(GstLADSPA *ladspa);
static void			gst_ladspa_deactivate		(GstLADSPA *ladspa);

static GstElementStateReturn	gst_ladspa_change_state		(GstElement *element);
static void			gst_ladspa_loop			(GstElement *element);
static void			gst_ladspa_chain		(GstPad *pad,GstData *_data);
static GstData *		gst_ladspa_get			(GstPad *pad);

static GstElementClass *parent_class = NULL;

static GstPlugin *ladspa_plugin;
static GHashTable *ladspa_descriptors;

enum {
  ARG_0,
  ARG_SAMPLERATE,
  ARG_BUFFERSIZE,
  ARG_LAST,
};

GST_DEBUG_CATEGORY_STATIC (ladspa_debug);
#define DEBUG(...) \
    GST_CAT_LEVEL_LOG (ladspa_debug, GST_LEVEL_DEBUG, NULL, __VA_ARGS__)
#define DEBUG_OBJ(obj,...) \
    GST_CAT_LEVEL_LOG (ladspa_debug, GST_LEVEL_DEBUG, obj, __VA_ARGS__)

static void
gst_ladspa_class_init (GstLADSPAClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  LADSPA_Descriptor *desc;
  gint i,current_portnum,sinkcount,srccount,controlcount;
  gint hintdesc;
  gint argtype,argperms;
  GParamSpec *paramspec = NULL;
  gchar *argname, *tempstr, *paren;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  gobject_class->set_property = gst_ladspa_set_property;
  gobject_class->get_property = gst_ladspa_get_property;

  gstelement_class->change_state = gst_ladspa_change_state;

  /* look up and store the ladspa descriptor */
  klass->descriptor = g_hash_table_lookup(ladspa_descriptors,GINT_TO_POINTER(G_TYPE_FROM_CLASS(klass)));
  desc = klass->descriptor;

  klass->numports = desc->PortCount;

  klass->numsinkpads = 0;
  klass->numsrcpads = 0;
  klass->numcontrols = 0;

  /* walk through the ports, count the input, output and control ports */
  for (i=0; i<desc->PortCount; i++) {
    if (LADSPA_IS_PORT_AUDIO(desc->PortDescriptors[i]))
      if (LADSPA_IS_PORT_INPUT(desc->PortDescriptors[i]))
        klass->numsinkpads++;
      else
        klass->numsrcpads++;
    else if (LADSPA_IS_PORT_INPUT(desc->PortDescriptors[i]))
      klass->numcontrols++;
  }

  DEBUG ("ladspa element class: init %s with %d sink, %d src, %d control\n",
         g_type_name (G_TYPE_FROM_CLASS (klass)),
         klass->numsinkpads, klass->numsrcpads, klass->numcontrols);

  klass->srcpad_portnums = g_new0(gint,klass->numsrcpads);
  klass->sinkpad_portnums = g_new0(gint,klass->numsinkpads);
  klass->control_portnums = g_new0(gint,klass->numcontrols);
  sinkcount = 0;
  srccount = 0;
  controlcount = 0;

  /* walk through the ports, note the portnums for srcpads, sinkpads and control
     params */
  for (i=0; i<desc->PortCount; i++) {
    if (LADSPA_IS_PORT_AUDIO(desc->PortDescriptors[i]))
      if (LADSPA_IS_PORT_INPUT(desc->PortDescriptors[i]))
        klass->sinkpad_portnums[sinkcount++] = i;
      else
        klass->srcpad_portnums[srccount++] = i;
    else if (LADSPA_IS_PORT_INPUT(desc->PortDescriptors[i]))
      klass->control_portnums[controlcount++] = i;
  }

  /* now build the control info from the control ports */
  klass->control_info = g_new0(ladspa_control_info,klass->numcontrols);
    
  for (i=0;i<klass->numcontrols;i++) {
    current_portnum = klass->control_portnums[i];
    
    /* short name for hint descriptor */
    hintdesc = desc->PortRangeHints[current_portnum].HintDescriptor;

    /* get the various bits */
    if (LADSPA_IS_HINT_TOGGLED(hintdesc))
      klass->control_info[i].toggled = TRUE;
    if (LADSPA_IS_HINT_LOGARITHMIC(hintdesc))
      klass->control_info[i].logarithmic = TRUE;
    if (LADSPA_IS_HINT_INTEGER(hintdesc))
      klass->control_info[i].integer = TRUE;

    /* figure out the argument details */
    if (klass->control_info[i].toggled) argtype = G_TYPE_BOOLEAN;
    else if (klass->control_info[i].integer) argtype = G_TYPE_INT;
    else argtype = G_TYPE_FLOAT;

    /* grab the bounds */
    if (LADSPA_IS_HINT_BOUNDED_BELOW(hintdesc)) {
      klass->control_info[i].lower = TRUE;
      klass->control_info[i].lowerbound =
        desc->PortRangeHints[current_portnum].LowerBound;
    } else {
      if (argtype==G_TYPE_INT) klass->control_info[i].lowerbound = (gfloat)G_MININT;
      if (argtype==G_TYPE_FLOAT) klass->control_info[i].lowerbound = -G_MAXFLOAT;
    }
    
    if (LADSPA_IS_HINT_BOUNDED_ABOVE(hintdesc)) {
      klass->control_info[i].upper = TRUE;
      klass->control_info[i].upperbound =
        desc->PortRangeHints[current_portnum].UpperBound;
      if (LADSPA_IS_HINT_SAMPLE_RATE(hintdesc)) {
        klass->control_info[i].samplerate = TRUE;
        klass->control_info[i].upperbound *= 44100; /* FIXME? */
      }
    } else {
      if (argtype==G_TYPE_INT) klass->control_info[i].upperbound = (gfloat)G_MAXINT;
      if (argtype==G_TYPE_FLOAT) klass->control_info[i].upperbound = G_MAXFLOAT;
    }

    /* use the lowerbound as the default value */
    klass->control_info[i].def = klass->control_info[i].lowerbound;

#ifdef LADSPA_IS_HINT_HAS_DEFAULT
    /* figure out the defaults */
    if (LADSPA_IS_HINT_HAS_DEFAULT (hintdesc)) {
      if (LADSPA_IS_HINT_DEFAULT_MINIMUM (hintdesc))
        klass->control_info[i].def = klass->control_info[i].lowerbound;
      else if (LADSPA_IS_HINT_DEFAULT_LOW (hintdesc))
        if (LADSPA_IS_HINT_LOGARITHMIC (hintdesc))
          klass->control_info[i].def = exp (0.75*log(klass->control_info[i].lowerbound) +
                                                0.25*log(klass->control_info[i].upperbound));
        else
          klass->control_info[i].def = (0.75*klass->control_info[i].lowerbound +
                                            0.25*klass->control_info[i].upperbound);
      else if (LADSPA_IS_HINT_DEFAULT_MIDDLE (hintdesc))
        if (LADSPA_IS_HINT_LOGARITHMIC (hintdesc))
          klass->control_info[i].def = exp (0.5*log(klass->control_info[i].lowerbound) +
                                                0.5*log(klass->control_info[i].upperbound));
        else
          klass->control_info[i].def = (0.5*klass->control_info[i].lowerbound +
                                            0.5*klass->control_info[i].upperbound);
      else if (LADSPA_IS_HINT_DEFAULT_HIGH (hintdesc))
        if (LADSPA_IS_HINT_LOGARITHMIC (hintdesc))
          klass->control_info[i].def = exp (0.25*log(klass->control_info[i].lowerbound) +
                                                0.75*log(klass->control_info[i].upperbound));
        else
          klass->control_info[i].def = (0.25*klass->control_info[i].lowerbound +
                                            0.75*klass->control_info[i].upperbound);
      else if (LADSPA_IS_HINT_DEFAULT_MAXIMUM (hintdesc))
        klass->control_info[i].def = klass->control_info[i].upperbound;
      else if (LADSPA_IS_HINT_DEFAULT_0 (hintdesc))
        klass->control_info[i].def = 0.0;
      else if (LADSPA_IS_HINT_DEFAULT_1 (hintdesc))
        klass->control_info[i].def = 1.0;
      else if (LADSPA_IS_HINT_DEFAULT_100 (hintdesc))
        klass->control_info[i].def = 100.0;
      else if (LADSPA_IS_HINT_DEFAULT_440 (hintdesc))
        klass->control_info[i].def = 440.0;
    }
#endif /* LADSPA_IS_HINT_HAS_DEFAULT */

    klass->control_info[i].def = CLAMP(klass->control_info[i].def,
                                       klass->control_info[i].lowerbound,
                                       klass->control_info[i].upperbound);
    
    if (LADSPA_IS_PORT_INPUT(desc->PortDescriptors[current_portnum])) {
      argperms = G_PARAM_READWRITE;
      klass->control_info[i].writable = TRUE;
    } else {
      argperms = G_PARAM_READABLE;
      klass->control_info[i].writable = FALSE;
    }

    klass->control_info[i].name = g_strdup(desc->PortNames[current_portnum]);
    argname = g_strdup(klass->control_info[i].name);
    /* find out if there is a (unitname) at the end of the argname and get rid
       of it */
    paren = g_strrstr (argname, " (");
    if (paren != NULL) {
      *paren = '\0';
    }
    /* this is the same thing that param_spec_* will do */
    g_strcanon (argname, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-", '-');
    /* satisfy glib2 (argname[0] must be [A-Za-z]) */
    if (!((argname[0] >= 'a' && argname[0] <= 'z') || (argname[0] >= 'A' && argname[0] <= 'Z'))) {
      tempstr = argname;
      argname = g_strconcat("param-", argname, NULL);
      g_free (tempstr);
    }
    
    /* check for duplicate property names */
    if (g_object_class_find_property(G_OBJECT_CLASS(klass), argname) != NULL){
      gint numarg=1;
      gchar *numargname = g_strdup_printf("%s_%d",argname,numarg++);
      while (g_object_class_find_property(G_OBJECT_CLASS(klass), numargname) != NULL){
        g_free(numargname);
        numargname = g_strdup_printf("%s_%d",argname,numarg++);
      }
      argname = numargname;
    }
    
    klass->control_info[i].param_name = argname;
    
    DEBUG ("adding arg %s from %s", argname, klass->control_info[i].name);
    
    if (argtype==G_TYPE_BOOLEAN){
      paramspec = g_param_spec_boolean(argname,argname,argname, FALSE, argperms);
    } else if (argtype==G_TYPE_INT){      
      paramspec = g_param_spec_int(argname,argname,argname, 
        (gint)klass->control_info[i].lowerbound, 
        (gint)klass->control_info[i].upperbound, 
        (gint)klass->control_info[i].def, argperms);
    } else if (klass->control_info[i].samplerate){
      paramspec = g_param_spec_float(argname,argname,argname, 
        0.0, G_MAXFLOAT, 
        0.0, argperms);
    } else {
      paramspec = g_param_spec_float(argname,argname,argname, 
        klass->control_info[i].lowerbound, klass->control_info[i].upperbound, 
        klass->control_info[i].def, argperms);
    }
    
    /* properties have an offset of 1 */
    g_object_class_install_property(G_OBJECT_CLASS(klass), i+1, paramspec);
  }
}

static void
gst_ladspa_init (GstLADSPA *ladspa)
{
  GstLADSPAClass *oclass;
  ladspa_control_info cinfo;
  GList *l;
  LADSPA_Descriptor *desc;
  gint i,sinkcount,srccount;

  oclass = (GstLADSPAClass*)G_OBJECT_GET_CLASS (ladspa);
  desc = oclass->descriptor;
  ladspa->descriptor = oclass->descriptor;
  
  /* allocate the various arrays */
  ladspa->srcpads = g_new0(GstPad*,oclass->numsrcpads);
  ladspa->sinkpads = g_new0(GstPad*,oclass->numsinkpads);
  ladspa->controls = g_new(gfloat,oclass->numcontrols);
  ladspa->dpman = gst_dpman_new ("ladspa_dpman", GST_ELEMENT(ladspa));
  
  /* set up pads */
  sinkcount = 0;
  srccount = 0;
  for (l=GST_ELEMENT_CLASS (oclass)->padtemplates; l; l=l->next) {
    GstPad *pad = gst_pad_new_from_template (GST_PAD_TEMPLATE (l->data),
                                             GST_PAD_TEMPLATE_NAME_TEMPLATE (l->data));
    gst_pad_set_link_function (pad, gst_ladspa_link);
    gst_element_add_pad ((GstElement*)ladspa, pad);

    if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK)
      ladspa->sinkpads[sinkcount++] = pad;
    else
      ladspa->srcpads[srccount++] = pad;
  }
  
  /* set up dparams */
  for (i=0; i<oclass->numcontrols; i++) {
    if (LADSPA_IS_PORT_INPUT(desc->PortDescriptors[i])) {
      cinfo = oclass->control_info[i];
      ladspa->controls[i]=cinfo.def;
      
      if (cinfo.toggled){
        gst_dpman_add_required_dparam_callback (
          ladspa->dpman, 
          g_param_spec_int(cinfo.param_name, cinfo.name, cinfo.name,
                           0, 1, (gint)(ladspa->controls[i]), G_PARAM_READWRITE),
          "int", gst_ladspa_update_int, &(ladspa->controls[i])
        );
      }
      else if (cinfo.integer){
        gst_dpman_add_required_dparam_callback (
          ladspa->dpman, 
          g_param_spec_int(cinfo.param_name, cinfo.name, cinfo.name,
                           (gint)cinfo.lowerbound, (gint)cinfo.upperbound,
                           (gint)ladspa->controls[i], G_PARAM_READWRITE),
          "int", gst_ladspa_update_int, &(ladspa->controls[i])
        );
      }
      else if (cinfo.samplerate){
        gst_dpman_add_required_dparam_direct (
          ladspa->dpman, 
          g_param_spec_float(cinfo.param_name, cinfo.name, cinfo.name,
                           cinfo.lowerbound, cinfo.upperbound,
                           ladspa->controls[i], G_PARAM_READWRITE),
          "hertz-rate-bound", &(ladspa->controls[i])
        );
      }
      else {
        gst_dpman_add_required_dparam_direct (
          ladspa->dpman, 
          g_param_spec_float(cinfo.param_name, cinfo.name, cinfo.name,
                           cinfo.lowerbound, cinfo.upperbound,
                           ladspa->controls[i], G_PARAM_READWRITE),
          "float", &(ladspa->controls[i])
        );
      }
    }
  }

  /* nonzero default needed to instantiate() some plugins */
  ladspa->samplerate = 44100;

  ladspa->buffer_frames = 0; /* should be set with caps */
  ladspa->activated = FALSE;
  ladspa->bufpool = NULL;
  ladspa->inplace_broken = LADSPA_IS_INPLACE_BROKEN(ladspa->descriptor->Properties);

  if (sinkcount==0 && srccount == 1) {
    /* get mode (no sink pads) */
    DEBUG_OBJ (ladspa, "mono get mode with 1 src pad");

    gst_pad_set_get_function (ladspa->srcpads[0], gst_ladspa_get);
  } else if (sinkcount==1){
    /* with one sink we can use the chain function */
    DEBUG_OBJ (ladspa, "chain mode");

    gst_pad_set_chain_function (ladspa->sinkpads[0], gst_ladspa_chain);
  } else if (sinkcount > 1){
    /* more than one sink pad needs loop mode */
    DEBUG_OBJ (ladspa, "loop mode with %d sink pads and %d src pads", sinkcount, srccount);

    gst_element_set_loop_function (GST_ELEMENT (ladspa), gst_ladspa_loop);
  } else if (sinkcount==0 && srccount == 0) {
    /* for example, a plugin with only control inputs and output -- just ignore
     * it for now */
  } else {
    g_warning ("%d sink pads, %d src pads not yet supported", sinkcount, srccount);
  }

  gst_ladspa_instantiate (ladspa);
}

static void
gst_ladspa_update_int(const GValue *value, gpointer data)
{
  gfloat *target = (gfloat*) data;
  *target = (gfloat)g_value_get_int(value);
}

static GstPadLinkReturn
gst_ladspa_link (GstPad *pad, GstCaps *caps)
{
  GstElement *element = (GstElement*)GST_PAD_PARENT (pad);
  GstLADSPA *ladspa = (GstLADSPA*)element;
  const GList *l = NULL;
  gint rate;

  if (GST_CAPS_IS_FIXED (caps)) {
    /* if this fails in some other plugin, the graph is left in an inconsistent
       state */
    for (l=gst_element_get_pad_list (element); l; l=l->next)
      if (pad != (GstPad*)l->data)
        if (gst_pad_try_set_caps ((GstPad*)l->data, caps) <= 0)
          return GST_PAD_LINK_REFUSED;
    
    /* we assume that the ladspa plugin can handle any sample rate, so this
       check gets put last */
    gst_caps_get_int (caps, "rate", &rate);
    /* have to instantiate ladspa plugin when samplerate changes (groan) */
    if (ladspa->samplerate != rate) {
      ladspa->samplerate = rate;
      if (! gst_ladspa_instantiate(ladspa))
        return GST_PAD_LINK_REFUSED;
    }
    
    gst_caps_get_int (caps, "buffer-frames", &ladspa->buffer_frames);
    
    if (ladspa->bufpool)
      gst_buffer_pool_unref (ladspa->bufpool);
    ladspa->bufpool = gst_buffer_pool_get_default (ladspa->buffer_frames * sizeof(gfloat),
                                                   3);
    
    return GST_PAD_LINK_OK;
  }
  
  return GST_PAD_LINK_DELAYED;
}

static void
gst_ladspa_force_src_caps(GstLADSPA *ladspa, GstPad *pad)
{
  if (!ladspa->buffer_frames) {
    ladspa->buffer_frames = 256; /* 5 ms at 44100 kHz (just a default...) */
    g_return_if_fail (ladspa->bufpool == NULL);
    ladspa->bufpool =
      gst_buffer_pool_get_default (ladspa->buffer_frames * sizeof(gfloat), 3);
  }

  DEBUG_OBJ (ladspa, "forcing caps with rate=%d, buffer-frames=%d",
             ladspa->samplerate, ladspa->buffer_frames);

  gst_pad_try_set_caps (pad,
    gst_caps_new (
    "ladspa_src_caps",
    "audio/x-raw-float",
    gst_props_new (
      "width",          GST_PROPS_INT (32),
      "endianness",     GST_PROPS_INT (G_BYTE_ORDER),
      "rate",           GST_PROPS_INT (ladspa->samplerate),
      "buffer-frames",	GST_PROPS_INT (ladspa->buffer_frames),
      "channels",	GST_PROPS_INT (1),
      NULL)));
}

static void
gst_ladspa_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstLADSPA *ladspa = (GstLADSPA*)object;
  GstLADSPAClass *oclass;
  ladspa_control_info *control_info;

  oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS (object));

  /* remember, properties have an offset of 1 */
  prop_id--;

  /* verify it exists */
  g_return_if_fail (prop_id < oclass->numcontrols);

  control_info = &(oclass->control_info[prop_id]);
  g_return_if_fail (control_info->name != NULL);

  /* check to see if it's writable */
  g_return_if_fail (control_info->writable);

  /* now see what type it is */
  if (control_info->toggled)
    ladspa->controls[prop_id] = g_value_get_boolean (value) ? 1.f : 0.f;
  else if (control_info->integer)
    ladspa->controls[prop_id] = g_value_get_int (value);
  else
    ladspa->controls[prop_id] = g_value_get_float (value);

  DEBUG_OBJ (object, "set arg %s to %f", control_info->name, ladspa->controls[prop_id]);
}

static void
gst_ladspa_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstLADSPA *ladspa = (GstLADSPA*)object;
  GstLADSPAClass *oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS (object));
  ladspa_control_info *control_info;

  /* remember, properties have an offset of 1 */
  prop_id--;

  /* verify it exists */
  g_return_if_fail (prop_id < oclass->numcontrols);

  control_info = &(oclass->control_info[prop_id]);
  g_return_if_fail (control_info->name != NULL);

  /* now see what type it is */
  if (control_info->toggled)
    g_value_set_boolean (value, ladspa->controls[prop_id] == 1.0);
  else if (control_info->integer)
    g_value_set_int (value, (gint)ladspa->controls[prop_id]);
  else
    g_value_set_float (value, ladspa->controls[prop_id]);

  DEBUG_OBJ (object, "got arg %s as %f", control_info->name, ladspa->controls[prop_id]);
}

static gboolean
gst_ladspa_instantiate (GstLADSPA *ladspa)
{
  LADSPA_Descriptor *desc;
  int i;
  GstLADSPAClass *oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS (ladspa));
  gboolean was_activated;
  
  desc = ladspa->descriptor;
  
  /* check for old handle */
  was_activated = ladspa->activated;
  if (ladspa->handle != NULL){
    gst_ladspa_deactivate(ladspa);
    desc->cleanup(ladspa->handle);
  }
        
  /* instantiate the plugin */ 
  DEBUG_OBJ (ladspa, "instantiating the plugin at %d Hz", ladspa->samplerate);
  
  ladspa->handle = desc->instantiate(desc,ladspa->samplerate);
  g_return_val_if_fail (ladspa->handle != NULL, FALSE);

  /* connect the control ports */
  for (i=0;i<oclass->numcontrols;i++)
    desc->connect_port(ladspa->handle,
                       oclass->control_portnums[i],
                       &(ladspa->controls[i]));

  /* reactivate if it was activated before the reinstantiation */
  if (was_activated)
    gst_ladspa_activate(ladspa);

  return TRUE;
}

static GstElementStateReturn
gst_ladspa_change_state (GstElement *element)
{
  LADSPA_Descriptor *desc;
  GstLADSPA *ladspa = (GstLADSPA*)element;
  desc = ladspa->descriptor;

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      gst_ladspa_activate(ladspa);
      break;
    case GST_STATE_READY_TO_NULL:
      gst_ladspa_deactivate(ladspa);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_ladspa_activate (GstLADSPA *ladspa)
{
  LADSPA_Descriptor *desc;
  desc = ladspa->descriptor;
  
  if (ladspa->activated)
    gst_ladspa_deactivate(ladspa);
  
  DEBUG_OBJ (ladspa, "activating");

  /* activate the plugin (function might be null) */
  if (desc->activate != NULL)
    desc->activate(ladspa->handle);

  ladspa->activated = TRUE;
}

static void
gst_ladspa_deactivate (GstLADSPA *ladspa)
{
  LADSPA_Descriptor *desc;
  desc = ladspa->descriptor;

  DEBUG_OBJ (ladspa, "deactivating");

  /* deactivate the plugin (function might be null) */
  if (ladspa->activated && (desc->deactivate != NULL))
    desc->deactivate(ladspa->handle);

  ladspa->activated = FALSE;
}

static void
gst_ladspa_loop (GstElement *element)
{
  guint        i, j, numsrcpads, numsinkpads;
  guint        num_processed, num_to_process;
  gint         largest_buffer;
  LADSPA_Data  **data_in, **data_out;
  GstBuffer    **buffers_in, **buffers_out;
 
  GstLADSPA       *ladspa = (GstLADSPA *)element;
  GstLADSPAClass  *oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS (ladspa));
  LADSPA_Descriptor *desc = ladspa->descriptor;

  numsinkpads = oclass->numsinkpads;
  numsrcpads = oclass->numsrcpads;
  
  /* fixme: these mallocs need to die */
  data_in = g_new0(LADSPA_Data*, numsinkpads);
  data_out = g_new0(LADSPA_Data*, numsrcpads);
  buffers_in = g_new0(GstBuffer*, numsinkpads);
  buffers_out = g_new0(GstBuffer*, numsrcpads);
  
  largest_buffer = -1;

  /* first get all the necessary data from the input ports */
  for (i=0 ; i<numsinkpads ; i++){  
  get_buffer:
    buffers_in[i] = GST_BUFFER (gst_pad_pull (ladspa->sinkpads[i]));
    
    if (GST_IS_EVENT (buffers_in[i])) {
      /* push it out on all pads */
      gst_data_ref_by_count ((GstData*)buffers_in[i], numsrcpads);
      for (j=0; j<numsrcpads; j++)
        gst_pad_push (ladspa->srcpads[j], GST_DATA (buffers_in[i]));
      if (GST_EVENT_TYPE (buffers_in[i]) == GST_EVENT_EOS) {
        /* shut down */
        gst_element_set_eos (element);
        return;
      } else {
        goto get_buffer;
      }
    }

    if (largest_buffer < 0)
      largest_buffer = GST_BUFFER_SIZE (buffers_in[i])/sizeof(gfloat);
    else
      largest_buffer = MIN (GST_BUFFER_SIZE (buffers_in[i])/sizeof(gfloat), largest_buffer);
    data_in[i] = (LADSPA_Data *) GST_BUFFER_DATA(buffers_in[i]);
    GST_BUFFER_TIMESTAMP(buffers_in[i]) = ladspa->timestamp;
  }

  if (!ladspa->bufpool) {
    gst_element_error (element, "Caps were never set, bailing...");
    return;
  }

  i=0;
  if (!ladspa->inplace_broken) {
    for (; i<numsrcpads && i<numsinkpads; i++) {
      /* reuse input buffers */
      buffers_out[i] = buffers_in[i];
      data_out[i] = data_in[i];
    }
  }
  for (; i<numsrcpads; i++) {
    /* we have to make new buffers -- at least we're taking them from a pool */
    buffers_out[i] = gst_buffer_new_from_pool (ladspa->bufpool, 0, 0);
    GST_BUFFER_TIMESTAMP (buffers_out[i]) = ladspa->timestamp;
    data_out[i] = (LADSPA_Data*)GST_BUFFER_DATA (buffers_out[i]);
  }
  
  GST_DPMAN_PREPROCESS(ladspa->dpman, largest_buffer, ladspa->timestamp);
  num_processed = 0;

  /* split up processing of the buffer into chunks so that dparams can
   * be updated when required.
   * In many cases the buffer will be processed in one chunk anyway.
   */
  while (GST_DPMAN_PROCESS (ladspa->dpman, num_processed)) {
    num_to_process = GST_DPMAN_FRAMES_TO_PROCESS(ladspa->dpman);

    for (i=0 ; i<numsinkpads ; i++)
      desc->connect_port (ladspa->handle, oclass->sinkpad_portnums[i], data_in[i]);
    for (i=0 ; i<numsrcpads ; i++)
      desc->connect_port (ladspa->handle, oclass->srcpad_portnums[i], data_out[i]);

    desc->run(ladspa->handle, num_to_process);

    for (i=0 ; i<numsinkpads ; i++)
      data_in[i] += num_to_process;
    for (i=0 ; i<numsrcpads ; i++)
      data_out[i] += num_to_process;
    
    num_processed += num_to_process;
  }
    
  for (i=0 ; i<numsinkpads ; i++) {
    if (i >= numsrcpads || buffers_out[i] != buffers_in[i])
      gst_buffer_unref(buffers_in[i]);
    data_in[i] = NULL;
    buffers_in[i] = NULL;
  }      
  for (i=0 ; i<numsrcpads ; i++) {
    DEBUG_OBJ (ladspa, "pushing buffer (%p) on src pad %d", buffers_out[i], i);
    gst_pad_push (ladspa->srcpads[i], GST_DATA (buffers_out[i]));
    
    data_out[i] = NULL;
    buffers_out[i] = NULL;
  }
  
  ladspa->timestamp += ladspa->buffer_frames * GST_SECOND / ladspa->samplerate;

  /* FIXME: move these mallocs and frees to the state-change handler */

  g_free (buffers_out);
  g_free (buffers_in);
  g_free (data_out);
  g_free (data_in);
}

static void
gst_ladspa_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buffer_in = GST_BUFFER (_data);
  LADSPA_Descriptor *desc;
  LADSPA_Data *data_in, **data_out = NULL;
  GstBuffer **buffers_out = NULL;
  unsigned long num_samples;
  guint num_to_process, num_processed, i, numsrcpads;
  GstLADSPA *ladspa;
  GstLADSPAClass *oclass;

  ladspa = (GstLADSPA*)GST_OBJECT_PARENT (pad);
  oclass = (GstLADSPAClass *) (G_OBJECT_GET_CLASS (ladspa));
  data_in = (LADSPA_Data *) GST_BUFFER_DATA(buffer_in);
  num_samples = GST_BUFFER_SIZE(buffer_in) / sizeof(gfloat);
  numsrcpads = oclass->numsrcpads;
  desc = ladspa->descriptor;

  /* we shouldn't get events here... */
  g_return_if_fail (GST_IS_BUFFER (buffer_in));
  
  if (!ladspa->bufpool) {
    gst_element_error ((GstElement*)ladspa, "Caps were never set, bailing...");
    return;
  }

  /* FIXME: this function shouldn't need to malloc() anything */
  if (numsrcpads > 0) {
    buffers_out = g_new(GstBuffer*, numsrcpads);
    data_out = g_new(LADSPA_Data*, numsrcpads);
  }

  i=0;
  if (!ladspa->inplace_broken && numsrcpads) {
    /* reuse the first (chained) buffer */
    buffers_out[i] = buffer_in;
    DEBUG ("reuse: %d", GST_BUFFER_SIZE (buffer_in));
    data_out[i] = data_in;
    i++;
  }
  for (; i<numsrcpads; i++) {
    /* we have to make new buffers -- at least we're taking them from a pool */
    buffers_out[i] = gst_buffer_new_from_pool (ladspa->bufpool, 0, 0);
    /* the size of the buffer returned from the pool is the maximum size; this
       chained buffer might be smaller */
    GST_BUFFER_SIZE (buffers_out[i]) = GST_BUFFER_SIZE (buffer_in);
    DEBUG ("new %d", GST_BUFFER_SIZE (buffer_in));
    GST_BUFFER_TIMESTAMP (buffers_out[i]) = ladspa->timestamp;
    data_out[i] = (LADSPA_Data*)GST_BUFFER_DATA (buffers_out[i]);
  }

  GST_DPMAN_PREPROCESS(ladspa->dpman, num_samples, GST_BUFFER_TIMESTAMP(buffer_in));
  num_processed = 0;

  /* split up processing of the buffer into chunks so that dparams can
   * be updated when required.
   * In many cases the buffer will be processed in one chunk anyway.
   */
  while(GST_DPMAN_PROCESS(ladspa->dpman, num_processed)) {
    num_to_process = GST_DPMAN_FRAMES_TO_PROCESS(ladspa->dpman);

    desc->connect_port(ladspa->handle,oclass->sinkpad_portnums[0],data_in);  
    for (i=0 ; i<numsrcpads ; i++)
      desc->connect_port(ladspa->handle,oclass->srcpad_portnums[i],data_out[i]);

    desc->run(ladspa->handle, num_to_process);
    
    data_in += num_to_process;
    for (i=0 ; i<numsrcpads ; i++)
      data_out[i] += num_to_process;

    num_processed += num_to_process;
  }

  if (!numsrcpads || buffers_out[0] != buffer_in)
    gst_buffer_unref(buffer_in);

  if (numsrcpads) {
    for (i=0; i<numsrcpads; i++) {
      DEBUG_OBJ (ladspa, "pushing buffer (%p, length %u bytes) on src pad %d",
                 buffers_out[i], GST_BUFFER_SIZE (buffers_out[i]), i);
      gst_pad_push (ladspa->srcpads[i], GST_DATA (buffers_out[i]));
    }

    g_free(buffers_out);
    g_free(data_out);
  }
}

static GstData *
gst_ladspa_get(GstPad *pad)
{  
  GstLADSPA *ladspa;
  GstLADSPAClass *oclass;
  GstBuffer *buf;
  LADSPA_Data *data;
  LADSPA_Descriptor *desc;
  guint num_to_process, num_processed;

  ladspa = (GstLADSPA *)gst_pad_get_parent (pad);
  oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS(ladspa));
  desc = ladspa->descriptor;

  if (!ladspa->bufpool) {
    /* capsnego hasn't happened... */
    gst_ladspa_force_src_caps(ladspa, ladspa->srcpads[0]);
  }

  buf = gst_buffer_new_from_pool (ladspa->bufpool, 0, 0);
  GST_BUFFER_TIMESTAMP(buf) = ladspa->timestamp;
  data = (LADSPA_Data *) GST_BUFFER_DATA(buf);  

  GST_DPMAN_PREPROCESS(ladspa->dpman, ladspa->buffer_frames, ladspa->timestamp);
  num_processed = 0;

  /* split up processing of the buffer into chunks so that dparams can
   * be updated when required.
   * In many cases the buffer will be processed in one chunk anyway.
   */
  while(GST_DPMAN_PROCESS(ladspa->dpman, num_processed)) {
    num_to_process = GST_DPMAN_FRAMES_TO_PROCESS(ladspa->dpman);

    /* update timestamp */  
    ladspa->timestamp += num_to_process * GST_SECOND / ladspa->samplerate;

    desc->connect_port(ladspa->handle,oclass->srcpad_portnums[0],data);  

    desc->run(ladspa->handle, num_to_process);
    
    data += num_to_process;
    num_processed = num_to_process;
  }
  
  return GST_DATA (buf);
}

static void
ladspa_describe_plugin(const char *pcFullFilename,
                       void *pvPluginHandle,
                       LADSPA_Descriptor_Function pfDescriptorFunction)
{
  const LADSPA_Descriptor *desc;
  int i,j;
  
  GstElementDetails *details;
  GTypeInfo typeinfo = {
      sizeof(GstLADSPAClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_ladspa_class_init,
      NULL,
      NULL,
      sizeof(GstLADSPA),
      0,
      (GInstanceInitFunc)gst_ladspa_init,
  };
  GType type;
  GstElementFactory *factory;

  /* walk through all the plugins in this pluginlibrary */
  i = 0;
  while ((desc = pfDescriptorFunction(i++))) {
    gchar *type_name;

    /* construct the type */
    type_name = g_strdup_printf("ladspa-%s",desc->Label);
    g_strcanon (type_name, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-+", '-');
    /* if it's already registered, drop it */
    if (g_type_from_name(type_name)) {
      g_free(type_name);
      continue;
    }
    /* create the type now */
    type = g_type_register_static(GST_TYPE_ELEMENT, type_name, &typeinfo, 0);

    /* construct the element details struct */
    details = g_new0(GstElementDetails,1);
    details->longname = g_strdup(desc->Name);
    details->klass = "Filter/Audio/LADSPA";
    details->license = g_strdup (desc->Copyright);
    details->description = details->longname;
    details->version = g_strdup_printf("%ld",desc->UniqueID);
    details->author = g_strdup(desc->Maker);
    details->copyright = g_strdup(desc->Copyright);

    /* register the plugin with gstreamer */
    factory = gst_element_factory_new(type_name,type,details);
    g_return_if_fail(factory != NULL);
    gst_plugin_add_feature (ladspa_plugin, GST_PLUGIN_FEATURE (factory));

    /* add this plugin to the hash */
    g_hash_table_insert(ladspa_descriptors,
                        GINT_TO_POINTER(type),
                        (gpointer)desc);
    

    for (j=0;j<desc->PortCount;j++) {
      if (LADSPA_IS_PORT_AUDIO(desc->PortDescriptors[j])) {
        gchar *name = g_strdup((gchar *)desc->PortNames[j]);
        g_strcanon (name, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-", '-');
        /* the factories take ownership of the name */
        if (LADSPA_IS_PORT_INPUT(desc->PortDescriptors[j]))
          gst_element_factory_add_pad_template (factory, ladspa_sink_factory (name));
        else
          gst_element_factory_add_pad_template (factory, ladspa_src_factory (name));
      }
    }
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GST_DEBUG_CATEGORY_INIT (ladspa_debug, "ladspa",
                           GST_DEBUG_FG_GREEN | GST_DEBUG_BG_BLACK | GST_DEBUG_BOLD,
                           "LADSPA");

  ladspa_descriptors = g_hash_table_new(NULL,NULL);
  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  ladspa_plugin = plugin;

  LADSPAPluginSearch(ladspa_describe_plugin);

  /* initialize dparam support library */
  gst_control_init(NULL,NULL);
  
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "ladspa",
  plugin_init
};
