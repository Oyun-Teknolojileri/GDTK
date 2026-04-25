/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanPipeline.h"

#include "../Logger.h"
#include "VulkanContext.h"
#include "VulkanDescriptor.h"
#include "VulkanImage.h"
#include "VulkanPipelineCache.h"
#include "VulkanResources.h"
#include "VulkanShader.h"

#include <array>
#include <cstring>

namespace ToolKit
{

  // 4 quad corners + 6 indices (2 triangles) — Stage 3b switches from drawn triangle to
  // index-sourced quad, exercising vkCmdBindIndexBuffer + vkCmdDrawIndexed.
  // Stage 4b adds a per-vertex UV used to sample the checkerboard test texture in the frag
  // shader; vertex colour still modulates the sample so the gradient stays visible.
  struct TestVertex
  {
    float pos[3];
    float color[3];
    float uv[2];
  };

  static const TestVertex kQuadVertices[4] = {
      {{-0.6f, -0.6f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}}, // 0: bottom-left  red
      {{ 0.6f, -0.6f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}}, // 1: bottom-right green
      {{ 0.6f,  0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}}, // 2: top-right    blue
      {{-0.6f,  0.6f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}, // 3: top-left     yellow
  };

  static const uint16_t kQuadIndices[6] = {0, 1, 2, 0, 2, 3};

  static constexpr const char* kTestVert =
      "#version 450\n"
      "layout(location = 0) in vec3 inPos;\n"
      "layout(location = 1) in vec3 inColor;\n"
      "layout(location = 2) in vec2 inUV;\n"
      "layout(location = 0) out vec3 vColor;\n"
      "layout(location = 1) out vec2 vUV;\n"
      "layout(set = 0, binding = 1) uniform Camera {\n"
      "  mat4 view;\n"
      "  mat4 proj;\n"
      "} uCamera;\n"
      "layout(push_constant) uniform PC {\n"
      "  mat4 model;\n"
      "} pc;\n"
      "void main() {\n"
      "  gl_Position = uCamera.proj * uCamera.view * pc.model * vec4(inPos, 1.0);\n"
      "  vColor = inColor;\n"
      "  vUV = inUV;\n"
      "}\n";

  static constexpr const char* kTestFrag =
      "#version 450\n"
      "layout(location = 0) in vec3 vColor;\n"
      "layout(location = 1) in vec2 vUV;\n"
      "layout(location = 0) out vec4 oColor;\n"
      "layout(set = 0, binding = 0) uniform sampler2D uTex;\n"
      "void main() {\n"
      "  vec4 tex = texture(uTex, vUV);\n"
      "  oColor = tex * vec4(vColor, 1.0);\n"
      "}\n";

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

    // Two-binding layout: binding 0 = checkerboard sampler (frag), binding 1 = camera UBO (vert).
    m_descriptorLayout = VulkanDescriptor::CreateLayoutSamplerAndUbo(
        device, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_VERTEX_BIT);
    if (m_descriptorLayout == VK_NULL_HANDLE)
    {
      TK_ERR("VulkanTestPipeline::Init: descriptor set layout create failed");
      return false;
    }

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts    = &m_descriptorLayout;
    // Model matrix via push constant — 64 bytes sits well within the 128-byte minimum guarantee.
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(Mat4);
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcRange;
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

    // Descriptor set for the checkerboard sampler. Pool is shared across the engine; we rely
    // on VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT to release this set in Destroy.
    m_descriptorSet =
        VulkanDescriptor::AllocateSet(device, ctx->GetSharedDescriptorPool(), m_descriptorLayout);
    if (m_descriptorSet == VK_NULL_HANDLE)
    {
      TK_ERR("VulkanTestPipeline::Init: descriptor set allocation failed");
      return false;
    }
    VulkanDescriptor::WriteCombinedImageSampler(
        device, m_descriptorSet, 0, m_testTexture->view, m_testTexture->sampler);

    // Camera UBO — persistently-mapped host-visible buffer holding (view, proj). We fill it once
    // here with a simple look-at camera so the quad is rendered with a real 3D transform.
    // Stage 5c adds a push-constant model matrix that rotates it each frame.
    struct CameraUBO
    {
      Mat4 view;
      Mat4 proj;
    };
    m_cameraUbo = VulkanBuffer::CreateHostVisibleMapped(
        ctx, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(CameraUBO));
    if (m_cameraUbo.handle == VK_NULL_HANDLE)
    {
      TK_ERR("VulkanTestPipeline::Init: camera UBO create failed");
      return false;
    }

    CameraUBO cam{};
    // Diagonal camera (right + up + back) looking at origin — makes the quad's 3D orientation
    // visible. Stage 5c will animate a model matrix on top of this.
    cam.view = glm::lookAtRH(Vec3(1.5f, 1.5f, 2.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    // perspectiveRH_ZO: right-handed, depth clip [0, 1] — matches Vulkan's NDC depth range.
    // Flip Y afterwards because Vulkan's viewport Y is down (GLM assumes OpenGL-style up).
    cam.proj       = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    cam.proj[1][1] *= -1.0f;
    std::memcpy(m_cameraUbo.mapped, &cam, sizeof(cam));

    VulkanDescriptor::WriteUniformBuffer(
        device, m_descriptorSet, 1, m_cameraUbo.handle, 0, sizeof(CameraUBO));

    return true;
  }

  void VulkanTestPipeline::Destroy()
  {
    if (m_ctx == nullptr)
    {
      return;
    }
    VkDevice device = m_ctx->GetDevice();
    // Free the descriptor set before the pool lives on in VulkanContext — it was allocated from
    // the shared pool with FREE_DESCRIPTOR_SET_BIT.
    if (m_descriptorSet != VK_NULL_HANDLE)
    {
      vkFreeDescriptorSets(device, m_ctx->GetSharedDescriptorPool(), 1, &m_descriptorSet);
      m_descriptorSet = VK_NULL_HANDLE;
    }
    if (m_descriptorLayout != VK_NULL_HANDLE)
    {
      vkDestroyDescriptorSetLayout(device, m_descriptorLayout, nullptr);
      m_descriptorLayout = VK_NULL_HANDLE;
    }
    // Drop while m_ctx is still alive — ~VulkanTexture needs context->GetDevice/GetAllocator.
    m_testTexture.reset();
    VulkanBuffer::Destroy(m_ctx, m_cameraUbo);
    VulkanBuffer::Destroy(m_ctx, m_indexBuffer);
    VulkanBuffer::Destroy(m_ctx, m_vertexBuffer);
    // VkPipelines are owned by VulkanPipelineCache and destroyed by the backend.
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
    m_ctx      = nullptr;
  }

  void VulkanTestPipeline::Draw(VkCommandBuffer cb, VkRenderPass rp, VulkanPipelineCache* cache)
  {
    if (cb == VK_NULL_HANDLE || rp == VK_NULL_HANDLE || m_layout == VK_NULL_HANDLE ||
        cache == nullptr || m_ctx == nullptr)
    {
      return;
    }

    // Build the pipeline desc once per call — desc matches by value so the cache returns the
    // same VkPipeline for every frame targeting the same render pass.
    VulkanPipelineDesc desc{};
    desc.renderPass      = rp;
    desc.vert            = m_vert;
    desc.frag            = m_frag;
    desc.vertexStride    = sizeof(TestVertex);
    desc.attributeCount  = 3;
    desc.attributes[0]   = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TestVertex, pos)};
    desc.attributes[1]   = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TestVertex, color)};
    desc.attributes[2]   = {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(TestVertex, uv)};
    desc.topology        = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.cullMode        = VK_CULL_MODE_NONE;
    desc.frontFace       = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.depthTestEnable  = VK_TRUE;
    desc.depthWriteEnable = VK_TRUE;
    desc.depthCompareOp  = VK_COMPARE_OP_LESS_OR_EQUAL;
    desc.blendEnable     = VK_FALSE;
    desc.colorAttachmentCount = 1;

    VkPipeline pipe = cache->GetOrCreate(m_ctx, m_layout, desc);
    if (pipe == VK_NULL_HANDLE)
    {
      return;
    }

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    if (m_descriptorSet != VK_NULL_HANDLE)
    {
      vkCmdBindDescriptorSets(
          cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &m_descriptorSet, 0, nullptr);
    }
    VkBuffer vb       = m_vertexBuffer.handle;
    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &vb, &zero);
    vkCmdBindIndexBuffer(cb, m_indexBuffer.handle, 0, VK_INDEX_TYPE_UINT16);

    // Stage 5c/6b: two quads rotating in opposite directions at different Z's. Draw order is
    // deliberately near-first → far-second: only a working depth test keeps the far quad from
    // painting over the near one where they overlap.
    m_rotAngle += 0.01f;

    // Quad A — near the camera, rotates around Y.
    Mat4 nearModel = glm::translate(Mat4(1.0f), Vec3(-0.1f, 0.0f, 0.3f));
    nearModel      = glm::rotate(nearModel, m_rotAngle, Vec3(0.0f, 1.0f, 0.0f));
    vkCmdPushConstants(cb, m_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), &nearModel);
    vkCmdDrawIndexed(cb, 6, 1, 0, 0, 0);

    // Quad B — further from the camera, rotates the other way so they interpenetrate visibly.
    Mat4 farModel = glm::translate(Mat4(1.0f), Vec3(0.1f, 0.0f, -0.3f));
    farModel      = glm::rotate(farModel, -m_rotAngle * 1.3f, Vec3(0.0f, 1.0f, 0.0f));
    vkCmdPushConstants(cb, m_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), &farModel);
    vkCmdDrawIndexed(cb, 6, 1, 0, 0, 0);
  }

} // namespace ToolKit
