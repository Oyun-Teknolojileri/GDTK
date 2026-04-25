/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../Types.h"
#include "VulkanBuffer.h"

#include <vulkan/vulkan.h>

#include <functional>
#include <memory>

namespace ToolKit
{

  class VulkanContext;
  struct VulkanTexture;

  /**
   * Stage 2b scaffold: a single fullscreen-triangle pipeline (no vertex input, no descriptors,
   * no push constants). Compiles its shaders once at Init; lazily builds the VkPipeline on first
   * draw against an actual VkRenderPass. If a subsequent draw lands in a different render pass
   * (e.g. the viewport framebuffer was rebuilt), the old pipeline is handed to a deferred-destroy
   * callback and a new one is built.
   *
   * Stage 3+: replaced by a generic pipeline cache keyed by render-state + shader set.
   */
  class TK_API VulkanTestPipeline
  {
   public:
    VulkanTestPipeline()  = default;
    ~VulkanTestPipeline() = default;

    /** Compiles the test triangle shaders and creates the empty VkPipelineLayout. The actual
     *  VkPipeline is built lazily in Draw(). */
    bool Init(VulkanContext* ctx);

    /** Immediate teardown of layout + modules + buffers + descriptor. The cached VkPipelines
     *  themselves live in VulkanPipelineCache and are destroyed by the backend after this runs.
     *  Caller must already have vkDeviceWaitIdle'd. */
    void Destroy();

    /**
     * Issues the test-quad draws on @p cb inside the pass owning @p rp. Pipelines are fetched
     * from @p cache — same desc → same VkPipeline. Stage 6c swapped the old build-on-change
     * path for this cache-driven one in preparation for real material-driven pipelines.
     */
    void Draw(VkCommandBuffer cb, VkRenderPass rp, class VulkanPipelineCache* cache);

   private:
    VulkanContext* m_ctx      = nullptr;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkShaderModule m_vert     = VK_NULL_HANDLE;
    VkShaderModule m_frag     = VK_NULL_HANDLE;

    // Stage 3a/b: hardcoded quad geometry uploaded to device-local vertex + index buffers.
    // Vertex layout: struct { vec3 pos; vec3 color; vec2 uv; } — stride 32, attribs 0/1/2.
    // Index type: UINT16 (4 verts, 6 indices = 2 triangles).
    VulkanBuffer::Buffer m_vertexBuffer{};
    VulkanBuffer::Buffer m_indexBuffer{};

    // Stage 4a/b: 4x4 RGBA8 checkerboard, sampled in the fragment shader via a single-binding
    // combined image-sampler descriptor set. shared_ptr ownership mirrors how engine textures
    // live on Texture::m_gpuData — the dtor frees image/view/sampler. The descriptor set is
    // allocated from VulkanContext's shared pool (FREE_DESCRIPTOR_SET_BIT) so we free it
    // explicitly in Destroy.
    std::shared_ptr<VulkanTexture> m_testTexture;
    VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet          = VK_NULL_HANDLE;

    // Stage 5b: persistently-mapped host-visible UBO carrying (view, proj). Written once at
    // Init time with a static look-at camera; Stage 5c pushes a per-draw rotating model matrix
    // as a push constant so the quad visibly spins.
    VulkanBuffer::Buffer m_cameraUbo{};

    // Stage 5c: accumulator for the quad's rotation. Advanced each Draw — frame-rate dependent
    // but fine for a demo; the engine-owned time source lands with the real Mesh path (Stage 7+).
    float m_rotAngle = 0.0f;
  };

} // namespace ToolKit
