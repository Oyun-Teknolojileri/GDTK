/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanSwapchain.h"

#include "Logger.h"
#include "VulkanContext.h"

#include <algorithm>

namespace ToolKit
{

  VulkanSwapchain::VulkanSwapchain() {}

  VulkanSwapchain::~VulkanSwapchain() { Destroy(); }

  bool VulkanSwapchain::Init(VulkanContext* ctx)
  {
    m_ctx = ctx;
    if (!CreateRenderPass())
    {
      return false;
    }
    if (!CreateSyncObjects())
    {
      return false;
    }
    if (!CreateCommandObjects())
    {
      return false;
    }
    if (!CreateSwapchainObjects())
    {
      return false;
    }
    return true;
  }

  void VulkanSwapchain::Destroy()
  {
    if (m_ctx == nullptr || m_ctx->GetDevice() == VK_NULL_HANDLE)
    {
      return;
    }
    VkDevice device = m_ctx->GetDevice();
    vkDeviceWaitIdle(device);

    // m_renderFinished is per-image; DestroySwapchainObjects handles it.
    DestroySwapchainObjects();

    for (uint i = 0; i < FRAMES_IN_FLIGHT; ++i)
    {
      if (m_imageAvailable[i] != VK_NULL_HANDLE)
      {
        vkDestroySemaphore(device, m_imageAvailable[i], nullptr);
        m_imageAvailable[i] = VK_NULL_HANDLE;
      }
      if (m_inFlight[i] != VK_NULL_HANDLE)
      {
        vkDestroyFence(device, m_inFlight[i], nullptr);
        m_inFlight[i] = VK_NULL_HANDLE;
      }
    }

    if (m_cmdPool != VK_NULL_HANDLE)
    {
      vkDestroyCommandPool(device, m_cmdPool, nullptr);
      m_cmdPool = VK_NULL_HANDLE;
    }

    if (m_renderPass != VK_NULL_HANDLE)
    {
      vkDestroyRenderPass(device, m_renderPass, nullptr);
      m_renderPass = VK_NULL_HANDLE;
    }

    m_ctx = nullptr;
  }

  static VkSurfaceFormatKHR PickSurfaceFormat(VkPhysicalDevice phys, VkSurfaceKHR surface)
  {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &count, formats.data());

    // Prefer sRGB so the swapchain view applies linear→sRGB on store and shaders write linear.
    const VkFormat srgbPrefs[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB};
    for (VkFormat pref : srgbPrefs)
    {
      for (const auto& f : formats)
      {
        if (f.format == pref && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
          return f;
        }
      }
    }

    // Fallback to UNORM — engine sees m_backbufferFormatIsSRGB=false and gamma-encodes in shader.
    const VkFormat unormPrefs[] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM};
    for (VkFormat pref : unormPrefs)
    {
      for (const auto& f : formats)
      {
        if (f.format == pref && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
          return f;
        }
      }
    }

    return formats.empty() ? VkSurfaceFormatKHR {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                           : formats[0];
  }

  static VkPresentModeKHR PickPresentMode(VkPhysicalDevice phys, VkSurfaceKHR surface)
  {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &count, modes.data());

    for (VkPresentModeKHR m : modes)
    {
      if (m == VK_PRESENT_MODE_MAILBOX_KHR)
      {
        return m;
      }
    }
    return VK_PRESENT_MODE_FIFO_KHR; // guaranteed
  }

  bool VulkanSwapchain::CreateSwapchainObjects()
  {
    VkDevice device       = m_ctx->GetDevice();
    VkPhysicalDevice phys = m_ctx->GetPhysicalDevice();
    VkSurfaceKHR surface  = m_ctx->GetSurface();

    VkSurfaceCapabilitiesKHR caps;
    if (VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps); r != VK_SUCCESS)
    {
      TK_ERR("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: %d", r);
      return false;
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX)
    {
      extent = {1280, 720};
    }
    extent.width  = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    if (extent.width == 0 || extent.height == 0)
    {
      // Minimized — retry next frame.
      m_extent = extent;
      return true;
    }

    VkSurfaceFormatKHR surfaceFormat = PickSurfaceFormat(phys, surface);
    m_format                         = surfaceFormat.format;
    m_colorSpace                     = surfaceFormat.colorSpace;
    m_presentMode                    = PickPresentMode(phys, surface);

    uint32_t imageCount              = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
    {
      imageCount = caps.maxImageCount;
    }
    m_minImageCount = caps.minImageCount;

    VkSwapchainCreateInfoKHR ci {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface                = surface;
    ci.minImageCount          = imageCount;
    ci.imageFormat            = m_format;
    ci.imageColorSpace        = m_colorSpace;
    ci.imageExtent            = extent;
    ci.imageArrayLayers       = 1;
    // TRANSFER_DST so CopyFramebuffer(dst=nullptr) can blit into the backbuffer.
    ci.imageUsage             = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.preTransform           = caps.currentTransform;
    ci.compositeAlpha         = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode            = m_presentMode;
    ci.clipped                = VK_TRUE;
    ci.oldSwapchain           = VK_NULL_HANDLE;

    const uint graphicsFamily = m_ctx->GetGraphicsQueueFamily();
    const uint presentFamily  = m_ctx->GetPresentQueueFamily();
    uint32_t indices[]        = {graphicsFamily, presentFamily};
    if (graphicsFamily != presentFamily)
    {
      ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
      ci.queueFamilyIndexCount = 2;
      ci.pQueueFamilyIndices   = indices;
    }
    else
    {
      ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (VkResult r = vkCreateSwapchainKHR(device, &ci, nullptr, &m_swapchain); r != VK_SUCCESS)
    {
      TK_ERR("vkCreateSwapchainKHR failed: %d", r);
      return false;
    }
    m_extent             = extent;

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(device, m_swapchain, &actualCount, nullptr);
    m_images.resize(actualCount);
    vkGetSwapchainImagesKHR(device, m_swapchain, &actualCount, m_images.data());

    m_imageViews.resize(actualCount);
    for (uint i = 0; i < actualCount; ++i)
    {
      VkImageViewCreateInfo ivci {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      ivci.image                           = m_images[i];
      ivci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
      ivci.format                          = m_format;
      ivci.components                      = {VK_COMPONENT_SWIZZLE_IDENTITY,
                                              VK_COMPONENT_SWIZZLE_IDENTITY,
                                              VK_COMPONENT_SWIZZLE_IDENTITY,
                                              VK_COMPONENT_SWIZZLE_IDENTITY};
      ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      ivci.subresourceRange.baseMipLevel   = 0;
      ivci.subresourceRange.levelCount     = 1;
      ivci.subresourceRange.baseArrayLayer = 0;
      ivci.subresourceRange.layerCount     = 1;
      if (VkResult r = vkCreateImageView(device, &ivci, nullptr, &m_imageViews[i]); r != VK_SUCCESS)
      {
        TK_ERR("vkCreateImageView (swapchain %u) failed: %d", i, r);
        return false;
      }
    }

    m_framebuffers.resize(actualCount);
    for (uint i = 0; i < actualCount; ++i)
    {
      VkFramebufferCreateInfo fbci {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      fbci.renderPass      = m_renderPass;
      fbci.attachmentCount = 1;
      fbci.pAttachments    = &m_imageViews[i];
      fbci.width           = extent.width;
      fbci.height          = extent.height;
      fbci.layers          = 1;
      if (VkResult r = vkCreateFramebuffer(device, &fbci, nullptr, &m_framebuffers[i]); r != VK_SUCCESS)
      {
        TK_ERR("vkCreateFramebuffer (swapchain %u) failed: %d", i, r);
        return false;
      }
    }

    // renderFinished is per-image (image count can change on Recreate).
    VkSemaphoreCreateInfo sci {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    m_renderFinished.resize(actualCount, VK_NULL_HANDLE);
    for (uint i = 0; i < actualCount; ++i)
    {
      if (VkResult r = vkCreateSemaphore(device, &sci, nullptr, &m_renderFinished[i]); r != VK_SUCCESS)
      {
        TK_ERR("vkCreateSemaphore (renderFinished %u) failed: %d", i, r);
        return false;
      }
    }

    return true;
  }

  bool VulkanSwapchain::CreateRenderPass()
  {
    VkDevice device                  = m_ctx->GetDevice();

    VkSurfaceFormatKHR surfaceFormat = PickSurfaceFormat(m_ctx->GetPhysicalDevice(), m_ctx->GetSurface());
    m_format                         = surfaceFormat.format;
    m_colorSpace                     = surfaceFormat.colorSpace;

    VkAttachmentDescription color {};
    color.format         = m_format;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef {};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep {};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &color;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;

    if (VkResult r = vkCreateRenderPass(device, &rpci, nullptr, &m_renderPass); r != VK_SUCCESS)
    {
      TK_ERR("vkCreateRenderPass failed: %d", r);
      return false;
    }
    return true;
  }

  bool VulkanSwapchain::CreateSyncObjects()
  {
    VkDevice device = m_ctx->GetDevice();

    VkSemaphoreCreateInfo sci {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fci {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint i = 0; i < FRAMES_IN_FLIGHT; ++i)
    {
      if (vkCreateSemaphore(device, &sci, nullptr, &m_imageAvailable[i]) != VK_SUCCESS ||
          vkCreateFence(device, &fci, nullptr, &m_inFlight[i]) != VK_SUCCESS)
      {
        TK_ERR("VulkanSwapchain: sync object creation failed for frame %u", i);
        return false;
      }
    }
    return true;
  }

  bool VulkanSwapchain::CreateCommandObjects()
  {
    VkDevice device = m_ctx->GetDevice();

    VkCommandPoolCreateInfo pci {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = m_ctx->GetGraphicsQueueFamily();
    if (VkResult r = vkCreateCommandPool(device, &pci, nullptr, &m_cmdPool); r != VK_SUCCESS)
    {
      TK_ERR("vkCreateCommandPool failed: %d", r);
      return false;
    }

    VkCommandBufferAllocateInfo ai {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool        = m_cmdPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = FRAMES_IN_FLIGHT;
    if (VkResult r = vkAllocateCommandBuffers(device, &ai, m_cmdBuffers.data()); r != VK_SUCCESS)
    {
      TK_ERR("vkAllocateCommandBuffers failed: %d", r);
      return false;
    }
    return true;
  }

  void VulkanSwapchain::DestroySwapchainObjects()
  {
    VkDevice device = m_ctx->GetDevice();
    for (VkFramebuffer fb : m_framebuffers)
    {
      if (fb != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(device, fb, nullptr);
      }
    }
    m_framebuffers.clear();

    for (VkImageView v : m_imageViews)
    {
      if (v != VK_NULL_HANDLE)
      {
        vkDestroyImageView(device, v, nullptr);
      }
    }
    m_imageViews.clear();
    m_images.clear();

    for (VkSemaphore s : m_renderFinished)
    {
      if (s != VK_NULL_HANDLE)
      {
        vkDestroySemaphore(device, s, nullptr);
      }
    }
    m_renderFinished.clear();

    if (m_swapchain != VK_NULL_HANDLE)
    {
      vkDestroySwapchainKHR(device, m_swapchain, nullptr);
      m_swapchain = VK_NULL_HANDLE;
    }
  }

  bool VulkanSwapchain::Recreate()
  {
    if (m_ctx == nullptr || m_ctx->GetDevice() == VK_NULL_HANDLE)
    {
      return false;
    }
    vkDeviceWaitIdle(m_ctx->GetDevice());
    DestroySwapchainObjects();
    return CreateSwapchainObjects();
  }

  bool VulkanSwapchain::BeginFrame()
  {
    if (m_ctx == nullptr || m_ctx->GetDevice() == VK_NULL_HANDLE)
    {
      return false;
    }

    VkDevice device = m_ctx->GetDevice();
    m_presentable   = false;

    // Always wait the slot fence (EndFrame submits a fence-only cb when not presentable so the
    // fence cadence stays in lockstep with frame count; DeferDelete relies on this).
    vkWaitForFences(device, 1, &m_inFlight[m_currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &m_inFlight[m_currentFrame]);

    // Reset + begin cb regardless of swapchain state so the engine can keep recording uploads
    // and offscreen passes while the window is minimized.
    VkCommandBuffer cb = m_cmdBuffers[m_currentFrame];
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo bi {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (VkResult br = vkBeginCommandBuffer(cb, &bi); br != VK_SUCCESS)
    {
      TK_ERR("vkBeginCommandBuffer failed: %d", br);
      return false;
    }
    m_swapchainPassActive = false;
    m_frameActive         = true;

    // Acquire is best-effort — backend recreates on failure.
    if (m_swapchain != VK_NULL_HANDLE && m_extent.width != 0 && m_extent.height != 0)
    {
      uint32_t imageIndex = 0;
      VkResult r          = vkAcquireNextImageKHR(device,
                                         m_swapchain,
                                         UINT64_MAX,
                                         m_imageAvailable[m_currentFrame],
                                         VK_NULL_HANDLE,
                                         &imageIndex);
      if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
      {
        m_currentImage = imageIndex;
        m_presentable  = true;
      }
      else if (r != VK_ERROR_OUT_OF_DATE_KHR)
      {
        TK_ERR("vkAcquireNextImageKHR failed: %d", r);
      }
    }
    return true;
  }

  void VulkanSwapchain::BeginSwapchainPass(const Vec4& clearColor)
  {
    if (!m_frameActive || m_swapchainPassActive)
    {
      return;
    }

    VkCommandBuffer cb = m_cmdBuffers[m_currentFrame];

    VkClearValue clear {};
    clear.color = {
        {clearColor.r, clearColor.g, clearColor.b, clearColor.a}
    };

    VkRenderPassBeginInfo rpbi {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass        = m_renderPass;
    rpbi.framebuffer       = m_framebuffers[m_currentImage];
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = m_extent;
    rpbi.clearValueCount   = 1;
    rpbi.pClearValues      = &clear;
    vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp {};
    vp.x        = 0.0f;
    vp.y        = (float) m_extent.height;
    vp.width    = (float) m_extent.width;
    vp.height   = -(float) m_extent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &vp);

    VkRect2D scissor {};
    scissor.offset = {0, 0};
    scissor.extent = m_extent;
    vkCmdSetScissor(cb, 0, 1, &scissor);

    m_swapchainPassActive = true;
  }

  void VulkanSwapchain::EndSwapchainPass()
  {
    if (!m_swapchainPassActive)
    {
      return;
    }
    VkCommandBuffer cb = m_cmdBuffers[m_currentFrame];
    vkCmdEndRenderPass(cb);
    m_swapchainPassActive = false;
  }

  bool VulkanSwapchain::EndFrame()
  {
    if (!m_frameActive)
    {
      return false;
    }

    VkCommandBuffer cb = m_cmdBuffers[m_currentFrame];
    if (m_swapchainPassActive)
    {
      vkCmdEndRenderPass(cb);
      m_swapchainPassActive = false;
    }
    if (VkResult r = vkEndCommandBuffer(cb); r != VK_SUCCESS)
    {
      TK_ERR("vkEndCommandBuffer failed: %d", r);
      m_frameActive = false;
      m_presentable = false;
      return false;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo si {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    if (m_presentable)
    {
      si.waitSemaphoreCount   = 1;
      si.pWaitSemaphores      = &m_imageAvailable[m_currentFrame];
      si.pWaitDstStageMask    = &waitStage;
      si.signalSemaphoreCount = 1;
      si.pSignalSemaphores    = &m_renderFinished[m_currentImage];
    }
    // Else: no acquire — still submit a fence-only cb so the in-flight fence stays in cadence
    // with frame count (DeferDelete keeps tracking even while minimized).

    if (VkResult r = vkQueueSubmit(m_ctx->GetGraphicsQueue(), 1, &si, m_inFlight[m_currentFrame]); r != VK_SUCCESS)
    {
      TK_ERR("vkQueueSubmit failed: %d", r);
      m_frameActive = false;
      m_presentable = false;
      return false;
    }

    bool result = true;
    if (m_presentable)
    {
      VkPresentInfoKHR pi {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
      pi.waitSemaphoreCount = 1;
      pi.pWaitSemaphores    = &m_renderFinished[m_currentImage];
      pi.swapchainCount     = 1;
      pi.pSwapchains        = &m_swapchain;
      pi.pImageIndices      = &m_currentImage;

      VkResult pr           = vkQueuePresentKHR(m_ctx->GetPresentQueue(), &pi);
      if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR)
      {
        result = false;
      }
      else if (pr != VK_SUCCESS)
      {
        TK_ERR("vkQueuePresentKHR failed: %d", pr);
        result = false;
      }
    }

    m_frameActive  = false;
    m_presentable  = false;
    m_currentFrame = (m_currentFrame + 1) % FRAMES_IN_FLIGHT;
    return result;
  }

  bool VulkanSwapchain::FlushCommandBuffer()
  {
    if (!m_frameActive)
    {
      return false;
    }

    VkCommandBuffer cb = m_cmdBuffers[m_currentFrame];

    if (VkResult r = vkEndCommandBuffer(cb); r != VK_SUCCESS)
    {
      TK_ERR("VulkanSwapchain::FlushCommandBuffer: vkEndCommandBuffer failed: %d", r);
      return false;
    }

    // No semaphores/fence — those belong to the terminal EndFrame submission.
    VkSubmitInfo si {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    if (VkResult r = vkQueueSubmit(m_ctx->GetGraphicsQueue(), 1, &si, VK_NULL_HANDLE); r != VK_SUCCESS)
    {
      TK_ERR("VulkanSwapchain::FlushCommandBuffer: vkQueueSubmit failed: %d", r);
      return false;
    }

    // Heavy stall — only acceptable on the recovery path.
    if (VkResult r = vkQueueWaitIdle(m_ctx->GetGraphicsQueue()); r != VK_SUCCESS)
    {
      TK_ERR("VulkanSwapchain::FlushCommandBuffer: vkQueueWaitIdle failed: %d", r);
      return false;
    }

    if (VkResult r = vkResetCommandBuffer(cb, 0); r != VK_SUCCESS)
    {
      TK_ERR("VulkanSwapchain::FlushCommandBuffer: vkResetCommandBuffer failed: %d", r);
      return false;
    }

    VkCommandBufferBeginInfo bi {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (VkResult r = vkBeginCommandBuffer(cb, &bi); r != VK_SUCCESS)
    {
      TK_ERR("VulkanSwapchain::FlushCommandBuffer: vkBeginCommandBuffer failed: %d", r);
      return false;
    }

    return true;
  }

  VkCommandBuffer VulkanSwapchain::GetCurrentCommandBuffer() const { return m_cmdBuffers[m_currentFrame]; }

} // namespace ToolKit
