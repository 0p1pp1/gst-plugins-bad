/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

#include "metadataparsejpeg.h"

#include <string.h>

#include <libiptcdata/iptc-jpeg.h>

static int
metadataparse_jpeg_reading (JpegData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);


static int
metadataparse_jpeg_hold_chunk (JpegData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start,
    guint32 * next_size, GstAdapter ** adapter);

static int
metadataparse_jpeg_exif (JpegData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

static int
metadataparse_jpeg_iptc (JpegData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

static int
metadataparse_jpeg_xmp (JpegData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

static int
metadataparse_jpeg_jump (JpegData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

#define READ(buf, size) ( (size)--, *((buf)++) )

void
metadataparse_jpeg_init (JpegData * jpeg_data, GstAdapter ** adpt_exif,
    GstAdapter ** adpt_iptc, GstAdapter ** adpt_xmp)
{
  jpeg_data->state = JPEG_NULL;
  jpeg_data->adpt_exif = adpt_exif;
  jpeg_data->adpt_iptc = adpt_iptc;
  jpeg_data->adpt_xmp = adpt_xmp;
  jpeg_data->read = 0;

  metadataparse_xmp_init ();
}

void
metadataparse_jpeg_dispose (JpegData * jpeg_data)
{
  metadataparse_xmp_dispose ();

  jpeg_data->adpt_exif = NULL;
  jpeg_data->adpt_iptc = NULL;
  jpeg_data->adpt_xmp = NULL;
}

int
metadataparse_jpeg_parse (JpegData * jpeg_data, guint8 * buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{

  int ret = 0;
  guint8 mark[2] = { 0x00, 0x00 };

  *next_start = buf;

  if (jpeg_data->state == JPEG_NULL) {

    if (*bufsize < 2) {
      *next_size = (buf - *next_start) + 2;
      ret = 1;
      goto done;
    }

    mark[0] = READ (buf, *bufsize);
    mark[1] = READ (buf, *bufsize);

    if (mark[0] != 0xFF || mark[1] != 0xD8) {
      ret = -1;
      goto done;
    }

    jpeg_data->state = JPEG_READING;

  }

  while (ret == 0) {
    switch (jpeg_data->state) {
      case JPEG_READING:
        ret =
            metadataparse_jpeg_reading (jpeg_data, &buf, bufsize, next_start,
            next_size);
        break;
      case JPEG_JUMPING:
        ret =
            metadataparse_jpeg_jump (jpeg_data, &buf, bufsize, next_start,
            next_size);
        break;
      case JPEG_EXIF:
        ret =
            metadataparse_jpeg_exif (jpeg_data, &buf, bufsize, next_start,
            next_size);
        break;
      case JPEG_IPTC:
        ret =
            metadataparse_jpeg_iptc (jpeg_data, &buf, bufsize, next_start,
            next_size);
        break;
      case JPEG_XMP:
        ret =
            metadataparse_jpeg_xmp (jpeg_data, &buf, bufsize, next_start,
            next_size);
        break;
      case JPEG_DONE:
        goto done;
        break;
      default:
        ret = -1;
        break;
    }
  }

done:

  return ret;

}


/* look for markers */
static int
metadataparse_jpeg_reading (JpegData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{

  int ret = -1;
  guint8 mark[2] = { 0x00, 0x00 };
  guint16 chunk_size = 0;

  static const unsigned char ExifHeader[] =
      { 0x45, 0x78, 0x69, 0x66, 0x00, 0x00 };
  static const char IptcHeader[] = "Photoshop 3.0";
  static const char XmpHeader[] = "http://ns.adobe.com/xap/1.0/";

  *next_start = *buf;

  if (*bufsize < 2) {
    *next_size = (*buf - *next_start) + 2;
    ret = 1;
    goto done;
  }

  mark[0] = READ (*buf, *bufsize);
  mark[1] = READ (*buf, *bufsize);

  if (mark[0] == 0xFF) {
    if (mark[1] == 0xD9) {      /* end of image */
      ret = 0;
      jpeg_data->state = JPEG_DONE;
      goto done;
    } else if (mark[1] == 0xDA) {       /* start of scan, lets not look behinf of this */
      ret = 0;
      jpeg_data->state = JPEG_DONE;
      goto done;
    }

    if (*bufsize < 2) {
      *next_size = (*buf - *next_start) + 2;
      ret = 1;
      goto done;
    }

    chunk_size = READ (*buf, *bufsize) << 8;
    chunk_size += READ (*buf, *bufsize);

    if (mark[1] == 0xE1) {      /* may be it is Exif or XMP */

      if (chunk_size >= 8) {    /* size2 'EXIF' 0x00 0x00 */
        guint8 ch;

        if (*bufsize < 6) {
          *next_size = (*buf - *next_start) + 6;
          ret = 1;
          goto done;
        }

        if (jpeg_data->adpt_exif) {
          if (0 == memcmp (ExifHeader, *buf, 6)) {
            jpeg_data->read = chunk_size - 2;
            ret = 0;
            jpeg_data->state = JPEG_EXIF;
            goto done;
          }
        }
        if (chunk_size >= 31) { /* size2 "http://ns.adobe.com/xap/1.0/" */
          if (*bufsize < 29) {
            *next_size = (*buf - *next_start) + 29;
            ret = 1;
            goto done;
          }

          if (jpeg_data->adpt_xmp) {
            if (0 == memcmp (XmpHeader, *buf, 29)) {
              jpeg_data->read = chunk_size - 2;
              ret = 0;
              jpeg_data->state = JPEG_XMP;
              goto done;
            }
          }
        }
      }
    } else if (mark[1] == 0xED) {       /* may be it is photoshop and may be there is iptc */
      if (chunk_size >= 16) {   /* size2 "Photoshop 3.0" */

        if (*bufsize < 14) {
          *next_size = (*buf - *next_start) + 14;
          ret = 1;
          goto done;
        }

        if (jpeg_data->adpt_iptc) {
          if (0 == memcmp (IptcHeader, *buf, 14)) {
            jpeg_data->read = chunk_size - 2;
            ret = 0;
            jpeg_data->state = JPEG_IPTC;
            goto done;
          }
        }
      }
    }

    /* just set jump sise  */
    jpeg_data->read = chunk_size - 2;
    jpeg_data->state = JPEG_JUMPING;
    ret = 0;

  } else {
    /* invalid JPEG chunk */
    ret = -1;
  }


done:

  return ret;


}

static int
metadataparse_jpeg_hold_chunk (JpegData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start,
    guint32 * next_size, GstAdapter ** adapter)
{
  int ret;

  if (jpeg_data->read > *bufsize) {
    *next_start = *buf;
    *next_size = jpeg_data->read;
    ret = 1;
  } else {
    GstBuffer *gst_buf;

    if (NULL == *adapter) {
      *adapter = gst_adapter_new ();
    }
    gst_buf = gst_buffer_new_and_alloc (jpeg_data->read);
    memcpy (GST_BUFFER_DATA (gst_buf), *buf, jpeg_data->read);
    gst_adapter_push (*adapter, gst_buf);

    *next_start = *buf + jpeg_data->read;
    *buf += jpeg_data->read;
    *bufsize -= jpeg_data->read;
    jpeg_data->state = JPEG_READING;
    ret = 0;
  }

  return ret;
}

static int
metadataparse_jpeg_exif (JpegData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{

  return metadataparse_jpeg_hold_chunk (jpeg_data, buf,
      bufsize, next_start, next_size, jpeg_data->adpt_exif);

}

static int
metadataparse_jpeg_iptc (JpegData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{

  int ret;

  ret = metadataparse_jpeg_hold_chunk (jpeg_data, buf,
      bufsize, next_start, next_size, jpeg_data->adpt_iptc);


  if (ret == 0) {

    const guint8 *buf;
    guint32 size;
    unsigned int iptc_len;
    int res;

    size = gst_adapter_available (*jpeg_data->adpt_iptc);
    buf = gst_adapter_peek (*jpeg_data->adpt_iptc, size);

    res = iptc_jpeg_ps3_find_iptc (buf, size, &iptc_len);

    if (res < 0) {
      /* error */
      ret = -1;
    } else if (res == 0) {
      /* no iptc data found */
      gst_adapter_clear (*jpeg_data->adpt_iptc);
    } else {
      gst_adapter_flush (*jpeg_data->adpt_iptc, res);
      size = gst_adapter_available (*jpeg_data->adpt_iptc);
      if (size > iptc_len) {
        GstBuffer *buf;

        buf = gst_adapter_take_buffer (*jpeg_data->adpt_iptc, iptc_len);
        gst_adapter_clear (*jpeg_data->adpt_iptc);
        gst_adapter_push (*jpeg_data->adpt_iptc, buf);
      }
    }

  }

  return ret;

}

static int
metadataparse_jpeg_xmp (JpegData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{

  return metadataparse_jpeg_hold_chunk (jpeg_data, buf,
      bufsize, next_start, next_size, jpeg_data->adpt_xmp);

}

static int
metadataparse_jpeg_jump (JpegData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{
  int ret;

  if (jpeg_data->read > *bufsize) {
    jpeg_data->read -= *bufsize;
    *next_size = 2;
    *next_start = *buf + *bufsize + jpeg_data->read;
    jpeg_data->read = 0;
    *bufsize = 0;
    jpeg_data->state = JPEG_READING;
    ret = 1;
  } else {
    *next_start = *buf + jpeg_data->read;
    *buf += jpeg_data->read;
    *bufsize -= jpeg_data->read;
    jpeg_data->state = JPEG_READING;
    ret = 0;
  }
  return ret;
}
