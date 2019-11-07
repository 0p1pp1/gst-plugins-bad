/*
 * GStreamer
 * Copyright (C) 2019 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_VULKAN_PHYSICAL_DEVICE_H__
#define __GST_VULKAN_PHYSICAL_DEVICE_H__

#include <gst/vulkan/gstvkinstance.h>
#include <gst/vulkan/gstvkqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_VULKAN_PHYSICAL_DEVICE         (gst_vulkan_physical_device_get_type())
#define GST_VULKAN_PHYSICAL_DEVICE(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_VULKAN_PHYSICAL_DEVICE, GstVulkanPhysicalDevice))
#define GST_VULKAN_PHYSICAL_DEVICE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GST_TYPE_VULKAN_PHYSICAL_DEVICE, GstVulkanPhysicalDeviceClass))
#define GST_IS_VULKAN_PHYSICAL_DEVICE(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_VULKAN_PHYSICAL_DEVICE))
#define GST_IS_VULKAN_PHYSICAL_DEVICE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_VULKAN_PHYSICAL_DEVICE))
#define GST_VULKAN_PHYSICAL_DEVICE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_VULKAN_PHYSICAL_DEVICE, GstVulkanPhysicalDeviceClass))
GST_VULKAN_API
GType gst_vulkan_physical_device_get_type       (void);

struct _GstVulkanPhysicalDevice
{
  GstObject parent;

  GstVulkanInstance *instance;

  guint device_index;
  VkPhysicalDevice device; /* hides a pointer */

  VkLayerProperties *device_layers;
  guint32 n_device_layers;

  VkExtensionProperties *device_extensions;
  guint32 n_device_extensions;

  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceFeatures features;
  VkPhysicalDeviceMemoryProperties memory_properties;

  VkQueueFamilyProperties *queue_family_props;
  guint32 n_queue_families;
};

struct _GstVulkanPhysicalDeviceClass
{
  GstObjectClass parent_class;
};

GST_VULKAN_API
GstVulkanPhysicalDevice *   gst_vulkan_physical_device_new              (GstVulkanInstance * instance, guint device_index);
GST_VULKAN_API
GstVulkanInstance *         gst_vulkan_physical_device_get_instance     (GstVulkanPhysicalDevice * device);

GST_VULKAN_API
VkPhysicalDevice            gst_vulkan_physical_device_get_handle       (GstVulkanPhysicalDevice * device);

G_END_DECLS

#endif /* __GST_VULKAN_PHYSICAL_DEVICE_H__ */
