/* 
 * RTP Demux element
 *
 * Copyright (C) 2005 Nokia Corporation.
 * @author Kai Vehmanen <kai.vehmanen@nokia.com>
 *
 * Loosely based on GStreamer gstdecodebin
 * Copyright (C) <2004> Wim Taymans <wim@fluendo.com>
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
 * Contributors:
 * Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>
 */

/*
 * Status:
 *  - works with the test_rtpdemux.c tool
 *
 * Check:
 *  - is emitting a signal enough, or should we
 *    use GstEvent to notify downstream elements
 *    of the new packet... no?
 *
 * Notes:
 *  - emits event both for new PTs, and whenever
 *    a PT is changed
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpbin-marshal.h"
#include "gstrtpptdemux.h"

/* generic templates */
static GstStaticPadTemplate rtp_pt_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtp_pt_demux_src_template =
GST_STATIC_PAD_TEMPLATE ("src_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp, " "payload = (int) [ 0, 255 ]")
    );

GST_DEBUG_CATEGORY_STATIC (gst_rtp_pt_demux_debug);
#define GST_CAT_DEFAULT gst_rtp_pt_demux_debug

/**
 * Item for storing GstPad<->pt pairs.
 */
struct _GstRTPPtDemuxPad
{
  GstPad *pad;        /**< pointer to the actual pad */
  gint pt;             /**< RTP payload-type attached to pad */
};

/* signals */
enum
{
  SIGNAL_NEW_PAYLOAD_TYPE,
  SIGNAL_PAYLOAD_TYPE_CHANGE,
  LAST_SIGNAL
};

GST_BOILERPLATE (GstRTPPtDemux, gst_rtp_pt_demux, GstElement, GST_TYPE_ELEMENT);

static void gst_rtp_pt_demux_finalize (GObject * object);

static void gst_rtp_pt_demux_release (GstElement * element);
static gboolean gst_rtp_pt_demux_setup (GstElement * element);

static GstFlowReturn gst_rtp_pt_demux_chain (GstPad * pad, GstBuffer * buf);
static GstCaps *gst_rtp_pt_demux_getcaps (GstPad * pad);
static GstStateChangeReturn gst_rtp_pt_demux_change_state (GstElement * element,
    GstStateChange transition);

static GstPad *find_pad_for_pt (GstRTPPtDemux * rtpdemux, guint8 pt);

static guint gst_rtp_pt_demux_signals[LAST_SIGNAL] = { 0 };

static GstElementDetails gst_rtp_pt_demux_details = {
  "RTP Demux",
  /* XXX: what's the correct hierarchy? */
  "Codec/Demux/Network",
  "Parses codec streams transmitted in the same RTP session",
  "Kai Vehmanen <kai.vehmanen@nokia.com>"
};

static void
gst_rtp_pt_demux_base_init (gpointer g_class)
{
  GstElementClass *gstelement_klass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&rtp_pt_demux_sink_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&rtp_pt_demux_src_template));

  gst_element_class_set_details (gstelement_klass, &gst_rtp_pt_demux_details);
}

static void
gst_rtp_pt_demux_class_init (GstRTPPtDemuxClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;

  gst_rtp_pt_demux_signals[SIGNAL_NEW_PAYLOAD_TYPE] =
      g_signal_new ("new-payload-type",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTPPtDemuxClass, new_payload_type),
      NULL, NULL,
      gst_rtp_bin_marshal_VOID__UINT_OBJECT,
      G_TYPE_NONE, 2, G_TYPE_INT, GST_TYPE_PAD);
  gst_rtp_pt_demux_signals[SIGNAL_PAYLOAD_TYPE_CHANGE] =
      g_signal_new ("payload-type-change",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTPPtDemuxClass, payload_type_change),
      NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  gobject_klass->finalize = GST_DEBUG_FUNCPTR (gst_rtp_pt_demux_finalize);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_pt_demux_change_state);

  GST_DEBUG_CATEGORY_INIT (gst_rtp_pt_demux_debug,
      "rtpptdemux", 0, "RTP codec demuxer");

}

static void
gst_rtp_pt_demux_init (GstRTPPtDemux * ptdemux, GstRTPPtDemuxClass * g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (ptdemux);

  ptdemux->sink =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  g_assert (ptdemux->sink != NULL);

  gst_pad_set_chain_function (ptdemux->sink, gst_rtp_pt_demux_chain);

  gst_element_add_pad (GST_ELEMENT (ptdemux), ptdemux->sink);
}

static void
gst_rtp_pt_demux_finalize (GObject * object)
{
  gst_rtp_pt_demux_release (GST_ELEMENT (object));

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn
gst_rtp_pt_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstRTPPtDemux *rtpdemux;
  GstElement *element = GST_ELEMENT (GST_OBJECT_PARENT (pad));
  guint8 pt;
  GstPad *srcpad;

  rtpdemux = GST_RTP_PT_DEMUX (GST_OBJECT_PARENT (pad));

  if (!gst_rtp_buffer_validate (buf))
    goto invalid_buffer;

  pt = gst_rtp_buffer_get_payload_type (buf);

  GST_DEBUG_OBJECT (rtpdemux, "received buffer for pt %d", pt);

  srcpad = find_pad_for_pt (rtpdemux, pt);
  if (srcpad == NULL) {
    /* new PT, create a src pad */
    GstElementClass *klass;
    GstPadTemplate *templ;
    gchar *padname;
    GstCaps *caps;
    GstRTPPtDemuxPad *rtpdemuxpad;

    klass = GST_ELEMENT_GET_CLASS (rtpdemux);
    templ = gst_element_class_get_pad_template (klass, "src_%d");
    padname = g_strdup_printf ("src_%d", pt);
    srcpad = gst_pad_new_from_template (templ, padname);
    g_free (padname);

    caps = gst_pad_get_caps (srcpad);
    caps = gst_caps_make_writable (caps);
    gst_caps_set_simple (caps, "payload", G_TYPE_INT, pt, NULL);
    gst_pad_set_caps (srcpad, caps);

    /* XXX: set _link () function */
    gst_pad_set_getcaps_function (srcpad, gst_rtp_pt_demux_getcaps);
    gst_pad_set_active (srcpad, TRUE);
    gst_element_add_pad (element, srcpad);

    GST_DEBUG ("Adding pt=%d to the list.", pt);
    rtpdemuxpad = g_new0 (GstRTPPtDemuxPad, 1);
    rtpdemuxpad->pt = pt;
    rtpdemuxpad->pad = srcpad;
    rtpdemux->srcpads = g_slist_append (rtpdemux->srcpads, rtpdemuxpad);

    GST_DEBUG ("emitting new-payload_type for pt %d", pt);
    g_signal_emit (G_OBJECT (rtpdemux),
        gst_rtp_pt_demux_signals[SIGNAL_NEW_PAYLOAD_TYPE], 0, pt, srcpad);
  }

  if (pt != rtpdemux->last_pt) {
    gint emit_pt = pt;

    /* our own signal with an extra flag that this is the only pad */
    rtpdemux->last_pt = pt;
    GST_DEBUG ("emitting payload-type-changed for pt %d", emit_pt);
    g_signal_emit (G_OBJECT (rtpdemux),
        gst_rtp_pt_demux_signals[SIGNAL_PAYLOAD_TYPE_CHANGE], 0, emit_pt);
  }

  /* push to srcpad */
  if (srcpad)
    ret = gst_pad_push (srcpad, GST_BUFFER (buf));

  return ret;

  /* ERRORS */
invalid_buffer:
  {
    /* this is fatal and should be filtered earlier */
    GST_ELEMENT_ERROR (rtpdemux, STREAM, DECODE, (NULL),
        ("Dropping invalid RTP payload"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static GstCaps *
gst_rtp_pt_demux_getcaps (GstPad * pad)
{
  GstCaps *caps;

  GST_OBJECT_LOCK (pad);
  if ((caps = GST_PAD_CAPS (pad)))
    caps = gst_caps_ref (caps);
  GST_OBJECT_UNLOCK (pad);

  return caps;
}

static GstPad *
find_pad_for_pt (GstRTPPtDemux * rtpdemux, guint8 pt)
{
  GstPad *respad = NULL;
  GSList *item = rtpdemux->srcpads;

  for (; item; item = g_slist_next (item)) {
    GstRTPPtDemuxPad *pad = item->data;

    if (pad->pt == pt) {
      respad = pad->pad;
      break;
    }
  }

  return respad;
}

/**
 * Reserves resources for the object.
 */
static gboolean
gst_rtp_pt_demux_setup (GstElement * element)
{
  GstRTPPtDemux *ptdemux = GST_RTP_PT_DEMUX (element);
  gboolean res = TRUE;

  if (ptdemux) {
    ptdemux->srcpads = NULL;
    ptdemux->last_pt = 0xFFFF;
  }

  return res;
}

/**
 * Free resources for the object.
 */
static void
gst_rtp_pt_demux_release (GstElement * element)
{
  GstRTPPtDemux *ptdemux = GST_RTP_PT_DEMUX (element);

  if (ptdemux) {
    /* note: GstElement's dispose() will handle the pads */
    g_slist_free (ptdemux->srcpads);
    ptdemux->srcpads = NULL;
  }
}

static GstStateChangeReturn
gst_rtp_pt_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTPPtDemux *ptdemux;

  ptdemux = GST_RTP_PT_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (gst_rtp_pt_demux_setup (element) != TRUE)
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_rtp_pt_demux_release (element);
      break;
    default:
      break;
  }

  return ret;
}
