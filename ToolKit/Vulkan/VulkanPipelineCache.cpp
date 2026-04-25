/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanPipelineCache.h"

#include "../Logger.h"
#include "VulkanContext.h"

#include <cstring>

namespace ToolKit
{

  bool VulkanPipelineDesc::operator==(const VulkanPipelineDesc& o) const
  {
    if (renderPass != o.renderPass || vert != o.vert || frag != o.frag)
    {
      return false;
    }
    if (vertexStride != o.vertexStride || attributeCount != o.attributeCount)
    {
      return false;
    }
    for (uint32_t i = 0; i < attributeCount; ++i)
    {
      const auto& a = attributes[i];
      const auto& b = o.attributes[i];
      if (a.location != b.location || a.binding != b.binding || a.format != b.format || a.offset != b.offset)
      {
        return false;
      }
    }
    if (topology != o.topology || cullMode != o.cullMode || frontFace != o.frontFace)
    {
      return false;
    }
    if (depthTestEnable != o.depthTestEnable || depthWriteEnable != o.depthWriteEnable ||
        depthCompareOp != o.depthCompareOp)
    {
      return false;
    }
    if (blendEnable != o.blendEnable || colorAttachmentCount != o.colorAttachmentCount)
    {
      return false;
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
    for (uint32_t i = 0; i < d.attributeCount; ++i)
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

    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    att.blendEnable = desc.blendEnable;
    if (desc.blendEnable)
    {
      att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      att.colorBlendOp        = VK_BLEND_OP_ADD;
      att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      att.alphaBlendOp        = VK_BLEND_OP_ADD;
    }

    // One attachment state replicated across all color attachments — fine for the current test
    // path. Stage 7's MRT passes will need per-attachment state.
    std::array<VkPipelineColorBlendAttachmentState, 8> attStates{};
    for (uint32_t i = 0; i < desc.colorAttachmentCount && i < attStates.size(); ++i)
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

} // namespace ToolKit
