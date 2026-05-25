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
   * Owns the foundational Vulkan objects needed for every frame: instance, debug messenger, surface,
   * physical and logical device, queues, VMA allocator, and a shared descriptor pool (used by ImGui
   * among others). Created once by VulkanBackend::InitBackend, destroyed on backend shutdown.
   *
   * Platform coupling (SDL, X11, Win32) is avoided — the caller provides the required instance
   * extensions and a surface factory callback.
   */
  class TK_API VulkanContext
  {
   public:
    VulkanContext();
    ~VulkanContext();

    /**
     * Creates instance, surface, device, VMA, descriptor pool.
     * @param instanceExtensions - platform-specific extension names (VK_KHR_surface + VK_KHR_win32_surface etc.).
     * @param surfaceFactory     - given the created VkInstance (as void*), returns VkSurfaceKHR (as uint64) or 0.
     * @returns false on any fatal failure.
     */
    bool Init(const std::vector<const char*>& instanceExtensions,
              const std::function<uint64 (void*)>& surfaceFactory);

    /** Tears everything down in reverse order. Safe to call on a partially-initialized context. */
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

    /** Single descriptor set layout shared by every VulkanGpuProgram (Stage 7d-3). Reserves
        every binding the binding-map convention allows: textures 0..7, fixed UBOs at the
        post-remap positions for GL slots 3/4/7..10, and the per-draw dynamic UBO. Programs
        whose shaders only touch a subset of these bindings work fine — unused entries are
        ignored at descriptor write time, no runtime cost. */
    VkDescriptorSetLayout GetGlobalDescriptorSetLayout() const { return m_globalDescriptorSetLayout; }

    /** Per-frame descriptor pool used by VulkanBackend for transient descriptor sets allocated
        during a single frame's command recording (Stage 7d-4). One pool per frame-in-flight
        slot. Backend resets the active pool at BeginFrame, releasing every set allocated two
        frames ago in one cheap call (vkResetDescriptorPool) instead of freeing them
        individually. ImGui + other long-lived systems keep using GetSharedDescriptorPool; that
        pool is never reset. */
    VkDescriptorPool GetFrameDescriptorPool(uint frameIndex) const;

    /** Resets the frame's pool, releasing every descriptor set it allocated. Caller must
        guarantee no in-flight command buffer still references those sets (typically: this
        runs at BeginFrame, after the slot's fence has been waited on). */
    void ResetFrameDescriptorPool(uint frameIndex);

    /** Allocates a single descriptor set of @p layout from the per-frame pool. Returns
        VK_NULL_HANDLE on pool exhaustion or invalid frame index. The set's lifetime is bound
        to the next ResetFrameDescriptorPool(@p frameIndex). */
    VkDescriptorSet AllocateFrameDescriptorSet(uint frameIndex, VkDescriptorSetLayout layout);

    // -- Per-draw UBO ring ----------------------------------------------------------------------
    // Single persistent-mapped HOST_VISIBLE buffer reused every frame. SubmitPerDrawData appends
    // an aligned slice and reports the offset; the descriptor set's UNIFORM_BUFFER_DYNAMIC
    // entry at VulkanBindings::kPerDrawUboBinding points at the ring base, and the per-draw
    // shift travels through vkCmdBindDescriptorSets' pDynamicOffsets. BeginFrame resets head=0
    // (safe: previous frame's cmd buffer is fence-retired by the time the ring rolls over).

    /** Returns the ring buffer handle for descriptor writes. VK_NULL_HANDLE before Init. */
    VkBuffer GetPerDrawUboBuffer() const { return m_perDrawUboRing.handle; }

    /** Total ring capacity in bytes (set at Init). */
    VkDeviceSize GetPerDrawUboCapacity() const { return m_perDrawUboRing.size; }

    /** Resets the ring head for the given frame-in-flight slot back to that slot's region base.
        The ring is partitioned into FRAMES_IN_FLIGHT contiguous regions so cross-frame UBO
        stomp can't happen: slot S only ever writes to [S*regionSize, (S+1)*regionSize) and the
        other slot's region is untouched while its cb is still in flight. Caller must guarantee
        no in-flight cb still reads from slot S's region (typical sites: BeginFrame after the
        slot's fence has been waited on, or mid-frame FlushAndResetRing after the queue drained).
        Also latches @p slot as the current ring slot used by AllocatePerDrawSlot, and re-arms
        the overflow log. */
    void ResetPerDrawUboRing(uint slot)
    {
      m_currentRingSlot          = slot;
      m_perDrawUboHeads[slot]    = (VkDeviceSize) slot * m_perDrawUboRegionSize;
      m_perDrawUboOverflowLogged = false;
    }

    /**
     * Reserves @p size bytes in the ring (rounded up to minUniformBufferOffsetAlignment) and
     * fills @p outOffset / @p outMappedPtr. Returns false on capacity overflow (and logs once).
     */
    bool AllocatePerDrawSlot(VkDeviceSize size, VkDeviceSize& outOffset, void*& outMappedPtr);

    /**
     * Records @p recorder into the engine's current swapchain command buffer.
     *
     *   - If a frame is active and no render pass is currently open, recorder fires inline
     *     against the current command buffer. @p postFlushCleanup is handed to the deferred-
     *     delete sink so it runs after the cb retires on the GPU. Use this path for mid-frame
     *     resource creation that's about to be used later in the same frame (e.g.
     *     ReconstructIfNeeded on a texture that the next draw will sample / render to).
     *   - If a frame is active but a render pass is currently open (vkCmdPipelineBarrier and
     *     vkCmdCopy* are illegal inside a render pass without self-dependency), the entry is
     *     parked in a "during-render-pass" queue and replayed into the same cb the moment the
     *     active render pass closes (post-vkCmdEndRenderPass).
     *   - If no frame is active (engine init, between Present and BeginFrame), the entry is
     *     queued and replayed into the swapchain cb at the next BeginFrame, before any pass
     *     starts. The cleanup runs after that frame's cb retires.
     */
    void EnqueueGpuWork(std::function<void(VkCommandBuffer)> recorder,
                        std::function<void()> postFlushCleanup = {});

    /**
     * Replays every recorder queued while no frame was active into @p cb, clearing the queue.
     * Returns the corresponding postFlushCleanup callbacks so the caller can route them through
     * its frame-fenced deletion queue. Called by VulkanBackend::BeginFrame once the swapchain
     * command buffer is open. Inline (frame-active) entries do NOT travel through here — they
     * already executed against the current cb at enqueue time.
     */
    std::vector<std::function<void()>> FlushPendingGpuWork(VkCommandBuffer cb);

    /**
     * Replays every recorder parked in the during-render-pass queue into @p cb, clearing the
     * queue. Called by VulkanBackend immediately after closing a render pass (offscreen Draw's
     * vkCmdEndRenderPass and the swapchain pass close in FinishPass) so that uploads or
     * barriers that arrived mid-pass land in the cb before the next draw needs them. Returns
     * the cleanup callbacks for routing through frame-fenced deferred-delete.
     */
    std::vector<std::function<void()>> FlushDuringRenderPassWork(VkCommandBuffer cb);

    /**
     * Backend tells the context which cb is the "current frame cb" between BeginFrame and
     * Present and how to query whether a render pass is currently open. While the cb is set,
     * EnqueueGpuWork records inline (no RP) or parks in the during-RP queue (RP open) instead
     * of queueing for next frame. @p inlineCleanupSink receives the postFlushCleanup callback
     * for any inline-recorded work (typically wired to VulkanBackend::DeferDelete). Pass
     * VK_NULL_HANDLE + {} + {} to clear at frame end.
     */
    void SetCurrentRecordingCb(VkCommandBuffer cb,
                               std::function<void(std::function<void()>)> inlineCleanupSink,
                               std::function<bool()> renderPassActiveQuery);

    // -- Debug Utils Extension ------------------------------------------------------------------
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
    /** One pool per frame-in-flight slot (size = VulkanSwapchain::FRAMES_IN_FLIGHT). Sized
        independently from the shared pool. */
    std::array<VkDescriptorPool, 2> m_perFrameDescriptorPools = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    /** Per-draw UBO ring (Stage 7d-4b). Persistent-mapped HOST_VISIBLE buffer partitioned into
        FRAMES_IN_FLIGHT equal regions. Each frame slot owns one region; AllocatePerDrawSlot
        bumps m_perDrawUboHeads[m_currentRingSlot] and bounds it against that region's end so
        the other slot's still-in-flight data is never overwritten. ResetPerDrawUboRing(slot)
        re-bases the head to slot*regionSize and latches the slot for subsequent Allocate calls. */
    VulkanBuffer::Buffer m_perDrawUboRing{};
    std::array<VkDeviceSize, 2> m_perDrawUboHeads = {0, 0};
    VkDeviceSize m_perDrawUboRegionSize        = 0;
    uint m_currentRingSlot                     = 0;
    /** Cached vkGetPhysicalDeviceProperties::limits.minUniformBufferOffsetAlignment. Every
        per-draw slot offset is rounded up to this multiple so the dynamic offset is valid. */
    VkDeviceSize m_minUniformBufferAlignment   = 256;
    /** Once-per-session log gate for ring overflow so a runaway frame doesn't spam the console. */
    bool m_perDrawUboOverflowLogged            = false;

    /** GPU work queued by EnqueueGpuWork while no frame was active. Replayed into the
        swapchain command buffer by VulkanBackend at the next BeginFrame. */
    struct PendingGpuWork
    {
      std::function<void(VkCommandBuffer)> recorder;
      std::function<void()> postFlushCleanup;
    };
    std::vector<PendingGpuWork> m_pendingGpuWork;

    /** Mid-frame entries that arrived while a render pass was open. Flushed by VulkanBackend
        right after the active RP closes (per-Draw EndRenderPass for offscreen, EndSwapchainPass
        for the swapchain pass) — never carried across frames. Cleanups still ride
        m_inlineCleanupSink so they're frame-fenced through DeferDelete. */
    std::vector<PendingGpuWork> m_pendingDuringRpWork;

    /** Set by VulkanBackend between BeginFrame and Present so EnqueueGpuWork records inline
        instead of queueing. VK_NULL_HANDLE outside a frame. */
    VkCommandBuffer m_currentRecordingCb = VK_NULL_HANDLE;
    /** Set alongside m_currentRecordingCb. EnqueueGpuWork forwards postFlushCleanup callbacks
        to this sink (typically VulkanBackend::DeferDelete) when running inline. */
    std::function<void(std::function<void()>)> m_inlineCleanupSink;
    /** Set alongside m_currentRecordingCb. Returns true when an offscreen or swapchain render
        pass is currently open on the recording cb — EnqueueGpuWork uses this to decide between
        inline recording and the during-RP queue (vkCmdPipelineBarrier / vkCmdCopy* without
        self-dependency are illegal inside a render pass). */
    std::function<bool()> m_renderPassActiveQuery;

    bool m_validationEnabled                    = false;
  };

} // namespace ToolKit
