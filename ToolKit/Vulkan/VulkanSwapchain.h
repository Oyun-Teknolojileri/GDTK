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
    static constexpr uint FRAMES_IN_FLIGHT = 2;

    VulkanSwapchain();
    ~VulkanSwapchain();

    bool Init(VulkanContext* ctx);
    void Destroy();

    /**
     * Acquires the next swapchain image, waits for the in-flight fence, begins the command buffer
     * and the main render pass with the requested clear color. After this returns true, the caller
     * can record draw commands into GetCurrentCommandBuffer().
     * Returns false when the swapchain is out-of-date; the caller should call Recreate() and skip
     * this frame.
     */
    bool BeginFrame(const Vec4& clearColor);

    /** Ends the render pass, ends the command buffer, submits to the graphics queue and presents. */
    bool EndFrame();

    /** Tears down swapchain-dependent objects and rebuilds them from the current surface extent. */
    bool Recreate();

    VkCommandBuffer GetCurrentCommandBuffer() const;
    VkRenderPass GetRenderPass() const { return m_renderPass; }
    uint GetImageCount() const { return (uint) m_images.size(); }
    uint GetMinImageCount() const { return m_minImageCount; }
    VkExtent2D GetExtent() const { return m_extent; }
    VkFormat GetFormat() const { return m_format; }

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

    std::array<VkSemaphore, FRAMES_IN_FLIGHT> m_imageAvailable {};
    std::array<VkSemaphore, FRAMES_IN_FLIGHT> m_renderFinished {};
    std::array<VkFence, FRAMES_IN_FLIGHT> m_inFlight {};

    uint m_currentFrame = 0;
    uint m_currentImage = 0;
    bool m_frameActive  = false;
  };

} // namespace ToolKit
