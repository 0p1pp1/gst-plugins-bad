/*
 * GStreamer Plugins
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

#ifndef __GST_VULKAN_FULL_SCREEN_QUAD_H__
#define __GST_VULKAN_FULL_SCREEN_QUAD_H__


#include <gst/gst.h>

#include <gst/vulkan/vulkan.h>

G_BEGIN_DECLS

GType gst_vulkan_full_screen_quad_get_type (void);
#define GST_TYPE_VULKAN_FULL_SCREEN_QUAD            (gst_vulkan_full_screen_quad_get_type ())
#define GST_VULKAN_FULL_SCREEN_QUAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VULKAN_FULL_SCREEN_QUAD, GstVulkanFullScreenQuad))
#define GST_VULKAN_FULL_SCREEN_QUAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VULKAN_FULL_SCREEN_QUAD, GstVulkanFullScreenQuadClass))
#define GST_IS_VULKAN_FULL_SCREEN_QUAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VULKAN_FULL_SCREEN_QUAD))
#define GST_IS_VULKAN_FULL_SCREEN_QUAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VULKAN_FULL_SCREEN_QUAD))
#define GST_VULKAN_FULL_SCREEN_QUAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VULKAN_FULL_SCREEN_QUAD, GstVulkanFullScreenQuadClass))

/* XXX: privatise this on moving to lib */
struct Vertex
{
  float x, y, z;
  float s, t;
};

typedef struct _GstVulkanFullScreenQuad GstVulkanFullScreenQuad;
typedef struct _GstVulkanFullScreenQuadClass GstVulkanFullScreenQuadClass;
typedef struct _GstVulkanFullScreenQuadPrivate GstVulkanFullScreenQuadPrivate;

struct _GstVulkanFullScreenQuad
{
  GstObject                         parent;

  GstVideoInfo                      out_info;
  GstVideoInfo                      in_info;

  GstVulkanQueue                   *queue;

  GstVulkanHandle                  *render_pass;
  GstVulkanHandle                  *pipeline_layout;
  GstVulkanHandle                  *graphics_pipeline;
  GstVulkanHandle                  *descriptor_set_layout;
  GstVulkanDescriptorCache         *descriptor_cache;
  GstVulkanDescriptorSet           *descriptor_set;
  GstVulkanHandle                  *framebuffer;
  GstVulkanHandle                  *sampler;

  GstVulkanCommandPool             *cmd_pool;

  GstVulkanTrashList               *trash_list;
  GstVulkanFence                   *last_fence;
};

struct _GstVulkanFullScreenQuadClass
{
  GstObjectClass                    parent_class;
};

GstVulkanFullScreenQuad *   gst_vulkan_full_screen_quad_new         (GstVulkanQueue * queue);

gboolean            gst_vulkan_full_screen_quad_set_info            (GstVulkanFullScreenQuad * self, GstVideoInfo *in_info, GstVideoInfo * out_info);
gboolean            gst_vulkan_full_screen_quad_set_shaders         (GstVulkanFullScreenQuad * self, GstVulkanHandle * vert, GstVulkanHandle * frag);
gboolean            gst_vulkan_full_screen_quad_set_uniform_buffer  (GstVulkanFullScreenQuad * self, GstMemory * uniforms, GError ** error);

gboolean            gst_vulkan_full_screen_quad_set_input_buffer    (GstVulkanFullScreenQuad * self, GstBuffer * buffer, GError ** error);
gboolean            gst_vulkan_full_screen_quad_set_output_buffer   (GstVulkanFullScreenQuad * self, GstBuffer * buffer, GError ** error);

gboolean            gst_vulkan_full_screen_quad_prepare_draw        (GstVulkanFullScreenQuad * self, GstVulkanFence * fence, GError ** error);
gboolean            gst_vulkan_full_screen_quad_fill_command_buffer (GstVulkanFullScreenQuad * self, GstVulkanCommandBuffer * cmd, GstVulkanFence * fence, GError ** error);
gboolean            gst_vulkan_full_screen_quad_submit              (GstVulkanFullScreenQuad * self, GstVulkanCommandBuffer * cmd, GstVulkanFence * fence, GError ** error);
gboolean            gst_vulkan_full_screen_quad_draw                (GstVulkanFullScreenQuad * self, GError ** error);

GstVulkanFence *    gst_vulkan_full_screen_quad_get_last_fence      (GstVulkanFullScreenQuad * self);

G_END_DECLS
#endif /* __GST_VULKAN_FULL_SCREEN_QUAD_H__ */
