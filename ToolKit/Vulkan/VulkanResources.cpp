/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanResources.h"

#include "VulkanContext.h"

#include <vma/vk_mem_alloc.h>

namespace ToolKit
{

  VkFormat ToVkFormat(GraphicTypes format)
  {
    switch (format)
    {
      case GraphicTypes::FormatR8:                return VK_FORMAT_R8_UNORM;
      case GraphicTypes::FormatRG8:               return VK_FORMAT_R8G8_UNORM;
      case GraphicTypes::FormatRGB8:              return VK_FORMAT_R8G8B8_UNORM;
      case GraphicTypes::FormatRGBA8:
      case GraphicTypes::FormatRGBA:              return VK_FORMAT_R8G8B8A8_UNORM;
      case GraphicTypes::FormatSRGB8_A8:          return VK_FORMAT_R8G8B8A8_SRGB;
      case GraphicTypes::FormatR16F:              return VK_FORMAT_R16_SFLOAT;
      case GraphicTypes::FormatRG16F:             return VK_FORMAT_R16G16_SFLOAT;
      case GraphicTypes::FormatRGB16F:            return VK_FORMAT_R16G16B16_SFLOAT;
      case GraphicTypes::FormatRGBA16F:           return VK_FORMAT_R16G16B16A16_SFLOAT;
      case GraphicTypes::FormatR32F:              return VK_FORMAT_R32_SFLOAT;
      case GraphicTypes::FormatRG32F:             return VK_FORMAT_R32G32_SFLOAT;
      case GraphicTypes::FormatRGB32F:            return VK_FORMAT_R32G32B32_SFLOAT;
      case GraphicTypes::FormatRGBA32F:           return VK_FORMAT_R32G32B32A32_SFLOAT;
      case GraphicTypes::FormatR16SNorm:          return VK_FORMAT_R16_SNORM;
      // VK_FORMAT_X8_D24_UNORM_PACK32 is rarely supported as a depth attachment; fall back to
      // D32_SFLOAT which every desktop GPU supports and gives better precision.
      case GraphicTypes::FormatDepth24:           return VK_FORMAT_D32_SFLOAT;
      case GraphicTypes::FormatDepth24Stencil8:   return VK_FORMAT_D24_UNORM_S8_UINT;
      default:                                    return VK_FORMAT_UNDEFINED;
    }
  }

  bool IsDepthFormat(VkFormat format)
  {
    switch (format)
    {
      case VK_FORMAT_D16_UNORM:
      case VK_FORMAT_X8_D24_UNORM_PACK32:
      case VK_FORMAT_D32_SFLOAT:
      case VK_FORMAT_D16_UNORM_S8_UINT:
      case VK_FORMAT_D24_UNORM_S8_UINT:
      case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;
      default:
        return false;
    }
  }

  VulkanTexture::~VulkanTexture()
  {
    if (context == nullptr)
    {
      return;
    }

    VkDevice device       = context->GetDevice();
    VmaAllocator allocator = context->GetAllocator();

    if (sampler != VK_NULL_HANDLE)
    {
      vkDestroySampler(device, sampler, nullptr);
      sampler = VK_NULL_HANDLE;
    }
    if (view != VK_NULL_HANDLE)
    {
      vkDestroyImageView(device, view, nullptr);
      view = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE)
    {
      vmaDestroyImage(allocator, image, allocation);
      image      = VK_NULL_HANDLE;
      allocation = VK_NULL_HANDLE;
    }
  }

  void VulkanFramebuffer::ReleaseLazyObjects()
  {
    if (context == nullptr)
    {
      return;
    }

    VkDevice device = context->GetDevice();
    if (framebuffer != VK_NULL_HANDLE)
    {
      vkDestroyFramebuffer(device, framebuffer, nullptr);
      framebuffer = VK_NULL_HANDLE;
    }
    if (renderPass != VK_NULL_HANDLE)
    {
      vkDestroyRenderPass(device, renderPass, nullptr);
      renderPass = VK_NULL_HANDLE;
    }
  }

  void VulkanFramebuffer::ReleaseOwnedViews()
  {
    if (context == nullptr)
    {
      return;
    }
    VkDevice device = context->GetDevice();

    auto releaseSlot = [device](Slot& s)
    {
      if (s.ownsView && s.view != VK_NULL_HANDLE)
      {
        vkDestroyImageView(device, s.view, nullptr);
      }
      s.view     = VK_NULL_HANDLE;
      s.ownsView = false;
    };

    for (int i = 0; i < kMaxColorAttachments; ++i)
    {
      releaseSlot(colorAttachments[i]);
    }
    releaseSlot(depthAttachment);
  }

  VulkanFramebuffer::~VulkanFramebuffer()
  {
    ReleaseOwnedViews();
    ReleaseLazyObjects();
  }

  VulkanUniformBuffer::~VulkanUniformBuffer()
  {
    if (context == nullptr || buffer == VK_NULL_HANDLE)
    {
      return;
    }
    VmaAllocator allocator = context->GetAllocator();
    vmaDestroyBuffer(allocator, buffer, alloc);
    buffer  = VK_NULL_HANDLE;
    alloc   = VK_NULL_HANDLE;
    mapped  = nullptr;
  }

} // namespace ToolKit
