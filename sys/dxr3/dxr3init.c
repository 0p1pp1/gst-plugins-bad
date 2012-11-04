/* GStreamer
 * Copyright (C) 2003 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * dxr3init.c: DXR3 plugin initialization.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst/gst-i18n-plugin.h"

#include "dxr3videosink.h"
#include "dxr3spusink.h"
#include "dxr3audiosink.h"


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "dxr3videosink",
          GST_RANK_NONE, GST_TYPE_DXR3VIDEOSINK) ||
      !gst_element_register (plugin, "dxr3audiosink",
          GST_RANK_NONE, GST_TYPE_DXR3AUDIOSINK) ||
      !gst_element_register (plugin, "dxr3spusink",
          GST_RANK_NONE, GST_TYPE_DXR3SPUSINK))
    return FALSE;

#ifdef ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
#endif /* ENABLE_NLS */

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dxr3,
    "dxr3 mpeg video board elements",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
