/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim@fluendo.com>
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

/**
 * SECTION:element-gstrtpbin
 * @short_description: handle media from one RTP bin
 * @see_also: gstrtpjitterbuffer, gstrtpsession, gstrtpptdemux, gstrtpssrcdemux
 *
 * <refsect2>
 * <para>
 * RTP bin combines the functions of gstrtpsession, gstrtpssrcdemux, gstrtpjitterbuffer
 * and gstrtpptdemux in one element. It allows for multiple RTP sessions that will
 * be synchronized together using RTCP SR packets.
 * </para>
 * <para>
 * gstrtpbin is configured with a number of request pads that define the
 * functionality that is activated, similar to the gstrtpsession element.
 * </para>
 * <para>
 * To use gstrtpbin as an RTP receiver, request a recv_rtp_sink_%%d pad. The session
 * number must be specified in the pad name. 
 * Data received on the recv_rtp_sink_%%d pad will be processed in the gstrtpsession
 * manager and after being validated forwarded on gstrtpssrcdemuxer element. Each
 * RTP stream is demuxed based on the SSRC and send to a gstrtpjitterbuffer. After
 * the packets are released from the jitterbuffer, they will be forwarded to a
 * gstrtpptdemuxer element. The gstrtpptdemuxer element will demux the packets based
 * on the payload type and will create a unique pad recv_rtp_src_%%d_%%d_%%d on
 * gstrtpbin with the session number, SSRC and payload type respectively as the pad
 * name.
 * </para>
 * <para>
 * To also use gstrtpbin as an RTCP receiver, request a recv_rtcp_sink_%%d pad. The
 * session number must be specified in the pad name.
 * </para>
 * <para>
 * If you want the session manager to generate and send RTCP packets, request
 * the send_rtcp_src_%%d pad with the session number in the pad name. Packet pushed
 * on this pad contain SR/RR RTCP reports that should be sent to all participants
 * in the session.
 * </para>
 * <para>
 * To use gstrtpbin as a sender, request a send_rtp_sink_%%d pad, which will
 * automatically create a send_rtp_src_%%d pad. The session number must be specified when
 * requesting the sink pad. The session manager will modify the
 * SSRC in the RTP packets to its own SSRC and wil forward the packets on the
 * send_rtp_src_%%d pad after updating its internal state.
 * </para>
 * <para>
 * The session manager needs the clock-rate of the payload types it is handling
 * and will signal the GstRtpSession::request-pt-map signal when it needs such a
 * mapping. One can clear the cached values with the GstRtpSession::clear-pt-map
 * signal.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch udpsrc port=5000 caps="application/x-rtp, ..." ! .recv_rtp_sink_0 \
 *     gstrtpbin ! rtptheoradepay ! theoradec ! xvimagesink
 * </programlisting>
 * Receive RTP data from port 5000 and send to the session 0 in gstrtpbin.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch gstrtpbin name=rtpbin \
 *         v4l2src ! ffmpegcolorspace ! ffenc_h263 ! rtph263ppay ! rtpbin.send_rtp_sink_0 \
 *                   rtpbin.send_rtp_src_0 ! udpsink port=5000                            \
 *                   rtpbin.send_rtcp_src_0 ! udpsink port=5001 sync=false async=false    \
 *                   udpsrc port=5005 ! rtpbin.recv_rtcp_sink_0                           \
 *         audiotestsrc ! amrnbenc ! rtpamrpay ! rtpbin.send_rtp_sink_1                   \
 *                   rtpbin.send_rtp_src_1 ! udpsink port=5002                            \
 *                   rtpbin.send_rtcp_src_1 ! udpsink port=5003 sync=false async=false    \
 *                   udpsrc port=5007 ! rtpbin.recv_rtcp_sink_1
 * </programlisting>
 * Encode and payload H263 video captured from a v4l2src. Encode and payload AMR
 * audio generated from audiotestsrc. The video is sent to session 0 in rtpbin
 * and the audio is sent to session 1. Video packets are sent on UDP port 5000
 * and audio packets on port 5002. The video RTCP packets for session 0 are sent
 * on port 5001 and the audio RTCP packets for session 0 are sent on port 5003.
 * RTCP packets for session 0 are received on port 5005 and RTCP for session 1
 * is received on port 5007. Since RTCP packets from the sender should be sent
 * as soon as possible and do not participate in preroll, sync=false and 
 * async=false is configured on udpsink
 * </para>
 * <para>
 * <programlisting>
 *  gst-launch -v gstrtpbin name=rtpbin                                          \
 *     udpsrc caps="application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H263-1998" \
 *             port=5000 ! rtpbin.recv_rtp_sink_0                                \
 *         rtpbin. ! rtph263pdepay ! ffdec_h263 ! xvimagesink                    \
 *      udpsrc port=5001 ! rtpbin.recv_rtcp_sink_0                               \
 *      rtpbin.send_rtcp_src_0 ! udpsink port=5005 sync=false async=false        \
 *     udpsrc caps="application/x-rtp,media=(string)audio,clock-rate=(int)8000,encoding-name=(string)AMR,encoding-params=(string)1,octet-align=(string)1" \
 *             port=5002 ! rtpbin.recv_rtp_sink_1                                \
 *         rtpbin. ! rtpamrdepay ! amrnbdec ! alsasink                           \
 *      udpsrc port=5003 ! rtpbin.recv_rtcp_sink_1                               \
 *      rtpbin.send_rtcp_src_1 ! udpsink port=5007 sync=false async=false
 * </programlisting>
 * Receive H263 on port 5000, send it through rtpbin in session 0, depayload,
 * decode and display the video.
 * Receive AMR on port 5002, send it through rtpbin in session 1, depayload,
 * decode and play the audio.
 * Receive server RTCP packets for session 0 on port 5001 and RTCP packets for
 * session 1 on port 5003. These packets will be used for session management and
 * synchronisation.
 * Send RTCP reports for session 0 on port 5005 and RTCP reports for session 1
 * on port 5007.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2007-08-30 (0.10.6)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>

#include "gstrtpbin-marshal.h"
#include "gstrtpbin.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_bin_debug);
#define GST_CAT_DEFAULT gst_rtp_bin_debug


/* elementfactory information */
static const GstElementDetails rtpbin_details = GST_ELEMENT_DETAILS ("RTP Bin",
    "Filter/Network/RTP",
    "Implement an RTP bin",
    "Wim Taymans <wim@fluendo.com>");

/* sink pads */
static GstStaticPadTemplate rtpbin_recv_rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtp_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtpbin_recv_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtcp_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate rtpbin_send_rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("send_rtp_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

/* src pads */
static GstStaticPadTemplate rtpbin_recv_rtp_src_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtp_src_%d_%d_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtpbin_send_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("send_rtcp_src_%d",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate rtpbin_send_rtp_src_template =
GST_STATIC_PAD_TEMPLATE ("send_rtp_src_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp")
    );

/* padtemplate for the internal pad */
static GstStaticPadTemplate rtpbin_sync_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

#define GST_RTP_BIN_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTP_BIN, GstRtpBinPrivate))

#define GST_RTP_BIN_LOCK(bin)   g_mutex_lock ((bin)->priv->bin_lock)
#define GST_RTP_BIN_UNLOCK(bin) g_mutex_unlock ((bin)->priv->bin_lock)

struct _GstRtpBinPrivate
{
  GMutex *bin_lock;

  GstClockTime ntp_ns_base;
};

/* signals and args */
enum
{
  SIGNAL_REQUEST_PT_MAP,
  SIGNAL_CLEAR_PT_MAP,

  SIGNAL_ON_NEW_SSRC,
  SIGNAL_ON_SSRC_COLLISION,
  SIGNAL_ON_SSRC_VALIDATED,
  SIGNAL_ON_BYE_SSRC,
  SIGNAL_ON_BYE_TIMEOUT,
  SIGNAL_ON_TIMEOUT,
  LAST_SIGNAL
};

#define DEFAULT_LATENCY_MS	200

enum
{
  PROP_0,
  PROP_LATENCY
};

/* helper objects */
typedef struct _GstRtpBinSession GstRtpBinSession;
typedef struct _GstRtpBinStream GstRtpBinStream;
typedef struct _GstRtpBinClient GstRtpBinClient;

static guint gst_rtp_bin_signals[LAST_SIGNAL] = { 0 };

static GstCaps *pt_map_requested (GstElement * element, guint pt,
    GstRtpBinSession * session);

static void free_stream (GstRtpBinStream * stream);

/* Manages the RTP stream for one SSRC.
 *
 * We pipe the stream (comming from the SSRC demuxer) into a jitterbuffer.
 * If we see an SDES RTCP packet that links multiple SSRCs together based on a
 * common CNAME, we create a GstRtpBinClient structure to group the SSRCs
 * together (see below).
 */
struct _GstRtpBinStream
{
  /* the SSRC of this stream */
  guint32 ssrc;

  /* parent bin */
  GstRtpBin *bin;

  /* the session this SSRC belongs to */
  GstRtpBinSession *session;

  /* the jitterbuffer of the SSRC */
  GstElement *buffer;

  /* the PT demuxer of the SSRC */
  GstElement *demux;
  gulong demux_newpad_sig;
  gulong demux_ptreq_sig;

  /* the internal pad we use to get RTCP sync messages */
  GstPad *sync_pad;
  gboolean have_sync;
  guint64 last_unix;
  guint64 last_extrtptime;

  /* mapping to local RTP and NTP time */
  guint64 local_rtp;
  guint64 local_unix;
  gint64 unix_delta;

  /* for lip-sync */
  guint64 clock_base;
  gint clock_rate;
  gint64 ts_offset;
  gint64 prev_ts_offset;
};

#define GST_RTP_SESSION_LOCK(sess)   g_mutex_lock ((sess)->lock)
#define GST_RTP_SESSION_UNLOCK(sess) g_mutex_unlock ((sess)->lock)

/* Manages the receiving end of the packets.
 *
 * There is one such structure for each RTP session (audio/video/...).
 * We get the RTP/RTCP packets and stuff them into the session manager. From
 * there they are pushed into an SSRC demuxer that splits the stream based on
 * SSRC. Each of the SSRC streams go into their own jitterbuffer (managed with
 * the GstRtpBinStream above).
 */
struct _GstRtpBinSession
{
  /* session id */
  gint id;
  /* the parent bin */
  GstRtpBin *bin;
  /* the session element */
  GstElement *session;
  /* the SSRC demuxer */
  GstElement *demux;
  gulong demux_newpad_sig;

  GMutex *lock;

  /* list of GstRtpBinStream */
  GSList *streams;

  /* mapping of payload type to caps */
  GHashTable *ptmap;

  /* the pads of the session */
  GstPad *recv_rtp_sink;
  GstPad *recv_rtp_src;
  GstPad *recv_rtcp_sink;
  GstPad *sync_src;
  GstPad *send_rtp_sink;
  GstPad *send_rtp_src;
  GstPad *send_rtcp_src;
};

/* Manages the RTP streams that come from one client and should therefore be
 * synchronized.
 */
struct _GstRtpBinClient
{
  /* the common CNAME for the streams */
  gchar *cname;
  guint cname_len;

  /* the streams */
  guint nstreams;
  GSList *streams;

  gint64 min_delta;
};

/* find a session with the given id. Must be called with RTP_BIN_LOCK */
static GstRtpBinSession *
find_session_by_id (GstRtpBin * rtpbin, gint id)
{
  GSList *walk;

  for (walk = rtpbin->sessions; walk; walk = g_slist_next (walk)) {
    GstRtpBinSession *sess = (GstRtpBinSession *) walk->data;

    if (sess->id == id)
      return sess;
  }
  return NULL;
}

static void
on_new_ssrc (GstElement * session, guint32 ssrc, GstRtpBinSession * sess)
{
  g_signal_emit (sess->bin, gst_rtp_bin_signals[SIGNAL_ON_NEW_SSRC], 0,
      sess->id, ssrc);
}

static void
on_ssrc_collision (GstElement * session, guint32 ssrc, GstRtpBinSession * sess)
{
  g_signal_emit (sess->bin, gst_rtp_bin_signals[SIGNAL_ON_SSRC_COLLISION], 0,
      sess->id, ssrc);
}

static void
on_ssrc_validated (GstElement * session, guint32 ssrc, GstRtpBinSession * sess)
{
  g_signal_emit (sess->bin, gst_rtp_bin_signals[SIGNAL_ON_SSRC_VALIDATED], 0,
      sess->id, ssrc);
}

static void
on_bye_ssrc (GstElement * session, guint32 ssrc, GstRtpBinSession * sess)
{
  g_signal_emit (sess->bin, gst_rtp_bin_signals[SIGNAL_ON_BYE_SSRC], 0,
      sess->id, ssrc);
}

static void
on_bye_timeout (GstElement * session, guint32 ssrc, GstRtpBinSession * sess)
{
  g_signal_emit (sess->bin, gst_rtp_bin_signals[SIGNAL_ON_BYE_TIMEOUT], 0,
      sess->id, ssrc);
}

static void
on_timeout (GstElement * session, guint32 ssrc, GstRtpBinSession * sess)
{
  g_signal_emit (sess->bin, gst_rtp_bin_signals[SIGNAL_ON_TIMEOUT], 0,
      sess->id, ssrc);
}

/* create a session with the given id.  Must be called with RTP_BIN_LOCK */
static GstRtpBinSession *
create_session (GstRtpBin * rtpbin, gint id)
{
  GstRtpBinSession *sess;
  GstElement *session, *demux;

  if (!(session = gst_element_factory_make ("gstrtpsession", NULL)))
    goto no_session;

  if (!(demux = gst_element_factory_make ("gstrtpssrcdemux", NULL)))
    goto no_demux;

  sess = g_new0 (GstRtpBinSession, 1);
  sess->lock = g_mutex_new ();
  sess->id = id;
  sess->bin = rtpbin;
  sess->session = session;
  sess->demux = demux;
  sess->ptmap = g_hash_table_new (NULL, NULL);
  rtpbin->sessions = g_slist_prepend (rtpbin->sessions, sess);

  /* provide clock_rate to the session manager when needed */
  g_signal_connect (session, "request-pt-map",
      (GCallback) pt_map_requested, sess);

  g_signal_connect (sess->session, "on-new-ssrc",
      (GCallback) on_new_ssrc, sess);
  g_signal_connect (sess->session, "on-ssrc-collision",
      (GCallback) on_ssrc_collision, sess);
  g_signal_connect (sess->session, "on-ssrc-validated",
      (GCallback) on_ssrc_validated, sess);
  g_signal_connect (sess->session, "on-bye-ssrc",
      (GCallback) on_bye_ssrc, sess);
  g_signal_connect (sess->session, "on-bye-timeout",
      (GCallback) on_bye_timeout, sess);
  g_signal_connect (sess->session, "on-timeout", (GCallback) on_timeout, sess);

  gst_bin_add (GST_BIN_CAST (rtpbin), session);
  gst_element_set_state (session, GST_STATE_PLAYING);
  gst_bin_add (GST_BIN_CAST (rtpbin), demux);
  gst_element_set_state (demux, GST_STATE_PLAYING);

  return sess;

  /* ERRORS */
no_session:
  {
    g_warning ("gstrtpbin: could not create gstrtpsession element");
    return NULL;
  }
no_demux:
  {
    gst_object_unref (session);
    g_warning ("gstrtpbin: could not create gstrtpssrcdemux element");
    return NULL;
  }
}

static void
free_session (GstRtpBinSession * sess)
{
  GstRtpBin *bin;

  bin = sess->bin;

  gst_element_set_state (sess->session, GST_STATE_NULL);
  gst_element_set_state (sess->demux, GST_STATE_NULL);

  gst_bin_remove (GST_BIN_CAST (bin), sess->session);
  gst_bin_remove (GST_BIN_CAST (bin), sess->demux);

  g_slist_foreach (sess->streams, (GFunc) free_stream, NULL);
  g_slist_free (sess->streams);

  g_mutex_free (sess->lock);
  g_hash_table_destroy (sess->ptmap);

  bin->sessions = g_slist_remove (bin->sessions, sess);

  g_free (sess);
}

#if 0
static GstRtpBinStream *
find_stream_by_ssrc (GstRtpBinSession * session, guint32 ssrc)
{
  GSList *walk;

  for (walk = session->streams; walk; walk = g_slist_next (walk)) {
    GstRtpBinStream *stream = (GstRtpBinStream *) walk->data;

    if (stream->ssrc == ssrc)
      return stream;
  }
  return NULL;
}
#endif

/* get the payload type caps for the specific payload @pt in @session */
static GstCaps *
get_pt_map (GstRtpBinSession * session, guint pt)
{
  GstCaps *caps = NULL;
  GstRtpBin *bin;
  GValue ret = { 0 };
  GValue args[3] = { {0}, {0}, {0} };

  GST_DEBUG ("searching pt %d in cache", pt);

  GST_RTP_SESSION_LOCK (session);

  /* first look in the cache */
  caps = g_hash_table_lookup (session->ptmap, GINT_TO_POINTER (pt));
  if (caps)
    goto done;

  bin = session->bin;

  GST_DEBUG ("emiting signal for pt %d in session %d", pt, session->id);

  /* not in cache, send signal to request caps */
  g_value_init (&args[0], GST_TYPE_ELEMENT);
  g_value_set_object (&args[0], bin);
  g_value_init (&args[1], G_TYPE_UINT);
  g_value_set_uint (&args[1], session->id);
  g_value_init (&args[2], G_TYPE_UINT);
  g_value_set_uint (&args[2], pt);

  g_value_init (&ret, GST_TYPE_CAPS);
  g_value_set_boxed (&ret, NULL);

  g_signal_emitv (args, gst_rtp_bin_signals[SIGNAL_REQUEST_PT_MAP], 0, &ret);

  caps = (GstCaps *) g_value_get_boxed (&ret);
  if (!caps)
    goto no_caps;

  GST_DEBUG ("caching pt %d as %" GST_PTR_FORMAT, pt, caps);

  /* store in cache */
  g_hash_table_insert (session->ptmap, GINT_TO_POINTER (pt), caps);

done:
  GST_RTP_SESSION_UNLOCK (session);

  return caps;

  /* ERRORS */
no_caps:
  {
    GST_RTP_SESSION_UNLOCK (session);
    GST_DEBUG ("no pt map could be obtained");
    return NULL;
  }
}

static gboolean
return_true (gpointer key, gpointer value, gpointer user_data)
{
  return TRUE;
}

static void
gst_rtp_bin_clear_pt_map (GstRtpBin * bin)
{
  GSList *walk;

  GST_RTP_BIN_LOCK (bin);
  GST_DEBUG_OBJECT (bin, "clearing pt map");
  for (walk = bin->sessions; walk; walk = g_slist_next (walk)) {
    GstRtpBinSession *session = (GstRtpBinSession *) walk->data;

    GST_RTP_SESSION_LOCK (session);
#if 0
    /* This requires GLib 2.12 */
    g_hash_table_remove_all (session->ptmap);
#else
    g_hash_table_foreach_remove (session->ptmap, return_true, NULL);
#endif
    GST_RTP_SESSION_UNLOCK (session);
  }
  GST_RTP_BIN_UNLOCK (bin);
}

static GstRtpBinClient *
get_client (GstRtpBin * bin, guint8 len, guint8 * data, gboolean * created)
{
  GstRtpBinClient *result = NULL;
  GSList *walk;

  for (walk = bin->clients; walk; walk = g_slist_next (walk)) {
    GstRtpBinClient *client = (GstRtpBinClient *) walk->data;

    if (len != client->cname_len)
      continue;

    if (!strncmp ((gchar *) data, client->cname, client->cname_len)) {
      GST_DEBUG_OBJECT (bin, "found existing client %p with CNAME %s", client,
          client->cname);
      result = client;
      break;
    }
  }

  /* nothing found, create one */
  if (result == NULL) {
    result = g_new0 (GstRtpBinClient, 1);
    result->cname = g_strndup ((gchar *) data, len);
    result->cname_len = len;
    result->min_delta = G_MAXINT64;
    bin->clients = g_slist_prepend (bin->clients, result);
    GST_DEBUG_OBJECT (bin, "created new client %p with CNAME %s", result,
        result->cname);
  }
  return result;
}

static void
free_client (GstRtpBinClient * client, GstRtpBin * bin)
{
  bin->clients = g_slist_remove (bin->clients, client);
  g_free (client->cname);
  g_free (client);
}

/* associate a stream to the given CNAME. This will make sure all streams for
 * that CNAME are synchronized together. */
static void
gst_rtp_bin_associate (GstRtpBin * bin, GstRtpBinStream * stream, guint8 len,
    guint8 * data)
{
  GstRtpBinClient *client;
  gboolean created;
  GSList *walk;

  /* first find or create the CNAME */
  client = get_client (bin, len, data, &created);

  /* find stream in the client */
  for (walk = client->streams; walk; walk = g_slist_next (walk)) {
    GstRtpBinStream *ostream = (GstRtpBinStream *) walk->data;

    if (ostream == stream)
      break;
  }
  /* not found, add it to the list */
  if (walk == NULL) {
    GST_DEBUG_OBJECT (bin,
        "new association of SSRC %08x with client %p with CNAME %s",
        stream->ssrc, client, client->cname);
    client->streams = g_slist_prepend (client->streams, stream);
    client->nstreams++;
  } else {
    GST_DEBUG_OBJECT (bin,
        "found association of SSRC %08x with client %p with CNAME %s",
        stream->ssrc, client, client->cname);
  }

  /* we can only continue if we know the local clock-base and clock-rate */
  if (stream->clock_base == -1)
    goto no_clock_base;
  if (stream->clock_rate <= 0)
    goto no_clock_rate;

  /* map last RTP time to local timeline using our clock-base */
  stream->local_rtp = stream->last_extrtptime - stream->clock_base;

  GST_DEBUG_OBJECT (bin,
      "base %" G_GUINT64_FORMAT ", extrtptime %" G_GUINT64_FORMAT
      ", local RTP %" G_GUINT64_FORMAT ", clock-rate %d", stream->clock_base,
      stream->last_extrtptime, stream->local_rtp, stream->clock_rate);

  /* calculate local NTP time in gstreamer timestamp */
  stream->local_unix =
      gst_util_uint64_scale_int (stream->local_rtp, GST_SECOND,
      stream->clock_rate);
  /* calculate delta between server and receiver */
  stream->unix_delta = stream->last_unix - stream->local_unix;

  GST_DEBUG_OBJECT (bin,
      "local UNIX %" G_GUINT64_FORMAT ", remote UNIX %" G_GUINT64_FORMAT
      ", delta %" G_GINT64_FORMAT, stream->local_unix, stream->last_unix,
      stream->unix_delta);

  /* recalc inter stream playout offset, but only if there are more than one
   * stream. */
  if (client->nstreams > 1) {
    gint64 min;

    /* calculate the min of all deltas */
    min = G_MAXINT64;
    for (walk = client->streams; walk; walk = g_slist_next (walk)) {
      GstRtpBinStream *ostream = (GstRtpBinStream *) walk->data;

      if (ostream->unix_delta < min)
        min = ostream->unix_delta;
    }

    GST_DEBUG_OBJECT (bin, "client %p min delta %" G_GINT64_FORMAT, client,
        min);

    /* calculate offsets for each stream */
    for (walk = client->streams; walk; walk = g_slist_next (walk)) {
      GstRtpBinStream *ostream = (GstRtpBinStream *) walk->data;

      ostream->ts_offset = ostream->unix_delta - min;

      /* delta changed, see how much */
      if (ostream->prev_ts_offset != ostream->ts_offset) {
        gint64 diff;

        if (ostream->prev_ts_offset > ostream->ts_offset)
          diff = ostream->prev_ts_offset - ostream->ts_offset;
        else
          diff = ostream->ts_offset - ostream->prev_ts_offset;

        /* only change diff when it changed more than 1 millisecond. This
         * compensates for rounding errors in NTP to RTP timestamp
         * conversions */
        if (diff > GST_MSECOND)
          g_object_set (ostream->buffer, "ts-offset", ostream->ts_offset, NULL);

        ostream->prev_ts_offset = ostream->ts_offset;
      }
      GST_DEBUG_OBJECT (bin, "stream SSRC %08x, delta %" G_GINT64_FORMAT,
          ostream->ssrc, ostream->ts_offset);
    }
  }
  return;

no_clock_base:
  {
    GST_WARNING_OBJECT (bin, "we have no clock-base");
    return;
  }
no_clock_rate:
  {
    GST_WARNING_OBJECT (bin, "we have no clock-rate");
    return;
  }
}

#define GST_RTCP_BUFFER_FOR_PACKETS(b,buffer,packet) \
  for ((b) = gst_rtcp_buffer_get_first_packet ((buffer), (packet)); (b); \
          (b) = gst_rtcp_packet_move_to_next ((packet)))

#define GST_RTCP_SDES_FOR_ITEMS(b,packet) \
  for ((b) = gst_rtcp_packet_sdes_first_item ((packet)); (b); \
          (b) = gst_rtcp_packet_sdes_next_item ((packet)))

#define GST_RTCP_SDES_FOR_ENTRIES(b,packet) \
  for ((b) = gst_rtcp_packet_sdes_first_entry ((packet)); (b); \
          (b) = gst_rtcp_packet_sdes_next_entry ((packet)))

static GstFlowReturn
gst_rtp_bin_sync_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstRtpBinStream *stream;
  GstRtpBin *bin;
  GstRTCPPacket packet;
  guint32 ssrc;
  guint64 ntptime;
  guint32 rtptime;
  gboolean have_sr, have_sdes;
  gboolean more;

  stream = gst_pad_get_element_private (pad);
  bin = stream->bin;

  GST_DEBUG_OBJECT (bin, "received sync packet");

  if (!gst_rtcp_buffer_validate (buffer))
    goto invalid_rtcp;

  have_sr = FALSE;
  have_sdes = FALSE;
  GST_RTCP_BUFFER_FOR_PACKETS (more, buffer, &packet) {
    /* first packet must be SR or RR or else the validate would have failed */
    switch (gst_rtcp_packet_get_type (&packet)) {
      case GST_RTCP_TYPE_SR:
        /* only parse first. There is only supposed to be one SR in the packet
         * but we will deal with malformed packets gracefully */
        if (have_sr)
          break;
        /* get NTP and RTP times */
        gst_rtcp_packet_sr_get_sender_info (&packet, &ssrc, &ntptime, &rtptime,
            NULL, NULL);

        GST_DEBUG_OBJECT (bin, "received sync packet from SSRC %08x", ssrc);
        /* ignore SR that is not ours */
        if (ssrc != stream->ssrc)
          continue;

        have_sr = TRUE;

        /* store values in the stream */
        stream->have_sync = TRUE;
        stream->last_unix = gst_rtcp_ntp_to_unix (ntptime);
        /* use extended timestamp */
        gst_rtp_buffer_ext_timestamp (&stream->last_extrtptime, rtptime);
        break;
      case GST_RTCP_TYPE_SDES:
      {
        gboolean more_items, more_entries;

        /* only deal with first SDES, there is only supposed to be one SDES in
         * the RTCP packet but we deal with bad packets gracefully. Also bail
         * out if we have not seen an SR item yet. */
        if (have_sdes || !have_sr)
          break;

        GST_RTCP_SDES_FOR_ITEMS (more_items, &packet) {
          /* skip items that are not about the SSRC of the sender */
          if (gst_rtcp_packet_sdes_get_ssrc (&packet) != ssrc)
            continue;

          /* find the CNAME entry */
          GST_RTCP_SDES_FOR_ENTRIES (more_entries, &packet) {
            GstRTCPSDESType type;
            guint8 len;
            guint8 *data;

            gst_rtcp_packet_sdes_get_entry (&packet, &type, &len, &data);

            if (type == GST_RTCP_SDES_CNAME) {
              stream->clock_base = GST_BUFFER_OFFSET (buffer);
              /* associate the stream to CNAME */
              gst_rtp_bin_associate (bin, stream, len, data);
            }
          }
        }
        have_sdes = TRUE;
        break;
      }
      default:
        /* we can ignore these packets */
        break;
    }
  }

  gst_buffer_unref (buffer);

  return ret;

  /* ERRORS */
invalid_rtcp:
  {
    /* this is fatal and should be filtered earlier */
    GST_ELEMENT_ERROR (bin, STREAM, DECODE, (NULL),
        ("invalid RTCP packet received"));
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
}

/* create a new stream with @ssrc in @session. Must be called with
 * RTP_SESSION_LOCK. */
static GstRtpBinStream *
create_stream (GstRtpBinSession * session, guint32 ssrc)
{
  GstElement *buffer, *demux;
  GstRtpBinStream *stream;
  GstPadTemplate *templ;
  gchar *padname;

  if (!(buffer = gst_element_factory_make ("gstrtpjitterbuffer", NULL)))
    goto no_jitterbuffer;

  if (!(demux = gst_element_factory_make ("gstrtpptdemux", NULL)))
    goto no_demux;

  stream = g_new0 (GstRtpBinStream, 1);
  stream->ssrc = ssrc;
  stream->bin = session->bin;
  stream->session = session;
  stream->buffer = buffer;
  stream->demux = demux;
  stream->last_extrtptime = -1;
  stream->have_sync = FALSE;
  session->streams = g_slist_prepend (session->streams, stream);

  /* make an internal sinkpad for RTCP sync packets. Take ownership of the
   * pad. We will link this pad later. */
  padname = g_strdup_printf ("sync_%d", ssrc);
  templ = gst_static_pad_template_get (&rtpbin_sync_sink_template);
  stream->sync_pad = gst_pad_new_from_template (templ, padname);
  gst_object_unref (templ);
  gst_object_ref (stream->sync_pad);
  gst_object_sink (stream->sync_pad);
  gst_pad_set_element_private (stream->sync_pad, stream);
  gst_pad_set_chain_function (stream->sync_pad, gst_rtp_bin_sync_chain);
  gst_pad_set_active (stream->sync_pad, TRUE);

  /* provide clock_rate to the jitterbuffer when needed */
  g_signal_connect (buffer, "request-pt-map",
      (GCallback) pt_map_requested, session);

  /* configure latency */
  g_object_set (buffer, "latency", session->bin->latency, NULL);

  gst_bin_add (GST_BIN_CAST (session->bin), buffer);
  gst_element_set_state (buffer, GST_STATE_PLAYING);
  gst_bin_add (GST_BIN_CAST (session->bin), demux);
  gst_element_set_state (demux, GST_STATE_PLAYING);

  /* link stuff */
  gst_element_link (buffer, demux);

  return stream;

  /* ERRORS */
no_jitterbuffer:
  {
    g_warning ("gstrtpbin: could not create gstrtpjitterbuffer element");
    return NULL;
  }
no_demux:
  {
    gst_object_unref (buffer);
    g_warning ("gstrtpbin: could not create gstrtpptdemux element");
    return NULL;
  }
}

static void
free_stream (GstRtpBinStream * stream)
{
  GstRtpBinSession *session;

  session = stream->session;

  gst_element_set_state (stream->buffer, GST_STATE_NULL);
  gst_element_set_state (stream->demux, GST_STATE_NULL);

  gst_bin_remove (GST_BIN_CAST (session->bin), stream->buffer);
  gst_bin_remove (GST_BIN_CAST (session->bin), stream->demux);

  gst_object_unref (stream->sync_pad);

  session->streams = g_slist_remove (session->streams, stream);

  g_free (stream);
}

/* GObject vmethods */
static void gst_rtp_bin_dispose (GObject * object);
static void gst_rtp_bin_finalize (GObject * object);
static void gst_rtp_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* GstElement vmethods */
static GstClock *gst_rtp_bin_provide_clock (GstElement * element);
static GstStateChangeReturn gst_rtp_bin_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_rtp_bin_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_rtp_bin_release_pad (GstElement * element, GstPad * pad);
static void gst_rtp_bin_clear_pt_map (GstRtpBin * bin);

GST_BOILERPLATE (GstRtpBin, gst_rtp_bin, GstBin, GST_TYPE_BIN);

static void
gst_rtp_bin_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* sink pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpbin_recv_rtp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpbin_recv_rtcp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpbin_send_rtp_sink_template));

  /* src pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpbin_recv_rtp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpbin_send_rtcp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpbin_send_rtp_src_template));

  gst_element_class_set_details (element_class, &rtpbin_details);
}

static void
gst_rtp_bin_class_init (GstRtpBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_type_class_add_private (klass, sizeof (GstRtpBinPrivate));

  gobject_class->dispose = gst_rtp_bin_dispose;
  gobject_class->finalize = gst_rtp_bin_finalize;
  gobject_class->set_property = gst_rtp_bin_set_property;
  gobject_class->get_property = gst_rtp_bin_get_property;

  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Buffer latency in ms",
          "Default amount of ms to buffer in the jitterbuffers", 0,
          G_MAXUINT, DEFAULT_LATENCY_MS, G_PARAM_READWRITE));

  /**
   * GstRtpBin::request-pt-map:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @pt: the pt
   *
   * Request the payload type as #GstCaps for @pt in @session.
   */
  gst_rtp_bin_signals[SIGNAL_REQUEST_PT_MAP] =
      g_signal_new ("request-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpBinClass, request_pt_map),
      NULL, NULL, gst_rtp_bin_marshal_BOXED__UINT_UINT, GST_TYPE_CAPS, 2,
      G_TYPE_UINT, G_TYPE_UINT);
  /**
   * GstRtpBin::clear-pt-map:
   * @rtpbin: the object which received the signal
   *
   * Clear all previously cached pt-mapping obtained with
   * GstRtpBin::request-pt-map.
   */
  gst_rtp_bin_signals[SIGNAL_CLEAR_PT_MAP] =
      g_signal_new ("clear-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstRtpBinClass, clear_pt_map),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstRtpBin::on-new-ssrc:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC 
   *
   * Notify of a new SSRC that entered @session.
   */
  gst_rtp_bin_signals[SIGNAL_ON_NEW_SSRC] =
      g_signal_new ("on-new-ssrc", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpBinClass, on_new_ssrc),
      NULL, NULL, gst_rtp_bin_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);
  /**
   * GstRtpBin::on-ssrc_collision:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC 
   *
   * Notify when we have an SSRC collision
   */
  gst_rtp_bin_signals[SIGNAL_ON_SSRC_COLLISION] =
      g_signal_new ("on-ssrc-collision", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpBinClass, on_ssrc_collision),
      NULL, NULL, gst_rtp_bin_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);
  /**
   * GstRtpBin::on-ssrc_validated:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC 
   *
   * Notify of a new SSRC that became validated.
   */
  gst_rtp_bin_signals[SIGNAL_ON_SSRC_VALIDATED] =
      g_signal_new ("on-ssrc-validated", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpBinClass, on_ssrc_validated),
      NULL, NULL, gst_rtp_bin_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);

  /**
   * GstRtpBin::on-bye-ssrc:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC 
   *
   * Notify of an SSRC that became inactive because of a BYE packet.
   */
  gst_rtp_bin_signals[SIGNAL_ON_BYE_SSRC] =
      g_signal_new ("on-bye-ssrc", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpBinClass, on_bye_ssrc),
      NULL, NULL, gst_rtp_bin_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);
  /**
   * GstRtpBin::on-bye-timeout:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC 
   *
   * Notify of an SSRC that has timed out because of BYE
   */
  gst_rtp_bin_signals[SIGNAL_ON_BYE_TIMEOUT] =
      g_signal_new ("on-bye-timeout", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpBinClass, on_bye_timeout),
      NULL, NULL, gst_rtp_bin_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);
  /**
   * GstRtpBin::on-timeout:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC 
   *
   * Notify of an SSRC that has timed out
   */
  gst_rtp_bin_signals[SIGNAL_ON_TIMEOUT] =
      g_signal_new ("on-timeout", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpBinClass, on_timeout),
      NULL, NULL, gst_rtp_bin_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);

  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_rtp_bin_provide_clock);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_rtp_bin_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_bin_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_rtp_bin_release_pad);

  klass->clear_pt_map = GST_DEBUG_FUNCPTR (gst_rtp_bin_clear_pt_map);

  GST_DEBUG_CATEGORY_INIT (gst_rtp_bin_debug, "rtpbin", 0, "RTP bin");
}

static void
gst_rtp_bin_init (GstRtpBin * rtpbin, GstRtpBinClass * klass)
{
  rtpbin->priv = GST_RTP_BIN_GET_PRIVATE (rtpbin);
  rtpbin->priv->bin_lock = g_mutex_new ();
  rtpbin->provided_clock = gst_system_clock_obtain ();
  rtpbin->latency = DEFAULT_LATENCY_MS;
}

static void
gst_rtp_bin_dispose (GObject * object)
{
  GstRtpBin *rtpbin;

  rtpbin = GST_RTP_BIN (object);

  g_slist_foreach (rtpbin->sessions, (GFunc) free_session, NULL);
  g_slist_foreach (rtpbin->clients, (GFunc) free_client, NULL);
  g_slist_free (rtpbin->sessions);
  rtpbin->sessions = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_rtp_bin_finalize (GObject * object)
{
  GstRtpBin *rtpbin;

  rtpbin = GST_RTP_BIN (object);

  g_mutex_free (rtpbin->priv->bin_lock);
  gst_object_unref (rtpbin->provided_clock);
  g_slist_free (rtpbin->sessions);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpBin *rtpbin;

  rtpbin = GST_RTP_BIN (object);

  switch (prop_id) {
    case PROP_LATENCY:
      rtpbin->latency = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpBin *rtpbin;

  rtpbin = GST_RTP_BIN (object);

  switch (prop_id) {
    case PROP_LATENCY:
      g_value_set_uint (value, rtpbin->latency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstClock *
gst_rtp_bin_provide_clock (GstElement * element)
{
  GstRtpBin *rtpbin;

  rtpbin = GST_RTP_BIN (element);

  return GST_CLOCK_CAST (gst_object_ref (rtpbin->provided_clock));
}

static void
calc_ntp_ns_base (GstRtpBin * bin)
{
  GstClockTime now;
  GTimeVal current;
  GSList *walk;

  /* get the current time and convert it to NTP time in nanoseconds */
  g_get_current_time (&current);
  now = GST_TIMEVAL_TO_TIME (current);
  now += (2208988800LL * GST_SECOND);

  GST_RTP_BIN_LOCK (bin);
  bin->priv->ntp_ns_base = now;
  for (walk = bin->sessions; walk; walk = g_slist_next (walk)) {
    GstRtpBinSession *session = (GstRtpBinSession *) walk->data;

    g_object_set (session->session, "ntp-ns-base", now, NULL);
  }
  GST_RTP_BIN_UNLOCK (bin);

  return;
}

static GstStateChangeReturn
gst_rtp_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  GstRtpBin *rtpbin;

  rtpbin = GST_RTP_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      calc_ntp_ns_base (rtpbin);
      break;
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return res;
}

/* a new pad (SSRC) was created in @session */
static void
new_payload_found (GstElement * element, guint pt, GstPad * pad,
    GstRtpBinStream * stream)
{
  GstRtpBin *rtpbin;
  GstElementClass *klass;
  GstPadTemplate *templ;
  gchar *padname;
  GstPad *gpad;

  rtpbin = stream->bin;

  GST_DEBUG ("new payload pad %d", pt);

  /* ghost the pad to the parent */
  klass = GST_ELEMENT_GET_CLASS (rtpbin);
  templ = gst_element_class_get_pad_template (klass, "recv_rtp_src_%d_%d_%d");
  padname = g_strdup_printf ("recv_rtp_src_%d_%u_%d",
      stream->session->id, stream->ssrc, pt);
  gpad = gst_ghost_pad_new_from_template (padname, pad, templ);
  g_free (padname);

  gst_pad_set_caps (gpad, GST_PAD_CAPS (pad));
  gst_pad_set_active (gpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), gpad);
}

static GstCaps *
pt_map_requested (GstElement * element, guint pt, GstRtpBinSession * session)
{
  GstRtpBin *rtpbin;
  GstCaps *caps;

  rtpbin = session->bin;

  GST_DEBUG_OBJECT (rtpbin, "payload map requested for pt %d in session %d", pt,
      session->id);

  caps = get_pt_map (session, pt);
  if (!caps)
    goto no_caps;

  return caps;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (rtpbin, "could not get caps");
    return NULL;
  }
}

/* emited when caps changed for the session */
static void
caps_changed (GstPad * pad, GParamSpec * pspec, GstRtpBinSession * session)
{
  GstRtpBin *bin;
  GstCaps *caps;
  gint payload;
  const GstStructure *s;

  bin = session->bin;

  g_object_get (pad, "caps", &caps, NULL);

  if (caps == NULL)
    return;

  GST_DEBUG_OBJECT (bin, "got caps %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);

  /* get payload, finish when it's not there */
  if (!gst_structure_get_int (s, "payload", &payload))
    return;

  GST_RTP_SESSION_LOCK (session);
  GST_DEBUG_OBJECT (bin, "insert caps for payload %d", payload);
  g_hash_table_insert (session->ptmap, GINT_TO_POINTER (payload), caps);
  GST_RTP_SESSION_UNLOCK (session);
}

/* a new pad (SSRC) was created in @session */
static void
new_ssrc_pad_found (GstElement * element, guint ssrc, GstPad * pad,
    GstRtpBinSession * session)
{
  GstRtpBinStream *stream;
  GstPad *sinkpad, *srcpad;
  gchar *padname;
  GstCaps *caps;

  GST_DEBUG_OBJECT (session->bin, "new SSRC pad %08x", ssrc);

  GST_RTP_SESSION_LOCK (session);

  /* create new stream */
  stream = create_stream (session, ssrc);
  if (!stream)
    goto no_stream;

  /* get the caps of the pad, we need the clock-rate and base_time if any. */
  if ((caps = gst_pad_get_caps (pad))) {
    const GstStructure *s;
    guint val;

    GST_DEBUG_OBJECT (session->bin, "pad has caps %" GST_PTR_FORMAT, caps);

    s = gst_caps_get_structure (caps, 0);

    if (!gst_structure_get_int (s, "clock-rate", &stream->clock_rate))
      stream->clock_rate = -1;

    if (gst_structure_get_uint (s, "clock-base", &val))
      stream->clock_base = val;
    else
      stream->clock_base = -1;
  }

  /* get pad and link */
  GST_DEBUG_OBJECT (session->bin, "linking jitterbuffer");
  sinkpad = gst_element_get_static_pad (stream->buffer, "sink");
  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);

  /* get the RTCP sync pad */
  GST_DEBUG_OBJECT (session->bin, "linking sync pad");
  padname = g_strdup_printf ("rtcp_src_%d", ssrc);
  srcpad = gst_element_get_pad (element, padname);
  g_free (padname);
  gst_pad_link (srcpad, stream->sync_pad);
  gst_object_unref (srcpad);

  /* connect to the new-pad signal of the payload demuxer, this will expose the
   * new pad by ghosting it. */
  stream->demux_newpad_sig = g_signal_connect (stream->demux,
      "new-payload-type", (GCallback) new_payload_found, stream);
  /* connect to the request-pt-map signal. This signal will be emited by the
   * demuxer so that it can apply a proper caps on the buffers for the
   * depayloaders. */
  stream->demux_ptreq_sig = g_signal_connect (stream->demux,
      "request-pt-map", (GCallback) pt_map_requested, session);

  GST_RTP_SESSION_UNLOCK (session);

  return;

  /* ERRORS */
no_stream:
  {
    GST_RTP_SESSION_UNLOCK (session);
    GST_DEBUG ("could not create stream");
    return;
  }
}

/* Create a pad for receiving RTP for the session in @name. Must be called with
 * RTP_BIN_LOCK.
 */
static GstPad *
create_recv_rtp (GstRtpBin * rtpbin, GstPadTemplate * templ, const gchar * name)
{
  GstPad *result, *sinkdpad;
  guint sessid;
  GstRtpBinSession *session;
  GstPadLinkReturn lres;

  /* first get the session number */
  if (name == NULL || sscanf (name, "recv_rtp_sink_%d", &sessid) != 1)
    goto no_name;

  GST_DEBUG_OBJECT (rtpbin, "finding session %d", sessid);

  /* get or create session */
  session = find_session_by_id (rtpbin, sessid);
  if (!session) {
    GST_DEBUG_OBJECT (rtpbin, "creating session %d", sessid);
    /* create session now */
    session = create_session (rtpbin, sessid);
    if (session == NULL)
      goto create_error;
  }

  /* check if pad was requested */
  if (session->recv_rtp_sink != NULL)
    goto existed;

  GST_DEBUG_OBJECT (rtpbin, "getting RTP sink pad");
  /* get recv_rtp pad and store */
  session->recv_rtp_sink =
      gst_element_get_request_pad (session->session, "recv_rtp_sink");
  if (session->recv_rtp_sink == NULL)
    goto pad_failed;

  g_signal_connect (session->recv_rtp_sink, "notify::caps",
      (GCallback) caps_changed, session);

  GST_DEBUG_OBJECT (rtpbin, "getting RTP src pad");
  /* get srcpad, link to SSRCDemux */
  session->recv_rtp_src =
      gst_element_get_static_pad (session->session, "recv_rtp_src");
  if (session->recv_rtp_src == NULL)
    goto pad_failed;

  GST_DEBUG_OBJECT (rtpbin, "getting demuxer RTP sink pad");
  sinkdpad = gst_element_get_static_pad (session->demux, "sink");
  GST_DEBUG_OBJECT (rtpbin, "linking demuxer RTP sink pad");
  lres = gst_pad_link (session->recv_rtp_src, sinkdpad);
  gst_object_unref (sinkdpad);
  if (lres != GST_PAD_LINK_OK)
    goto link_failed;

  /* connect to the new-ssrc-pad signal of the SSRC demuxer */
  session->demux_newpad_sig = g_signal_connect (session->demux,
      "new-ssrc-pad", (GCallback) new_ssrc_pad_found, session);

  GST_DEBUG_OBJECT (rtpbin, "ghosting session sink pad");
  result =
      gst_ghost_pad_new_from_template (name, session->recv_rtp_sink, templ);
  gst_pad_set_active (result, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), result);

  return result;

  /* ERRORS */
no_name:
  {
    g_warning ("gstrtpbin: invalid name given");
    return NULL;
  }
create_error:
  {
    /* create_session already warned */
    return NULL;
  }
existed:
  {
    g_warning ("gstrtpbin: recv_rtp pad already requested for session %d",
        sessid);
    return NULL;
  }
pad_failed:
  {
    g_warning ("gstrtpbin: failed to get session pad");
    return NULL;
  }
link_failed:
  {
    g_warning ("gstrtpbin: failed to link pads");
    return NULL;
  }
}

/* Create a pad for receiving RTCP for the session in @name. Must be called with
 * RTP_BIN_LOCK.
 */
static GstPad *
create_recv_rtcp (GstRtpBin * rtpbin, GstPadTemplate * templ,
    const gchar * name)
{
  GstPad *result;
  guint sessid;
  GstRtpBinSession *session;
  GstPad *sinkdpad;
  GstPadLinkReturn lres;

  /* first get the session number */
  if (name == NULL || sscanf (name, "recv_rtcp_sink_%d", &sessid) != 1)
    goto no_name;

  GST_DEBUG_OBJECT (rtpbin, "finding session %d", sessid);

  /* get or create the session */
  session = find_session_by_id (rtpbin, sessid);
  if (!session) {
    GST_DEBUG_OBJECT (rtpbin, "creating session %d", sessid);
    /* create session now */
    session = create_session (rtpbin, sessid);
    if (session == NULL)
      goto create_error;
  }

  /* check if pad was requested */
  if (session->recv_rtcp_sink != NULL)
    goto existed;

  /* get recv_rtp pad and store */
  GST_DEBUG_OBJECT (rtpbin, "getting RTCP sink pad");
  session->recv_rtcp_sink =
      gst_element_get_request_pad (session->session, "recv_rtcp_sink");
  if (session->recv_rtcp_sink == NULL)
    goto pad_failed;

  /* get srcpad, link to SSRCDemux */
  GST_DEBUG_OBJECT (rtpbin, "getting sync src pad");
  session->sync_src = gst_element_get_static_pad (session->session, "sync_src");
  if (session->sync_src == NULL)
    goto pad_failed;

  GST_DEBUG_OBJECT (rtpbin, "getting demuxer RTCP sink pad");
  sinkdpad = gst_element_get_static_pad (session->demux, "rtcp_sink");
  lres = gst_pad_link (session->sync_src, sinkdpad);
  gst_object_unref (sinkdpad);
  if (lres != GST_PAD_LINK_OK)
    goto link_failed;

  result =
      gst_ghost_pad_new_from_template (name, session->recv_rtcp_sink, templ);
  gst_pad_set_active (result, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), result);

  return result;

  /* ERRORS */
no_name:
  {
    g_warning ("gstrtpbin: invalid name given");
    return NULL;
  }
create_error:
  {
    /* create_session already warned */
    return NULL;
  }
existed:
  {
    g_warning ("gstrtpbin: recv_rtcp pad already requested for session %d",
        sessid);
    return NULL;
  }
pad_failed:
  {
    g_warning ("gstrtpbin: failed to get session pad");
    return NULL;
  }
link_failed:
  {
    g_warning ("gstrtpbin: failed to link pads");
    return NULL;
  }
}

/* Create a pad for sending RTP for the session in @name. Must be called with
 * RTP_BIN_LOCK.
 */
static GstPad *
create_send_rtp (GstRtpBin * rtpbin, GstPadTemplate * templ, const gchar * name)
{
  GstPad *result, *srcghost;
  gchar *gname;
  guint sessid;
  GstRtpBinSession *session;
  GstElementClass *klass;

  /* first get the session number */
  if (name == NULL || sscanf (name, "send_rtp_sink_%d", &sessid) != 1)
    goto no_name;

  /* get or create session */
  session = find_session_by_id (rtpbin, sessid);
  if (!session) {
    /* create session now */
    session = create_session (rtpbin, sessid);
    if (session == NULL)
      goto create_error;
  }

  /* check if pad was requested */
  if (session->send_rtp_sink != NULL)
    goto existed;

  /* get send_rtp pad and store */
  session->send_rtp_sink =
      gst_element_get_request_pad (session->session, "send_rtp_sink");
  if (session->send_rtp_sink == NULL)
    goto pad_failed;

  result =
      gst_ghost_pad_new_from_template (name, session->send_rtp_sink, templ);
  gst_pad_set_active (result, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), result);

  /* get srcpad */
  session->send_rtp_src =
      gst_element_get_static_pad (session->session, "send_rtp_src");
  if (session->send_rtp_src == NULL)
    goto no_srcpad;

  /* ghost the new source pad */
  klass = GST_ELEMENT_GET_CLASS (rtpbin);
  gname = g_strdup_printf ("send_rtp_src_%d", sessid);
  templ = gst_element_class_get_pad_template (klass, "send_rtp_src_%d");
  srcghost =
      gst_ghost_pad_new_from_template (gname, session->send_rtp_src, templ);
  gst_pad_set_active (srcghost, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), srcghost);
  g_free (gname);

  return result;

  /* ERRORS */
no_name:
  {
    g_warning ("gstrtpbin: invalid name given");
    return NULL;
  }
create_error:
  {
    /* create_session already warned */
    return NULL;
  }
existed:
  {
    g_warning ("gstrtpbin: send_rtp pad already requested for session %d",
        sessid);
    return NULL;
  }
pad_failed:
  {
    g_warning ("gstrtpbin: failed to get session pad for session %d", sessid);
    return NULL;
  }
no_srcpad:
  {
    g_warning ("gstrtpbin: failed to get rtp source pad for session %d",
        sessid);
    return NULL;
  }
}

/* Create a pad for sending RTCP for the session in @name. Must be called with
 * RTP_BIN_LOCK.
 */
static GstPad *
create_rtcp (GstRtpBin * rtpbin, GstPadTemplate * templ, const gchar * name)
{
  GstPad *result;
  guint sessid;
  GstRtpBinSession *session;

  /* first get the session number */
  if (name == NULL || sscanf (name, "send_rtcp_src_%d", &sessid) != 1)
    goto no_name;

  /* get or create session */
  session = find_session_by_id (rtpbin, sessid);
  if (!session)
    goto no_session;

  /* check if pad was requested */
  if (session->send_rtcp_src != NULL)
    goto existed;

  /* get rtcp_src pad and store */
  session->send_rtcp_src =
      gst_element_get_request_pad (session->session, "send_rtcp_src");
  if (session->send_rtcp_src == NULL)
    goto pad_failed;

  result =
      gst_ghost_pad_new_from_template (name, session->send_rtcp_src, templ);
  gst_pad_set_active (result, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), result);

  return result;

  /* ERRORS */
no_name:
  {
    g_warning ("gstrtpbin: invalid name given");
    return NULL;
  }
no_session:
  {
    g_warning ("gstrtpbin: session with id %d does not exist", sessid);
    return NULL;
  }
existed:
  {
    g_warning ("gstrtpbin: send_rtcp_src pad already requested for session %d",
        sessid);
    return NULL;
  }
pad_failed:
  {
    g_warning ("gstrtpbin: failed to get rtcp pad for session %d", sessid);
    return NULL;
  }
}

/* 
 */
static GstPad *
gst_rtp_bin_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  GstRtpBin *rtpbin;
  GstElementClass *klass;
  GstPad *result;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTP_BIN (element), NULL);

  rtpbin = GST_RTP_BIN (element);
  klass = GST_ELEMENT_GET_CLASS (element);

  GST_RTP_BIN_LOCK (rtpbin);

  /* figure out the template */
  if (templ == gst_element_class_get_pad_template (klass, "recv_rtp_sink_%d")) {
    result = create_recv_rtp (rtpbin, templ, name);
  } else if (templ == gst_element_class_get_pad_template (klass,
          "recv_rtcp_sink_%d")) {
    result = create_recv_rtcp (rtpbin, templ, name);
  } else if (templ == gst_element_class_get_pad_template (klass,
          "send_rtp_sink_%d")) {
    result = create_send_rtp (rtpbin, templ, name);
  } else if (templ == gst_element_class_get_pad_template (klass,
          "send_rtcp_src_%d")) {
    result = create_rtcp (rtpbin, templ, name);
  } else
    goto wrong_template;

  GST_RTP_BIN_UNLOCK (rtpbin);

  return result;

  /* ERRORS */
wrong_template:
  {
    GST_RTP_BIN_UNLOCK (rtpbin);
    g_warning ("gstrtpbin: this is not our template");
    return NULL;
  }
}

static void
gst_rtp_bin_release_pad (GstElement * element, GstPad * pad)
{
}
