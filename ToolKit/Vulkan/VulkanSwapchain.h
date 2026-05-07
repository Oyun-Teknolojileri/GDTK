/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../Types.h"

#include <vulkan/vulkan.h>

#include <array>
#include <vector>

namespace ToolKit
{

  class VulkanContext;

  /**
   * Owns the swapchain, its image views + framebuffers, a single-color render pass used to drive
   * clear + ImGui composition, the command pool/buffers, and the per-frame sync objects needed to
   * acquire + submit + present. Recreated on window resize (detected via VK_ERROR_OUT_OF_DATE_KHR
   * or VK_SUBOPTIMAL_KHR).
   */
  class TK_API VulkanSwapchain
  {
   public:
    // Set to 1 to fully serialize CPU recording and GPU execution per frame: each BeginFrame
    // waits on the in-flight fence so no two cb's ever execute concurrently on the queue.
    // Eliminates cross-cb GPU hazards that subpass-EXTERNAL deps don't reliably bridge across
    // submissions on this driver/architecture (observed: editor viewport intermittently shows
    // intermediate offscreen-pass content with FRAMES_IN_FLIGHT=2, even with ping-pong'd RTs
    // and tightened RP deps). Cost is minimal in the editor: GPU is the bottleneck and CPU
    // pipelining doesn't hide much there. Swapchain image double/triple-buffering for tear-free
    // presentation is independent of this and continues to operate.
    static constexpr uint FRAMES_IN_FLIGHT = 1;

    VulkanSwapchain();
    ~VulkanSwapchain();

    bool Init(VulkanContext* ctx);
    void Destroy();

    /**
     * Acquires the next swapchain image, waits for the in-flight fence and begins the command
     * buffer. Does **not** begin the render pass � the caller drives pass boundaries through
     * BeginSwapchainPass / EndSwapchainPass so offscreen passes can be interleaved on the same
     * command buffer.
     * Returns false when the swapchain is out-of-date; caller should Recreate() and skip frame.
     */
    bool BeginFrame();

    /** Ends any still-active swapchain pass (defensive), closes the command buffer, submits and
     *  presents. */
    bool EndFrame();

    /** Begins the swapchain's main render pass with @p clearColor as loadOp value. No-op if a
     *  swapchain pass is already active or no frame is in flight. */
    void BeginSwapchainPass(const Vec4& clearColor);

    /** Ends the swapchain render pass opened by BeginSwapchainPass. No-op if not active. */
    void EndSwapchainPass();

    bool IsSwapchainPassActive() const { return m_swapchainPassActive; }

    /** True between BeginFrame() success and EndFrame() return � the cmd buffer is in
        recording state during this window. Used by the backend to gate Draw / Bind* calls
        that may fire before any frame has begun (engine init, hot-reload, etc.). */
    bool IsFrameActive() const { return m_frameActive; }

    /** Tears down swapchain-dependent objects and rebuilds them from the current surface extent. */
    bool Recreate();

    VkCommandBuffer GetCurrentCommandBuffer() const;
    VkRenderPass GetRenderPass() const { return m_renderPass; }
    uint GetImageCount() const { return (uint) m_images.size(); }
    uint GetMinImageCount() const { return m_minImageCount; }
    VkExtent2D GetExtent() const { return m_extent; }
    VkFormat GetFormat() const { return m_format; }
    uint GetCurrentFrameIndex() const { return m_currentFrame; }

   private:
    bool CreateSwapchainObjects();
    bool CreateRenderPass();
    bool CreateSyncObjects();
    bool CreateCommandObjects();
    void DestroySwapchainObjects();

   private:
    VulkanContext* m_ctx           = nullptr;

    VkSwapchainKHR m_swapchain     = VK_NULL_HANDLE;
    VkFormat m_format              = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR m_colorSpace   = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D m_extent            = {0, 0};
    uint m_minImageCount           = 0;

    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<VkFramebuffer> m_framebuffers;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    VkCommandPool m_cmdPool   = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, FRAMES_IN_FLIGHT> m_cmdBuffers {};

    // imageAvailable + inFlight are per-frame-in-flight (submission side).
    // renderFinished is per-swapchain-image: the present queue may keep waiting on this semaphore
    // until the image is reacquired, which can outlive the FRAMES_IN_FLIGHT slot recycle. Sharing
    // the per-frame slot would let us re-submit a still-pending semaphore. See:
    // https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
    std::array<VkSemaphore, FRAMES_IN_FLIGHT> m_imageAvailable {};
    std::array<VkFence, FRAMES_IN_FLIGHT> m_inFlight {};
    std::vector<VkSemaphore> m_renderFinished;

    uint m_currentFrame = 0;
    uint m_currentImage = 0;
    bool m_frameActive  = false;
    bool m_swapchainPassActive = false;
  };

} // namespace ToolKit
