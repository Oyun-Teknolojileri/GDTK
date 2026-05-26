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
   * Owns the swapchain, its image views + framebuffers, the swapchain render pass, the command
   * pool/buffers, and per-frame sync objects. Recreated on resize / VK_ERROR_OUT_OF_DATE_KHR.
   */
  class TK_API VulkanSwapchain
  {
   public:
    /** Frames-in-flight. Must match RHIConstants::FramesInFlight on the engine side. */
    static constexpr uint FRAMES_IN_FLIGHT = 2;

    VulkanSwapchain();
    ~VulkanSwapchain();

    bool Init(VulkanContext* ctx);
    void Destroy();

    /** Waits the in-flight fence, resets and begins this frame's cb, and tries to acquire the
        next swapchain image. The cb opens even when acquire fails (minimize / out-of-date) so
        engine keeps recording uploads + offscreen work; caller queries IsPresentable() to gate
        the swapchain pass. Returns false only when the swapchain object itself is unusable. */
    bool BeginFrame();

    /** True when this frame acquired a swapchain image and may drive the swapchain pass. */
    bool IsPresentable() const { return m_presentable; }

    /** Closes + submits the cb. Fence-only submit when not presentable so fence cadence stays
        in lockstep with frame count. When presentable, also waits imageAvailable, signals
        renderFinished, and presents. */
    bool EndFrame();

    /** Mid-frame flush: close + submit cb (no semaphores/fence), wait the queue idle, reset and
        re-begin the same cb. Used by per-draw ring overflow recovery. Caller closes any open
        render pass first and restores dynamic state afterwards. */
    bool FlushCommandBuffer();

    /** Begins the swapchain RP with @p clearColor. No-op if already active or no frame. */
    void BeginSwapchainPass(const Vec4& clearColor);

    /** Ends the swapchain RP. No-op if not active. */
    void EndSwapchainPass();

    bool IsSwapchainPassActive() const { return m_swapchainPassActive; }

    /** True between BeginFrame success and EndFrame — the cb is in recording state. */
    bool IsFrameActive() const { return m_frameActive; }

    /** Rebuilds swapchain-dependent objects from the current surface extent. */
    bool Recreate();

    VkCommandBuffer GetCurrentCommandBuffer() const;
    VkRenderPass GetRenderPass() const { return m_renderPass; }

    /** Image acquired by the most recent BeginFrame, or VK_NULL_HANDLE. Backend uses it for
        CopyFramebuffer(src, nullptr, ColorBits) (blit-to-screen). */
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

    // imageAvailable + inFlight are per-FIF (submission side). renderFinished is per-swapchain-
    // image: the present queue may still be waiting on it after the FIF slot recycles, so we
    // can't share it across slots. See:
    // https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
    std::array<VkSemaphore, FRAMES_IN_FLIGHT> m_imageAvailable {};
    std::array<VkFence, FRAMES_IN_FLIGHT> m_inFlight {};
    std::vector<VkSemaphore> m_renderFinished;

    uint m_currentFrame = 0;
    uint m_currentImage = 0;
    bool m_frameActive  = false;
    bool m_swapchainPassActive = false;
    bool m_presentable  = false;
  };

} // namespace ToolKit
