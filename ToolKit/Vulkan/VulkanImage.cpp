/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanImage.h"

#include "../Logger.h"
#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanResources.h"

#include <vma/vk_mem_alloc.h>

#include <cstring>

namespace ToolKit
{
  namespace VulkanImage
  {

    std::shared_ptr<VulkanTexture> CreateSampled2DFromData(VulkanContext* ctx,
                                                           VkFormat format,
                                                           uint32_t width,
                                                           uint32_t height,
                                                           const void* pixels,
                                                           VkDeviceSize byteCount)
    {
      if (ctx == nullptr || pixels == nullptr || byteCount == 0 || width == 0 || height == 0)
      {
        return nullptr;
      }

      auto tex            = std::make_shared<VulkanTexture>();
      tex->context        = ctx;
      tex->format         = format;
      tex->aspect         = VK_IMAGE_ASPECT_COLOR_BIT;
      tex->extent         = {width, height};
      tex->arrayLayers    = 1;
      tex->mipLevels      = 1;
      tex->isCubemap      = false;
      tex->currentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;

      VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      imageInfo.imageType     = VK_IMAGE_TYPE_2D;
      imageInfo.format        = format;
      imageInfo.extent        = {width, height, 1};
      imageInfo.mipLevels     = 1;
      imageInfo.arrayLayers   = 1;
      imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
      imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
      imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
      imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

      VmaAllocationCreateInfo allocInfo{};
      allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

      if (vmaCreateImage(ctx->GetAllocator(), &imageInfo, &allocInfo, &tex->image, &tex->allocation, nullptr) !=
          VK_SUCCESS)
      {
        TK_ERR("VulkanImage::CreateSampled2DFromData vmaCreateImage failed");
        return nullptr;
      }

      VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      viewInfo.image                       = tex->image;
      viewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format                      = format;
      viewInfo.subresourceRange.aspectMask = tex->aspect;
      viewInfo.subresourceRange.levelCount = 1;
      viewInfo.subresourceRange.layerCount = 1;
      if (vkCreateImageView(ctx->GetDevice(), &viewInfo, nullptr, &tex->view) != VK_SUCCESS)
      {
        TK_ERR("VulkanImage::CreateSampled2DFromData vkCreateImageView failed");
        return nullptr; // ~VulkanTexture cleans up the partially-built image.
      }

      VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
      samplerInfo.magFilter    = VK_FILTER_LINEAR;
      samplerInfo.minFilter    = VK_FILTER_LINEAR;
      samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      samplerInfo.minLod       = 0.0f;
      samplerInfo.maxLod       = 1.0f;
      if (vkCreateSampler(ctx->GetDevice(), &samplerInfo, nullptr, &tex->sampler) != VK_SUCCESS)
      {
        TK_ERR("VulkanImage::CreateSampled2DFromData vkCreateSampler failed");
        return nullptr;
      }

      VulkanBuffer::Buffer staging =
          VulkanBuffer::Create(ctx, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, byteCount, VMA_MEMORY_USAGE_CPU_TO_GPU);
      if (staging.handle == VK_NULL_HANDLE)
      {
        return nullptr;
      }
      void* mapped = nullptr;
      if (vmaMapMemory(ctx->GetAllocator(), staging.alloc, &mapped) != VK_SUCCESS || mapped == nullptr)
      {
        TK_ERR("VulkanImage::CreateSampled2DFromData vmaMapMemory failed");
        VulkanBuffer::Destroy(ctx, staging);
        return nullptr;
      }
      std::memcpy(mapped, pixels, (size_t) byteCount);
      vmaUnmapMemory(ctx->GetAllocator(), staging.alloc);

      ctx->EnqueueGpuWork(
          [img    = tex->image,
           aspect = tex->aspect,
           staging,
           width,
           height](VkCommandBuffer cb)
          {
            // UNDEFINED → TRANSFER_DST
            VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toDst.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
            toDst.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toDst.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            toDst.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            toDst.image                           = img;
            toDst.subresourceRange.aspectMask     = aspect;
            toDst.subresourceRange.levelCount     = 1;
            toDst.subresourceRange.layerCount     = 1;
            toDst.srcAccessMask                   = 0;
            toDst.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &toDst);

            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = aspect;
            region.imageSubresource.layerCount = 1;
            region.imageExtent                 = {width, height, 1};
            vkCmdCopyBufferToImage(cb, staging.handle, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            // TRANSFER_DST → SHADER_READ_ONLY
            VkImageMemoryBarrier toShader{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toShader.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toShader.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toShader.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            toShader.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            toShader.image                           = img;
            toShader.subresourceRange.aspectMask     = aspect;
            toShader.subresourceRange.levelCount     = 1;
            toShader.subresourceRange.layerCount     = 1;
            toShader.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
            toShader.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &toShader);
          },
          [ctx, staging]() mutable { VulkanBuffer::Destroy(ctx, staging); });

      tex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      return tex;
    }

  } // namespace VulkanImage
} // namespace ToolKit
