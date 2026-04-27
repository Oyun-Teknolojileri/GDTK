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

    /** Resets the ring head to 0 — caller must guarantee no in-flight cmd buffer still reads
        from the ring (typically: this runs at BeginFrame, after the slot's fence has been
        waited on). */
    void ResetPerDrawUboRing() { m_perDrawUboHead = 0; }

    /**
     * Reserves @p size bytes in the ring (rounded up to minUniformBufferOffsetAlignment) and
     * fills @p outOffset / @p outMappedPtr. Returns false on capacity overflow (and logs once).
     */
    bool AllocatePerDrawSlot(VkDeviceSize size, VkDeviceSize& outOffset, void*& outMappedPtr);

    /**
     * Executes @p recorder on a throwaway primary command buffer allocated from an internal
     * transient pool, submits it to the graphics queue and waits for completion. Serialized and
     * blocking — use only for one-time setup work (image layout transitions during Create,
     * texture uploads, etc.) and never inside the per-frame render loop.
     */
    void SubmitOneShot(const std::function<void(VkCommandBuffer)>& recorder);

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
    bool CreateOneShotPool();

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

    /** Per-draw UBO ring (Stage 7d-4b). Persistent-mapped HOST_VISIBLE; head bumps on each
        AllocatePerDrawSlot, resets on ResetPerDrawUboRing at frame begin. */
    VulkanBuffer::Buffer m_perDrawUboRing{};
    VkDeviceSize m_perDrawUboHead              = 0;
    /** Cached vkGetPhysicalDeviceProperties::limits.minUniformBufferOffsetAlignment. Every
        per-draw slot offset is rounded up to this multiple so the dynamic offset is valid. */
    VkDeviceSize m_minUniformBufferAlignment   = 256;
    /** Once-per-session log gate for ring overflow so a runaway frame doesn't spam the console. */
    bool m_perDrawUboOverflowLogged            = false;

    VkCommandPool m_oneShotPool                 = VK_NULL_HANDLE;

    bool m_validationEnabled                    = false;
  };

} // namespace ToolKit
