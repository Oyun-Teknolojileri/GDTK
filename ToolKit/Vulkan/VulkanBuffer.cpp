/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanBuffer.h"

#include "../Logger.h"
#include "VulkanContext.h"

#include <vma/vk_mem_alloc.h>

#include <cstring>

namespace ToolKit
{
  namespace VulkanBuffer
  {

    Buffer Create(VulkanContext* ctx, VkBufferUsageFlags usage, VkDeviceSize size, int vmaUsageFlag)
    {
      Buffer out{};
      if (ctx == nullptr || ctx->GetAllocator() == nullptr || size == 0)
      {
        return out;
      }

      VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      bci.size        = size;
      bci.usage       = usage;
      bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

      VmaAllocationCreateInfo aci{};
      aci.usage = (VmaMemoryUsage) vmaUsageFlag;

      if (VkResult r = vmaCreateBuffer(ctx->GetAllocator(), &bci, &aci, &out.handle, &out.alloc, nullptr);
          r != VK_SUCCESS)
      {
        TK_ERR("VulkanBuffer::Create vmaCreateBuffer failed: %d", r);
        out = Buffer{};
        return out;
      }
      out.size = size;
      return out;
    }

    void Destroy(VulkanContext* ctx, Buffer& buf)
    {
      if (ctx == nullptr || buf.handle == VK_NULL_HANDLE)
      {
        buf = Buffer{};
        return;
      }
      vmaDestroyBuffer(ctx->GetAllocator(), buf.handle, buf.alloc);
      buf = Buffer{};
    }

    Buffer CreateHostVisibleMapped(VulkanContext* ctx, VkBufferUsageFlags usage, VkDeviceSize size)
    {
      Buffer out{};
      if (ctx == nullptr || ctx->GetAllocator() == nullptr || size == 0)
      {
        return out;
      }

      VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      bci.size        = size;
      bci.usage       = usage;
      bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

      // CPU_TO_GPU + MAPPED_BIT ? VMA picks HOST_VISIBLE+HOST_COHERENT memory and keeps it
      // persistently mapped for the lifetime of the allocation. We don't need vkFlushMappedMemoryRanges
      // because the memory is host-coherent.
      VmaAllocationCreateInfo aci{};
      aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
      aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

      VmaAllocationInfo info{};
      if (VkResult r = vmaCreateBuffer(ctx->GetAllocator(), &bci, &aci, &out.handle, &out.alloc, &info);
          r != VK_SUCCESS)
      {
        TK_ERR("VulkanBuffer::CreateHostVisibleMapped vmaCreateBuffer failed: %d", r);
        out = Buffer{};
        return out;
      }
      out.size   = size;
      out.mapped = info.pMappedData;
      if (out.mapped == nullptr)
      {
        TK_ERR("VulkanBuffer::CreateHostVisibleMapped: MAPPED_BIT requested but pMappedData null");
        vmaDestroyBuffer(ctx->GetAllocator(), out.handle, out.alloc);
        out = Buffer{};
      }
      return out;
    }

    Buffer UploadDeviceLocal(VulkanContext* ctx, VkBufferUsageFlags usage, const void* data, VkDeviceSize size)
    {
      Buffer out{};
      if (ctx == nullptr || data == nullptr || size == 0)
      {
        return out;
      }

      // Staging buffer: CPU-visible, sized to the payload, used as transfer source.
      Buffer staging = Create(ctx, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size, VMA_MEMORY_USAGE_CPU_TO_GPU);
      if (staging.handle == VK_NULL_HANDLE)
      {
        return out;
      }

      void* mapped = nullptr;
      if (vmaMapMemory(ctx->GetAllocator(), staging.alloc, &mapped) != VK_SUCCESS || mapped == nullptr)
      {
        TK_ERR("VulkanBuffer::UploadDeviceLocal vmaMapMemory failed");
        Destroy(ctx, staging);
        return out;
      }
      std::memcpy(mapped, data, (size_t) size);
      vmaUnmapMemory(ctx->GetAllocator(), staging.alloc);

      // Destination: device-local, with caller's usage + TRANSFER_DST so the copy can land in it.
      out = Create(ctx, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, size, VMA_MEMORY_USAGE_GPU_ONLY);
      if (out.handle == VK_NULL_HANDLE)
      {
        Destroy(ctx, staging);
        return out;
      }

      ctx->EnqueueGpuWork(
          [staging, out, size](VkCommandBuffer cb)
          {
            VkBufferCopy region{};
            region.size = size;
            vkCmdCopyBuffer(cb, staging.handle, out.handle, 1, &region);

            // Memory dependency: the destination buffer is about to be read as
            // vertex/index/uniform input by draws later in this same command buffer
            // (or in subsequent submissions on the same queue, which the implicit
            // submission-order dependency does NOT cover for memory visibility).
            // Without this barrier, NVIDIA's Vulkan ICD has been observed to
            // dereference an unwritten region during vertex fetch -- crash manifests
            // as Access violation @ 0x0 inside nvoglv64.dll mid-vkCmdDraw, only
            // when the upload lands in the same cb as the draw (no FlushAndResetRing
            // waitIdle in between to drain the transfer). dstStage/dstAccess cover
            // the three usages a HOST -> DEVICE_LOCAL upload through this helper can
            // land on: vertex attribute fetch, index fetch, and shader UBO read.
            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask       = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                                          VK_ACCESS_INDEX_READ_BIT |
                                          VK_ACCESS_UNIFORM_READ_BIT |
                                          VK_ACCESS_SHADER_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer              = out.handle;
            barrier.offset              = 0;
            barrier.size                = size;

            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                                     VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0, nullptr,
                                 1, &barrier,
                                 0, nullptr);
          },
          [ctx, staging]() mutable { Destroy(ctx, staging); });
      return out;
    }

  } // namespace VulkanBuffer
} // namespace ToolKit
