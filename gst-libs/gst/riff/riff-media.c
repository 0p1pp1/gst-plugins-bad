/* GStreamer RIFF I/O
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * riff-media.h: RIFF-id to/from caps routines
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "riff-ids.h"
#include "riff-media.h"

/**
 * gst_riff_create_video_caps_with_data:
 * @codec_fcc: fourCC codec for this codec.
 * @strh: pointer to the strh stream header structure.
 * @strf: pointer to the strf stream header structure, including any
 *        data that is within the range of strf.size, but excluding any
 *        additional data withint this chunk but outside strf.size.
 * @strf_data: a #GstBuffer containing the additional data in the strf
 *             chunk outside reach of strf.size. Ususally a palette.
 * @strd_data: a #GstBuffer containing the data in the strd stream header
 *             chunk. Usually codec initialization data.
 * @codec_name: if given, will be filled with a human-readable codec name.
 */

GstCaps *
gst_riff_create_video_caps_with_data (guint32 codec_fcc,
    gst_riff_strh * strh, gst_riff_strf_vids * strf,
    GstBuffer * strf_data, GstBuffer * strd_data, char **codec_name)
{
  GstCaps *caps = NULL;
  GstBuffer *palette = NULL;

  switch (codec_fcc) {
    case GST_MAKE_FOURCC ('D', 'I', 'B', ' '):
      caps = gst_caps_new_simple ("video/x-raw-rgb",
          "bpp", G_TYPE_INT, 8,
          "depth", G_TYPE_INT, 8, "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);
      palette = strf_data;
      strf_data = NULL;
      if (codec_name)
        *codec_name = g_strdup ("Palettized 8-bit RGB");
      break;

    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, codec_fcc, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Uncompressed planar YUV 4:2:0");
      break;

    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, codec_fcc, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Uncompressed packed YUV 4:2:2");
      break;

    case GST_MAKE_FOURCC ('M', 'J', 'P', 'G'): /* YUY2 MJPEG */
      caps = gst_caps_new_simple ("image/jpeg", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Motion JPEG");
      break;

    case GST_MAKE_FOURCC ('J', 'P', 'E', 'G'): /* generic (mostly RGB) MJPEG */
      caps = gst_caps_new_simple ("image/jpeg", NULL);
      if (codec_name)
        *codec_name = g_strdup ("JPEG Still Image");
      break;

    case GST_MAKE_FOURCC ('P', 'I', 'X', 'L'): /* Miro/Pinnacle fourccs */
    case GST_MAKE_FOURCC ('V', 'I', 'X', 'L'): /* Miro/Pinnacle fourccs */
      caps = gst_caps_new_simple ("image/jpeg", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Miro/Pinnacle Motion JPEG Video");
      break;

    case GST_MAKE_FOURCC ('H', 'F', 'Y', 'U'):
      caps = gst_caps_new_simple ("video/x-huffyuv", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Huffman Lossless Codec");
      break;

    case GST_MAKE_FOURCC ('M', 'P', 'E', 'G'):
    case GST_MAKE_FOURCC ('M', 'P', 'G', 'I'):
    case GST_MAKE_FOURCC ('m', 'p', 'g', '1'):
    case GST_MAKE_FOURCC ('M', 'P', 'G', '1'):
      caps = gst_caps_new_simple ("video/mpeg",
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "mpegversion", G_TYPE_INT, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MPEG video");
      break;
    case GST_MAKE_FOURCC ('M', 'P', 'G', '2'):
    case GST_MAKE_FOURCC ('m', 'p', 'g', '2'):
      caps = gst_caps_new_simple ("video/mpeg",
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "mpegversion", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MPEG 2 video");
      break;

    case GST_MAKE_FOURCC ('H', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("ITU H.26n");
      break;

    case GST_MAKE_FOURCC ('i', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("ITU H.263");
      break;

    case GST_MAKE_FOURCC ('L', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Lead H.263");
      break;

    case GST_MAKE_FOURCC ('M', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft H.263");
      break;

    case GST_MAKE_FOURCC ('V', 'D', 'O', 'W'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("VDOLive");
      break;

    case GST_MAKE_FOURCC ('V', 'I', 'V', 'O'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Vivo H.263");
      break;

    case GST_MAKE_FOURCC ('x', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Xirlink H.263");
      break;

    case GST_MAKE_FOURCC ('D', 'I', 'V', '3'):
    case GST_MAKE_FOURCC ('d', 'i', 'v', '3'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', '4'):
    case GST_MAKE_FOURCC ('d', 'i', 'v', '4'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', '5'):
    case GST_MAKE_FOURCC ('d', 'i', 'v', '5'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', '6'):
    case GST_MAKE_FOURCC ('d', 'i', 'v', '6'):
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("DivX MS-MPEG-4 Version 3");
      break;

    case GST_MAKE_FOURCC ('d', 'i', 'v', 'x'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', 'X'):
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 4, NULL);
      if (codec_name)
        *codec_name = g_strdup ("DivX MPEG-4 Version 4");
      break;

    case GST_MAKE_FOURCC ('D', 'X', '5', '0'):
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 5, NULL);
      if (codec_name)
        *codec_name = g_strdup ("DivX MPEG-4 Version 5");
      break;

    case GST_MAKE_FOURCC ('X', 'V', 'I', 'D'):
    case GST_MAKE_FOURCC ('x', 'v', 'i', 'd'):
      caps = gst_caps_new_simple ("video/x-xvid", NULL);
      if (codec_name)
        *codec_name = g_strdup ("XVID MPEG-4");
      break;

    case GST_MAKE_FOURCC ('M', 'P', 'G', '4'):
    case GST_MAKE_FOURCC ('M', 'P', '4', 'S'):
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 41, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.1");
      break;

    case GST_MAKE_FOURCC ('m', 'p', '4', '2'):
    case GST_MAKE_FOURCC ('M', 'P', '4', '2'):
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 42, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.2");
      break;

    case GST_MAKE_FOURCC ('m', 'p', '4', '3'):
    case GST_MAKE_FOURCC ('M', 'P', '4', '3'):
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 43, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.3");
      break;

    case GST_MAKE_FOURCC ('3', 'I', 'V', '1'):
    case GST_MAKE_FOURCC ('3', 'I', 'V', '2'):
      caps = gst_caps_new_simple ("video/x-3ivx", NULL);
      if (codec_name)
        *codec_name = g_strdup ("3ivx");
      break;

    case GST_MAKE_FOURCC ('D', 'V', 'S', 'D'):
    case GST_MAKE_FOURCC ('d', 'v', 's', 'd'):
      caps = gst_caps_new_simple ("video/x-dv",
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Generic DV");
      break;

    case GST_MAKE_FOURCC ('W', 'M', 'V', '1'):
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft Windows Media 7 (WMV1)");
      break;

    case GST_MAKE_FOURCC ('W', 'M', 'V', '2'):
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft Windows Media 8 (WMV2)");
      break;

    case GST_MAKE_FOURCC ('W', 'M', 'V', '3'):
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft Windows Media 9 (WMV3)");
      break;

    case GST_MAKE_FOURCC ('c', 'v', 'i', 'd'):
      caps = gst_caps_new_simple ("video/x-cinepak", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Cinepak video");
      break;

    case GST_MAKE_FOURCC ('M', 'S', 'V', 'C'):
    case GST_MAKE_FOURCC ('m', 's', 'v', 'c'):
    case GST_MAKE_FOURCC ('C', 'R', 'A', 'M'):
    case GST_MAKE_FOURCC ('c', 'r', 'a', 'm'):
    case GST_MAKE_FOURCC ('W', 'H', 'A', 'M'):
    case GST_MAKE_FOURCC ('w', 'h', 'a', 'm'):
      caps = gst_caps_new_simple ("video/x-msvideocodec",
          "msvideoversion", G_TYPE_INT, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MS video v1");
      break;

    case GST_MAKE_FOURCC ('R', 'L', 'E', ' '):
    case GST_MAKE_FOURCC ('m', 'r', 'l', 'e'):
    case GST_MAKE_FOURCC (0x1, 0x0, 0x0, 0x0): /* why, why, why? */
      caps = gst_caps_new_simple ("video/x-rle",
          "layout", G_TYPE_STRING, "microsoft", NULL);
      palette = strf_data;
      strf_data = NULL;
      if (strf) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) strf->bit_cnt, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 1, 64, NULL);
      }
      if (codec_name)
        *codec_name = g_strdup ("Microsoft RLE");
      break;

    case GST_MAKE_FOURCC ('X', 'x', 'a', 'n'):
      caps = gst_caps_new_simple ("video/x-xan",
          "wcversion", G_TYPE_INT, 4, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Xan Wing Commander 4");
      break;

    case GST_MAKE_FOURCC ('I', 'V', '5', '0'):
      caps = gst_caps_new_simple ("video/x-intel",
          "ivversion", G_TYPE_INT, 5, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Intel Video 5");
      break;

    default:
      GST_WARNING ("Unknown video fourcc " GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (codec_fcc));
      return NULL;
  }

  if (strh != NULL) {
    gdouble fps = 1. * strh->rate / strh->scale;

    gst_caps_set_simple (caps, "framerate", G_TYPE_DOUBLE, fps, NULL);
  } else {
    gst_caps_set_simple (caps,
        "framerate", GST_TYPE_DOUBLE_RANGE, 0., G_MAXDOUBLE, NULL);
  }

  if (strf != NULL) {
    gst_caps_set_simple (caps,
        "width", G_TYPE_INT, strf->width,
        "height", G_TYPE_INT, strf->height, NULL);
  } else {
    gst_caps_set_simple (caps,
        "width", GST_TYPE_INT_RANGE, 16, 4096,
        "height", GST_TYPE_INT_RANGE, 16, 4096, NULL);
  }

  /* extradata */
  if (strf_data || strd_data) {
    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER,
        strf_data ? strf_data : strd_data, NULL);
  }

  /* palette */
  if (palette && GST_BUFFER_SIZE (palette) >= 256 * 4) {
    GstBuffer *copy = gst_buffer_copy (palette);

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    gint n;
    guint32 *data = (guint32 *) GST_BUFFER_DATA (copy);

    /* own endianness */
    for (n = 0; n < 256; n++)
      data[n] = GUINT32_FROM_LE (data[n]);
#endif
    gst_caps_set_simple (caps, "palette_data", GST_TYPE_BUFFER, copy, NULL);
    gst_buffer_unref (copy);
  }

  return caps;
}

GstCaps *
gst_riff_create_video_caps (guint32 codec_fcc,
    gst_riff_strh * strh, gst_riff_strf_vids * strf, char **codec_name)
{
  return gst_riff_create_video_caps_with_data (codec_fcc,
      strh, strf, NULL, NULL, codec_name);
}

GstCaps *
gst_riff_create_audio_caps_with_data (guint16 codec_id,
    gst_riff_strh * strh, gst_riff_strf_auds * strf,
    GstBuffer * strf_data, GstBuffer * strd_data, char **codec_name)
{
  gboolean block_align = FALSE, rate_chan = TRUE;
  GstCaps *caps = NULL;
  gint rate_min = 8000, rate_max = 96000;
  gint channels_max = 2;

  switch (codec_id) {
    case GST_RIFF_WAVE_FORMAT_MPEGL3:  /* mp3 */
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MPEG 1 layer 3");
      break;

    case GST_RIFF_WAVE_FORMAT_MPEGL12: /* mp1 or mp2 */
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MPEG 1 layer 2");
      break;

    case GST_RIFF_WAVE_FORMAT_PCM:     /* PCM */
      if (strf != NULL) {
        gint ba = strf->blockalign;
        gint ch = strf->channels;
        gint ws = strf->size;

        caps = gst_caps_new_simple ("audio/x-raw-int",
            "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
            "width", G_TYPE_INT, (int) (ba * 8 / ch),
            "depth", G_TYPE_INT, ws, "signed", G_TYPE_BOOLEAN, ws != 8, NULL);
      } else {
        caps = gst_caps_from_string ("audio/x-raw-int, "
            "endianness = (int) LITTLE_ENDIAN, "
            "signed = (boolean) { true, false }, "
            "width = (int) { 8, 16 }, " "depth = (int) { 8, 16 }");
      }
      if (codec_name)
        *codec_name = g_strdup ("Uncompressed PCM audio");
      break;

    case GST_RIFF_WAVE_FORMAT_ADPCM:
      caps = gst_caps_new_simple ("audio/x-adpcm",
          "layout", G_TYPE_STRING, "microsoft", NULL);
      if (codec_name)
        *codec_name = g_strdup ("ADPCM audio");
      block_align = TRUE;
      break;

    case GST_RIFF_WAVE_FORMAT_DVI_ADPCM:
      caps = gst_caps_new_simple ("audio/x-adpcm",
          "layout", G_TYPE_STRING, "dvi", NULL);
      if (codec_name)
        *codec_name = g_strdup ("DVI ADPCM audio");
      block_align = TRUE;
      break;

    case GST_RIFF_WAVE_FORMAT_MULAW:
      if (strf != NULL && strf->size != 8) {
        GST_WARNING ("invalid depth (%d) of mulaw audio, overwriting.",
            strf->size);
      }
      caps = gst_caps_new_simple ("audio/x-mulaw", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Mulaw");
      break;

    case GST_RIFF_WAVE_FORMAT_ALAW:
      if (strf != NULL && strf->size != 8) {
        GST_WARNING ("invalid depth (%d) of alaw audio, overwriting.",
            strf->size);
      }
      caps = gst_caps_new_simple ("audio/x-alaw", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Alaw");
      break;

    case GST_RIFF_WAVE_FORMAT_VORBIS1: /* ogg/vorbis mode 1 */
    case GST_RIFF_WAVE_FORMAT_VORBIS2: /* ogg/vorbis mode 2 */
    case GST_RIFF_WAVE_FORMAT_VORBIS3: /* ogg/vorbis mode 3 */
    case GST_RIFF_WAVE_FORMAT_VORBIS1PLUS:     /* ogg/vorbis mode 1+ */
    case GST_RIFF_WAVE_FORMAT_VORBIS2PLUS:     /* ogg/vorbis mode 2+ */
    case GST_RIFF_WAVE_FORMAT_VORBIS3PLUS:     /* ogg/vorbis mode 3+ */
      caps = gst_caps_new_simple ("audio/x-vorbis", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Vorbis");
      break;

    case GST_RIFF_WAVE_FORMAT_A52:
      channels_max = 6;
      caps = gst_caps_new_simple ("audio/x-ac3", NULL);
      if (codec_name)
        *codec_name = g_strdup ("AC3");
      break;
    case GST_RIFF_WAVE_FORMAT_WMAV1:
    case GST_RIFF_WAVE_FORMAT_WMAV2:
    case GST_RIFF_WAVE_FORMAT_WMAV3:
    {
      gint version = (codec_id - GST_RIFF_WAVE_FORMAT_WMAV1) + 1;

      channels_max = 6;

      block_align = TRUE;

      caps = gst_caps_new_simple ("audio/x-wma",
          "wmaversion", G_TYPE_INT, version, NULL);

      if (codec_name)
        *codec_name = g_strdup_printf ("WMA Version %d", version);

      if (strf != NULL) {
        gst_caps_set_simple (caps,
            "bitrate", G_TYPE_INT, strf->av_bps * 8, NULL);
      } else {
        gst_caps_set_simple (caps,
            "bitrate", GST_TYPE_INT_RANGE, 0, G_MAXINT, NULL);
      }
      break;
    }
    case GST_RIFF_WAVE_FORMAT_SONY_ATRAC3:
      caps = gst_caps_new_simple ("audio/x-vnd.sony.atrac3", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Sony ATRAC3");
      break;

    default:
      GST_WARNING ("Unknown audio tag 0x%04x", codec_id);
      return NULL;
  }

  if (strf != NULL) {
    if (rate_chan) {
      gst_caps_set_simple (caps,
          "rate", G_TYPE_INT, strf->rate,
          "channels", G_TYPE_INT, strf->channels, NULL);
    }
    if (block_align) {
      gst_caps_set_simple (caps,
          "block_align", G_TYPE_INT, strf->blockalign, NULL);
    }
  } else {
    if (rate_chan) {
      gst_caps_set_simple (caps,
          "rate", GST_TYPE_INT_RANGE, rate_min, rate_max,
          "channels", GST_TYPE_INT_RANGE, 1, channels_max, NULL);
    }
    if (block_align) {
      gst_caps_set_simple (caps,
          "block_align", GST_TYPE_INT_RANGE, 1, 8192, NULL);
    }
  }

  /* extradata */
  if (strf_data || strd_data) {
    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER,
        strf_data ? strf_data : strd_data, NULL);
  }

  return caps;
}

GstCaps *
gst_riff_create_audio_caps (guint16 codec_id,
    gst_riff_strh * strh, gst_riff_strf_auds * strf, char **codec_name)
{
  return gst_riff_create_audio_caps_with_data (codec_id,
      strh, strf, NULL, NULL, codec_name);
}

GstCaps *
gst_riff_create_iavs_caps (guint32 codec_fcc,
    gst_riff_strh * strh, gst_riff_strf_iavs * strf, char **codec_name)
{
  GstCaps *caps = NULL;

  switch (codec_fcc) {
      /* is this correct? */
    case GST_MAKE_FOURCC ('D', 'V', 'S', 'D'):
    case GST_MAKE_FOURCC ('d', 'v', 's', 'd'):
      caps = gst_caps_new_simple ("video/x-dv",
          "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Generic DV");
      break;

    default:
      GST_WARNING ("Unknown IAVS fourcc " GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (codec_fcc));
      return NULL;
  }

  return caps;
}

/*
 * Functions below are for template caps. All is variable.
 */

GstCaps *
gst_riff_create_video_template_caps (void)
{
  guint32 tags[] = {
    GST_MAKE_FOURCC ('I', '4', '2', '0'),
    GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
    GST_MAKE_FOURCC ('M', 'J', 'P', 'G'),
    GST_MAKE_FOURCC ('D', 'V', 'S', 'D'),
    GST_MAKE_FOURCC ('W', 'M', 'V', '1'),
    GST_MAKE_FOURCC ('W', 'M', 'V', '2'),
    GST_MAKE_FOURCC ('W', 'M', 'V', '3'),
    GST_MAKE_FOURCC ('M', 'P', 'G', '4'),
    GST_MAKE_FOURCC ('M', 'P', '4', '2'),
    GST_MAKE_FOURCC ('M', 'P', '4', '3'),
    GST_MAKE_FOURCC ('H', 'F', 'Y', 'U'),
    GST_MAKE_FOURCC ('D', 'I', 'V', '3'),
    GST_MAKE_FOURCC ('M', 'P', 'E', 'G'),
    GST_MAKE_FOURCC ('H', '2', '6', '3'),
    GST_MAKE_FOURCC ('D', 'I', 'V', 'X'),
    GST_MAKE_FOURCC ('D', 'X', '5', '0'),
    GST_MAKE_FOURCC ('X', 'V', 'I', 'D'),
    GST_MAKE_FOURCC ('3', 'I', 'V', '1'),
    GST_MAKE_FOURCC ('c', 'v', 'i', 'd'),
    GST_MAKE_FOURCC ('m', 's', 'v', 'c'),
    GST_MAKE_FOURCC ('R', 'L', 'E', ' '),
    GST_MAKE_FOURCC ('D', 'I', 'B', ' '),
    GST_MAKE_FOURCC ('X', 'x', 'a', 'n'),
    GST_MAKE_FOURCC ('I', 'V', '5', '0'),
    /* FILL ME */
    0
  };
  guint i;
  GstCaps *caps, *one;

  caps = gst_caps_new_empty ();
  for (i = 0; tags[i] != 0; i++) {
    one = gst_riff_create_video_caps (tags[i], NULL, NULL, NULL);
    if (one)
      gst_caps_append (caps, one);
  }

  return caps;
}

GstCaps *
gst_riff_create_audio_template_caps (void)
{
  guint16 tags[] = {
    GST_RIFF_WAVE_FORMAT_MPEGL3,
    GST_RIFF_WAVE_FORMAT_MPEGL12,
    GST_RIFF_WAVE_FORMAT_PCM,
    GST_RIFF_WAVE_FORMAT_VORBIS1,
    GST_RIFF_WAVE_FORMAT_A52,
    GST_RIFF_WAVE_FORMAT_ALAW,
    GST_RIFF_WAVE_FORMAT_MULAW,
    GST_RIFF_WAVE_FORMAT_ADPCM,
    GST_RIFF_WAVE_FORMAT_DVI_ADPCM,
    GST_RIFF_WAVE_FORMAT_WMAV1,
    GST_RIFF_WAVE_FORMAT_WMAV2,
    GST_RIFF_WAVE_FORMAT_WMAV3,
    /* FILL ME */
    0
  };
  guint i;
  GstCaps *caps, *one;

  caps = gst_caps_new_empty ();
  for (i = 0; tags[i] != 0; i++) {
    one = gst_riff_create_audio_caps (tags[i], NULL, NULL, NULL);
    if (one)
      gst_caps_append (caps, one);
  }

  return caps;
}

GstCaps *
gst_riff_create_iavs_template_caps (void)
{
  guint32 tags[] = {
    GST_MAKE_FOURCC ('D', 'V', 'S', 'D'),
    /* FILL ME */
    0
  };
  guint i;
  GstCaps *caps, *one;

  caps = gst_caps_new_empty ();
  for (i = 0; tags[i] != 0; i++) {
    one = gst_riff_create_iavs_caps (tags[i], NULL, NULL, NULL);
    if (one)
      gst_caps_append (caps, one);
  }

  return caps;
}
