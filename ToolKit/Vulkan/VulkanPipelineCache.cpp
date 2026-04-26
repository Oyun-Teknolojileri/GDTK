/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanPipelineCache.h"

#include "../Logger.h"
#include "VulkanContext.h"

namespace ToolKit
{

  // -- VulkanPipelineDesc equality + hash --------------------------------------------------------

  bool VulkanPipelineDesc::operator==(const VulkanPipelineDesc& o) const
  {
    // Compare scalar/POD fields in one shot. The attributes tail (slots beyond attributeCount)
    // is uninitialized garbage on either side, so it must not enter the comparison — we walk
    // only the live prefix.
    const bool scalarsEqual =
        renderPass == o.renderPass && vert == o.vert && frag == o.frag &&
        vertexStride == o.vertexStride && attributeCount == o.attributeCount &&
        topology == o.topology && cullMode == o.cullMode && frontFace == o.frontFace &&
        depthTestEnable == o.depthTestEnable && depthWriteEnable == o.depthWriteEnable &&
        depthCompareOp == o.depthCompareOp && blendEnable == o.blendEnable &&
        srcColorBlendFactor == o.srcColorBlendFactor && dstColorBlendFactor == o.dstColorBlendFactor &&
        colorBlendOp == o.colorBlendOp && srcAlphaBlendFactor == o.srcAlphaBlendFactor &&
        dstAlphaBlendFactor == o.dstAlphaBlendFactor && alphaBlendOp == o.alphaBlendOp &&
        colorAttachmentCount == o.colorAttachmentCount;

    if (!scalarsEqual)
    {
      return false;
    }

    for (uint i = 0; i < attributeCount; ++i)
    {
      const VkVertexInputAttributeDescription& a = attributes[i];
      const VkVertexInputAttributeDescription& b = o.attributes[i];
      if (a.location != b.location || a.binding != b.binding || a.format != b.format ||
          a.offset != b.offset)
      {
        return false;
      }
    }
    return true;
  }

  // 64-bit splitmix / xxHash-style mixer — deterministic and reasonably distributing for the
  // handfuls of fields we combine here.
  static inline std::size_t MixBits(std::size_t seed, std::size_t v)
  {
    return seed ^ (v + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
  }

  std::size_t VulkanPipelineDescHash::operator()(const VulkanPipelineDesc& d) const noexcept
  {
    std::size_t h = 0;
    h = MixBits(h, reinterpret_cast<std::uintptr_t>(d.renderPass));
    h = MixBits(h, reinterpret_cast<std::uintptr_t>(d.vert));
    h = MixBits(h, reinterpret_cast<std::uintptr_t>(d.frag));
    h = MixBits(h, d.vertexStride);
    h = MixBits(h, d.attributeCount);
    for (uint i = 0; i < d.attributeCount; ++i)
    {
      const auto& a = d.attributes[i];
      h = MixBits(h, a.location);
      h = MixBits(h, a.binding);
      h = MixBits(h, (std::size_t) a.format);
      h = MixBits(h, a.offset);
    }
    h = MixBits(h, (std::size_t) d.topology);
    h = MixBits(h, (std::size_t) d.cullMode);
    h = MixBits(h, (std::size_t) d.frontFace);
    h = MixBits(h, (std::size_t) d.depthTestEnable);
    h = MixBits(h, (std::size_t) d.depthWriteEnable);
    h = MixBits(h, (std::size_t) d.depthCompareOp);
    h = MixBits(h, (std::size_t) d.blendEnable);
    h = MixBits(h, (std::size_t) d.srcColorBlendFactor);
    h = MixBits(h, (std::size_t) d.dstColorBlendFactor);
    h = MixBits(h, (std::size_t) d.colorBlendOp);
    h = MixBits(h, (std::size_t) d.srcAlphaBlendFactor);
    h = MixBits(h, (std::size_t) d.dstAlphaBlendFactor);
    h = MixBits(h, (std::size_t) d.alphaBlendOp);
    h = MixBits(h, d.colorAttachmentCount);
    return h;
  }

  VkPipeline VulkanPipelineCache::GetOrCreate(VulkanContext* ctx,
                                              VkPipelineLayout layout,
                                              const VulkanPipelineDesc& desc)
  {
    if (ctx == nullptr || layout == VK_NULL_HANDLE || desc.renderPass == VK_NULL_HANDLE ||
        desc.vert == VK_NULL_HANDLE || desc.frag == VK_NULL_HANDLE)
    {
      return VK_NULL_HANDLE;
    }

    auto it = m_pipelines.find(desc);
    if (it != m_pipelines.end())
    {
      return it->second;
    }

    VkDevice device = ctx->GetDevice();

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = desc.vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = desc.frag;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = desc.vertexStride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (desc.vertexStride > 0 && desc.attributeCount > 0)
    {
      vi.vertexBindingDescriptionCount   = 1;
      vi.pVertexBindingDescriptions      = &binding;
      vi.vertexAttributeDescriptionCount = desc.attributeCount;
      vi.pVertexAttributeDescriptions    = desc.attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = desc.topology;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = desc.cullMode;
    rs.frontFace   = desc.frontFace;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = desc.depthTestEnable;
    ds.depthWriteEnable = desc.depthWriteEnable;
    ds.depthCompareOp   = desc.depthCompareOp;

    // No build-time branching on blend mode — desc carries the full recipe. When blendEnable is
    // VK_FALSE the factor/op fields are ignored by the driver, so we can assign them
    // unconditionally and keep the cache key stable for the disabled-blend case.
    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    att.blendEnable         = desc.blendEnable;
    att.srcColorBlendFactor = desc.srcColorBlendFactor;
    att.dstColorBlendFactor = desc.dstColorBlendFactor;
    att.colorBlendOp        = desc.colorBlendOp;
    att.srcAlphaBlendFactor = desc.srcAlphaBlendFactor;
    att.dstAlphaBlendFactor = desc.dstAlphaBlendFactor;
    att.alphaBlendOp        = desc.alphaBlendOp;

    // One attachment state replicated across all color attachments — fine for the current test
    // path. Stage 7's MRT passes will need per-attachment state.
    std::array<VkPipelineColorBlendAttachmentState, 8> attStates{};
    for (uint i = 0; i < desc.colorAttachmentCount && i < attStates.size(); ++i)
    {
      attStates[i] = att;
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = desc.colorAttachmentCount;
    cb.pAttachments    = attStates.data();

    VkDynamicState dynStates[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dynStates;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount          = 2;
    pci.pStages             = stages;
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState   = &ms;
    pci.pDepthStencilState  = &ds;
    pci.pColorBlendState    = &cb;
    pci.pDynamicState       = &dyn;
    pci.layout              = layout;
    pci.renderPass          = desc.renderPass;
    pci.subpass             = 0;

    VkPipeline newPipe = VK_NULL_HANDLE;
    if (VkResult r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &newPipe); r != VK_SUCCESS)
    {
      TK_ERR("VulkanPipelineCache::GetOrCreate vkCreateGraphicsPipelines failed: %d", r);
      return VK_NULL_HANDLE;
    }

    m_pipelines.emplace(desc, newPipe);
    return newPipe;
  }

  void VulkanPipelineCache::Destroy(VkDevice device)
  {
    for (auto& kv : m_pipelines)
    {
      if (kv.second != VK_NULL_HANDLE)
      {
        vkDestroyPipeline(device, kv.second, nullptr);
      }
    }
    m_pipelines.clear();
  }

  // -------- RenderState ? VulkanPipelineDesc -----------------------------------------------
  // Single switch-table conversion; no branching beyond the four enum maps. Same RenderState
  // always produces the same desc bytes ? same cache hit.

  static VkCullModeFlags ToVkCullMode(CullingType c)
  {
    switch (c)
    {
      case CullingType::Front: return VK_CULL_MODE_FRONT_BIT;
      case CullingType::Back:  return VK_CULL_MODE_BACK_BIT;
      case CullingType::TwoSided:
      default:                 return VK_CULL_MODE_NONE;
    }
  }

  static VkCompareOp ToVkCompareOp(CompareFunctions f)
  {
    switch (f)
    {
      case CompareFunctions::FuncNever:   return VK_COMPARE_OP_NEVER;
      case CompareFunctions::FuncLess:    return VK_COMPARE_OP_LESS;
      case CompareFunctions::FuncEqual:   return VK_COMPARE_OP_EQUAL;
      case CompareFunctions::FuncLequal:  return VK_COMPARE_OP_LESS_OR_EQUAL;
      case CompareFunctions::FuncGreater: return VK_COMPARE_OP_GREATER;
      case CompareFunctions::FuncNEqual:  return VK_COMPARE_OP_NOT_EQUAL;
      case CompareFunctions::FuncGEqual:  return VK_COMPARE_OP_GREATER_OR_EQUAL;
      case CompareFunctions::FuncAlways:
      default:                            return VK_COMPARE_OP_ALWAYS;
    }
  }

  static VkPrimitiveTopology ToVkTopology(DrawType d)
  {
    switch (d)
    {
      case DrawType::Point:      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
      case DrawType::Line:       return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
      case DrawType::LineStrip:  return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
      case DrawType::LineLoop:   // Vulkan has no line-loop primitive; closest match is LINE_STRIP.
                                 return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
      case DrawType::Triangle:
      default:                   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
  }

  // (blendEnable, srcColor, dstColor, colorOp, srcAlpha, dstAlpha, alphaOp).
  struct BlendRecipe
  {
    VkBool32 enable;
    VkBlendFactor srcColor;
    VkBlendFactor dstColor;
    VkBlendOp colorOp;
    VkBlendFactor srcAlpha;
    VkBlendFactor dstAlpha;
    VkBlendOp alphaOp;
  };

  static BlendRecipe ToBlendRecipe(BlendFunction f)
  {
    // ALPHA_MASK is a fragment-shader feature (discard-on-threshold) — no blend-state difference
    // from opaque, so it falls through to the default opaque recipe.
    switch (f)
    {
      case BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA:
        return {VK_TRUE,
                VK_BLEND_FACTOR_SRC_ALPHA,
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                VK_BLEND_OP_ADD,
                VK_BLEND_FACTOR_ONE,
                VK_BLEND_FACTOR_ZERO,
                VK_BLEND_OP_ADD};
      case BlendFunction::ONE_TO_ONE: // Additive.
        return {VK_TRUE,
                VK_BLEND_FACTOR_ONE,
                VK_BLEND_FACTOR_ONE,
                VK_BLEND_OP_ADD,
                VK_BLEND_FACTOR_ONE,
                VK_BLEND_FACTOR_ONE,
                VK_BLEND_OP_ADD};
      case BlendFunction::NONE:
      case BlendFunction::ALPHA_MASK:
      default:
        return {VK_FALSE,
                VK_BLEND_FACTOR_ONE,
                VK_BLEND_FACTOR_ZERO,
                VK_BLEND_OP_ADD,
                VK_BLEND_FACTOR_ONE,
                VK_BLEND_FACTOR_ZERO,
                VK_BLEND_OP_ADD};
    }
  }

  void RenderStateToPipelineDesc(const RenderState& state, VulkanPipelineDesc& out)
  {
    // RenderState carries a `blendOverride` flag that tells the renderer to ignore the material's
    // declared blend mode for this draw — honored here by selecting the override function when set.
    const BlendFunction blend = state.blendOverride ? state.blendOverrideFunc : state.blendFunction;
    const BlendRecipe r       = ToBlendRecipe(blend);

    out.topology            = ToVkTopology(state.drawType);
    out.cullMode            = ToVkCullMode(state.cullMode);
    out.frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    out.depthTestEnable     = state.depthTestEnabled ? VK_TRUE : VK_FALSE;
    out.depthWriteEnable    = state.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    out.depthCompareOp      = ToVkCompareOp(state.depthFunction);

    out.blendEnable         = r.enable;
    out.srcColorBlendFactor = r.srcColor;
    out.dstColorBlendFactor = r.dstColor;
    out.colorBlendOp        = r.colorOp;
    out.srcAlphaBlendFactor = r.srcAlpha;
    out.dstAlphaBlendFactor = r.dstAlpha;
    out.alphaBlendOp        = r.alphaOp;
  }

} // namespace ToolKit
