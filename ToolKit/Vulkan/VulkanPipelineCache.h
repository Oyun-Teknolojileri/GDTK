/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../RenderState.h"
#include "../Types.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace ToolKit
{

  class VulkanContext;

  /**
   * Full pipeline state needed to build a VkPipeline — also the cache key (hashed + compared
   * field-by-field). Flat POD-ish layout for cheap hashing; aggregate init zeroes attribute tail
   * slots so callers only touch what they use.
   */
  struct VulkanPipelineDesc
  {
    static constexpr int kMaxAttribs = 8;

    VkRenderPass renderPass    = VK_NULL_HANDLE;
    VkShaderModule vert        = VK_NULL_HANDLE;
    VkShaderModule frag        = VK_NULL_HANDLE;

    // Vertex input (single binding).
    uint vertexStride      = 0;
    uint attributeCount    = 0;
    std::array<VkVertexInputAttributeDescription, kMaxAttribs> attributes{};

    // Raster + primitive.
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cullMode     = VK_CULL_MODE_NONE;
    VkFrontFace frontFace        = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkBool32 depthClampEnable    = VK_FALSE;

    // Depth.
    VkBool32 depthTestEnable   = VK_FALSE;
    VkBool32 depthWriteEnable  = VK_FALSE;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Stencil. When stencilTestEnable is false the op fields are ignored by the driver.
    VkBool32     stencilTestEnable = VK_FALSE;
    VkStencilOp  stencilFailOp     = VK_STENCIL_OP_KEEP;
    VkStencilOp  stencilPassOp     = VK_STENCIL_OP_KEEP;
    VkStencilOp  stencilDepthFailOp= VK_STENCIL_OP_KEEP;
    VkCompareOp  stencilCompareOp  = VK_COMPARE_OP_ALWAYS;
    uint32_t     stencilReference  = 1;

    // Color blend (single attachment state replicated across colorAttachmentCount).
    VkBool32 blendEnable           = VK_FALSE;
    VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp colorBlendOp            = VK_BLEND_OP_ADD;
    VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alphaBlendOp            = VK_BLEND_OP_ADD;
    uint colorAttachmentCount         = 1;

    /** 0 = all channels masked. */
    VkColorComponentFlags colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    /** Subpass sample count. Drives pMultisampleState.rasterizationSamples and participates in
        the cache key so MSAA / non-MSAA copies of the same recipe land in distinct slots. */
    VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    bool operator==(const VulkanPipelineDesc& o) const;
  };

  struct VulkanPipelineDescHash
  {
    std::size_t operator()(const VulkanPipelineDesc& d) const noexcept;
  };

  /** Translates RenderState onto raster/depth/blend fields of @p out. RP / shaders / vertex
      input are caller responsibility. Branchless — identical states always hash identically. */
  void RenderStateToPipelineDesc(const RenderState& state, VulkanPipelineDesc& out);

  /** VkPipeline cache keyed by VulkanPipelineDesc. Owned by VulkanBackend; drained in dtor
      after vkDeviceWaitIdle. */
  class TK_API VulkanPipelineCache
  {
   public:
    VulkanPipelineCache()  = default;
    ~VulkanPipelineCache() = default;

    /** Lookup with build-on-miss. VK_NULL_HANDLE on build failure (logged). */
    VkPipeline GetOrCreate(VulkanContext* ctx, VkPipelineLayout layout, const VulkanPipelineDesc& desc);

    /** Evicts every cached pipeline tied to @p rp and forwards handles to @p deferDelete.
        Must run BEFORE the caller defer-deletes the RP — driver handle recycling otherwise
        lets a recycled-handle RP cache-hit a stale pipeline (NVIDIA NULL-derefs vkCmdDraw). */
    void InvalidateForRenderPass(VkRenderPass rp, const std::function<void(VkPipeline)>& deferDelete);

    /** Same contract as InvalidateForRenderPass but keyed on VkPipelineLayout. Must run from
        DestroyGpuProgram BEFORE the program's shared_ptr is queued, so pipelines and the
        layout land in the same deleter bucket in push order. */
    void InvalidateForPipelineLayout(VkPipelineLayout layout, const std::function<void(VkPipeline)>& deferDelete);

    /** Destroys every cached pipeline. Caller must have vkDeviceWaitIdle'd. */
    void Destroy(VkDevice device);

   private:
    struct Entry
    {
      VkPipeline pipeline       = VK_NULL_HANDLE;
      VkPipelineLayout layout   = VK_NULL_HANDLE;
    };
    std::unordered_map<VulkanPipelineDesc, Entry, VulkanPipelineDescHash> m_pipelines;
  };

} // namespace ToolKit
