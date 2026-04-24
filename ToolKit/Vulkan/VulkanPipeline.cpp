/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanPipeline.h"

#include "../Logger.h"
#include "VulkanContext.h"
#include "VulkanImage.h"
#include "VulkanResources.h"
#include "VulkanShader.h"

#include <array>

namespace ToolKit
{

  // 4 quad corners + 6 indices (2 triangles) — Stage 3b switches from drawn triangle to
  // index-sourced quad, exercising vkCmdBindIndexBuffer + vkCmdDrawIndexed.
  struct TestVertex
  {
    float pos[3];
    float color[3];
  };

  static const TestVertex kQuadVertices[4] = {
      {{-0.6f, -0.6f, 0.0f}, {1.0f, 0.0f, 0.0f}}, // 0: bottom-left  red
      {{ 0.6f, -0.6f, 0.0f}, {0.0f, 1.0f, 0.0f}}, // 1: bottom-right green
      {{ 0.6f,  0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}}, // 2: top-right    blue
      {{-0.6f,  0.6f, 0.0f}, {1.0f, 1.0f, 0.0f}}, // 3: top-left     yellow
  };

  static const uint16_t kQuadIndices[6] = {0, 1, 2, 0, 2, 3};

  static constexpr const char* kTestVert =
      "#version 450\n"
      "layout(location = 0) in vec3 inPos;\n"
      "layout(location = 1) in vec3 inColor;\n"
      "layout(location = 0) out vec3 vColor;\n"
      "void main() {\n"
      "  gl_Position = vec4(inPos, 1.0);\n"
      "  vColor = inColor;\n"
      "}\n";

  static constexpr const char* kTestFrag =
      "#version 450\n"
      "layout(location = 0) in vec3 vColor;\n"
      "layout(location = 0) out vec4 oColor;\n"
      "void main() { oColor = vec4(vColor, 1.0); }\n";

  bool VulkanTestPipeline::Init(VulkanContext* ctx)
  {
    m_ctx           = ctx;
    VkDevice device = ctx->GetDevice();

    auto vSpv       = VulkanShader::CompileGlslToSpirv(VulkanShader::Stage::Vertex, kTestVert, "test.vert");
    auto fSpv       = VulkanShader::CompileGlslToSpirv(VulkanShader::Stage::Fragment, kTestFrag, "test.frag");
    m_vert          = VulkanShader::CreateShaderModule(device, vSpv);
    m_frag          = VulkanShader::CreateShaderModule(device, fSpv);
    if (m_vert == VK_NULL_HANDLE || m_frag == VK_NULL_HANDLE)
    {
      TK_ERR("VulkanTestPipeline::Init: shader module creation failed");
      return false;
    }

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    if (VkResult r = vkCreatePipelineLayout(device, &plci, nullptr, &m_layout); r != VK_SUCCESS)
    {
      TK_ERR("vkCreatePipelineLayout failed: %d", r);
      return false;
    }

    m_vertexBuffer =
        VulkanBuffer::UploadDeviceLocal(ctx, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kQuadVertices, sizeof(kQuadVertices));
    if (m_vertexBuffer.handle == VK_NULL_HANDLE)
    {
      TK_ERR("VulkanTestPipeline::Init: vertex buffer upload failed");
      return false;
    }

    m_indexBuffer =
        VulkanBuffer::UploadDeviceLocal(ctx, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, kQuadIndices, sizeof(kQuadIndices));
    if (m_indexBuffer.handle == VK_NULL_HANDLE)
    {
      TK_ERR("VulkanTestPipeline::Init: index buffer upload failed");
      return false;
    }

    // 4x4 RGBA8 checkerboard — magenta + black. Verifies upload + layout transitions; not yet
    // sampled (Stage 4b wires descriptor set + shader read).
    constexpr uint32_t kTexSize        = 4;
    std::array<uint8_t, kTexSize * kTexSize * 4> pixels{};
    for (uint32_t y = 0; y < kTexSize; ++y)
    {
      for (uint32_t x = 0; x < kTexSize; ++x)
      {
        const bool on    = ((x ^ y) & 1) != 0;
        const size_t idx = (y * kTexSize + x) * 4;
        pixels[idx + 0]  = on ? 255 : 0;
        pixels[idx + 1]  = 0;
        pixels[idx + 2]  = on ? 255 : 0;
        pixels[idx + 3]  = 255;
      }
    }
    m_testTexture =
        VulkanImage::CreateSampled2DFromData(ctx, VK_FORMAT_R8G8B8A8_UNORM, kTexSize, kTexSize, pixels.data(), pixels.size());
    if (m_testTexture == nullptr)
    {
      TK_ERR("VulkanTestPipeline::Init: test texture create failed");
      return false;
    }

    return true;
  }

  void VulkanTestPipeline::Destroy()
  {
    if (m_ctx == nullptr)
    {
      return;
    }
    VkDevice device = m_ctx->GetDevice();
    // Drop while m_ctx is still alive — ~VulkanTexture needs context->GetDevice/GetAllocator.
    m_testTexture.reset();
    VulkanBuffer::Destroy(m_ctx, m_indexBuffer);
    VulkanBuffer::Destroy(m_ctx, m_vertexBuffer);
    if (m_pipeline != VK_NULL_HANDLE)
    {
      vkDestroyPipeline(device, m_pipeline, nullptr);
      m_pipeline = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE)
    {
      vkDestroyPipelineLayout(device, m_layout, nullptr);
      m_layout = VK_NULL_HANDLE;
    }
    if (m_vert != VK_NULL_HANDLE)
    {
      vkDestroyShaderModule(device, m_vert, nullptr);
      m_vert = VK_NULL_HANDLE;
    }
    if (m_frag != VK_NULL_HANDLE)
    {
      vkDestroyShaderModule(device, m_frag, nullptr);
      m_frag = VK_NULL_HANDLE;
    }
    m_builtFor = VK_NULL_HANDLE;
    m_ctx      = nullptr;
  }

  bool VulkanTestPipeline::BuildPipeline(VkRenderPass rp)
  {
    VkDevice device = m_ctx->GetDevice();

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_frag;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(TestVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribs[2]{};
    attribs[0].location = 0;
    attribs[0].binding  = 0;
    attribs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribs[0].offset   = offsetof(TestVertex, pos);
    attribs[1].location = 1;
    attribs[1].binding  = 0;
    attribs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribs[1].offset   = offsetof(TestVertex, color);

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions    = attribs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport + scissor are set by VulkanBackend::BeginPass — we just declare 1 each via dynamic state.
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth attached on viewport FB but we don't care about it for the test triangle.
    // Disabling test+write keeps the pipeline compatible with depth-bearing render passes.
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    att.blendEnable    = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments    = &att;

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
    pci.layout              = m_layout;
    pci.renderPass          = rp;
    pci.subpass             = 0;

    VkPipeline newPipe = VK_NULL_HANDLE;
    if (VkResult r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &newPipe); r != VK_SUCCESS)
    {
      TK_ERR("vkCreateGraphicsPipelines (test triangle) failed: %d", r);
      return false;
    }

    m_pipeline = newPipe;
    m_builtFor = rp;
    return true;
  }

  void VulkanTestPipeline::Draw(VkCommandBuffer cb,
                                VkRenderPass rp,
                                const std::function<void(VkPipeline)>& deferDestroyPipeline)
  {
    if (cb == VK_NULL_HANDLE || rp == VK_NULL_HANDLE || m_layout == VK_NULL_HANDLE)
    {
      return;
    }

    if (m_pipeline == VK_NULL_HANDLE || rp != m_builtFor)
    {
      VkPipeline old = m_pipeline;
      if (!BuildPipeline(rp))
      {
        return;
      }
      // Old pipeline may still be referenced by an in-flight cmd buffer — defer its destruction.
      // BuildPipeline already overwrote m_pipeline, so handing off the captured `old` is safe.
      if (old != VK_NULL_HANDLE && deferDestroyPipeline)
      {
        deferDestroyPipeline(old);
      }
    }

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    VkBuffer vb       = m_vertexBuffer.handle;
    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &vb, &zero);
    vkCmdBindIndexBuffer(cb, m_indexBuffer.handle, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cb, 6, 1, 0, 0, 0);
  }

} // namespace ToolKit
