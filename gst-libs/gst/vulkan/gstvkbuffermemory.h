/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_VULKAN_BUFFER_MEMORY_H__
#define __GST_VULKAN_BUFFER_MEMORY_H__

#include <gst/gst.h>
#include <gst/gstallocator.h>
#include <gst/gstmemory.h>

#include <gst/vulkan/gstvkbarrier.h>
#include <gst/vulkan/vulkan.h>

G_BEGIN_DECLS

#define GST_TYPE_VULKAN_BUFFER_MEMORY_ALLOCATOR (gst_vulkan_buffer_memory_allocator_get_type())
GST_VULKAN_API
GType gst_vulkan_buffer_memory_allocator_get_type(void);

#define GST_IS_VULKAN_BUFFER_MEMORY_ALLOCATOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VULKAN_BUFFER_MEMORY_ALLOCATOR))
#define GST_IS_VULKAN_BUFFER_MEMORY_ALLOCATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VULKAN_BUFFER_MEMORY_ALLOCATOR))
#define GST_VULKAN_BUFFER_MEMORY_ALLOCATOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VULKAN_MEMORY_ALLOCATOR, GstVulkanBufferMemoryAllocatorClass))
#define GST_VULKAN_BUFFER_MEMORY_ALLOCATOR(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VULKAN_MEMORY_ALLOCATOR, GstVulkanBufferMemoryAllocator))
#define GST_VULKAN_BUFFER_MEMORY_ALLOCATOR_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VULKAN_MEMORY_ALLOCATOR, GstVulkanBufferMemoryAllocatorClass))
#define GST_VULKAN_BUFFER_MEMORY_ALLOCATOR_CAST(obj)            ((GstVulkanBufferMemoryAllocator *)(obj))

#define GST_VULKAN_BUFFER_MEMORY_ALLOCATOR_NAME "VulkanBuffer"
#define GST_CAPS_FEATURE_MEMORY_VULKAN_BUFFER "memory:VulkanBuffer"


struct _GstVulkanBarrierBufferInfo
{
  GstVulkanBarrierMemoryInfo parent;

  VkDeviceSize offset;
  VkDeviceSize size;
};

struct _GstVulkanBufferMemory
{
  GstMemory parent;

  GstVulkanDevice * device;

  VkBuffer buffer;
  VkBufferView view;
  GstVulkanMemory *vk_mem;

  VkMemoryRequirements requirements;
  VkBufferUsageFlags usage;

  GstVulkanBarrierBufferInfo barrier;

  GMutex lock;
  gboolean wrapped;
  GDestroyNotify notify;
  gpointer user_data;
};

/**
 * GstVulkanBufferMemoryAllocator
 *
 * Opaque #GstVulkanBufferMemoryAllocator struct
 */
struct _GstVulkanBufferMemoryAllocator
{
  GstAllocator parent;
};

/**
 * GstVulkanBufferMemoryAllocatorClass:
 *
 * The #GstVulkanBufferMemoryAllocatorClass only contains private data
 */
struct _GstVulkanBufferMemoryAllocatorClass
{
  GstAllocatorClass parent_class;
};

GST_VULKAN_API
void            gst_vulkan_buffer_memory_init_once       (void);
GST_VULKAN_API
gboolean        gst_is_vulkan_buffer_memory              (GstMemory * mem);

GST_VULKAN_API
GstMemory *     gst_vulkan_buffer_memory_alloc           (GstVulkanDevice * device,
                                                         VkFormat format,
                                                         gsize size,
                                                         VkBufferUsageFlags usage,
                                                         VkMemoryPropertyFlags mem_prop_flags);

GST_VULKAN_API
GstMemory *     gst_vulkan_buffer_memory_wrapped         (GstVulkanDevice * device,
                                                         VkBuffer buffer,
                                                         VkFormat format,
                                                         VkBufferUsageFlags usage,
                                                         gpointer user_data,
                                                         GDestroyNotify notify);

G_END_DECLS

#endif /* __GST_VULKAN_BUFFER_MEMORY_H__ */
