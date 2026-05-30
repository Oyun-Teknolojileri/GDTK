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
    // Attribute tail (beyond attributeCount) is uninitialized — compare only the live prefix.
    const bool scalarsEqual =
        renderPass == o.renderPass && vert == o.vert && frag == o.frag &&
        vertexStride == o.vertexStride && attributeCount == o.attributeCount &&
        topology == o.topology && cullMode == o.cullMode && frontFace == o.frontFace &&
        depthClampEnable == o.depthClampEnable &&
        depthTestEnable == o.depthTestEnable && depthWriteEnable == o.depthWriteEnable &&
        depthCompareOp == o.depthCompareOp &&
        stencilTestEnable == o.stencilTestEnable &&
        (!stencilTestEnable || (stencilFailOp == o.stencilFailOp &&
                                stencilPassOp == o.stencilPassOp &&
                                stencilDepthFailOp == o.stencilDepthFailOp &&
                                stencilCompareOp == o.stencilCompareOp &&
                                stencilReference == o.stencilReference)) &&
        blendEnable == o.blendEnable &&
        srcColorBlendFactor == o.srcColorBlendFactor && dstColorBlendFactor == o.dstColorBlendFactor &&
        colorBlendOp == o.colorBlendOp && srcAlphaBlendFactor == o.srcAlphaBlendFactor &&
        dstAlphaBlendFactor == o.dstAlphaBlendFactor && alphaBlendOp == o.alphaBlendOp &&
        colorAttachmentCount == o.colorAttachmentCount &&
        colorWriteMask == o.colorWriteMask &&
        rasterizationSamples == o.rasterizationSamples;

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
    h = MixBits(h, (std::size_t) d.depthClampEnable);
    h = MixBits(h, (std::size_t) d.depthTestEnable);
    h = MixBits(h, (std::size_t) d.depthWriteEnable);
    h = MixBits(h, (std::size_t) d.depthCompareOp);
    h = MixBits(h, (std::size_t) d.stencilTestEnable);
    if (d.stencilTestEnable)
    {
      h = MixBits(h, (std::size_t) d.stencilFailOp);
      h = MixBits(h, (std::size_t) d.stencilPassOp);
      h = MixBits(h, (std::size_t) d.stencilDepthFailOp);
      h = MixBits(h, (std::size_t) d.stencilCompareOp);
      h = MixBits(h, (std::size_t) d.stencilReference);
    }
    h = MixBits(h, (std::size_t) d.blendEnable);
    h = MixBits(h, (std::size_t) d.srcColorBlendFactor);
    h = MixBits(h, (std::size_t) d.dstColorBlendFactor);
    h = MixBits(h, (std::size_t) d.colorBlendOp);
    h = MixBits(h, (std::size_t) d.srcAlphaBlendFactor);
    h = MixBits(h, (std::size_t) d.dstAlphaBlendFactor);
    h = MixBits(h, (std::size_t) d.alphaBlendOp);
    h = MixBits(h, d.colorAttachmentCount);
    h = MixBits(h, (std::size_t) d.colorWriteMask);
    h = MixBits(h, (std::size_t) d.rasterizationSamples);
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
      return it->second.pipeline;
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
    rs.polygonMode      = VK_POLYGON_MODE_FILL;
    rs.cullMode         = desc.cullMode;
    rs.frontFace        = desc.frontFace;
    rs.lineWidth        = 1.0f;
    // Shadow passes flip this on so geometry behind the light's near plane still writes depth.
    rs.depthClampEnable = desc.depthClampEnable;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = desc.rasterizationSamples != 0 ? desc.rasterizationSamples
                                                             : VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable       = desc.depthTestEnable;
    ds.depthWriteEnable      = desc.depthWriteEnable;
    ds.depthCompareOp        = desc.depthCompareOp;
    ds.stencilTestEnable     = desc.stencilTestEnable;
    if (desc.stencilTestEnable)
    {
      VkStencilOpState sop{};
      sop.failOp      = desc.stencilFailOp;
      sop.passOp      = desc.stencilPassOp;
      sop.depthFailOp = desc.stencilDepthFailOp;
      sop.compareOp   = desc.stencilCompareOp;
      sop.compareMask = 0xFF;
      sop.writeMask   = 0xFF;
      sop.reference   = desc.stencilReference;
      ds.front = sop;
      ds.back  = sop;
    }

    // Desc carries the full blend recipe; factor/op fields are ignored when blendEnable=FALSE.
    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask      = desc.colorWriteMask;
    att.blendEnable         = desc.blendEnable;
    att.srcColorBlendFactor = desc.srcColorBlendFactor;
    att.dstColorBlendFactor = desc.dstColorBlendFactor;
    att.colorBlendOp        = desc.colorBlendOp;
    att.srcAlphaBlendFactor = desc.srcAlphaBlendFactor;
    att.dstAlphaBlendFactor = desc.dstAlphaBlendFactor;
    att.alphaBlendOp        = desc.alphaBlendOp;

    // Single blend state replicated across all attachments. MRT with per-target blend is TBD.
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

    m_pipelines.emplace(desc, Entry{newPipe, layout});
    return newPipe;
  }

  void VulkanPipelineCache::InvalidateForRenderPass(VkRenderPass rp,
                                                    const std::function<void(VkPipeline)>& deferDelete)
  {
    if (rp == VK_NULL_HANDLE)
    {
      return;
    }
    for (auto it = m_pipelines.begin(); it != m_pipelines.end(); )
    {
      if (it->first.renderPass == rp)
      {
        if (it->second.pipeline != VK_NULL_HANDLE && deferDelete)
        {
          deferDelete(it->second.pipeline);
        }
        it = m_pipelines.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }

  void VulkanPipelineCache::InvalidateForPipelineLayout(VkPipelineLayout layout,
                                                        const std::function<void(VkPipeline)>& deferDelete)
  {
    if (layout == VK_NULL_HANDLE)
    {
      return;
    }
    for (auto it = m_pipelines.begin(); it != m_pipelines.end(); )
    {
      if (it->second.layout == layout)
      {
        if (it->second.pipeline != VK_NULL_HANDLE && deferDelete)
        {
          deferDelete(it->second.pipeline);
        }
        it = m_pipelines.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }

  void VulkanPipelineCache::Destroy(VkDevice device)
  {
    for (auto& kv : m_pipelines)
    {
      if (kv.second.pipeline != VK_NULL_HANDLE)
      {
        vkDestroyPipeline(device, kv.second.pipeline, nullptr);
      }
    }
    m_pipelines.clear();
  }

  // -- RenderState → VulkanPipelineDesc translation --
  // Switch-table conversion; same RenderState produces the same desc bytes → same cache hit.

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
      case DrawType::LineLoop:   // No native line-loop in Vulkan; closest is LINE_STRIP.
                                 return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
      case DrawType::Triangle:
      default:                   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
  }

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
    // ALPHA_MASK is shader-side discard; blend state matches opaque.
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
    // blendFunction / cullMode arrive pre-resolved by Renderer::Render.
    const BlendRecipe r = ToBlendRecipe(state.blendFunction);

    out.topology            = ToVkTopology(state.drawType);
    out.cullMode            = ToVkCullMode(state.cullMode);
    out.frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    out.depthClampEnable    = state.depthClampEnabled ? VK_TRUE : VK_FALSE;

    out.depthTestEnable     = state.depthTestEnabled ? VK_TRUE : VK_FALSE;
    out.depthWriteEnable    = state.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    out.depthCompareOp      = ToVkCompareOp(state.depthFunction);

    // Engine uses a binary stencil model: reference=1, full masks.
    switch (state.stencilOperation)
    {
      case StencilOperation::None:
        out.stencilTestEnable  = VK_FALSE;
        break;
      case StencilOperation::AllowAllPixels:
        // Write 1 for every fragment that survives depth test.
        out.stencilTestEnable  = VK_TRUE;
        out.stencilCompareOp   = VK_COMPARE_OP_ALWAYS;
        out.stencilPassOp      = VK_STENCIL_OP_REPLACE;
        out.stencilFailOp      = VK_STENCIL_OP_KEEP;
        out.stencilDepthFailOp = VK_STENCIL_OP_KEEP;
        out.stencilReference   = 1;
        break;
      case StencilOperation::AllowPixelsPassingStencil:
        // Draw where stencil == 1.
        out.stencilTestEnable  = VK_TRUE;
        out.stencilCompareOp   = VK_COMPARE_OP_EQUAL;
        out.stencilPassOp      = VK_STENCIL_OP_KEEP;
        out.stencilFailOp      = VK_STENCIL_OP_KEEP;
        out.stencilDepthFailOp = VK_STENCIL_OP_KEEP;
        out.stencilReference   = 1;
        break;
      case StencilOperation::AllowPixelsFailingStencil:
        // Draw where stencil == 0.
        out.stencilTestEnable  = VK_TRUE;
        out.stencilCompareOp   = VK_COMPARE_OP_NOT_EQUAL;
        out.stencilPassOp      = VK_STENCIL_OP_KEEP;
        out.stencilFailOp      = VK_STENCIL_OP_KEEP;
        out.stencilDepthFailOp = VK_STENCIL_OP_KEEP;
        out.stencilReference   = 1;
        break;
    }

    out.blendEnable         = r.enable;
    out.srcColorBlendFactor = r.srcColor;
    out.dstColorBlendFactor = r.dstColor;
    out.colorBlendOp        = r.colorOp;
    out.srcAlphaBlendFactor = r.srcAlpha;
    out.dstAlphaBlendFactor = r.dstAlpha;
    out.alphaBlendOp        = r.alphaOp;

    out.colorWriteMask = state.colorMaskEnabled
                             ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)
                             : 0;
  }

} // namespace ToolKit
