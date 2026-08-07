/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "VulkanResources.h"

#include "VulkanContext.h"

#include <vk_mem_alloc.h>

namespace ToolKit
{

  VkFormat ToVkFormat(GraphicTypes format)
  {
    switch (format)
    {
      case GraphicTypes::FormatR8:
        return VK_FORMAT_R8_UNORM;
      case GraphicTypes::FormatRG8:
        return VK_FORMAT_R8G8_UNORM;
      case GraphicTypes::FormatRGB8:
        return VK_FORMAT_R8G8B8_UNORM;
      case GraphicTypes::FormatRGBA8:
      case GraphicTypes::FormatRGBA:
        return VK_FORMAT_R8G8B8A8_UNORM;
      case GraphicTypes::FormatSRGB8_A8:
        return VK_FORMAT_R8G8B8A8_SRGB;
      case GraphicTypes::FormatR16F:
        return VK_FORMAT_R16_SFLOAT;
      case GraphicTypes::FormatRG16F:
        return VK_FORMAT_R16G16_SFLOAT;
      case GraphicTypes::FormatRGB16F:
        return VK_FORMAT_R16G16B16_SFLOAT;
      case GraphicTypes::FormatRGBA16F:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
      case GraphicTypes::FormatR32F:
        return VK_FORMAT_R32_SFLOAT;
      case GraphicTypes::FormatRG32F:
        return VK_FORMAT_R32G32_SFLOAT;
      case GraphicTypes::FormatRGB32F:
        return VK_FORMAT_R32G32B32_SFLOAT;
      case GraphicTypes::FormatRGBA32F:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
      case GraphicTypes::FormatR16SNorm:
        return VK_FORMAT_R16_SNORM;
      // D32_SFLOAT — wider support + better precision than X8_D24_UNORM_PACK32.
      case GraphicTypes::FormatDepth24:
        return VK_FORMAT_D32_SFLOAT;
      case GraphicTypes::FormatDepth24Stencil8:
        return VK_FORMAT_D24_UNORM_S8_UINT;
      default:
        return VK_FORMAT_UNDEFINED;
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

  bool IsStencilFormat(VkFormat format)
  {
    switch (format)
    {
      case VK_FORMAT_S8_UINT:
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

    VkDevice device        = context->GetDevice();
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
    for (SubresourceViewEntry& e : subresourceViews)
    {
      if (e.valid && e.view != VK_NULL_HANDLE)
      {
        vkDestroyImageView(device, e.view, nullptr);
      }
      e.view  = VK_NULL_HANDLE;
      e.valid = false;
    }
    subresourceViews.clear();
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
    for (FbCacheEntry& e : fbCache)
    {
      if (e.valid && e.fb != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(device, e.fb, nullptr);
      }
      e.fb        = VK_NULL_HANDLE;
      e.viewCount = 0;
      e.valid     = false;
    }
    framebuffer = VK_NULL_HANDLE;

    for (RpVariant& v : rpVariants)
    {
      if (v.valid && v.rp != VK_NULL_HANDLE)
      {
        vkDestroyRenderPass(device, v.rp, nullptr);
      }
      v.rp        = VK_NULL_HANDLE;
      v.clearBits = GraphicBitFields::None;
      v.valid     = false;
    }
    renderPass = VK_NULL_HANDLE;
  }

  void VulkanFramebuffer::ReleaseOwnedViews()
  {
    if (context == nullptr)
    {
      return;
    }
    VkDevice device  = context->GetDevice();

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
    if (context != nullptr && buffer.handle != VK_NULL_HANDLE)
    {
      VulkanBuffer::Destroy(context, buffer);
    }
  }

  VulkanMesh::~VulkanMesh()
  {
    if (context != nullptr)
    {
      VulkanBuffer::Destroy(context, vertex);
      VulkanBuffer::Destroy(context, index);
    }
  }

  VulkanShaderModule::~VulkanShaderModule()
  {
    if (context != nullptr && module != VK_NULL_HANDLE)
    {
      vkDestroyShaderModule(context->GetDevice(), module, nullptr);
      module = VK_NULL_HANDLE;
    }
  }

  VulkanGpuProgram::~VulkanGpuProgram()
  {
    if (context == nullptr)
    {
      return;
    }
    VkDevice device = context->GetDevice();
    if (pipelineLayout != VK_NULL_HANDLE)
    {
      vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
      pipelineLayout = VK_NULL_HANDLE;
    }
    // Descriptor set layout is owned by VulkanContext — do not destroy here.
  }

} // namespace ToolKit
