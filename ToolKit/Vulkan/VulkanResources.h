/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../IGraphicsBackend.h"
#include "../Types.h"

#include <vulkan/vulkan.h>

struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

namespace ToolKit
{

  class VulkanContext;
  class Texture;
  class Framebuffer;

  /**
   * Maps ToolKit GraphicTypes format enum to a concrete VkFormat. Returns VK_FORMAT_UNDEFINED for
   * formats not yet supported by the Vulkan backend � caller should assert/skip.
   */
  VkFormat ToVkFormat(GraphicTypes format);

  /** Returns true for depth or depth-stencil formats. */
  bool IsDepthFormat(VkFormat format);

  /**
   * Backend GPU data for a Texture / RenderTarget / DepthTexture.
   * Stored on Texture::m_gpuData as GpuResourceDataPtr (shared_ptr<GpuResourceData>).
   *
   * Currently supports the minimal subset needed by Stage 1f (color 2D render targets + depth 2D,
   * MSAA x1). Cubemap / 2DArray / mip-mapped textures will be extended in later stages.
   */
  struct VulkanTexture : public GpuResourceData
  {
    VulkanContext* context  = nullptr;

    VkImage image           = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view        = VK_NULL_HANDLE;
    VkSampler sampler       = VK_NULL_HANDLE;

    VkFormat format         = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = 0;
    VkExtent2D extent       = {0, 0};
    uint32_t arrayLayers    = 1; //!< 6 for cubemaps, >1 for 2D arrays.
    uint32_t mipLevels      = 1;
    bool isCubemap          = false;

    /** Last layout we transitioned the image to. Drives pipeline barriers + render pass
        initialLayout selection. */
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    ~VulkanTexture() override;
  };

  /**
   * Backend GPU data for a Framebuffer.
   * Stored on Framebuffer::m_gpuData.
   *
   * The VkRenderPass + VkFramebuffer are built lazily on first BeginPass after the attachment
   * set / size stabilizes. Any Attach/Detach call flips @ref dirty so the next BeginPass rebuilds.
   */
  struct VulkanFramebuffer : public GpuResourceData
  {
    VulkanContext* context = nullptr;

    /** Attachment slot � records the source texture and the specific view used by this slot.
        @ref view may differ from @ref tex->view when a face/mip/layer selector is active
        (e.g., cubemap face-as-color-attachment). When we create a transient view for this slot
        we own it and release it on detach / destroy. */
    struct Slot
    {
      VulkanTexture* tex   = nullptr;
      VkImageView view     = VK_NULL_HANDLE;
      bool ownsView        = false;
    };

    static constexpr int kMaxColorAttachments = 8;
    Slot colorAttachments[kMaxColorAttachments] = {};
    Slot depthAttachment = {};

    uint32_t width  = 0;
    uint32_t height = 0;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    bool dirty = true;

    ~VulkanFramebuffer() override;

    /** Release cached VkRenderPass + VkFramebuffer (called on teardown or attachment change). */
    void ReleaseLazyObjects();

    /** Release any transient views owned by attachment slots. */
    void ReleaseOwnedViews();
  };

  /**
   * Backend GPU data for a UniformBuffer. Stored on UniformBuffer::m_gpuData.
   *
   * HOST_VISIBLE + HOST_COHERENT + persistently-mapped via VMA so updates are a plain memcpy
   * with no flush. Sized once at Create; subsequent Update calls assume the size matches
   * (UniformBuffer::Map enforces this on the engine side).
   */
  struct VulkanUniformBuffer : public GpuResourceData
  {
    VulkanContext* context = nullptr;

    VkBuffer buffer        = VK_NULL_HANDLE;
    VmaAllocation alloc    = VK_NULL_HANDLE;
    void* mapped           = nullptr;
    VkDeviceSize size      = 0;

    ~VulkanUniformBuffer() override;
  };

} // namespace ToolKit
