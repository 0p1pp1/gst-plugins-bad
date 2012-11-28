/*
 * This library is licensed under 2 different licenses and you
 * can choose to use it under the terms of either one of them. The
 * two licenses are the MPL 1.1 and the LGPL.
 *
 * MPL:
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * LGPL:
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
 *
 * The Original Code is Fluendo MPEG Demuxer plugin.
 *
 * The Initial Developer of the Original Code is Fluendo, S.A.
 * Portions created by Fluendo, S.A. are Copyright (C) 2005,2006,2007,2008,2009
 * Fluendo, S.A. All Rights Reserved.
 *
 * Contributor(s): Wim Taymans <wim@fluendo.com>
 */

#ifndef __GST_MPEGTS_DEMUX_H__
#define __GST_MPEGTS_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#include "gstmpegdesc.h"
#include "gstpesfilter.h"
#include "gstsectionfilter.h"

G_BEGIN_DECLS

#define MPEGTS_MIN_PES_BUFFER_SIZE     4 * 1024
#define MPEGTS_MAX_PES_BUFFER_SIZE   256 * 1024

#define MPEGTS_MAX_PID 0x1fff
#define MPEGTS_NORMAL_TS_PACKETSIZE  188
#define MPEGTS_M2TS_TS_PACKETSIZE    192
#define MPEGTS_DVB_ASI_TS_PACKETSIZE 204
#define MPEGTS_ATSC_TS_PACKETSIZE    208

#define ECM_PID_NONSCRAMBLED         0x1FFF
#define ECM_PID_UNDEFINED            MPEGTS_MAX_PID + 1


#define IS_MPEGTS_SYNC(data) (((data)[0] == 0x47) && \
                                    (((data)[1] & 0x80) == 0x00) && \
                                    (((data)[3] & 0x30) != 0x00))

#define GST_TYPE_MPEGTS_DEMUX              (gst_mpegts_demux_get_type())
#define GST_MPEGTS_DEMUX(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                            GST_TYPE_MPEGTS_DEMUX,GstMpegTSDemux))
#define GST_MPEGTS_DEMUX_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass),\
                                            GST_TYPE_MPEGTS_DEMUX,GstMpegTSDemuxClass))
#define GST_MPEGTS_DEMUX_GET_CLASS(klass)  (G_TYPE_INSTANCE_GET_CLASS((klass),\
                                            GST_TYPE_MPEGTS_DEMUX,GstMpegTSDemuxClass))
#define GST_IS_MPEGTS_DEMUX(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                            GST_TYPE_MPEGTS_DEMUX))
#define GST_IS_MPEGTS_DEMUX_CLASS(obj)     (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                            GST_TYPE_MPEGTS_DEMUX))

typedef struct _GstMpegTSStream GstMpegTSStream;
typedef struct _GstMpegTSPMTEntry GstMpegTSPMTEntry;
typedef struct _GstMpegTSPMT GstMpegTSPMT;
typedef struct _GstMpegTSPATEntry GstMpegTSPATEntry;
typedef struct _GstMpegTSPAT GstMpegTSPAT;
typedef struct _GstMpegTSCATEntry GstMpegTSCATEntry;
typedef struct _GstMpegTSCAT GstMpegTSCAT;
typedef struct _GstMpegTSECM GstMpegTSECM;
typedef struct _GstMpegTSDemux GstMpegTSDemux;
typedef struct _GstMpegTSDemuxClass GstMpegTSDemuxClass;


struct _GstMpegTSPMTEntry {
  guint16           PID;
  guint16           stream_ECM_PID;
};

struct _GstMpegTSPMT {
  guint16           program_number;
  guint8            version_number;
  gboolean          current_next_indicator;
  guint8            section_number;
  guint8            last_section_number;
  guint16           PCR_PID;
  guint16           program_info_length;
  GstMPEGDescriptor * program_info;

  GArray            * entries;

  guint16           program_ECM_PID;
};

struct _GstMpegTSPATEntry {
  guint16           program_number;
  guint16           PID;
};

struct _GstMpegTSPAT  {
  guint16           transport_stream_id;
  guint8            version_number;
  gboolean          current_next_indicator;
  guint8            section_number;
  guint8            last_section_number;

  GArray            * entries;
};

struct _GstMpegTSCATEntry {
  guint16           emm_PID;
};

struct _GstMpegTSCAT {
  guint8            version_number;
  gboolean          current_next_indicator;
  guint8            section_number;
  guint8            last_section_number;

  GArray            * entries;
};

struct _GstMpegTSECM {
  guint16           cas_id;

  guint8            version_number;
  gboolean          current_next_indicator;
  guint8            section_number;
  guint8            last_section_number;
};

typedef enum _MpegTsStreamFlags {
  MPEGTS_STREAM_FLAG_STREAM_TYPE_UNKNOWN = 0x01,
  MPEGTS_STREAM_FLAG_PMT_VALID = 0x02,
  MPEGTS_STREAM_FLAG_IS_VIDEO  = 0x04,
  MPEGTS_STREAM_FLAG_IS_AUDIO  = 0x08
} MpegTsStreamFlags;

/* Information associated to a single MPEG stream. */
struct _GstMpegTSStream {
  GstMpegTSDemux     * demux;

  MpegTsStreamFlags  flags;

  /* PID and type */
  guint16           PID;
  guint8            PID_type;

  /* for PAT streams */
  GstMpegTSPAT       PAT;

  /* for PMT streams */
  GstMpegTSPMT       PMT;

  /* for CA streams */
  GstMpegTSCAT       CAT;

  /* for PAT, PMT, CA, ECM, EMM and private streams */
  GstSectionFilter  section_filter;


  /* for ECM streams */
  GstMpegTSECM      ECM;

  /* for PCR streams & per-program info */
  guint64           first_pcr;
  gint64            pcr_adjust;    //compensation for PCR wrap-arounds/discont
  guint64           last_pcr;      // current/latest pcr, incl. wrap
  guint64           last_pcr_delta;
  guint64           bitrate_base_pcr;
  guint64           base_pcr_offset; // byte_offset of hte bitrate_base_pcr
  /* for pull mode */
  guint64           tail_pcr;
  guint64           first_pts;
  guint64           tail_pts;
  /* bitrate: estimated bitrate (based on pcr_delta and last_pcr_offset) */
  guint64           bitrate;
  GstSegment        src_segment;


  /* for PES streams */
  guint8            id;
  guint8            stream_type;
  GstBuffer         * pes_buffer;
  guint32           pes_buffer_size;
  guint32           pes_buffer_used;
  gboolean          pes_buffer_overflow;
  gboolean          pes_buffer_in_sync;
  GstPESFilter      filter;
  GstPad            * pad;
  GstFlowReturn     last_ret;
  GstMPEGDescriptor *ES_info;

  guint64           last_pts;
  GstClockTime      segment_thresh;
  GstClockTime      last_segment_start;

  /* pid of PMT that this stream belongs to */
  guint16           PMT_pid;
  gboolean          discont;
  gboolean          need_segment;

  /* pid of ECM that this stream belongs to */
  guint16           ECM_pid;
  gint              tag; /* component tag. used in vid/aid filtering */
};


struct _GstMpegTSDemux {
  GstElement        parent;

  /* properties */
  gboolean          check_crc;

  /* sink pad and adapter */
  GstPad            * sinkpad;
  GstAdapter        * adapter;
  guint8            ** sync_lut;
  guint             sync_lut_len;

  /* current PMT PID */
  guint16           current_PMT;

  /* Array of MPEGTS_MAX_PID + 1 stream entries */
  GstMpegTSStream    **  streams;
  
  /* Array of Elementary Stream pids for ts with PMT */
  guint16           * elementary_pids;
  guint             nb_elementary_pids;

  /* Program number to use */
  gint              program_number;

  /* indicates that we need to close our pad group, because we've added
   * at least one pad */
  gboolean          need_no_more_pads;
  gint              packetsize;
  gboolean          m2ts_mode;
  /* clocking */
  GstClock          * clock;
  GstClockTime      clock_base;

  /* Additional information required for seeking.
   * num_packets: Number of packets outputted
   * bitrate: estimated bitrate (based on pcr and num_packets */
  guint64           num_packets;
  gint64            bitrate;

  /* Two PCRs observations to calculate bitrate */
  guint64            pcr[2];

  /* video/audio stream number to use */
  gint              vid;
  gint              aid;

  /* in pull-mode or not */
  gboolean          random_access;

  // current byte-offset in the input stream
  guint64           byte_offset;

  /* Cached duration estimation */
  GstClockTime      cache_duration;

  /* Cached base_PCR in GStreamer time. */
  GstClockTime      base_pts;

  /* base timings on first buffer timestamp */
  GstClockTime      first_buf_ts;
  GstClockTime      in_gap;

  /* Detect when the source stops for a while, we will resync the interpolation gap */
  GstClockTime      last_buf_ts;

  /* Number of expected pads which have not been added yet */
  gint              pending_pads;

  gboolean          tried_adding_pads;

  gboolean          in_flushing;

  /* bcas descrambling */
  gboolean          bcas_descramble;
  void              * dm2_handle;
};

struct _GstMpegTSDemuxClass {
  GstElementClass   parent_class;

  GstPadTemplate    * sink_template;
  GstPadTemplate    * video_template;
  GstPadTemplate    * audio_template;
  GstPadTemplate    * subpicture_template;
  GstPadTemplate    * private_template;
};

GType     gst_mpegts_demux_get_type (void);

gboolean  gst_mpegts_demux_plugin_init (GstPlugin *plugin);

G_END_DECLS

#endif /* __GST_MPEGTS_DEMUX_H__ */
