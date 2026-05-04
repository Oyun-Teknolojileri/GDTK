/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../Types.h"

#include <vulkan/vulkan.h>

struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

namespace ToolKit
{

  class VulkanContext;

  /**
   * VMA-allocated VkBuffer wrapper + helpers. Stateless — callers own the returned `Buffer` and
   * call Destroy when done. Stage 3a scaffold; Stage 7 will introduce a richer per-resource type
   * (VulkanMesh/VulkanUniformBuffer) that builds on these.
   */
  namespace VulkanBuffer
  {

    struct Buffer
    {
      VkBuffer handle     = VK_NULL_HANDLE;
      VmaAllocation alloc = VK_NULL_HANDLE;
      VkDeviceSize size   = 0;
      /** Non-null only for buffers created via CreateHostVisibleMapped — the memory is
       *  persistently mapped by VMA for the lifetime of the allocation. memcpy directly. */
      void* mapped        = nullptr;
    };

    /**
     * Creates an uninitialized VkBuffer with the given usage + memory placement.
     * @param vmaUsageFlag - one of VMA_MEMORY_USAGE_* (e.g., GPU_ONLY for device-local,
     *                      CPU_TO_GPU for staging or persistently-mapped uploads).
     * Returns a Buffer with handle == VK_NULL_HANDLE on failure.
     */
    Buffer Create(VulkanContext* ctx, VkBufferUsageFlags usage, VkDeviceSize size, int vmaUsageFlag);

    /**
     * Allocates a HOST_VISIBLE + HOST_COHERENT buffer with VMA persistent mapping. The returned
     * Buffer has `mapped != nullptr`; caller writes via memcpy and does not need to flush.
     * Intended for per-frame uniform buffers updated from the CPU. Returns handle == VK_NULL_HANDLE
     * on failure.
     */
    Buffer CreateHostVisibleMapped(VulkanContext* ctx, VkBufferUsageFlags usage, VkDeviceSize size);

    /** Frees the VkBuffer + VMA allocation. Safe to call on a default-constructed Buffer. */
    void Destroy(VulkanContext* ctx, Buffer& buf);

    /**
     * Allocates a DEVICE_LOCAL buffer of @p size with @p usage | TRANSFER_DST and queues an upload
     * of @p data through a temporary staging buffer (CPU_TO_GPU) via VulkanContext::EnqueueGpuWork.
     * The recorded copy runs on the swapchain command buffer at the next BeginFrame; the staging
     * buffer is destroyed by the engine's frame-fenced deletion queue once the GPU has retired
     * the recorded work. Returns the destination Buffer immediately; its handle is valid for
     * binding right away, but contents are only visible to the GPU after the upload runs.
     * Returns a Buffer with handle == VK_NULL_HANDLE on failure.
     */
    Buffer UploadDeviceLocal(VulkanContext* ctx, VkBufferUsageFlags usage, const void* data, VkDeviceSize size);

  } // namespace VulkanBuffer

} // namespace ToolKit
