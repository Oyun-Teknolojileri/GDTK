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
#include <unordered_map>

namespace ToolKit
{

  class VulkanContext;

  /**
   * Full pipeline state needed to build a VkPipeline � serves both as the cache key (hashed +
   * compared field-by-field) and as the creation record handed to vkCreateGraphicsPipelines.
   * Deliberately flat + POD-ish so hashing is cheap. Aggregate {}-init zeroes everything
   * including the attribute tail slots; callers only touch the fields they use.
   *
   * Stage 6c scaffold � carries the state the test pipeline needs today (depth, cull, blend on/off,
   * topology, one vertex binding with N<=8 attribs, optional single descriptor set layout + one
   * push constant range). Stage 7 grows this when real materials / multiple descriptor sets / MRT
   * come online.
   */
  struct VulkanPipelineDesc
  {
    static constexpr int kMaxAttribs = 8;

    VkRenderPass renderPass    = VK_NULL_HANDLE;
    VkShaderModule vert        = VK_NULL_HANDLE;
    VkShaderModule frag        = VK_NULL_HANDLE;

    // Vertex input (single binding for now).
    uint vertexStride      = 0;
    uint attributeCount    = 0;
    std::array<VkVertexInputAttributeDescription, kMaxAttribs> attributes{};

    // Raster + primitive.
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cullMode     = VK_CULL_MODE_NONE;
    VkFrontFace frontFace        = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // Depth / stencil.
    VkBool32 depthTestEnable   = VK_FALSE;
    VkBool32 depthWriteEnable  = VK_FALSE;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Stencil. stencilTestEnable=false means the three op fields are ignored by the driver
    // and do not need to participate in cache key equality when stencil is off.
    VkBool32     stencilTestEnable = VK_FALSE;
    VkStencilOp  stencilFailOp     = VK_STENCIL_OP_KEEP;   // fragment fails stencil test
    VkStencilOp  stencilPassOp     = VK_STENCIL_OP_KEEP;   // fragment passes both tests
    VkStencilOp  stencilDepthFailOp= VK_STENCIL_OP_KEEP;   // passes stencil, fails depth
    VkCompareOp  stencilCompareOp  = VK_COMPARE_OP_ALWAYS;
    uint32_t     stencilReference  = 1;

    // Color blend (single attachment state replicated across colorAttachmentCount). Factors
    // and ops live in the desc so the cache builder has zero branching ? callers spell out the
    // exact blend recipe they want and equal recipes hash equal.
    VkBool32 blendEnable           = VK_FALSE;
    VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp colorBlendOp            = VK_BLEND_OP_ADD;
    VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alphaBlendOp            = VK_BLEND_OP_ADD;
    uint colorAttachmentCount         = 1;

    /** 0 = all channels masked (colorMaskEnabled=false). Full mask = RGBA bits set. */
    VkColorComponentFlags colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    /** Subpass sample count this pipeline targets (Stage 10). Must equal the active render
        pass's per-attachment sampleCount. Drives `pMultisampleState.rasterizationSamples`;
        also part of the cache key so a non-MSAA + an MSAA copy of the "same" recipe end up
        in different slots. */
    VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    bool operator==(const VulkanPipelineDesc& o) const;
  };

  struct VulkanPipelineDescHash
  {
    std::size_t operator()(const VulkanPipelineDesc& d) const noexcept;
  };

  /**
   * Single conversion point from ToolKit's flat RenderState POD to the raster / depth / blend
   * fields of VulkanPipelineDesc. Branch-free: every enum maps through a small switch table; no
   * if-chains. The remaining desc fields (renderPass, shaders, vertex input, color attachment
   * count) are caller responsibility ? they're orthogonal to RenderState.
   *
   * Postconditions:
   *  - out.cullMode, frontFace, topology, depthTestEnable, depthWriteEnable, depthCompareOp,
   *    blendEnable + 6 blend factor/op fields are written.
   *  - All other fields of @p out are left untouched.
   */
  void RenderStateToPipelineDesc(const RenderState& state, VulkanPipelineDesc& out);

  /**
   * Tiny VkPipeline cache keyed on VulkanPipelineDesc. Two callers can request the "same"
   * pipeline (same shaders + state + render pass) and get the same VkPipeline back, which is
   * the precondition for Stage 7's state-sorted rendering.
   *
   * Lifetime: owned by VulkanBackend; drained in backend dtor after vkDeviceWaitIdle so every
   * cached VkPipeline is safely destroyed while the device is still alive.
   */
  class TK_API VulkanPipelineCache
  {
   public:
    VulkanPipelineCache()  = default;
    ~VulkanPipelineCache() = default;

    /** Looks up (or builds on miss) a VkPipeline matching @p desc + @p layout. Returns
     *  VK_NULL_HANDLE if vkCreateGraphicsPipelines fails (logged). */
    VkPipeline GetOrCreate(VulkanContext* ctx, VkPipelineLayout layout, const VulkanPipelineDesc& desc);

    /** Destroys every cached VkPipeline. Caller must have vkDeviceWaitIdle'd. */
    void Destroy(VkDevice device);

   private:
    std::unordered_map<VulkanPipelineDesc, VkPipeline, VulkanPipelineDescHash> m_pipelines;
  };

  /**
   * Maps ToolKit's @ref RenderState onto the corresponding fields of @p out. Render-pass /
   * shader / vertex input fields are left untouched � the caller fills those in based on the
   * active pass + bound program. Single conversion path, branchless: every RenderState field
   * resolves through a small switch table, no per-state if chains, so identical RenderStates
   * always produce identical (hashable) descs.
   */
  void RenderStateToPipelineDesc(const RenderState& state, VulkanPipelineDesc& out);

} // namespace ToolKit
