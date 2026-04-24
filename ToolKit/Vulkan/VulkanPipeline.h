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

namespace ToolKit
{

  class VulkanContext;

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

    /** Immediate teardown of layout, modules, and the cached pipeline. Caller must already have
     *  vkDeviceWaitIdle'd. */
    void Destroy();

    /**
     * Binds (building if needed against @p rp) and emits vkCmdDraw(3,1,0,0).
     * @param deferDestroyPipeline - called with the previous VkPipeline when @p rp changes.
     *        Caller is expected to push it onto VulkanBackend's frame-safe deletion queue.
     */
    void Draw(VkCommandBuffer cb,
              VkRenderPass rp,
              const std::function<void(VkPipeline)>& deferDestroyPipeline);

   private:
    bool BuildPipeline(VkRenderPass rp);

    VulkanContext* m_ctx      = nullptr;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline     = VK_NULL_HANDLE;
    VkRenderPass m_builtFor   = VK_NULL_HANDLE;
    VkShaderModule m_vert     = VK_NULL_HANDLE;
    VkShaderModule m_frag     = VK_NULL_HANDLE;

    // Stage 3a: hardcoded test geometry uploaded to a device-local vertex buffer.
    // Layout: struct { vec3 pos; vec3 color; } — stride 24, attribs 0/1.
    VulkanBuffer::Buffer m_vertexBuffer{};
  };

} // namespace ToolKit
