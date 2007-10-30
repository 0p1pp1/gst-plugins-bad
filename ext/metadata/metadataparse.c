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

#include "metadataparse.h"

#include "metadataparsejpeg.h"


/*
 *static declarations
 */

static int
metadataparse_parse_none (ParseData * parse_data, const guint8 * buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

/*
 * extern implementation
 */

void
metadataparse_init (ParseData * parse_data)
{
  parse_data->state = STATE_NULL;
  parse_data->img_type = IMG_NONE;
  parse_data->option = PARSE_OPT_ALL;
  parse_data->adpt_exif = NULL;
  parse_data->adpt_iptc = NULL;
  parse_data->adpt_xmp = NULL;
}

/*
 * offset: number of bytes to jump (just a hint to jump a chunk)
 * size: number of bytes to read on next call (just a hint to get all chunk)
 * return:
 *   -1 -> error
 *    0 -> done
 *    1 -> need more data
 */
int
metadataparse_parse (ParseData * parse_data, const guint8 * buf,
    guint32 bufsize, guint32 * next_offset, guint32 * next_size)
{

  int ret = 0;

  guint8 *next_start = (guint8 *) buf;

  if (parse_data->state == STATE_NULL) {
    ret =
        metadataparse_parse_none (parse_data, buf, &bufsize, &next_start,
        next_size);
    if (ret == 0)
      parse_data->state = STATE_READING;
    else
      goto done;
  }

  switch (parse_data->img_type) {
    case IMG_JPEG:
      ret =
          metadataparse_jpeg_parse (&parse_data->format_data.jpeg,
          (guint8 *) buf, &bufsize, &next_start, next_size);
      break;
    case IMG_PNG:
      ret = 0;
      break;
    default:
      /* unexpected */
      ret = -1;
      goto done;
      break;
  }

  *next_offset = next_start - buf;

done:

  if (ret == 0) {
    parse_data->state = STATE_DONE;
  }

  return ret;
}

void
metadataparse_dispose (ParseData * parse_data)
{

  switch (parse_data->img_type) {
    case IMG_JPEG:
      metadataparse_jpeg_dispose (&parse_data->format_data.jpeg);
      break;
  }

  if (parse_data->adpt_xmp) {
    gst_object_unref (parse_data->adpt_xmp);
    parse_data->adpt_xmp = NULL;
  }

  if (parse_data->adpt_iptc) {
    gst_object_unref (parse_data->adpt_iptc);
    parse_data->adpt_iptc = NULL;
  }

  if (parse_data->adpt_exif) {
    gst_object_unref (parse_data->adpt_exif);
    parse_data->adpt_exif = NULL;
  }

}

/*
 * static implementation
 */

/* find image type */
static int
metadataparse_parse_none (ParseData * parse_data, const guint8 * buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{

  int ret = -1;
  GstAdapter **adpt_exif = NULL;
  GstAdapter **adpt_iptc = NULL;
  GstAdapter **adpt_xmp = NULL;

  *next_start = buf;

  parse_data->img_type = IMG_NONE;

  if (*bufsize < 4) {
    *next_size = 4;
    ret = 1;
    goto done;
  }

  ret = 0;
  if (parse_data->option & PARSE_OPT_EXIF)
    adpt_exif = &parse_data->adpt_exif;
  if (parse_data->option & PARSE_OPT_IPTC)
    adpt_iptc = &parse_data->adpt_iptc;
  if (parse_data->option & PARSE_OPT_XMP)
    adpt_xmp = &parse_data->adpt_xmp;

  if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
    metadataparse_jpeg_init (&parse_data->format_data.jpeg, adpt_exif,
        adpt_iptc, adpt_xmp);
    parse_data->img_type = IMG_JPEG;
  } else if (buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4e
      && buf[3] == 0x47) {
    parse_data->img_type = IMG_PNG;
  } else
    ret = -1;

done:

  return ret;
}
