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
    static constexpr uint FRAMES_IN_FLIGHT = 2;

    VulkanSwapchain();
    ~VulkanSwapchain();

    bool Init(VulkanContext* ctx);
    void Destroy();

    /**
     * Waits for the in-flight fence, resets and begins this frame's command buffer, and tries to
     * acquire the next swapchain image. The command buffer is opened **even when the acquire
     * fails** (window minimized, swapchain out-of-date) so the engine can keep recording GPU work
     * — uploads, offscreen passes, etc. — while the window isn't presentable. Caller queries
     * IsPresentable() to decide whether to drive the swapchain render pass / final blit.
     * Returns false only when the swapchain object itself is unusable (pre-init or device lost).
     */
    bool BeginFrame();

    /** True when this frame successfully acquired a swapchain image and is safe to drive the
     *  swapchain render pass + final present. False during minimize / between out-of-date and the
     *  next Recreate() — backend should still record cb and EndFrame() will submit it, but the
     *  swapchain-specific work must be skipped. Valid only between BeginFrame() and EndFrame(). */
    bool IsPresentable() const { return m_presentable; }

    /** Closes the command buffer and submits it (fence-only submission when not presentable, so
     *  fences stay in lockstep with frame count and DeferDelete keeps draining each frame). When
     *  IsPresentable(), additionally waits on the image-available semaphore, signals the
     *  per-image renderFinished semaphore, and calls vkQueuePresentKHR. */
    bool EndFrame();

    /** Mid-frame command buffer flush. Closes the current cmd buffer (without semaphores or
     *  the in-flight fence — those belong to EndFrame's terminal submission), submits it,
     *  waits for the graphics queue to go idle, then resets and re-begins the same cmd buffer
     *  so the rest of the frame can keep recording. Used by the backend to recover from
     *  per-draw ring overflow without growing the ring: drain queued GPU work, then reset the
     *  ring head from a known-empty state. Caller is responsible for ending any open render
     *  pass before calling and for restoring dynamic state (viewport/scissor) afterwards. */
    bool FlushCommandBuffer();

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
    /** Image acquired by the most recent BeginFrame. VK_NULL_HANDLE if no frame is in flight.
     *  Exposed so the backend can blit into the backbuffer outside the swapchain render pass
     *  (engine path: `CopyFramebuffer(src, nullptr, ColorBits)` — splash screen, post-process
     *  composite). */
    VkImage GetCurrentImage() const
    {
      return (m_currentImage < m_images.size()) ? m_images[m_currentImage] : VK_NULL_HANDLE;
    }
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

    /** True when the current frame acquired a swapchain image; false during minimize / between
     *  out-of-date and recreate. EndFrame's present path keys on this. */
    bool m_presentable  = false;
  };

} // namespace ToolKit
