/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Types.h"

#include <vulkan/vulkan.h>

struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

namespace ToolKit
{

  class VulkanContext;

  /** VMA-allocated VkBuffer wrapper + helpers. Caller owns the returned Buffer. */
  namespace VulkanBuffer
  {

    struct Buffer
    {
      VkBuffer handle     = VK_NULL_HANDLE;
      VmaAllocation alloc = VK_NULL_HANDLE;
      VkDeviceSize size   = 0;
      /** Non-null only for CreateHostVisibleMapped buffers. memcpy directly. */
      void* mapped        = nullptr;
    };

    /** @p vmaUsageFlag = VMA_MEMORY_USAGE_* (GPU_ONLY for device-local, CPU_TO_GPU for staging). */
    Buffer Create(VulkanContext* ctx, VkBufferUsageFlags usage, VkDeviceSize size, int vmaUsageFlag);

    /** HOST_VISIBLE + HOST_COHERENT, persistently mapped. */
    Buffer CreateHostVisibleMapped(VulkanContext* ctx, VkBufferUsageFlags usage, VkDeviceSize size);

    void Destroy(VulkanContext* ctx, Buffer& buf);

    /** Allocates DEVICE_LOCAL buffer + queues an upload through a temporary staging buffer.
        The destination handle is valid immediately; content is visible to the GPU after the
        recorded copy retires (staging buffer is freed by the frame-fenced deletion queue). */
    Buffer UploadDeviceLocal(VulkanContext* ctx, VkBufferUsageFlags usage, const void* data, VkDeviceSize size);

  } // namespace VulkanBuffer

} // namespace ToolKit
