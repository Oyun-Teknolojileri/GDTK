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

  /**
   * Maps ToolKit GraphicTypes format enum to a concrete VkFormat. Returns VK_FORMAT_UNDEFINED for
   * formats not yet supported by the Vulkan backend � caller should assert/skip.
   */
  VkFormat ToVkFormat(GraphicTypes format);

  /** Returns true for depth or depth-stencil formats. */
  bool IsDepthFormat(VkFormat format);

  /** Returns true for stencil or depth-stencil formats. */
  bool IsStencilFormat(VkFormat format);

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
    /** Sample count this image was created with (Stage 10). VK_SAMPLE_COUNT_1_BIT for the
        non-MSAA path; >1 means the image is a multi-sampled render target — driving the
        render pass attachment sampleCount, the pipeline's rasterizationSamples, and the
        ResolveFramebuffer branch that swaps vkCmdBlitImage for vkCmdResolveImage. */
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
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
   * The VkRenderPass + VkFramebuffer are built lazily on first StartPass after the attachment
   * set / size stabilizes. Any Attach/Detach call flips @ref dirty so the next StartPass rebuilds.
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
      VulkanTexture* tex       = nullptr;
      VkImageView view         = VK_NULL_HANDLE;
      bool ownsView            = false;
      /** Subresource range covered by @ref view. Used by ClearBuffer to target the right
          layer/mip when clearing a per-layer or per-face attachment. */
      uint32_t baseArrayLayer  = 0;
      uint32_t layerCount      = 1;
      uint32_t baseMipLevel    = 0;
    };

    static constexpr int kMaxColorAttachments = 8;
    Slot colorAttachments[kMaxColorAttachments] = {};
    Slot depthAttachment = {};

    uint32_t width  = 0;
    uint32_t height = 0;

    /** Sample count adopted by the most recent BuildRpVariant — every attachment in
        the pass shares the same VkSampleCountFlagBits value. Pipelines drawn into this FB
        must use this as their rasterizationSamples (Stage 10). */
    VkSampleCountFlagBits subpassSamples = VK_SAMPLE_COUNT_1_BIT;

    /** Per-clearBits VkRenderPass variants. loadOp is baked into VkRenderPass, so the same
        framebuffer needs a separate RP per (clearColor, clearDepth, clearStencil) combination —
        the engine cycles between "first use clears, later uses load" patterns on the same target
        (m_oneColorAttachmentFramebuffer, shadow atlas across cascades, etc.). All variants share
        the same VkFramebuffer because Vulkan considers RPs with identical attachment counts /
        formats / samples "compatible" regardless of loadOp. */
    struct RpVariant
    {
      VkRenderPass rp            = VK_NULL_HANDLE;
      GraphicBitFields clearBits = GraphicBitFields::None;
      bool valid                 = false;
    };
    static constexpr int kMaxRpVariants = 4;
    RpVariant rpVariants[kMaxRpVariants] = {};

    /** VkFramebuffer cache keyed on (view tuple). VkFramebuffer is immutable in Vulkan — every
        attachment view change requires a fresh handle, and the old one used to defer-delete +
        recreate. ShadowPass cycles through 4 atlas-layer views + blur ping-pong views; without
        this cache that's ~100 vkCreate/Destroy pairs per frame piling up in the deferred-delete
        queue, causing frame-time hitches when the queue drains. With cache: 4-8 stable FBs live
        the texture's lifetime, AttachColorTarget just flips the active pointer. */
    struct FbCacheEntry
    {
      VkFramebuffer fb     = VK_NULL_HANDLE;
      /** Same indexing as a Vulkan framebuffer attachment list: colors in declared order,
          followed by depth (if any). All slots beyond viewCount are unused/VK_NULL_HANDLE. */
      std::array<VkImageView, kMaxColorAttachments + 1> views{};
      uint32_t viewCount   = 0;
      bool valid           = false;
    };
    static constexpr int kMaxFbCacheEntries = 8;
    std::array<FbCacheEntry, kMaxFbCacheEntries> fbCache{};

    /** Currently-active VkRenderPass. Equals rpVariants[i].rp for the entry whose clearBits
        matches the most recent StartPass desc. Set by EnsureRpForClearBits. */
    VkRenderPass renderPass = VK_NULL_HANDLE;
    /** Currently-active VkFramebuffer — a pointer into fbCache[].fb. Owned by the cache; never
        defer-deleted in isolation. Eviction (cache full / fbData teardown) is the only path that
        destroys these handles. */
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    bool dirty = true;

    ~VulkanFramebuffer() override;

    /** Release cached VkRenderPass + VkFramebuffer (called on teardown or attachment change). */
    void ReleaseLazyObjects();

    /** Release any transient views owned by attachment slots. */
    void ReleaseOwnedViews();
  };

  /**
   * Backend GPU data for a UniformBuffer.
   * Stored on UniformBuffer::m_gpuData.
   *
   * Uses a HOST_VISIBLE + HOST_COHERENT buffer with VMA persistent mapping � the CPU writes
   * directly through @ref buffer.mapped on every UpdateUniformBuffer call. Coherent memory means
   * no explicit flush; the next vkQueueSubmit observes the write thanks to the host-write
   * implicit memory dependency.
   *
   * One UBO per ToolKit `UniformBuffer` instance � small, fixed-size, infrequently-resized.
   * Per-draw uniforms (PerDrawUniforms) will use a dedicated dynamic UBO ring buffer in Stage 7c,
   * not this struct.
   */
  struct VulkanUniformBuffer : public GpuResourceData
  {
    VulkanContext* context = nullptr;
    VulkanBuffer::Buffer buffer;

    ~VulkanUniformBuffer() override;
  };

  /**
   * Backend GPU data for a Mesh.
   * Stored on Mesh::m_gpuData.
   *
   * Vertex + index buffers are uploaded once (DEVICE_LOCAL via staging) at CreateMesh time and
   * reused for every Draw. Index size is fixed at 32-bit because ToolKit's UIntArray uses uint
   * indices � VK_INDEX_TYPE_UINT32 in Vulkan terms. SkinMesh uses the same struct; the per-vertex
   * stride and vertex attribute layout are baked into the pipeline (selected via DrawDesc.vertexLayout).
   */
  struct VulkanMesh : public GpuResourceData
  {
    VulkanContext* context = nullptr;
    VulkanBuffer::Buffer vertex;
    VulkanBuffer::Buffer index;

    ~VulkanMesh() override;
  };

  /**
   * Backend GPU data for a Shader.
   * Stored on Shader::m_gpuData via CreateShader.
   *
   * Holds a single VkShaderModule built from the shader's GLSL source compiled to SPIR-V. The
   * shaderc spirv binary is not retained � once vkCreateShaderModule consumed it, only the
   * driver-side module is needed.
   */
  struct VulkanShaderModule : public GpuResourceData
  {
    VulkanContext* context = nullptr;
    VkShaderModule module  = VK_NULL_HANDLE;

    ~VulkanShaderModule() override;
  };

  /**
   * Backend GPU data for a GpuProgram (vertex + fragment shader pair).
   * Stored on GpuProgram::m_gpuData via CreateGpuProgram.
   *
   * Owns its VkPipelineLayout. The descriptor set layout it references is shared across every
   * program (VulkanContext::GetGlobalDescriptorSetLayout) � context owns that, programs only
   * point at it. Shader modules are NOT owned here; they live on the source Shader's
   * VulkanShaderModule. This struct caches raw module handles for cheap pipeline rebuild.
   *
   * Stage 7d-3: pipeline layout uses the global descriptor set layout (single set, kitchen-sink
   * binding reservation \u2014 see VulkanBindings.h). Stage 7d-4 adds descriptor set allocation +
   * BindTexture / SubmitPerDrawData wiring on top.
   */
  struct VulkanGpuProgram : public GpuResourceData
  {
    VulkanContext* context = nullptr;

    // Cached references; modules are owned by the source Shader's VulkanShaderModule.
    VkShaderModule vert    = VK_NULL_HANDLE;
    VkShaderModule frag    = VK_NULL_HANDLE;

    /** Per-program pipeline layout. Built off the context-owned global descriptor set layout, so
        ~VulkanGpuProgram destroys only this handle (not the set layout itself). */
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    /** Snapshot of the engine GpuProgram's aggregated resource declarations (textures + UBOs).
        Cached at CreateGpuProgram time so descriptor flush logic in Draw can iterate required
        bindings without dereferencing the engine-side GpuProgram. */
    ShaderResourceArray resources;

    ~VulkanGpuProgram() override;
  };

} // namespace ToolKit
