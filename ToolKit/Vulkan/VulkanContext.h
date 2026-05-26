/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../Types.h"
#include "VulkanBuffer.h"

#include <vulkan/vulkan.h>

#include <array>
#include <functional>
#include <vector>

struct VmaAllocator_T;
typedef struct VmaAllocator_T* VmaAllocator;

namespace ToolKit
{

  /**
   * Owns the foundational Vulkan objects: instance, debug messenger, surface, physical and
   * logical device, queues, VMA allocator, shared + per-frame descriptor pools, and the
   * persistent per-draw UBO ring. Platform-agnostic — caller supplies the instance extensions
   * and a surface factory callback.
   */
  class TK_API VulkanContext
  {
   public:
    VulkanContext();
    ~VulkanContext();

    /**
     * Creates instance, surface, device, VMA, descriptor pool.
     * @param instanceExtensions - platform-specific extension names.
     * @param surfaceFactory     - given the created VkInstance (as void*), returns VkSurfaceKHR (as uint64) or 0.
     */
    bool Init(const std::vector<const char*>& instanceExtensions,
              const std::function<uint64 (void*)>& surfaceFactory);

    /** Tears everything down in reverse order. Safe on a partially-initialized context. */
    void Destroy();

    VkInstance GetInstance() const { return m_instance; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetDevice() const { return m_device; }
    VkSurfaceKHR GetSurface() const { return m_surface; }

    VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue GetPresentQueue() const { return m_presentQueue; }
    uint GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    uint GetPresentQueueFamily() const { return m_presentQueueFamily; }

    VmaAllocator GetAllocator() const { return m_allocator; }
    VkDescriptorPool GetSharedDescriptorPool() const { return m_descriptorPool; }

    /** Single descriptor set layout shared by every VulkanGpuProgram. Programs only touching a
        subset of bindings work fine — unused entries are ignored at write time. */
    VkDescriptorSetLayout GetGlobalDescriptorSetLayout() const { return m_globalDescriptorSetLayout; }

    /** Per-frame descriptor pool for transient sets. One pool per FIF slot — reset at BeginFrame
        to release every set in one call. ImGui and other long-lived systems use the shared pool
        which is never reset. */
    VkDescriptorPool GetFrameDescriptorPool(uint frameIndex) const;

    /** Resets the frame's pool. Caller guarantees no in-flight cb still references its sets. */
    void ResetFrameDescriptorPool(uint frameIndex);

    /** Allocates a set of @p layout from the per-frame pool. VK_NULL_HANDLE on exhaustion. */
    VkDescriptorSet AllocateFrameDescriptorSet(uint frameIndex, VkDescriptorSetLayout layout);

    // -- Per-draw UBO ring --
    // Persistent-mapped HOST_VISIBLE buffer partitioned per FIF slot. SubmitPerDrawData appends
    // an aligned slice; offset travels through vkCmdBindDescriptorSets pDynamicOffsets.

    VkBuffer GetPerDrawUboBuffer() const { return m_perDrawUboRing.handle; }
    VkDeviceSize GetPerDrawUboCapacity() const { return m_perDrawUboRing.size; }

    /** Re-bases @p slot's head to its region start and latches @p slot as the current slot.
        Caller guarantees no in-flight cb still reads from this region (BeginFrame after fence
        wait, or mid-frame FlushAndResetRing after queue drain). */
    void ResetPerDrawUboRing(uint slot)
    {
      m_currentRingSlot          = slot;
      m_perDrawUboHeads[slot]    = (VkDeviceSize) slot * m_perDrawUboRegionSize;
      m_perDrawUboOverflowLogged = false;
    }

    /** Reserves aligned @p size bytes in the current slot's region. Logs once on overflow. */
    bool AllocatePerDrawSlot(VkDeviceSize size, VkDeviceSize& outOffset, void*& outMappedPtr);

    /**
     * Records @p recorder onto the engine's current frame command buffer.
     *
     *   - Frame active, no RP open: recorder fires inline against the current cb;
     *     postFlushCleanup is handed to the deferred-delete sink.
     *   - Frame active, RP open: parked in a during-RP queue, replayed when the RP closes
     *     (barriers + copies are illegal mid-RP).
     *   - No frame active: queued and replayed at the next BeginFrame before any pass starts.
     */
    void EnqueueGpuWork(std::function<void(VkCommandBuffer)> recorder,
                        std::function<void()> postFlushCleanup = {});

    /** Replays the no-frame queue into @p cb. Returns the cleanup callbacks for the caller's
        frame-fenced deletion queue. Called by VulkanBackend at BeginFrame. */
    std::vector<std::function<void()>> FlushPendingGpuWork(VkCommandBuffer cb);

    /** Replays the during-RP queue into @p cb right after vkCmdEndRenderPass. */
    std::vector<std::function<void()>> FlushDuringRenderPassWork(VkCommandBuffer cb);

    /** Backend tells us which cb is "current" between BeginFrame and Present, plus an RP-active
        query. When cb is set, EnqueueGpuWork records inline (or parks during-RP). Pass
        VK_NULL_HANDLE + {} + {} to clear at frame end. */
    void SetCurrentRecordingCb(VkCommandBuffer cb,
                               std::function<void(std::function<void()>)> inlineCleanupSink,
                               std::function<bool()> renderPassActiveQuery);

    PFN_vkCmdBeginDebugUtilsLabelEXT m_vkCmdBeginDebugUtilsLabelEXT = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT m_vkCmdEndDebugUtilsLabelEXT     = nullptr;

   private:
    bool CreateInstance(const std::vector<const char*>& requiredExtensions);
    bool CreateDebugMessenger();
    bool CreateSurface(const std::function<uint64 (void*)>& factory);
    bool PickPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateAllocator();
    bool CreateDescriptorPool();
    bool CreateGlobalDescriptorSetLayout();
    bool CreateFrameDescriptorPools();
    bool CreatePerDrawUboRing();

   private:
    VkInstance m_instance                       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger   = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface                      = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice           = VK_NULL_HANDLE;
    VkDevice m_device                           = VK_NULL_HANDLE;

    VkQueue m_graphicsQueue                     = VK_NULL_HANDLE;
    VkQueue m_presentQueue                      = VK_NULL_HANDLE;
    uint m_graphicsQueueFamily                  = (uint) -1;
    uint m_presentQueueFamily                   = (uint) -1;

    VmaAllocator m_allocator                    = nullptr;
    VkDescriptorPool m_descriptorPool           = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_globalDescriptorSetLayout = VK_NULL_HANDLE;

    /** One pool per FIF slot. */
    std::array<VkDescriptorPool, 2> m_perFrameDescriptorPools = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    /** Per-draw UBO ring partitioned into FIF equal regions. Slot S writes only to
        [S*regionSize, (S+1)*regionSize); the other slot's region stays untouched. */
    VulkanBuffer::Buffer m_perDrawUboRing{};
    std::array<VkDeviceSize, 2> m_perDrawUboHeads = {0, 0};
    VkDeviceSize m_perDrawUboRegionSize        = 0;
    uint m_currentRingSlot                     = 0;
    VkDeviceSize m_minUniformBufferAlignment   = 256;
    bool m_perDrawUboOverflowLogged            = false;

    /** EnqueueGpuWork backlog while no frame is active; replayed at next BeginFrame. */
    struct PendingGpuWork
    {
      std::function<void(VkCommandBuffer)> recorder;
      std::function<void()> postFlushCleanup;
    };
    std::vector<PendingGpuWork> m_pendingGpuWork;

    /** EnqueueGpuWork entries that arrived while an RP was open; flushed when the RP closes. */
    std::vector<PendingGpuWork> m_pendingDuringRpWork;

    /** Backend sets this between BeginFrame and Present; null outside a frame. */
    VkCommandBuffer m_currentRecordingCb = VK_NULL_HANDLE;
    std::function<void(std::function<void()>)> m_inlineCleanupSink;
    std::function<bool()> m_renderPassActiveQuery;

    bool m_validationEnabled                    = false;
  };

} // namespace ToolKit
