/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../IGraphicsBackend.h"
#include "../Shader.h"
#include "../Types.h"
#include "VulkanBuffer.h"

#include <vulkan/vulkan.h>

#include <vector>

struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

namespace ToolKit
{

  class VulkanContext;
  class Texture;
  class Framebuffer;

  /** Maps a ToolKit GraphicTypes format to VkFormat. Returns VK_FORMAT_UNDEFINED on unsupported. */
  VkFormat ToVkFormat(GraphicTypes format);

  bool IsDepthFormat(VkFormat format);
  bool IsStencilFormat(VkFormat format);

  /** Backend GPU data for Texture / RenderTarget / DepthTexture. Stored on Texture::m_gpuData. */
  struct VulkanTexture : public GpuResourceData
  {
    VulkanContext* context        = nullptr;

    VkImage image                 = VK_NULL_HANDLE;
    VmaAllocation allocation      = VK_NULL_HANDLE;
    VkImageView view              = VK_NULL_HANDLE;
    VkSampler sampler             = VK_NULL_HANDLE;

    VkFormat format               = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect     = 0;
    VkExtent2D extent             = {0, 0};
    uint32_t arrayLayers          = 1; //!< 6 for cubemaps, >1 for 2D arrays.
    uint32_t mipLevels            = 1;

    /** VK_SAMPLE_COUNT_1_BIT on non-MSAA; >1 marks an MSAA target (drives RP sampleCount, pipeline
        rasterizationSamples, and the resolve-vs-blit branch). */
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool isCubemap                = false;

    /** Last layout we transitioned to. Drives pipeline barriers + RP initialLayout. */
    VkImageLayout currentLayout   = VK_IMAGE_LAYOUT_UNDEFINED;

    /** Subresource view cache for face/layer/mip-specific attachment views. Owned by this
        texture and destroyed in ~VulkanTexture so FB attachment swaps don't churn vkCreateView
        + defer-delete on every atlas-layer iteration. Linear scan; counts stay small in
        practice (shadow atlas: ~4 layers, env capture: 6 faces). */
    struct SubresourceViewEntry
    {
      uint32_t mip     = 0;
      uint32_t layer   = 0;
      VkImageView view = VK_NULL_HANDLE;
      bool valid       = false;
    };

    std::vector<SubresourceViewEntry> subresourceViews;

    ~VulkanTexture() override;
  };

  /** Backend GPU data for Framebuffer. Stored on Framebuffer::m_gpuData. VkRenderPass and
      VkFramebuffer are built lazily on first use after attachments stabilize. */
  struct VulkanFramebuffer : public GpuResourceData
  {
    VulkanContext* context = nullptr;

    /** Attachment slot. @ref view may differ from @ref tex->view when a face/mip/layer
        subresource view is in use; transient views the slot owns get destroyed on detach. */
    struct Slot
    {
      VulkanTexture* tex      = nullptr;
      VkImageView view        = VK_NULL_HANDLE;
      bool ownsView           = false;
      /** Subresource range covered by @ref view, used by clear to target the right slice. */
      uint32_t baseArrayLayer = 0;
      uint32_t layerCount     = 1;
      uint32_t baseMipLevel   = 0;
    };

    static constexpr int kMaxColorAttachments   = 8;
    Slot colorAttachments[kMaxColorAttachments] = {};
    Slot depthAttachment                        = {};

    uint32_t width                              = 0;
    uint32_t height                             = 0;

    /** Sample count adopted from the attachments; pipelines drawn into this fb match it. */
    VkSampleCountFlagBits subpassSamples        = VK_SAMPLE_COUNT_1_BIT;

    /** Per-clearBits VkRenderPass variants — loadOp is baked into VkRenderPass, but RP
        compatibility ignores loadOp so all variants share the same VkFramebuffer. */
    struct RpVariant
    {
      VkRenderPass rp            = VK_NULL_HANDLE;
      GraphicBitFields clearBits = GraphicBitFields::None;
      bool valid                 = false;
    };

    static constexpr int kMaxRpVariants  = 4;
    RpVariant rpVariants[kMaxRpVariants] = {};

    /** VkFramebuffer cache keyed by view tuple. Shadow atlas + post-process ping-pong cycle
        through ~6 unique tuples; caching avoids ~100 vkCreate/Destroy pairs per frame. */
    struct FbCacheEntry
    {
      VkFramebuffer fb = VK_NULL_HANDLE;
      /** Colors in declared order, then depth (matches the Vulkan attachment list ordering). */
      std::array<VkImageView, kMaxColorAttachments + 1> views {};
      uint32_t viewCount = 0;
      bool valid         = false;
    };

    static constexpr int kMaxFbCacheEntries = 8;
    std::array<FbCacheEntry, kMaxFbCacheEntries> fbCache {};

    /** Currently-active RP/FB. Set by EnsureRpForClearBits / BuildOffscreenFramebuffer.
        @ref framebuffer aliases into fbCache; cache owns lifetime. */
    VkRenderPass renderPass   = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    bool dirty                = true;

    ~VulkanFramebuffer() override;

    /** Releases cached RP variants + the active VkFramebuffer (called on teardown). */
    void ReleaseLazyObjects();

    /** Releases transient views owned by attachment slots. */
    void ReleaseOwnedViews();
  };

  /** Backend GPU data for UniformBuffer. HOST_VISIBLE+HOST_COHERENT, persistently mapped. */
  struct VulkanUniformBuffer : public GpuResourceData
  {
    VulkanContext* context = nullptr;
    VulkanBuffer::Buffer buffer;

    ~VulkanUniformBuffer() override;
  };

  /** Backend GPU data for Mesh. Vertex + index buffers uploaded once to DEVICE_LOCAL via
      staging at CreateMesh. Indices are always 32-bit (VK_INDEX_TYPE_UINT32). */
  struct VulkanMesh : public GpuResourceData
  {
    VulkanContext* context = nullptr;
    VulkanBuffer::Buffer vertex;
    VulkanBuffer::Buffer index;

    ~VulkanMesh() override;
  };

  /** Backend GPU data for Shader (single VkShaderModule). SPIR-V binary is discarded once the
      module is built. */
  struct VulkanShaderModule : public GpuResourceData
  {
    VulkanContext* context = nullptr;
    VkShaderModule module  = VK_NULL_HANDLE;

    ~VulkanShaderModule() override;
  };

  /** Backend GPU data for GpuProgram. Owns the per-program VkPipelineLayout; the descriptor
      set layout is the context's shared global layout (programs only point at it). Shader
      modules are owned by their source Shader's VulkanShaderModule. */
  struct VulkanGpuProgram : public GpuResourceData
  {
    VulkanContext* context          = nullptr;

    VkShaderModule vert             = VK_NULL_HANDLE;
    VkShaderModule frag             = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    /** Cached snapshot of declared resources for descriptor-flush iteration. */
    ShaderResourceArray resources;

    ~VulkanGpuProgram() override;
  };

} // namespace ToolKit
