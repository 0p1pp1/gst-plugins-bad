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
#ifndef __MODPLUG_TYPES_H__
#define __MODPLUG_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

gboolean MOD_CheckType (GstBuffer *buf);
gboolean Mod_669_CheckType (GstBuffer *buf);
gboolean Amf_CheckType (GstBuffer *buf);
gboolean Dsm_CheckType (GstBuffer *buf);
gboolean Fam_CheckType (GstBuffer *buf);
gboolean Gdm_CheckType (GstBuffer *buf);
gboolean Imf_CheckType (GstBuffer *buf);
gboolean It_CheckType (GstBuffer *buf);
gboolean M15_CheckType (GstBuffer *buf);
gboolean Mtm_CheckType (GstBuffer *buf);
gboolean Okt_CheckType (GstBuffer *buf);
gboolean S3m_CheckType (GstBuffer *buf);
gboolean Xm_CheckType (GstBuffer *buf);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MODPLUG_TYPES_H__ */
