/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanBackend.h"

#include "../EngineSettings.h"
#include "../Framebuffer.h"
#include "../GpuProgram.h"
#include "../Logger.h"
#include "../Mesh.h"
#include "../Shader.h"
#include "../Texture.h"
#include "../ToolKit.h"
#include "../Types.h"
#include "../UniformBuffer.h"
#include "../Util.h"
#include "VulkanBindings.h"
#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanDescriptor.h"
#include "VulkanPipelineCache.h"
#include "VulkanResources.h"
#include "VulkanShader.h"
#include "VulkanSwapchain.h"

namespace
{
  // Uploads pixels to a VulkanTexture sub-resource via a single-use staging buffer.
  // Layout: current -> TRANSFER_DST -> SHADER_READ_ONLY_OPTIMAL. Recording deferred to the
  // active cb; staging buffer destroyed after the cb retires via DeferDelete.
  static void UploadTexelData(ToolKit::VulkanContext* ctx,
                              ToolKit::VulkanTexture* vt,
                              const void* pixels,
                              VkDeviceSize byteCount,
                              uint32_t layer,
                              uint32_t mip)
  {
    using namespace ToolKit;
    if (!pixels || byteCount == 0 || !vt || vt->image == VK_NULL_HANDLE)
      return;

    VulkanBuffer::Buffer staging =
        VulkanBuffer::CreateHostVisibleMapped(ctx, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, byteCount);
    if (staging.handle == VK_NULL_HANDLE)
    {
      TK_ERR("UploadTexelData: staging buffer alloc failed (%llu bytes)", (unsigned long long) byteCount);
      return;
    }
    std::memcpy(staging.mapped, pixels, static_cast<size_t>(byteCount));

    const VkImageLayout srcLayout = vt->currentLayout;
    uint32_t w                    = std::max(1u, vt->extent.width >> mip);
    uint32_t h                    = std::max(1u, vt->extent.height >> mip);

    ctx->EnqueueGpuWork(
        [staging, vt, srcLayout, layer, mip, w, h](VkCommandBuffer cb)
        {
          VkImageMemoryBarrier toTransfer {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
          toTransfer.oldLayout                       = srcLayout;
          toTransfer.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
          toTransfer.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
          toTransfer.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
          toTransfer.image                           = vt->image;
          toTransfer.subresourceRange.aspectMask     = vt->aspect;
          toTransfer.subresourceRange.baseMipLevel   = mip;
          toTransfer.subresourceRange.levelCount     = 1;
          toTransfer.subresourceRange.baseArrayLayer = layer;
          toTransfer.subresourceRange.layerCount     = 1;
          toTransfer.srcAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
          toTransfer.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
          vkCmdPipelineBarrier(cb,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &toTransfer);

          VkBufferImageCopy region {};
          region.imageSubresource.aspectMask     = vt->aspect;
          region.imageSubresource.mipLevel       = mip;
          region.imageSubresource.baseArrayLayer = layer;
          region.imageSubresource.layerCount     = 1;
          region.imageExtent                     = {w, h, 1};
          vkCmdCopyBufferToImage(cb, staging.handle, vt->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

          VkImageMemoryBarrier toRead = toTransfer;
          toRead.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
          toRead.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          toRead.srcAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
          toRead.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
          vkCmdPipelineBarrier(cb,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &toRead);
        },
        [ctx, staging]() mutable { VulkanBuffer::Destroy(ctx, staging); });

    vt->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }
} // anonymous namespace

#include <vma/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstring>

namespace ToolKit
{

  VulkanBackend::VulkanBackend()
      : m_context(std::make_unique<VulkanContext>()), m_swapchain(std::make_unique<VulkanSwapchain>()),
        m_pipelineCache(std::make_unique<VulkanPipelineCache>())
  {
    // One bucket per frame-in-flight.
    m_pendingDeleters.resize(VulkanSwapchain::FRAMES_IN_FLIGHT);
  }

  VulkanBackend::~VulkanBackend()
  {
    if (m_context && m_context->GetDevice() != VK_NULL_HANDLE)
    {
      vkDeviceWaitIdle(m_context->GetDevice());
    }

    // Release sticky bindings while the device + allocator are still alive. The shadow holds
    // TexturePtr refs intentionally kept across passes; letting them release during member
    // destruction would call vkDestroy* on a dead device.
    m_shadow.Reset();
    for (auto& entry : m_globalUboRegistry)
    {
      entry = {};
    }
    for (auto& bucket : m_descriptorCache)
    {
      bucket.clear();
    }

    if (m_dummyTexture)
    {
      m_dummyTexture.reset();
    }
    if (m_dummyCubeTexture)
    {
      m_dummyCubeTexture.reset();
    }
    if (m_dummy2DArrayTexture)
    {
      m_dummy2DArrayTexture.reset();
    }
    DrainAllDeleters();
    if (m_timestampPool != VK_NULL_HANDLE && m_context && m_context->GetDevice() != VK_NULL_HANDLE)
    {
      vkDestroyQueryPool(m_context->GetDevice(), m_timestampPool, nullptr);
      m_timestampPool = VK_NULL_HANDLE;
    }
    if (m_pipelineCache && m_context && m_context->GetDevice() != VK_NULL_HANDLE)
    {
      m_pipelineCache->Destroy(m_context->GetDevice());
    }
    m_pipelineCache.reset();
    m_swapchain.reset();

    // Final drain catches deleters queued by m_pipelineCache->Destroy / m_swapchain.reset.
    DrainAllDeleters();
    m_context.reset();
  }

  void VulkanBackend::DeferDelete(std::function<void()> fn)
  {
    if (!fn || m_pendingDeleters.empty())
    {
      return;
    }
    const uint slot = m_deleterSlot < m_pendingDeleters.size() ? m_deleterSlot : 0;
    m_pendingDeleters[slot].emplace_back(std::move(fn));
  }

  void VulkanBackend::DrainDeleterBucket(uint slot)
  {
    if (slot >= m_pendingDeleters.size())
    {
      return;
    }
    auto& bucket = m_pendingDeleters[slot];
    if (bucket.empty())
    {
      return;
    }
    // Move out so a deleter that itself calls DeferDelete doesn't invalidate iteration.
    std::vector<std::function<void()>> local;
    local.swap(bucket);
    for (auto& fn : local)
    {
      fn();
    }
  }

  void VulkanBackend::DrainAllDeleters()
  {
    for (uint i = 0; i < (uint) m_pendingDeleters.size(); ++i)
    {
      DrainDeleterBucket(i);
    }
  }

  void VulkanBackend::CreateDummyTexture()
  {
    m_dummyTexture                = std::make_shared<VulkanTexture>();

    VkFormat vkFormat             = VK_FORMAT_R8G8B8A8_UNORM;
    m_dummyTexture->context       = m_context.get();
    m_dummyTexture->format        = vkFormat;
    m_dummyTexture->aspect        = VK_IMAGE_ASPECT_COLOR_BIT;
    m_dummyTexture->extent        = {1, 1};
    m_dummyTexture->arrayLayers   = 1;
    m_dummyTexture->mipLevels     = 1;
    m_dummyTexture->isCubemap     = false;
    m_dummyTexture->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_dummyTexture->samples       = VK_SAMPLE_COUNT_1_BIT;

    VkImageCreateInfo ci {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = vkFormat;
    ci.extent        = {1, 1, 1};
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo aci {};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(m_context->GetAllocator(), &ci, &aci, &m_dummyTexture->image, &m_dummyTexture->allocation, nullptr);

    VkImageViewCreateInfo vci {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image                       = m_dummyTexture->image;
    vci.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                      = vkFormat;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    vkCreateImageView(m_context->GetDevice(), &vci, nullptr, &m_dummyTexture->view);

    VkSamplerCreateInfo sci {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter    = VK_FILTER_NEAREST;
    sci.minFilter    = VK_FILTER_NEAREST;
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod       = 1.0f;
    vkCreateSampler(m_context->GetDevice(), &sci, nullptr, &m_dummyTexture->sampler);

    m_context->EnqueueGpuWork(
        [img = m_dummyTexture->image](VkCommandBuffer cb)
        {
          VkImageMemoryBarrier b {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
          b.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
          b.newLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          b.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
          b.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
          b.image                       = img;
          b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          b.subresourceRange.levelCount = 1;
          b.subresourceRange.layerCount = 1;
          b.srcAccessMask               = 0;
          b.dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;
          vkCmdPipelineBarrier(cb,
                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &b);
        });
    m_dummyTexture->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    const uint32_t whitePixels    = 0xFFFFFFFF;
    UploadTexelData(m_context.get(), m_dummyTexture.get(), &whitePixels, 4, 0, 0);

    // Cube dummy
    m_dummyCubeTexture                = std::make_shared<VulkanTexture>();
    m_dummyCubeTexture->context       = m_context.get();
    m_dummyCubeTexture->format        = vkFormat;
    m_dummyCubeTexture->aspect        = VK_IMAGE_ASPECT_COLOR_BIT;
    m_dummyCubeTexture->extent        = {1, 1};
    m_dummyCubeTexture->arrayLayers   = 6;
    m_dummyCubeTexture->mipLevels     = 1;
    m_dummyCubeTexture->isCubemap     = true;
    m_dummyCubeTexture->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_dummyCubeTexture->samples       = VK_SAMPLE_COUNT_1_BIT;

    ci.flags                          = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ci.arrayLayers                    = 6;
    vmaCreateImage(m_context->GetAllocator(),
                   &ci,
                   &aci,
                   &m_dummyCubeTexture->image,
                   &m_dummyCubeTexture->allocation,
                   nullptr);

    vci.image                       = m_dummyCubeTexture->image;
    vci.viewType                    = VK_IMAGE_VIEW_TYPE_CUBE;
    vci.subresourceRange.layerCount = 6;
    vkCreateImageView(m_context->GetDevice(), &vci, nullptr, &m_dummyCubeTexture->view);

    vkCreateSampler(m_context->GetDevice(), &sci, nullptr, &m_dummyCubeTexture->sampler);

    m_context->EnqueueGpuWork(
        [img = m_dummyCubeTexture->image](VkCommandBuffer cb)
        {
          VkImageMemoryBarrier b {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
          b.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
          b.newLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          b.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
          b.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
          b.image                       = img;
          b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          b.subresourceRange.levelCount = 1;
          b.subresourceRange.layerCount = 6;
          b.srcAccessMask               = 0;
          b.dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;
          vkCmdPipelineBarrier(cb,
                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &b);
        });
    m_dummyCubeTexture->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    for (int i = 0; i < 6; i++)
    {
      UploadTexelData(m_context.get(), m_dummyCubeTexture.get(), &whitePixels, 4, i, 0);
    }

    // 2D Array dummy
    m_dummy2DArrayTexture                = std::make_shared<VulkanTexture>();
    m_dummy2DArrayTexture->context       = m_context.get();
    m_dummy2DArrayTexture->format        = vkFormat;
    m_dummy2DArrayTexture->aspect        = VK_IMAGE_ASPECT_COLOR_BIT;
    m_dummy2DArrayTexture->extent        = {1, 1};
    m_dummy2DArrayTexture->arrayLayers   = 1;
    m_dummy2DArrayTexture->mipLevels     = 1;
    m_dummy2DArrayTexture->isCubemap     = false;
    m_dummy2DArrayTexture->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_dummy2DArrayTexture->samples       = VK_SAMPLE_COUNT_1_BIT;

    ci.flags                             = 0;
    ci.arrayLayers                       = 1;
    vmaCreateImage(m_context->GetAllocator(),
                   &ci,
                   &aci,
                   &m_dummy2DArrayTexture->image,
                   &m_dummy2DArrayTexture->allocation,
                   nullptr);

    vci.image                       = m_dummy2DArrayTexture->image;
    vci.viewType                    = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vci.subresourceRange.layerCount = 1;
    vkCreateImageView(m_context->GetDevice(), &vci, nullptr, &m_dummy2DArrayTexture->view);

    vkCreateSampler(m_context->GetDevice(), &sci, nullptr, &m_dummy2DArrayTexture->sampler);

    m_context->EnqueueGpuWork(
        [img = m_dummy2DArrayTexture->image](VkCommandBuffer cb)
        {
          VkImageMemoryBarrier b {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
          b.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
          b.newLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          b.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
          b.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
          b.image                       = img;
          b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          b.subresourceRange.levelCount = 1;
          b.subresourceRange.layerCount = 1;
          b.srcAccessMask               = 0;
          b.dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;
          vkCmdPipelineBarrier(cb,
                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &b);
        });
    m_dummy2DArrayTexture->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    UploadTexelData(m_context.get(), m_dummy2DArrayTexture.get(), &whitePixels, 4, 0, 0);
  }

  void VulkanBackend::InitBackend(const BackendInitParams& params)
  {
    if (!m_context->Init(params.vkInstanceExtensions, params.vkCreateSurface))
    {
      TK_ERR("VulkanBackend: VulkanContext init failed");
      return;
    }
    if (!m_swapchain->Init(m_context.get()))
    {
      TK_ERR("VulkanBackend: VulkanSwapchain init failed");
    }
    CreateDummyTexture();

    // Timer query infra. Disabled if the device can't time graphics work.
    {
      VkPhysicalDeviceProperties props {};
      vkGetPhysicalDeviceProperties(m_context->GetPhysicalDevice(), &props);
      m_timestampPeriodNs = props.limits.timestampPeriod;

      uint32_t qfCount    = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(m_context->GetPhysicalDevice(), &qfCount, nullptr);
      std::vector<VkQueueFamilyProperties> qfProps(qfCount);
      vkGetPhysicalDeviceQueueFamilyProperties(m_context->GetPhysicalDevice(), &qfCount, qfProps.data());
      uint32_t validBits = (m_context->GetGraphicsQueueFamily() < qfCount)
                               ? qfProps[m_context->GetGraphicsQueueFamily()].timestampValidBits
                               : 0;

      if (m_timestampPeriodNs > 0.0f && validBits > 0)
      {
        VkQueryPoolCreateInfo qci {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        qci.queryCount = 2;
        if (vkCreateQueryPool(m_context->GetDevice(), &qci, nullptr, &m_timestampPool) == VK_SUCCESS)
        {
          m_timerSupported = true;
        }
        else
        {
          TK_WRN("VulkanBackend: vkCreateQueryPool (timestamp) failed — render time stats disabled");
        }
      }
      else
      {
        TK_WRN("VulkanBackend: device timestamps unsupported on graphics queue — render time stats disabled");
      }
    }
  }

  void VulkanBackend::BeginFrame()
  {
    // Skip Recreate while the surface has no presentable extent (window minimized); keep running
    // no-present frames so engine state (uploads, offscreen passes) keeps advancing.
    if (m_needsRecreate)
    {
      VkSurfaceCapabilitiesKHR caps {};
      if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_context->GetPhysicalDevice(), m_context->GetSurface(), &caps) ==
              VK_SUCCESS &&
          caps.currentExtent.width != 0 && caps.currentExtent.height != 0 && caps.currentExtent.width != UINT32_MAX)
      {
        m_swapchain->Recreate();
        m_needsRecreate = false;
      }
    }

    m_frameStarted = m_swapchain->BeginFrame();
    if (!m_frameStarted)
    {
      // Hard failure (device lost, etc.). Nothing else to do this tick.
      return;
    }
    if (!m_swapchain->IsPresentable())
    {
      m_needsRecreate = true;
    }
    // BeginFrame waited on this slot's fence; previous cb has retired. Reap its deleter bucket.
    // First frame skips the drain: init-time DeferDelete'd entries live here but have never been
    // consumed by a cb yet — draining now would destroy resources FlushPendingGpuWork is about
    // to touch.
    m_deleterSlot = m_swapchain->GetCurrentFrameIndex();
    if (!m_firstFrame)
    {
      DrainDeleterBucket(m_deleterSlot);
    }
    m_firstFrame = false;

    m_context->ResetFrameDescriptorPool(m_deleterSlot);

    // Cache rows point at the sets we just released — drop them.
    if (m_deleterSlot < m_descriptorCache.size())
    {
      m_descriptorCache[m_deleterSlot].clear();
    }

    // Per-draw UBO ring is partitioned per FIF slot; reset re-bases this slot's head to its
    // region base. The other slot's region stays untouched while its cb is still in flight.
    m_context->ResetPerDrawUboRing(m_deleterSlot);
    m_currentDynamicOffset = 0;

    // Timer query pump. Slot fence above guarantees the previous cycle's BEGIN/END timestamps
    // are host-readable. If a cycle is in flight, poll; otherwise reset the pool here (illegal
    // inside a render pass) so the BEGIN can fire from any RenderPath later.
    if (m_timerSupported)
    {
      if (m_timerQueryWaiting)
      {
        // Non-blocking poll with WITH_AVAILABILITY_BIT (16B stride: t0, avail0, t1, avail1).
        uint64_t results[4] = {0, 0, 0, 0};
        vkGetQueryPoolResults(m_context->GetDevice(),
                              m_timestampPool,
                              0,
                              2,
                              sizeof(results),
                              results,
                              sizeof(uint64_t) * 2,
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (results[1] != 0 && results[3] != 0)
        {
          double deltaTicks   = (double) (results[2] - results[0]);
          double deltaMs      = (deltaTicks * (double) m_timestampPeriodNs) / 1.0e6;
          m_gpuTimeMs         = (float) std::max(1.0, deltaMs);
          m_timerQueryWaiting = false;
        }
      }
      if (!m_timerQueryActive && !m_timerQueryWaiting)
      {
        VkCommandBuffer timerCb = m_swapchain->GetCurrentCommandBuffer();
        if (timerCb != VK_NULL_HANDLE)
        {
          vkCmdResetQueryPool(timerCb, m_timestampPool, 0, 2);
        }
      }
    }

    // Drop sticky shadow bindings — stale TexturePtr/UniformBuffer* would otherwise outlive their
    // engine-side owners (e.g. viewport resize). Per-draw resets happen elsewhere; the full sweep
    // belongs at frame boundary.
    m_shadow.Reset();
    m_lastFlushedSet                                   = VK_NULL_HANDLE;
    m_lastFlushedProgram                               = nullptr;

    VkCommandBuffer cb                                 = m_swapchain->GetCurrentCommandBuffer();

    // Flush GPU work queued while no frame was active (init-time uploads, transitions). Cleanups
    // ride DeferDelete so staging buffers outlive the cb.
    std::vector<std::function<void()>> pendingCleanups = m_context->FlushPendingGpuWork(cb);
    for (std::function<void()>& cleanup : pendingCleanups)
    {
      DeferDelete(std::move(cleanup));
    }

    // Route subsequent EnqueueGpuWork into this cb (inline when no RP active, parked when one
    // is — vkCmdPipelineBarrier is illegal mid-RP). Cleared in Present.
    m_context->SetCurrentRecordingCb(
        cb,
        [this](std::function<void()> fn) { DeferDelete(std::move(fn)); },
        [this]() { return m_rpActive || (m_swapchain != nullptr && m_swapchain->IsSwapchainPassActive()); });
  }

  void VulkanBackend::EndFrame()
  {
    // No-op. Present() does the submit.
  }

  void VulkanBackend::FlushAndResetRing()
  {
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }

    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
    if (cb == VK_NULL_HANDLE)
    {
      return;
    }

    // Offscreen RP may be open (FlushDescriptor can fire mid-pass). Close it; the next Draw
    // reopens with LOAD to keep prior content.
    CloseOffscreenRenderPassIfOpen(cb);
    // Swapchain RP uses LOAD_OP_CLEAR, so we can't safely close+reopen — would lose draws.
    if (m_swapchain->IsSwapchainPassActive())
    {
      TK_ERR("VulkanBackend::FlushAndResetRing: per-draw ring overflowed inside swapchain RP — "
             "flush skipped, triggering draw will fail descriptor validation");
      return;
    }

    if (!m_swapchain->FlushCommandBuffer())
    {
      return;
    }

    const uint frameIdx = m_swapchain->GetCurrentFrameIndex();
    m_context->ResetFrameDescriptorPool(frameIdx);
    if (frameIdx < m_descriptorCache.size())
    {
      m_descriptorCache[frameIdx].clear();
    }
    m_lastFlushedSet     = VK_NULL_HANDLE;
    m_lastFlushedProgram = nullptr;
    m_shadow.dirty       = true;

    // Queue is idle after FlushCommandBuffer — safe to re-base this slot's ring head.
    m_context->ResetPerDrawUboRing(frameIdx);
    m_currentDynamicOffset = 0;

    // Re-issue dynamic state on the new cmd buffer. CPU shadow state is preserved; the next Draw
    // re-records pipeline + descriptor + vertex/index binds.
    if (m_cachedViewport.valid)
    {
      VkViewport vp {};
      vp.x        = (float) m_cachedViewport.x;
      vp.y        = (float) (m_cachedViewport.y + m_cachedViewport.h);
      vp.width    = (float) m_cachedViewport.w;
      vp.height   = -(float) m_cachedViewport.h;
      vp.minDepth = 0.0f;
      vp.maxDepth = 1.0f;
      vkCmdSetViewport(cb, 0, 1, &vp);
    }
    if (m_cachedScissor.valid)
    {
      VkRect2D sc {};
      sc.offset.x      = (int32_t) m_cachedScissor.x;
      sc.offset.y      = (int32_t) m_cachedScissor.y;
      sc.extent.width  = m_cachedScissor.w;
      sc.extent.height = m_cachedScissor.h;
      vkCmdSetScissor(cb, 0, 1, &sc);
    }
  }

  void VulkanBackend::Present()
  {
    if (!m_frameStarted)
    {
      return;
    }
    // Stop routing EnqueueGpuWork into this cb; anything queued past this point lands in the
    // pending queue and replays into the next frame's cb.
    m_context->SetCurrentRecordingCb(VK_NULL_HANDLE, {}, {});
    if (!m_swapchain->EndFrame())
    {
      m_needsRecreate = true;
    }
    m_frameStarted = false;
  }

  VkCommandBuffer VulkanBackend::GetCurrentCommandBuffer() const
  {
    if (!m_frameStarted || m_swapchain == nullptr)
    {
      return VK_NULL_HANDLE;
    }
    return m_swapchain->GetCurrentCommandBuffer();
  }

  void VulkanBackend::EvictFramebufferCache(VulkanFramebuffer* fbData)
  {
    VkDevice device = m_context->GetDevice();

    // NVIDIA's ICD recycles VkRenderPass handles; the pipeline cache must be invalidated by
    // handle BEFORE the RP is destroyed, otherwise a fresh RP landing on the same handle would
    // silently cache-hit a stale pipeline.
    for (VulkanFramebuffer::RpVariant& v : fbData->rpVariants)
    {
      if (v.valid && v.rp != VK_NULL_HANDLE)
      {
        VkRenderPass oldRp = v.rp;
        if (m_pipelineCache)
        {
          m_pipelineCache->InvalidateForRenderPass(
              oldRp,
              [this, device](VkPipeline pipe)
              { DeferDelete([device, pipe]() { vkDestroyPipeline(device, pipe, nullptr); }); });
        }
        DeferDelete([device, oldRp]() { vkDestroyRenderPass(device, oldRp, nullptr); });
      }
      v.rp        = VK_NULL_HANDLE;
      v.clearBits = GraphicBitFields::None;
      v.valid     = false;
    }
    for (VulkanFramebuffer::FbCacheEntry& e : fbData->fbCache)
    {
      if (e.valid && e.fb != VK_NULL_HANDLE)
      {
        VkFramebuffer oldFb = e.fb;
        DeferDelete([device, oldFb]() { vkDestroyFramebuffer(device, oldFb, nullptr); });
      }
      e.fb        = VK_NULL_HANDLE;
      e.viewCount = 0;
      e.valid     = false;
    }
    fbData->framebuffer = VK_NULL_HANDLE;
    fbData->renderPass  = VK_NULL_HANDLE;
  }

  bool VulkanBackend::BuildOffscreenFramebuffer(VulkanFramebuffer* fbData)
  {
    VkDevice device = m_context->GetDevice();
    if (fbData->renderPass == VK_NULL_HANDLE)
    {
      TK_ERR("BuildOffscreenFramebuffer: no active render pass — call EnsureRpForClearBits first");
      return false;
    }

    // Compute the current view tuple — colors first (in declared order), then depth.
    std::array<VkImageView, VulkanFramebuffer::kMaxColorAttachments + 1> views {};
    uint32_t viewCount = 0;
    for (int i = 0; i < VulkanFramebuffer::kMaxColorAttachments; ++i)
    {
      auto& slot = fbData->colorAttachments[i];
      if (slot.tex == nullptr || slot.view == VK_NULL_HANDLE)
      {
        continue;
      }
      views[viewCount++] = slot.view;
    }
    if (fbData->depthAttachment.view != VK_NULL_HANDLE)
    {
      views[viewCount++] = fbData->depthAttachment.view;
    }

    // Cache lookup over kMaxFbCacheEntries — recurring view tuples (shadow layer iteration,
    // blur ping-pong) hit here and skip vkCreate/vkDestroy churn.
    for (auto& entry : fbData->fbCache)
    {
      if (!entry.valid || entry.viewCount != viewCount)
      {
        continue;
      }
      bool match = true;
      for (uint32_t i = 0; i < viewCount; ++i)
      {
        if (entry.views[i] != views[i])
        {
          match = false;
          break;
        }
      }
      if (match)
      {
        fbData->framebuffer = entry.fb;
        return true;
      }
    }

    VkFramebufferCreateInfo fbci {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbci.renderPass      = fbData->renderPass;
    fbci.attachmentCount = viewCount;
    fbci.pAttachments    = viewCount > 0 ? views.data() : nullptr;
    fbci.width           = fbData->width;
    fbci.height          = fbData->height;
    fbci.layers          = 1;

    VkFramebuffer newFb  = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device, &fbci, nullptr, &newFb) != VK_SUCCESS)
    {
      TK_ERR("BuildOffscreenFramebuffer: vkCreateFramebuffer failed");
      return false;
    }

    int slot = -1;
    for (int i = 0; i < VulkanFramebuffer::kMaxFbCacheEntries; ++i)
    {
      if (!fbData->fbCache[i].valid)
      {
        slot = i;
        break;
      }
    }
    if (slot < 0)
    {
      // LRU-lite: evict slot 0 (defer-delete since in-flight cbs may still reference it).
      slot              = 0;
      VkFramebuffer old = fbData->fbCache[0].fb;
      if (old != VK_NULL_HANDLE)
      {
        DeferDelete([device, old]() { vkDestroyFramebuffer(device, old, nullptr); });
      }
    }
    fbData->fbCache[slot].fb        = newFb;
    fbData->fbCache[slot].views     = views;
    fbData->fbCache[slot].viewCount = viewCount;
    fbData->fbCache[slot].valid     = true;
    fbData->framebuffer             = newFb;
    return true;
  }

  bool VulkanBackend::EnsureRpForClearBits(VulkanFramebuffer* fbData, GraphicBitFields clearBits)
  {
    for (VulkanFramebuffer::RpVariant& v : fbData->rpVariants)
    {
      if (v.valid && v.clearBits == clearBits)
      {
        fbData->renderPass = v.rp;
        return true;
      }
    }
    return BuildRpVariant(fbData, clearBits);
  }

  void VulkanBackend::CloseOffscreenRenderPassIfOpen(VkCommandBuffer cb)
  {
    if (!m_rpActive || m_activePassFb == nullptr || cb == VK_NULL_HANDLE)
    {
      return;
    }
    vkCmdEndRenderPass(cb);
    // Mirror the RP's finalLayout into engine state — the "resting" layout used as the next
    // RP's initialLayout.
    for (int i = 0; i < VulkanFramebuffer::kMaxColorAttachments; ++i)
    {
      if (auto* tex = m_activePassFb->colorAttachments[i].tex)
      {
        tex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      }
    }
    if (auto* tex = m_activePassFb->depthAttachment.tex)
    {
      tex->currentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
    m_rpActive      = false;

    // Drain GPU work parked while the RP was open — now legal to record barriers + copies.
    auto rpCleanups = m_context->FlushDuringRenderPassWork(cb);
    for (std::function<void()>& cleanup : rpCleanups)
    {
      DeferDelete(std::move(cleanup));
    }
  }

  bool VulkanBackend::BuildRpVariant(VulkanFramebuffer* fbData, GraphicBitFields clearBits)
  {
    VkDevice device         = m_context->GetDevice();
    const bool clearColor   = (((int) clearBits) & ((int) GraphicBitFields::ColorBits)) != 0;
    const bool clearDepth   = (((int) clearBits) & ((int) GraphicBitFields::DepthBits)) != 0;
    const bool clearStencil = (((int) clearBits) & ((int) GraphicBitFields::StencilBits)) != 0;

    std::vector<VkAttachmentDescription> atts;
    std::vector<VkAttachmentReference> colorRefs;
    atts.reserve(VulkanFramebuffer::kMaxColorAttachments + 1);
    colorRefs.reserve(VulkanFramebuffer::kMaxColorAttachments);

    // Vulkan requires every attachment in a subpass to share the same sampleCount.
    VkSampleCountFlagBits subpassSamples = VK_SAMPLE_COUNT_1_BIT;
    bool subpassSamplesSet               = false;

    auto adoptSamples                    = [&](VkSampleCountFlagBits s, const char* label)
    {
      if (!subpassSamplesSet)
      {
        subpassSamples    = s;
        subpassSamplesSet = true;
      }
      else if (subpassSamples != s)
      {
        TK_ERR("BuildRpVariant: attachment '%s' sampleCount %u mismatches subpass %u",
               label,
               (unsigned) s,
               (unsigned) subpassSamples);
      }
    };

    for (int i = 0; i < VulkanFramebuffer::kMaxColorAttachments; ++i)
    {
      auto& slot = fbData->colorAttachments[i];
      if (slot.tex == nullptr || slot.view == VK_NULL_HANDLE)
      {
        continue;
      }
      adoptSamples(slot.tex->samples, "color");

      VkAttachmentDescription a {};
      a.format         = slot.tex->format;
      a.samples        = slot.tex->samples;
      a.loadOp         = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
      a.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
      a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      // Steady-state resting layout for color attachments (matches finalLayout below).
      a.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      a.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      VkAttachmentReference ref {};
      ref.attachment = (uint32_t) atts.size();
      ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      colorRefs.push_back(ref);
      atts.push_back(a);
    }

    VkAttachmentReference depthRef {};
    bool hasDepth = fbData->depthAttachment.view != VK_NULL_HANDLE;
    if (hasDepth)
    {
      adoptSamples(fbData->depthAttachment.tex->samples, "depth");

      VkAttachmentDescription a {};
      a.format            = fbData->depthAttachment.tex->format;
      a.samples           = fbData->depthAttachment.tex->samples;
      a.loadOp            = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
      a.storeOp           = VK_ATTACHMENT_STORE_OP_STORE;
      a.stencilLoadOp     = clearStencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
      a.stencilStoreOp    = VK_ATTACHMENT_STORE_OP_STORE;
      a.initialLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
      a.finalLayout       = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

      depthRef.attachment = (uint32_t) atts.size();
      depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      atts.push_back(a);
    }
    fbData->subpassSamples = subpassSamples;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = (uint32_t) colorRefs.size();
    subpass.pColorAttachments       = colorRefs.empty() ? nullptr : colorRefs.data();
    subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

    // External subpass deps must cover every prior access type so the implicit queue-order
    // memory dependency hands off cleanly. Engine opens fresh RP instances back-to-back on the
    // same fb (bloom chains, shadow atlas, etc.) and runs FIF>1 cb's on the queue, so the
    // src* masks pull in sampling, color writes, and depth writes from any prior pass.
    VkSubpassDependency deps[2] {};
    deps[0].srcSubpass   = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass   = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = (uint32_t) atts.size();
    rpci.pAttachments    = atts.empty() ? nullptr : atts.data();
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies   = deps;

    VkRenderPass newRp   = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &rpci, nullptr, &newRp) != VK_SUCCESS)
    {
      TK_ERR("BuildRpVariant: vkCreateRenderPass failed (clearBits=%u)", (unsigned) clearBits);
      return false;
    }

    int slot = -1;
    for (int i = 0; i < VulkanFramebuffer::kMaxRpVariants; ++i)
    {
      if (!fbData->rpVariants[i].valid)
      {
        slot = i;
        break;
      }
    }
    if (slot < 0)
    {
      // Cache full → evict slot 0. Pipeline cache must be invalidated by handle before RP
      // destroy (driver may recycle handles into fresh RPs).
      slot               = 0;
      VkRenderPass oldRp = fbData->rpVariants[0].rp;
      if (m_pipelineCache && oldRp != VK_NULL_HANDLE)
      {
        m_pipelineCache->InvalidateForRenderPass(
            oldRp,
            [this, device](VkPipeline pipe)
            { DeferDelete([device, pipe]() { vkDestroyPipeline(device, pipe, nullptr); }); });
      }
      if (oldRp != VK_NULL_HANDLE)
      {
        DeferDelete([device, oldRp]() { vkDestroyRenderPass(device, oldRp, nullptr); });
      }
    }

    fbData->rpVariants[slot].rp        = newRp;
    fbData->rpVariants[slot].clearBits = clearBits;
    fbData->rpVariants[slot].valid     = true;
    fbData->renderPass                 = newRp;
    return true;
  }

  void VulkanBackend::StartPass(const PassDesc& desc)
  {
    if (!m_frameStarted)
    {
      return;
    }

    // No pass nesting.
    FinishPass();

    if (desc.target == nullptr)
    {
      // Backbuffer pass. No-op when minimized; offscreen + upload work still flows.
      if (!m_swapchain->IsPresentable())
      {
        return;
      }
      m_pendingPassDesc = desc;
      m_swapchain->BeginSwapchainPass(desc.clearColor);
      return;
    }

    auto* fbData = static_cast<VulkanFramebuffer*>(desc.target->m_gpuData.get());
    if (fbData == nullptr)
    {
      TK_ERR("StartPass: target framebuffer has no gpu data");
      return;
    }

    // Attachment view swapped (e.g. shadow atlas layer). Drop the active VkFramebuffer alias;
    // fbCache lookup below either hits an existing entry or appends a new one. Cache owns
    // lifetime — no defer-delete here.
    if (fbData->dirty)
    {
      fbData->framebuffer = VK_NULL_HANDLE;
      fbData->dirty       = false;
    }

    // Same fb can host multiple loadOp variants — they share the VkFramebuffer (RP compatibility
    // ignores loadOp).
    if (!EnsureRpForClearBits(fbData, desc.clearBits))
    {
      return;
    }

    if (fbData->framebuffer == VK_NULL_HANDLE)
    {
      if (!BuildOffscreenFramebuffer(fbData))
      {
        return;
      }
    }

    m_pendingPassDesc = desc;
    m_activePassFb    = fbData;

    if (VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer(); cb != VK_NULL_HANDLE)
    {
      // Negative-height viewport: NDC Y+1 maps to top of framebuffer (matches GL convention).
      VkViewport vp {};
      vp.x        = 0.0f;
      vp.y        = (float) fbData->height;
      vp.width    = (float) fbData->width;
      vp.height   = -(float) fbData->height;
      vp.minDepth = 0.0f;
      vp.maxDepth = 1.0f;
      vkCmdSetViewport(cb, 0, 1, &vp);

      VkRect2D sc {};
      sc.offset = {0, 0};
      sc.extent = {fbData->width, fbData->height};
      vkCmdSetScissor(cb, 0, 1, &sc);

      // Mirror the implicit viewport/scissor into the dynamic-state cache so FlushAndResetRing
      // can restore them onto a freshly begun cmd buffer if a mid-pass per-draw ring overflow
      // forces a flush. SetViewport / SetScissor populate the cache themselves for engine-level
      // overrides (shadow slot viewports etc.); this branch covers the pass-default values.
      m_cachedViewport = {0, 0, fbData->width, fbData->height, true};
      m_cachedScissor  = {0, 0, fbData->width, fbData->height, true};
    }
  }

  void VulkanBackend::FinishPass()
  {
    if (!m_frameStarted)
    {
      return;
    }
    // Pipeline binding is per-pass.
    m_pipelineBound           = false;
    m_boundProgram            = nullptr;
    // Reset per-draw state. Texture / UBO slot bindings are intentionally preserved: engine
    // code (BloomPass / DoFPass) stages SetTexture before the next SetFramebuffer; wiping
    // slots here would lose those bindings. BeginFrame does the cross-frame sweep.
    m_currentDynamicOffset    = 0;
    m_shadow.perDrawSubmitted = false;
    m_shadow.perDrawSize      = 0;
    m_shadow.dirty            = true;
    m_lastFlushedSet          = VK_NULL_HANDLE;
    m_lastFlushedProgram      = nullptr;
    if (m_activePassFb != nullptr)
    {
      VkCommandBuffer cb = m_swapchain ? m_swapchain->GetCurrentCommandBuffer() : VK_NULL_HANDLE;
      CloseOffscreenRenderPassIfOpen(cb);
      m_activePassFb = nullptr;
      return;
    }
    m_rpActive = false;
    m_swapchain->EndSwapchainPass();

    // Drain GPU work parked while the swapchain pass was open (e.g. editor ImGui uploads).
    if (m_swapchain && m_swapchain->IsFrameActive())
    {
      VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
      if (cb != VK_NULL_HANDLE)
      {
        auto rpCleanups = m_context->FlushDuringRenderPassWork(cb);
        for (std::function<void()>& cleanup : rpCleanups)
        {
          DeferDelete(std::move(cleanup));
        }
      }
    }
  }

  void VulkanBackend::SetViewport(uint x, uint y, uint w, uint h)
  {
    // Cached so FlushAndResetRing can restore onto the new cb after a mid-frame flush.
    m_cachedViewport = {(uint32_t) x, (uint32_t) y, (uint32_t) w, (uint32_t) h, true};
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }
    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
    if (cb == VK_NULL_HANDLE)
    {
      return;
    }
    // Negative height flips Y to match GL screen-space (engine passes top-left origin).
    VkViewport vp {};
    vp.x        = (float) x;
    vp.y        = (float) (y + h);
    vp.width    = (float) w;
    vp.height   = -(float) h;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &vp);
  }

  void VulkanBackend::SetScissor(uint x, uint y, uint w, uint h)
  {
    m_cachedScissor = {(uint32_t) x, (uint32_t) y, (uint32_t) w, (uint32_t) h, true};
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }
    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
    if (cb == VK_NULL_HANDLE)
    {
      return;
    }
    VkRect2D sc {};
    sc.offset.x      = (int32_t) x;
    sc.offset.y      = (int32_t) y;
    sc.extent.width  = w;
    sc.extent.height = h;
    vkCmdSetScissor(cb, 0, 1, &sc);
  }

  void VulkanBackend::ClearBuffer(GraphicBitFields fields, const Vec4& color)
  {
    // Empty RP with loadOp=CLEAR: GPU clears at RP entry (often free via HiZ/attachment
    // compression). Avoids vkCmdClear*Image + manual barriers.
    if (!m_frameStarted || m_activePassFb == nullptr)
    {
      return;
    }
    if (fields == GraphicBitFields::None)
    {
      return;
    }

    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
    if (cb == VK_NULL_HANDLE)
    {
      return;
    }

    // RPs can't nest; switching loadOps means switching VkRenderPass instances.
    CloseOffscreenRenderPassIfOpen(cb);

    if (m_activePassFb->dirty)
    {
      m_activePassFb->framebuffer = VK_NULL_HANDLE;
      m_activePassFb->dirty       = false;
    }

    if (!EnsureRpForClearBits(m_activePassFb, fields))
    {
      return;
    }
    if (m_activePassFb->framebuffer == VK_NULL_HANDLE)
    {
      if (!BuildOffscreenFramebuffer(m_activePassFb))
      {
        return;
      }
    }

    // Attachment order: colors first, then depth — matches BuildRpVariant.
    std::vector<VkClearValue> clears;
    clears.reserve(VulkanFramebuffer::kMaxColorAttachments + 1);
    for (int i = 0; i < VulkanFramebuffer::kMaxColorAttachments; ++i)
    {
      if (m_activePassFb->colorAttachments[i].view != VK_NULL_HANDLE)
      {
        VkClearValue cv {};
        cv.color = {
            {color.r, color.g, color.b, color.a}
        };
        clears.push_back(cv);
      }
    }
    if (m_activePassFb->depthAttachment.view != VK_NULL_HANDLE)
    {
      VkClearValue cv {};
      cv.depthStencil = {1.0f, 0};
      clears.push_back(cv);
    }

    VkRenderPassBeginInfo rpbi {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass        = m_activePassFb->renderPass;
    rpbi.framebuffer       = m_activePassFb->framebuffer;
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = {m_activePassFb->width, m_activePassFb->height};
    rpbi.clearValueCount   = (uint32_t) clears.size();
    rpbi.pClearValues      = clears.empty() ? nullptr : clears.data();
    vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    // Empty subpass — the loadOp=CLEAR is the entire work.
    vkCmdEndRenderPass(cb);

    // Mirror RP finalLayout onto tracked currentLayout fields.
    for (int i = 0; i < VulkanFramebuffer::kMaxColorAttachments; ++i)
    {
      if (auto* tex = m_activePassFb->colorAttachments[i].tex)
      {
        tex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      }
    }
    if (auto* tex = m_activePassFb->depthAttachment.tex)
    {
      tex->currentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }

    // Mask just-cleared bits off pending StartPass clear so next Draw's lazy-open doesn't reclear.
    m_pendingPassDesc.clearBits = (GraphicBitFields) ((int) m_pendingPassDesc.clearBits & ~(int) fields);
  }

  void VulkanBackend::ClearColorBuffer(const Vec4& color) { ClearBuffer(GraphicBitFields::ColorBits, color); }

  void VulkanBackend::BindPipeline(const GpuProgramPtr& program, const RenderState* state)
  {
    if (program == nullptr || state == nullptr)
    {
      m_pipelineBound = false;
      m_boundProgram  = nullptr;
      return;
    }
    auto* gp = static_cast<VulkanGpuProgram*>(program->m_gpuData.get());
    if (gp == nullptr || gp->pipelineLayout == VK_NULL_HANDLE)
    {
      m_pipelineBound = false;
      m_boundProgram  = nullptr;
      return;
    }
    // VkPipeline is built lazily in Draw() (needs DrawDesc.vertexLayout).
    const bool programChanged = (m_boundProgram != gp);
    m_boundProgram            = gp;
    m_boundState              = *state;
    m_pipelineBound           = true;

    // perDrawSize stays cached across same-pipeline draws to keep the descriptor cache fast path.
    m_currentDynamicOffset    = 0;
    m_shadow.perDrawSubmitted = false;

    if (programChanged)
    {
      m_shadow.dirty       = true;
      m_lastFlushedSet     = VK_NULL_HANDLE;
      m_lastFlushedProgram = nullptr;
    }
  }

  void VulkanBackend::SubmitPerDrawData(const void* data, size_t size)
  {
    // Append data to the per-frame UBO ring; offset travels through pDynamicOffsets in Draw.
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }
    if (m_boundProgram == nullptr || m_boundProgram->pipelineLayout == VK_NULL_HANDLE)
    {
      return;
    }
    if (data == nullptr || size == 0)
    {
      return;
    }

    VkDeviceSize offset = 0;
    void* mapped        = nullptr;
    if (!m_context->AllocatePerDrawSlot(size, offset, mapped))
    {
      // Ring full → drain queue + reset region, then retry. Second failure means payload > ring.
      FlushAndResetRing();
      if (!m_context->AllocatePerDrawSlot(size, offset, mapped))
      {
        TK_ERR("VulkanBackend::SubmitPerDrawData: AllocatePerDrawSlot failed even after flush "
               "(payload %llu B > ring %llu B?)",
               (unsigned long long) size,
               (unsigned long long) m_context->GetPerDrawUboCapacity());
        return;
      }
    }
    std::memcpy(mapped, data, size);

    // Descriptor set only changes when the range (perDrawSize) changes — dynamicOffsets[0]
    // carries m_currentDynamicOffset at vkCmdBindDescriptorSets time, keeping cache fast path hot.
    const uint64_t newSize = (uint64_t) size;
    if (m_shadow.perDrawSize != newSize)
    {
      m_shadow.perDrawSize = newSize;
      m_shadow.dirty       = true;
    }
    m_currentDynamicOffset    = (uint32_t) offset;
    m_shadow.perDrawSubmitted = true;
  }

  void VulkanBackend::BindTexture(ubyte slot, TexturePtr tex)
  {
    // Shadow state only — FlushDescriptorState folds N BindTextures into one set alloc.
    if (slot >= VulkanBindings::kTextureBindingCount)
    {
      TK_ERR("BindTexture: slot %u beyond reserved texture binding range (%u)",
             (unsigned) slot,
             (unsigned) VulkanBindings::kTextureBindingCount);
      return;
    }
    // Only dirty if changed — material-sorted scenes rebind the same TexturePtr; redundant
    // dirty kills the descriptor cache fast path.
    if (m_shadow.boundTextures[slot] != tex)
    {
      m_shadow.boundTextures[slot] = tex;
      m_shadow.dirty               = true;
    }
  }

  void VulkanBackend::BindUniformBuffer(const String& name, UniformBuffer* ub)
  {
    // Shadow state only. Slot-keyed; name reserved for future lookups, m_globalUboRegistry
    // fills the fallback path on flush.
    (void) name;
    if (ub == nullptr || ub->m_slot < 0 || ub->m_slot >= kMaxUboSlots)
    {
      return;
    }
    UniformBuffer*& slot = m_shadow.boundUniforms[ub->m_slot];
    if (slot != ub)
    {
      slot           = ub;
      m_shadow.dirty = true;
    }
  }

  void VulkanBackend::BindUniformBuffer(UniformBuffer* ub, int slot)
  {
    // PassRequirements::customUbos entry point. Caller must keep the UBO's m_slot in sync with
    // the slot argument — typically the UBO was Init(slot)'d, so we sanity-check and then
    // route through the same shadow-state update as the name-based path.
    if (ub == nullptr)
    {
      return;
    }
    assert(ub->m_slot == slot && "VulkanBackend::BindUniformBuffer: UBO m_slot must match the bind slot");
    if (slot < 0 || slot >= kMaxUboSlots)
    {
      return;
    }
    UniformBuffer*& shadowSlot = m_shadow.boundUniforms[slot];
    if (shadowSlot != ub)
    {
      shadowSlot     = ub;
      m_shadow.dirty = true;
    }
  }

  // Fills the vertex-input portion of @p out for ToolKit's VertexLayout.
  // Locations: 0=pos(vec3) 1=norm(vec3) 2=tex(vec2) 3=tan(vec4) [SkinMesh: +4=bones, 5=weights]
  static void FillVertexInput(VertexLayout layout, VulkanPipelineDesc& out)
  {
    if (layout == VertexLayout::SkinMesh)
    {
      out.vertexStride   = sizeof(SkinVertex);
      out.attributeCount = 6;
      out.attributes[0]  = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
      out.attributes[1]  = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
      out.attributes[2]  = {2, 0, VK_FORMAT_R32G32_SFLOAT, 24};
      out.attributes[3]  = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32};
      out.attributes[4]  = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 48};
      out.attributes[5]  = {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 64};
    }
    else // VertexLayout::Mesh (default) and VertexLayout::None fall back to plain Vertex.
    {
      out.vertexStride   = sizeof(Vertex);
      out.attributeCount = 6;
      out.attributes[0]  = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
      out.attributes[1]  = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
      out.attributes[2]  = {2, 0, VK_FORMAT_R32G32_SFLOAT, 24};
      out.attributes[3]  = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32};

      // Vulkan Strictness Hack:
      // skinning.shader defines layout(location = 4) and layout(location = 5) unconditionally.
      // If we only provide 4 attributes, Vulkan throws VUID-VkGraphicsPipelineCreateInfo-Input-07904
      // even if the shader branch (isSkinned=false) never reads them. We provide dummy mappings
      // pointing safely at offset 0 (pos) to bypass the validation error.
      out.attributes[4]  = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
      out.attributes[5]  = {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
    }
  }

  void VulkanBackend::Draw(const DrawDesc& desc)
  {
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }
    if (m_activePassFb == nullptr && !m_swapchain->IsSwapchainPassActive())
    {
      return;
    }
    if (!m_pipelineBound || m_boundProgram == nullptr)
    {
      return;
    }
    if (desc.mesh == nullptr || desc.elementCount == 0)
    {
      return;
    }
    auto* meshGpu = static_cast<VulkanMesh*>(desc.mesh->m_gpuData.get());
    if (meshGpu == nullptr || meshGpu->vertex.handle == VK_NULL_HANDLE)
    {
      return;
    }

    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
    if (cb == VK_NULL_HANDLE)
    {
      return;
    }

    // Lazy pipeline build. Same RenderState + program + layout + RP hits the cache.
    VulkanPipelineDesc pdesc {};
    pdesc.vert = m_boundProgram->vert;
    pdesc.frag = m_boundProgram->frag;
    FillVertexInput(desc.vertexLayout, pdesc);
    RenderStateToPipelineDesc(m_boundState, pdesc);

    // Pipeline's colorAttachmentCount must match subpass (depth-only passes need 0, not 1).
    // Sample count propagates from fb's adopted subpassSamples so MSAA / non-MSAA recipes
    // land in distinct cache slots.
    if (m_activePassFb != nullptr)
    {
      pdesc.renderPass = m_activePassFb->renderPass;
      uint colorCount  = 0;
      for (int i = 0; i < VulkanFramebuffer::kMaxColorAttachments; ++i)
      {
        if (m_activePassFb->colorAttachments[i].tex != nullptr)
        {
          ++colorCount;
        }
      }
      pdesc.colorAttachmentCount = colorCount;
      pdesc.rasterizationSamples = m_activePassFb->subpassSamples;
    }
    else
    {
      pdesc.renderPass           = m_swapchain->GetRenderPass();
      pdesc.colorAttachmentCount = 1;
      pdesc.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    }

    VkPipeline pipe = m_pipelineCache->GetOrCreate(m_context.get(), m_boundProgram->pipelineLayout, pdesc);
    if (pipe == VK_NULL_HANDLE)
    {
      return;
    }

    // Lazy open of offscreen RP: one BeginRenderPass per logical pass. First Draw opens it,
    // subsequent Draws reuse, FinishPass closes. Mid-pass close (ClearBuffer, attachment swap,
    // FlushAndResetRing) means the next Draw reopens — clearBits consumed on first open, reopens
    // use LOAD so prior content survives.
    if (m_activePassFb != nullptr && m_activePassFb->dirty && m_rpActive)
    {
      // Attachment swap mid-pass — VkFramebuffer is bound at BeginRenderPass and can't be
      // swapped while open. Close, reopen with LOAD below.
      CloseOffscreenRenderPassIfOpen(cb);
    }
    if (m_activePassFb != nullptr && !m_rpActive)
    {
      if (m_activePassFb->dirty)
      {
        m_activePassFb->framebuffer = VK_NULL_HANDLE;
        if (!BuildOffscreenFramebuffer(m_activePassFb))
        {
          return;
        }
        m_activePassFb->dirty = false;
      }

      if (!EnsureRpForClearBits(m_activePassFb, m_pendingPassDesc.clearBits))
      {
        return;
      }
      pdesc.renderPass = m_activePassFb->renderPass;

      std::vector<VkClearValue> clears;
      clears.reserve(VulkanFramebuffer::kMaxColorAttachments + 1);
      for (int i = 0; i < VulkanFramebuffer::kMaxColorAttachments; ++i)
      {
        if (m_activePassFb->colorAttachments[i].view != VK_NULL_HANDLE)
        {
          VkClearValue cv {};
          cv.color = {
              {m_pendingPassDesc.clearColor.r,
               m_pendingPassDesc.clearColor.g,
               m_pendingPassDesc.clearColor.b,
               m_pendingPassDesc.clearColor.a}
          };
          clears.push_back(cv);
        }
      }
      if (m_activePassFb->depthAttachment.view != VK_NULL_HANDLE)
      {
        VkClearValue cv {};
        cv.depthStencil = {1.0f, 0};
        clears.push_back(cv);
      }

      VkRenderPassBeginInfo rpbi {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      rpbi.renderPass        = m_activePassFb->renderPass;
      rpbi.framebuffer       = m_activePassFb->framebuffer;
      rpbi.renderArea.offset = {0, 0};
      rpbi.renderArea.extent = {m_activePassFb->width, m_activePassFb->height};
      rpbi.clearValueCount   = (uint32_t) clears.size();
      rpbi.pClearValues      = clears.empty() ? nullptr : clears.data();
      vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      m_rpActive                  = true;

      // First open consumed the clear; reopens use LOAD.
      m_pendingPassDesc.clearBits = GraphicBitFields::None;

      // Re-pick pipeline against the now-current renderPass (likely cache hit).
      pipe = m_pipelineCache->GetOrCreate(m_context.get(), m_boundProgram->pipelineLayout, pdesc);
      if (pipe == VK_NULL_HANDLE)
      {
        return;
      }
    }

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

    VkDescriptorSet flushedSet = FlushDescriptorState();
    if (flushedSet == VK_NULL_HANDLE)
    {
      // Drop the draw rather than issue vkCmdDraw with an unbound set 0 (driver crashes).
      return;
    }
    const uint32_t dyn = m_currentDynamicOffset;
    vkCmdBindDescriptorSets(cb,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_boundProgram->pipelineLayout,
                            0,
                            1,
                            &flushedSet,
                            1,
                            &dyn);

    // ---- Bind geometry + draw ---------------------------------------------------------------
    const VkBuffer vbuf     = meshGpu->vertex.handle;
    const VkDeviceSize voff = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &vbuf, &voff);

    if (desc.indexed)
    {
      assert(meshGpu->index.handle != VK_NULL_HANDLE && "Draw: indexed=true but mesh has no index buffer");
      vkCmdBindIndexBuffer(cb, meshGpu->index.handle, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cb, desc.elementCount, desc.instanceCount, 0, 0, 0);
    }
    else
    {
      vkCmdDraw(cb, desc.elementCount, desc.instanceCount, 0, 0);
    }
  }

  // Pre-transition wrapper for color blit/resolve transfers. Caller must close any open RP.
  // Post-transition restores both images to SHADER_READ_ONLY_OPTIMAL.
  static void TransitionForColorTransfer(VkCommandBuffer cb, VulkanTexture* srcTex, VulkanTexture* dstTex)
  {
    VkImageMemoryBarrier pre[2] {};
    pre[0].sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre[0].oldLayout                   = srcTex->currentLayout;
    pre[0].newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    pre[0].srcAccessMask               = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    pre[0].dstAccessMask               = VK_ACCESS_TRANSFER_READ_BIT;
    pre[0].srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    pre[0].dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    pre[0].image                       = srcTex->image;
    pre[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pre[0].subresourceRange.levelCount = srcTex->mipLevels;
    pre[0].subresourceRange.layerCount = srcTex->arrayLayers;

    pre[1]                             = pre[0];
    pre[1].oldLayout                   = dstTex->currentLayout;
    pre[1].newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    pre[1].srcAccessMask               = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    pre[1].dstAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
    pre[1].image                       = dstTex->image;
    pre[1].subresourceRange.levelCount = dstTex->mipLevels;
    pre[1].subresourceRange.layerCount = dstTex->arrayLayers;

    vkCmdPipelineBarrier(cb,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         2,
                         pre);
  }

  static void TransitionAfterColorTransfer(VkCommandBuffer cb, VulkanTexture* srcTex, VulkanTexture* dstTex)
  {
    VkImageMemoryBarrier post[2] {};
    post[0].sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    post[0].oldLayout                   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    post[0].newLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post[0].srcAccessMask               = VK_ACCESS_TRANSFER_READ_BIT;
    post[0].dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;
    post[0].srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    post[0].dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    post[0].image                       = srcTex->image;
    post[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    post[0].subresourceRange.levelCount = srcTex->mipLevels;
    post[0].subresourceRange.layerCount = srcTex->arrayLayers;

    post[1]                             = post[0];
    post[1].oldLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    post[1].srcAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
    post[1].image                       = dstTex->image;
    post[1].subresourceRange.levelCount = dstTex->mipLevels;
    post[1].subresourceRange.layerCount = dstTex->arrayLayers;

    vkCmdPipelineBarrier(cb,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         2,
                         post);

    srcTex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dstTex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }

  // MSAA color → single-sample resolve. Extents must match (no scaling).
  static void ResolveColorAttachment(VkCommandBuffer cb, VulkanTexture* srcTex, VulkanTexture* dstTex)
  {
    TransitionForColorTransfer(cb, srcTex, dstTex);

    VkImageResolve region {};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.srcOffset                 = {0, 0, 0};
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.dstOffset                 = {0, 0, 0};
    region.extent                    = {srcTex->extent.width, srcTex->extent.height, 1};

    vkCmdResolveImage(cb,
                      srcTex->image,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      dstTex->image,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      1,
                      &region);

    TransitionAfterColorTransfer(cb, srcTex, dstTex);
  }

  // Full-extent LINEAR color blit. Caller must close any open RP. src must be single-sample
  // (use ResolveColorAttachment for MSAA).
  static void BlitColorAttachment(VkCommandBuffer cb, VulkanTexture* srcTex, VulkanTexture* dstTex)
  {
    TransitionForColorTransfer(cb, srcTex, dstTex);

    VkImageBlit region {};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.srcOffsets[0]             = {0, 0, 0};
    region.srcOffsets[1]             = {(int32_t) srcTex->extent.width, (int32_t) srcTex->extent.height, 1};
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.dstOffsets[0]             = {0, 0, 0};
    region.dstOffsets[1]             = {(int32_t) dstTex->extent.width, (int32_t) dstTex->extent.height, 1};

    vkCmdBlitImage(cb,
                   srcTex->image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dstTex->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1,
                   &region,
                   VK_FILTER_LINEAR);

    TransitionAfterColorTransfer(cb, srcTex, dstTex);
  }

  void VulkanBackend::ResolveFramebuffer(FramebufferPtr src, FramebufferPtr dst, const IntArray& attachments)
  {
    // Per requested attachment: vkCmdResolveImage if MSAA src, vkCmdBlitImage otherwise.
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }
    // Resolve/blit are illegal inside RP — close the active one (engine calls this mid-pass).
    if (VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer(); cb != VK_NULL_HANDLE)
    {
      CloseOffscreenRenderPassIfOpen(cb);
    }
    if (src == nullptr || dst == nullptr)
    {
      return;
    }

    auto* srcFb = static_cast<VulkanFramebuffer*>(src->m_gpuData.get());
    auto* dstFb = static_cast<VulkanFramebuffer*>(dst->m_gpuData.get());
    if (srcFb == nullptr || dstFb == nullptr)
    {
      return;
    }

    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
    if (cb == VK_NULL_HANDLE)
    {
      return;
    }

    for (int idx : attachments)
    {
      if (idx < 0 || idx >= VulkanFramebuffer::kMaxColorAttachments)
      {
        continue;
      }

      using Attachment      = Framebuffer::Attachment;
      Attachment atcEnum    = (Attachment) ((int) Attachment::ColorAttachment0 + idx);

      // Lazy create + m_resolvedTexture wiring (mirrors GLBackend). Destination FB is often
      // empty until this call materializes the resolved twin (used by SSAO / EditorViewport).
      RenderTargetPtr srcRt = src->GetColorAttachment(atcEnum);
      if (srcRt == nullptr)
      {
        continue;
      }
      RenderTargetPtr targetRt = dst->GetColorAttachment(atcEnum);
      if (targetRt == nullptr)
      {
        TextureSettings settings = srcRt->Settings();
        settings.msaaCount       = MsaaSampleCount::x0;
        targetRt                 = MakeNewPtr<RenderTarget>();
        targetRt->ReconstructIfNeeded(srcRt->m_width, srcRt->m_height, &settings);
        dst->SetColorAttachment(atcEnum, targetRt);
      }
      srcRt->m_resolvedTexture = targetRt;

      // SetColorAttachment above populated dstFb's slot via AttachColorTarget — re-read.
      auto* srcTex             = srcFb->colorAttachments[idx].tex;
      auto* dstTex             = dstFb->colorAttachments[idx].tex;
      if (srcTex == nullptr || dstTex == nullptr)
      {
        continue;
      }
      if (srcTex->samples != VK_SAMPLE_COUNT_1_BIT)
      {
        if (dstTex->samples != VK_SAMPLE_COUNT_1_BIT)
        {
          TK_ERR("ResolveFramebuffer: dst attachment %d is multi-sampled â€” resolve target must "
                 "be single-sample. Skipped.",
                 idx);
          continue;
        }
        ResolveColorAttachment(cb, srcTex, dstTex);
      }
      else
      {
        BlitColorAttachment(cb, srcTex, dstTex);
      }
    }
  }

  void VulkanBackend::CopyFramebuffer(FramebufferPtr src, FramebufferPtr dst, GraphicBitFields fields)
  {
    // Color-only blit between two FBs (post-process chains, intermediate copies). Mirrors
    // glBlitFramebuffer semantics. Depth/stencil paths not implemented (engine doesn't use yet).
    // dst == nullptr maps to "blit to swapchain image" (GL default framebuffer equivalent).
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }
    if (VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer(); cb != VK_NULL_HANDLE)
    {
      CloseOffscreenRenderPassIfOpen(cb);
    }
    if (src == nullptr)
    {
      return;
    }

    const uint mask = (uint) fields;
    if ((mask & (uint) GraphicBitFields::ColorBits) == 0)
    {
      static bool s_warnedDepthOnly = false;
      if (!s_warnedDepthOnly &&
          (mask & ((uint) GraphicBitFields::DepthBits | (uint) GraphicBitFields::StencilBits)) != 0)
      {
        TK_WRN("CopyFramebuffer: depth/stencil-only copy not implemented yet");
        s_warnedDepthOnly = true;
      }
      return;
    }

    if (dst == nullptr)
    {
      // Blit to swapchain (used by SplashScreenRenderPath / GameRenderer when no ImGui pass
      // follows). Skip if minimized — no acquired image.
      if (!m_swapchain->IsPresentable())
      {
        return;
      }
      if (m_swapchain->IsSwapchainPassActive())
      {
        TK_ERR("CopyFramebuffer(dst=nullptr) called while swapchain render pass is active");
        return;
      }

      auto* srcFb = static_cast<VulkanFramebuffer*>(src->m_gpuData.get());
      if (srcFb == nullptr)
      {
        return;
      }

      VulkanTexture* srcTex = srcFb->colorAttachments[0].tex;
      if (srcTex == nullptr)
      {
        return;
      }
      if (srcTex->samples != VK_SAMPLE_COUNT_1_BIT)
      {
        TK_ERR("CopyFramebuffer(dst=nullptr): MSAA source requires a resolve pass before blit");
        return;
      }

      VkImage swapImage = m_swapchain->GetCurrentImage();
      if (swapImage == VK_NULL_HANDLE)
      {
        return;
      }

      VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
      if (cb == VK_NULL_HANDLE)
      {
        return;
      }

      VkExtent2D swapExtent = m_swapchain->GetExtent();

      // Swapchain image: UNDEFINED → TRANSFER_DST (full-extent blit overwrites prior content).
      VkImageMemoryBarrier pre[2] {};
      pre[0].sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      pre[0].oldLayout                   = srcTex->currentLayout;
      pre[0].newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      pre[0].srcAccessMask               = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      pre[0].dstAccessMask               = VK_ACCESS_TRANSFER_READ_BIT;
      pre[0].srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
      pre[0].dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
      pre[0].image                       = srcTex->image;
      pre[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      pre[0].subresourceRange.levelCount = srcTex->mipLevels;
      pre[0].subresourceRange.layerCount = srcTex->arrayLayers;

      pre[1].sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      pre[1].oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
      pre[1].newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      pre[1].srcAccessMask               = 0;
      pre[1].dstAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
      pre[1].srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
      pre[1].dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
      pre[1].image                       = swapImage;
      pre[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      pre[1].subresourceRange.levelCount = 1;
      pre[1].subresourceRange.layerCount = 1;

      vkCmdPipelineBarrier(cb,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0,
                           0,
                           nullptr,
                           0,
                           nullptr,
                           2,
                           pre);

      VkImageBlit region {};
      region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.srcSubresource.layerCount = 1;
      region.srcOffsets[0]             = {0, 0, 0};
      region.srcOffsets[1]             = {(int32_t) srcTex->extent.width, (int32_t) srcTex->extent.height, 1};
      region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.dstSubresource.layerCount = 1;
      region.dstOffsets[0]             = {0, 0, 0};
      region.dstOffsets[1]             = {(int32_t) swapExtent.width, (int32_t) swapExtent.height, 1};

      vkCmdBlitImage(cb,
                     srcTex->image,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     swapImage,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     1,
                     &region,
                     VK_FILTER_LINEAR);

      // src → SHADER_READ_ONLY (resting); swapchain → PRESENT_SRC_KHR for vkQueuePresentKHR.
      VkImageMemoryBarrier post[2] {};
      post[0]               = pre[0];
      post[0].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      post[0].newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      post[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      post[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      post[1]               = pre[1];
      post[1].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      post[1].newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      post[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      post[1].dstAccessMask = 0;

      vkCmdPipelineBarrier(cb,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                           0,
                           0,
                           nullptr,
                           0,
                           nullptr,
                           2,
                           post);

      srcTex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      return;
    }

    auto* srcFb = static_cast<VulkanFramebuffer*>(src->m_gpuData.get());
    auto* dstFb = static_cast<VulkanFramebuffer*>(dst->m_gpuData.get());
    if (srcFb == nullptr || dstFb == nullptr)
    {
      return;
    }

    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
    if (cb == VK_NULL_HANDLE)
    {
      return;
    }

    for (int i = 0; i < VulkanFramebuffer::kMaxColorAttachments; ++i)
    {
      auto* srcTex = srcFb->colorAttachments[i].tex;
      auto* dstTex = dstFb->colorAttachments[i].tex;
      if (srcTex == nullptr || dstTex == nullptr)
      {
        continue;
      }
      BlitColorAttachment(cb, srcTex, dstTex);
    }
  }

  void VulkanBackend::BlitToScreen(FramebufferPtr src)
  {
    // No-op (Vulkan routes viewport pixels through ImGui). Engine uses CopyFramebuffer with
    // dst=nullptr for actual blit-to-screen.
    (void) src;
  }

  void VulkanBackend::StartTimerQuery()
  {
    m_cpuStartMs = GetElapsedMilliSeconds();
    if (!m_timerSupported || m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }
    if (m_timerQueryActive || m_timerQueryWaiting)
    {
      // Gate new cycles until the previous result is consumed.
      return;
    }
    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
    if (cb == VK_NULL_HANDLE)
    {
      return;
    }
    // Pool reset is recorded in BeginFrame; here we only write the BEGIN timestamp.
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_timestampPool, 0);
    m_timerQueryActive = true;
  }

  void VulkanBackend::EndTimerQuery()
  {
    float now   = GetElapsedMilliSeconds();
    m_cpuTimeMs = now - m_cpuStartMs;

    if (!m_timerSupported || m_swapchain == nullptr)
    {
      return;
    }

    if (m_timerQueryActive)
    {
      VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
      if (cb != VK_NULL_HANDLE)
      {
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampPool, 1);
        m_timerQueryActive  = false;
        m_timerQueryWaiting = true;
      }
    }
    // Result polled next BeginFrame after fence retirement.
  }

  void VulkanBackend::GetElapsedTime(float& cpu, float& gpu)
  {
    cpu = m_cpuTimeMs;
    gpu = m_gpuTimeMs;
  }

  static VkSamplerAddressMode ToVkAddressMode(GraphicTypes wrap)
  {
    switch (wrap)
    {
      case GraphicTypes::UVRepeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
      case GraphicTypes::UVClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      case GraphicTypes::UVClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
      default:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
  }

  static VkFilter ToVkFilter(GraphicTypes f)
  {
    switch (f)
    {
      case GraphicTypes::SampleNearest:
      case GraphicTypes::SampleNearestMipmapNearest:
        return VK_FILTER_NEAREST;
      case GraphicTypes::SampleLinear:
      case GraphicTypes::SampleLinearMipmapLinear:
      case GraphicTypes::SampleLinearMipmapNearest:
        return VK_FILTER_LINEAR;
      default:
        return VK_FILTER_LINEAR;
    }
  }

  static VkSamplerMipmapMode ToVkMipmapMode(GraphicTypes f)
  {
    switch (f)
    {
      case GraphicTypes::SampleNearest:
      case GraphicTypes::SampleNearestMipmapNearest:
      case GraphicTypes::SampleLinearMipmapNearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
      case GraphicTypes::SampleLinear:
      case GraphicTypes::SampleLinearMipmapLinear:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
      default:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
  }

  void VulkanBackend::CreateTexture(Texture* tex)
  {
    assert(tex && "CreateTexture: null texture");
    assert(tex->m_gpuData == nullptr && "CreateTexture: texture already has gpu data");

    if (m_context == nullptr || m_context->GetAllocator() == nullptr)
    {
      TK_ERR("VulkanBackend::CreateTexture called before VulkanContext init");
      return;
    }

    if (tex->m_width <= 0 || tex->m_height <= 0)
    {
      TK_ERR("VulkanBackend::CreateTexture - invalid dimensions (%d x %d)", tex->m_width, tex->m_height);
      return;
    }

    const TextureSettings& settings = tex->Settings();

    uint32_t arrayLayers            = 1;
    bool isCubemap                  = false;
    VkImageViewType viewType        = VK_IMAGE_VIEW_TYPE_2D;
    VkImageCreateFlags imageFlags   = 0;

    switch (settings.Target)
    {
      case GraphicTypes::Target2D:
        arrayLayers = 1;
        viewType    = VK_IMAGE_VIEW_TYPE_2D;
        break;
      case GraphicTypes::TargetCubeMap:
        arrayLayers = 6;
        viewType    = VK_IMAGE_VIEW_TYPE_CUBE;
        imageFlags  = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        isCubemap   = true;
        break;
      case GraphicTypes::Target2DArray:
        arrayLayers = (uint32_t) std::max(1, settings.Layers);
        viewType    = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        break;
      default:
        TK_ERR("VulkanBackend::CreateTexture - unsupported target (%d)", (int) settings.Target);
        return;
    }

    // DepthTexture overrides the (color-default) settings format via GetDepthFormat().
    GraphicTypes effectiveFormat = settings.InternalFormat;
    if (DepthTexture* dt = tex->As<DepthTexture>())
    {
      effectiveFormat = dt->GetDepthFormat();
    }

    VkFormat vkFormat = ToVkFormat(effectiveFormat);
    if (vkFormat == VK_FORMAT_UNDEFINED)
    {
      TK_ERR("VulkanBackend::CreateTexture - unsupported format (%d)", (int) effectiveFormat);
      return;
    }

    const bool isDepth   = IsDepthFormat(vkFormat);
    const bool isStencil = IsStencilFormat(vkFormat);

    auto data            = std::make_shared<VulkanTexture>();
    data->context        = m_context.get();
    data->format         = vkFormat;
    data->aspect         = 0;
    if (isDepth)
      data->aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (isStencil)
      data->aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if (!isDepth && !isStencil)
      data->aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    data->extent                           = {(uint32_t) tex->m_width, (uint32_t) tex->m_height};
    data->arrayLayers                      = arrayLayers;
    const bool wantsMipChain               = !isDepth && settings.GenerateMipMap;
    data->mipLevels                        = wantsMipChain ? (uint32_t) tex->CalculateMipmapLevels() : 1u;
    data->isCubemap                        = isCubemap;
    data->currentLayout                    = VK_IMAGE_LAYOUT_UNDEFINED;

    // MsaaSampleCount enum integer values match VK_SAMPLE_COUNT_*_BIT.
    VkSampleCountFlagBits requestedSamples = (VkSampleCountFlagBits) (uint32_t) settings.msaaCount;
    if (requestedSamples == 0)
    {
      requestedSamples = VK_SAMPLE_COUNT_1_BIT;
    }
    if (requestedSamples != VK_SAMPLE_COUNT_1_BIT && (data->mipLevels > 1 || isCubemap))
    {
      TK_WRN("CreateTexture: MSAA sampleCount %u demoted (mipLevels=%u or cubemap)",
             (unsigned) requestedSamples,
             (unsigned) data->mipLevels);
      requestedSamples = VK_SAMPLE_COUNT_1_BIT;
    }

    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    usage |= isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // Per-format MSAA support varies. Some formats (FormatRGBA32F on many GPUs, depth+stencil
    // Per-format MSAA support varies — query device caps and demote to the largest supported
    // sample count <= requested.
    if (requestedSamples != VK_SAMPLE_COUNT_1_BIT)
    {
      VkImageFormatProperties props {};
      VkResult fpRes = vkGetPhysicalDeviceImageFormatProperties(m_context->GetPhysicalDevice(),
                                                                vkFormat,
                                                                VK_IMAGE_TYPE_2D,
                                                                VK_IMAGE_TILING_OPTIMAL,
                                                                usage,
                                                                imageFlags,
                                                                &props);
      if (fpRes != VK_SUCCESS)
      {
        TK_WRN("CreateTexture: format props query failed (%d) for format %d — demoted to x1",
               (int) fpRes,
               (int) vkFormat);
        requestedSamples = VK_SAMPLE_COUNT_1_BIT;
      }
      else if ((props.sampleCounts & requestedSamples) == 0)
      {
        VkSampleCountFlagBits demoted = VK_SAMPLE_COUNT_1_BIT;
        for (VkSampleCountFlagBits cand :
             {VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_1_BIT})
        {
          if (cand <= requestedSamples && (props.sampleCounts & cand) != 0)
          {
            demoted = cand;
            break;
          }
        }
        TK_WRN("CreateTexture: format %d does not support MSAA x%u (supported mask=0x%x); demoted to x%u",
               (int) vkFormat,
               (unsigned) requestedSamples,
               (unsigned) props.sampleCounts,
               (unsigned) demoted);
        requestedSamples = demoted;
      }
    }
    data->samples                     = requestedSamples;

    VkImageCreateInfo imageInfo       = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.flags                   = imageFlags;
    imageInfo.imageType               = VK_IMAGE_TYPE_2D;
    imageInfo.format                  = vkFormat;
    imageInfo.extent                  = {data->extent.width, data->extent.height, 1};
    imageInfo.mipLevels               = data->mipLevels;
    imageInfo.arrayLayers             = data->arrayLayers;
    imageInfo.samples                 = data->samples;
    imageInfo.tiling                  = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage                   = usage;
    imageInfo.sharingMode             = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout           = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(m_context->GetAllocator(), &imageInfo, &allocInfo, &data->image, &data->allocation, nullptr) !=
        VK_SUCCESS)
    {
      TK_ERR("VulkanBackend::CreateTexture - vmaCreateImage failed");
      return;
    }

    VkImageViewCreateInfo viewInfo       = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image                       = data->image;
    viewInfo.viewType                    = viewType;
    viewInfo.format                      = vkFormat;
    viewInfo.subresourceRange.aspectMask = data->aspect;
    viewInfo.subresourceRange.levelCount = data->mipLevels;
    viewInfo.subresourceRange.layerCount = data->arrayLayers;
    if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &data->view) != VK_SUCCESS)
    {
      TK_ERR("VulkanBackend::CreateTexture - vkCreateImageView failed");
      vmaDestroyImage(m_context->GetAllocator(), data->image, data->allocation);
      data->image      = VK_NULL_HANDLE;
      data->allocation = VK_NULL_HANDLE;
      return;
    }

    // Default sampler for color. Depth targets get one lazily via ApplyTextureSettings.
    if (!isDepth)
    {
      VkSamplerCreateInfo samplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
      samplerInfo.magFilter           = VK_FILTER_LINEAR;
      samplerInfo.minFilter           = VK_FILTER_LINEAR;
      samplerInfo.addressModeU        = ToVkAddressMode(settings.WarpS);
      samplerInfo.addressModeV        = ToVkAddressMode(settings.WarpT);
      samplerInfo.addressModeW        = ToVkAddressMode(settings.WarpR);
      samplerInfo.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      samplerInfo.minLod              = 0.0f;
      samplerInfo.maxLod              = (float) data->mipLevels;
      vkCreateSampler(m_context->GetDevice(), &samplerInfo, nullptr, &data->sampler);
    }

    // Transition out of UNDEFINED to the resting sampling layout before any code path can
    // sample the texture (ImGui showing an empty viewport, thumbnail previews, etc.).
    tex->m_gpuData       = data;

    const bool hasData2D = !isDepth && (tex->m_image != nullptr || tex->m_imagef != nullptr);
    CubeMap* cubeMapTex  = tex->As<CubeMap>();
    const bool hasCubemapData =
        cubeMapTex != nullptr && cubeMapTex->m_images.size() == 6 && cubeMapTex->m_images[0] != nullptr;

    {
      const VkImageLayout targetLayout =
          isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      m_context->EnqueueGpuWork(
          [img        = data->image,
           aspect     = data->aspect,
           mipLevels  = data->mipLevels,
           layerCount = data->arrayLayers,
           targetLayout](VkCommandBuffer cb)
          {
            VkImageMemoryBarrier b {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout                   = targetLayout;
            b.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            b.image                       = img;
            b.subresourceRange.aspectMask = aspect;
            b.subresourceRange.levelCount = mipLevels;
            b.subresourceRange.layerCount = layerCount;
            b.srcAccessMask               = 0;
            b.dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &b);
          });
      data->currentLayout = targetLayout;
    }

    if (hasData2D)
    {
      const void* pixels       = tex->m_imagef ? (const void*) tex->m_imagef : (const void*) tex->m_image;
      const int bpp            = BytesOfFormat(tex->Settings().InternalFormat);
      const VkDeviceSize bytes = (VkDeviceSize) tex->m_width * tex->m_height * bpp;
      UploadTexelData(m_context.get(), data.get(), pixels, bytes, 0, 0);
    }
    else if (hasCubemapData)
    {
      const VkDeviceSize faceBytes = (VkDeviceSize) tex->m_width * tex->m_height * 4;
      for (int face = 0; face < 6; ++face)
      {
        UploadTexelData(m_context.get(), data.get(), cubeMapTex->m_images[face], faceBytes, (uint32_t) face, 0);
      }
    }
  }

  void VulkanBackend::DestroyTexture(Texture* tex)
  {
    if (tex == nullptr)
    {
      return;
    }
    // Defer the shared_ptr drop so the VulkanTexture dtor (vkDestroyImage/View/Sampler) fires
    // only after the in-flight cb has retired.
    if (auto data = tex->m_gpuData)
    {
      tex->m_gpuData = nullptr;
      DeferDelete([data]() mutable { data.reset(); });
    }
  }

  void VulkanBackend::ApplyTextureSettings(Texture* tex)
  {
    if (tex == nullptr)
      return;
    auto* vt = static_cast<VulkanTexture*>(tex->m_gpuData.get());
    if (vt == nullptr || vt->image == VK_NULL_HANDLE)
      return;

    const TextureSettings& s = tex->Settings();
    const bool isDepth       = IsDepthFormat(vt->format);

    VkSamplerCreateInfo info {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter    = ToVkFilter(s.MagFilter);
    info.minFilter    = ToVkFilter(s.MinFilter);
    info.mipmapMode   = ToVkMipmapMode(s.MinFilter);
    info.addressModeU = ToVkAddressMode(s.WarpS);
    info.addressModeV = ToVkAddressMode(s.WarpT);
    info.addressModeW = ToVkAddressMode(s.WarpR);
    info.minLod       = 0.0f;
    info.maxLod       = (float) vt->mipLevels;

    // Anisotropy only applies to sampled color 2D. Clamped to maxSamplerAnisotropy limit.
    if (!isDepth && s.Target == GraphicTypes::Target2D)
    {
      EngineSettings& engSettings = GetEngineSettings();
      int anisoVal                = engSettings.m_graphics->GetAnisotropicTextureFilteringVal().GetValue<int>();
      if (anisoVal > 1)
      {
        VkPhysicalDeviceProperties props {};
        vkGetPhysicalDeviceProperties(m_context->GetPhysicalDevice(), &props);
        float maxAniso        = props.limits.maxSamplerAnisotropy;
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy    = std::min(maxAniso, (float) anisoVal);
      }
    }

    VkSampler newSampler = VK_NULL_HANDLE;
    if (vkCreateSampler(m_context->GetDevice(), &info, nullptr, &newSampler) != VK_SUCCESS)
    {
      TK_ERR("VulkanBackend::ApplyTextureSettings - vkCreateSampler failed");
      return;
    }

    // Defer old sampler — in-flight cb's may still reference it via bound descriptor sets.
    if (vt->sampler != VK_NULL_HANDLE)
    {
      VkDevice device = m_context->GetDevice();
      VkSampler old   = vt->sampler;
      DeferDelete([device, old]() { vkDestroySampler(device, old, nullptr); });
    }
    vt->sampler = newSampler;
  }

  void VulkanBackend::SetTextureSwizzleAlpha(Texture* tex, bool swizzleToOne, bool setLastBindBack)
  {
    // TODO: recreate VkImageView with VK_COMPONENT_SWIZZLE_ONE on alpha.
  }

  void VulkanBackend::GenerateMipmaps(Texture* tex)
  {
    if (tex == nullptr)
      return;
    auto* vt = static_cast<VulkanTexture*>(tex->m_gpuData.get());
    if (vt == nullptr || vt->image == VK_NULL_HANDLE || vt->mipLevels <= 1)
      return;

    m_context->EnqueueGpuWork(
        [img         = vt->image,
         aspect      = vt->aspect,
         arrayLayers = vt->arrayLayers,
         mipLevels   = vt->mipLevels,
         extent      = vt->extent](VkCommandBuffer cb)
        {
          // Generates each mip from mip-1 via vkCmdBlitImage. Keeps every mip in
          // SHADER_READ_ONLY between iterations so the next read-back barrier is consistent.
          for (uint32_t mip = 1; mip < mipLevels; ++mip)
          {
            VkImageMemoryBarrier toSrc {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toSrc.oldLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toSrc.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toSrc.srcQueueFamilyIndex           = VK_QUEUE_FAMILY_IGNORED;
            toSrc.dstQueueFamilyIndex           = VK_QUEUE_FAMILY_IGNORED;
            toSrc.image                         = img;
            toSrc.subresourceRange.aspectMask   = aspect;
            toSrc.subresourceRange.baseMipLevel = mip - 1;
            toSrc.subresourceRange.levelCount   = 1;
            toSrc.subresourceRange.layerCount   = arrayLayers;
            toSrc.srcAccessMask                 = VK_ACCESS_SHADER_READ_BIT;
            toSrc.dstAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &toSrc);

            VkImageMemoryBarrier toDst          = toSrc;
            toDst.oldLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toDst.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toDst.subresourceRange.baseMipLevel = mip;
            toDst.srcAccessMask                 = VK_ACCESS_SHADER_READ_BIT;
            toDst.dstAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &toDst);

            int32_t srcW = std::max(1, (int32_t) (extent.width >> (mip - 1)));
            int32_t srcH = std::max(1, (int32_t) (extent.height >> (mip - 1)));
            int32_t dstW = std::max(1, (int32_t) (extent.width >> mip));
            int32_t dstH = std::max(1, (int32_t) (extent.height >> mip));

            VkImageBlit blit {};
            blit.srcSubresource.aspectMask     = aspect;
            blit.srcSubresource.mipLevel       = mip - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount     = arrayLayers;
            blit.srcOffsets[1]                 = {srcW, srcH, 1};
            blit.dstSubresource                = blit.srcSubresource;
            blit.dstSubresource.mipLevel       = mip;
            blit.dstOffsets[1]                 = {dstW, dstH, 1};
            vkCmdBlitImage(cb,
                           img,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           img,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &blit,
                           VK_FILTER_LINEAR);

            VkImageMemoryBarrier backToRead = toSrc;
            backToRead.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            backToRead.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            backToRead.srcAccessMask        = VK_ACCESS_TRANSFER_READ_BIT;
            backToRead.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &backToRead);

            VkImageMemoryBarrier dstToRead = toDst;
            dstToRead.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dstToRead.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            dstToRead.srcAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstToRead.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &dstToRead);
          }
        });
    vt->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }

  void VulkanBackend::UpdateTextureRegion(Texture* tex, const void* data)
  {
    if (tex == nullptr || data == nullptr)
      return;
    auto* vt = static_cast<VulkanTexture*>(tex->m_gpuData.get());
    if (vt == nullptr || vt->image == VK_NULL_HANDLE)
      return;
    const int bpp            = BytesOfFormat(tex->Settings().InternalFormat);
    const VkDeviceSize bytes = (VkDeviceSize) tex->m_width * tex->m_height * bpp;
    UploadTexelData(m_context.get(), vt, data, bytes, 0, 0);
  }

  void VulkanBackend::SetTextureMaxMipLevel(Texture* tex, int maxLevel)
  {
    // TODO: recreate VkImageView with limited mip range.
  }

  void VulkanBackend::AllocateCubemapMipStorage(Texture* tex)
  {
    // No-op: Vulkan allocates all mips at image creation.
  }

  void VulkanBackend::CopyCubemapFaceFromFramebuffer(Texture* cubemap,
                                                     int face,
                                                     int mip,
                                                     int width,
                                                     int height,
                                                     Framebuffer* readFb,
                                                     Framebuffer* writeFb)
  {
    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();

    if (cubemap == nullptr || readFb == nullptr || cb == VK_NULL_HANDLE)
      return;

    auto* vDst = static_cast<VulkanTexture*>(cubemap->m_gpuData.get());
    if (vDst == nullptr || vDst->image == VK_NULL_HANDLE)
      return;

    auto* vFb = static_cast<VulkanFramebuffer*>(readFb->m_gpuData.get());
    if (vFb == nullptr || vFb->colorAttachments[0].tex == nullptr)
      return;

    VulkanTexture* vSrc = vFb->colorAttachments[0].tex;
    if (vSrc->image == VK_NULL_HANDLE)
      return;

    // Source → TRANSFER_SRC.
    VkImageMemoryBarrier srcBarrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    srcBarrier.oldLayout                       = vSrc->currentLayout;
    srcBarrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    srcBarrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.image                           = vSrc->image;
    srcBarrier.subresourceRange.aspectMask     = vSrc->aspect;
    srcBarrier.subresourceRange.baseMipLevel   = 0;
    srcBarrier.subresourceRange.levelCount     = 1;
    srcBarrier.subresourceRange.baseArrayLayer = (uint32_t) face;
    srcBarrier.subresourceRange.layerCount     = 1;
    srcBarrier.srcAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    srcBarrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cb,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &srcBarrier);

    // 2. Hedef (Dest) Küp Yüzeyini Transfer'e Geçir
    VkImageMemoryBarrier dstBarrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    dstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Üzerine komple yazacağımız için eski verinin önemi yok
    dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dstBarrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.image                           = vDst->image;
    dstBarrier.subresourceRange.aspectMask     = vDst->aspect;
    dstBarrier.subresourceRange.baseMipLevel   = (uint32_t) mip;
    dstBarrier.subresourceRange.levelCount     = 1;
    dstBarrier.subresourceRange.baseArrayLayer = (uint32_t) face;
    dstBarrier.subresourceRange.layerCount     = 1;
    dstBarrier.srcAccessMask                   = 0;
    dstBarrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cb,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &dstBarrier);

    VkImageCopy region {};
    region.srcSubresource.aspectMask     = vSrc->aspect;
    region.srcSubresource.mipLevel       = 0;
    region.srcSubresource.baseArrayLayer = (uint32_t) face;
    region.srcSubresource.layerCount     = 1;
    region.dstSubresource.aspectMask     = vDst->aspect;
    region.dstSubresource.mipLevel       = (uint32_t) mip;
    region.dstSubresource.baseArrayLayer = (uint32_t) face;
    region.dstSubresource.layerCount     = 1;
    region.extent                        = {(uint32_t) width, (uint32_t) height, 1};

    vkCmdCopyImage(cb,
                   vSrc->image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   vDst->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1,
                   &region);

    // Restore source layout.
    srcBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    srcBarrier.newLayout     = vSrc->currentLayout;
    srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    srcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cb,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &srcBarrier);

    // Cube face → SHADER_READ_ONLY.
    dstBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dstBarrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dstBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cb,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &dstBarrier);
  }

  void VulkanBackend::CreateMesh(Mesh* mesh)
  {
    assert(mesh != nullptr && "CreateMesh: null Mesh");

    DestroyMesh(mesh);

    const void* vertexData   = mesh->GetClientVertexData();
    const size_t vertexCount = mesh->GetClientVertexCount();
    const int vertexStride   = mesh->GetVertexSize();

    if (vertexData == nullptr || vertexCount == 0 || vertexStride <= 0)
    {
      mesh->m_vertexCount = 0;
      mesh->m_indexCount  = 0;
      return;
    }

    auto data                      = std::make_shared<VulkanMesh>();
    data->context                  = m_context.get();

    const VkDeviceSize vertexBytes = (VkDeviceSize) vertexStride * (VkDeviceSize) vertexCount;
    data->vertex =
        VulkanBuffer::UploadDeviceLocal(m_context.get(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexData, vertexBytes);
    if (data->vertex.handle == VK_NULL_HANDLE)
    {
      TK_ERR("VulkanBackend::CreateMesh: vertex upload failed (%llu bytes)", (unsigned long long) vertexBytes);
      return;
    }
    mesh->m_vertexCount = (uint) vertexCount;

    if (!mesh->m_clientSideIndices.empty())
    {
      const VkDeviceSize indexBytes = sizeof(uint) * (VkDeviceSize) mesh->m_clientSideIndices.size();
      data->index                   = VulkanBuffer::UploadDeviceLocal(m_context.get(),
                                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                                    mesh->m_clientSideIndices.data(),
                                                    indexBytes);
      if (data->index.handle == VK_NULL_HANDLE)
      {
        TK_ERR("VulkanBackend::CreateMesh: index upload failed (%llu bytes)", (unsigned long long) indexBytes);
        return;
      }
      mesh->m_indexCount = (uint) mesh->m_clientSideIndices.size();
    }

    mesh->m_gpuData = data;
  }

  void VulkanBackend::DestroyMesh(Mesh* mesh)
  {
    if (mesh == nullptr || mesh->m_gpuData == nullptr)
    {
      return;
    }
    // Defer until the in-flight cb releases vertex/index buffers.
    auto data       = mesh->m_gpuData;
    mesh->m_gpuData = nullptr;
    DeferDelete([data]() mutable { data.reset(); });
  }

  void VulkanBackend::CreateUniformBuffer(UniformBuffer* ub, uint64 size)
  {
    assert(ub != nullptr && "CreateUniformBuffer: null UniformBuffer");
    assert(ub->m_gpuData == nullptr && "CreateUniformBuffer: already has gpu data");

    if (size == 0)
    {
      // ToolKit allows size-0 UBOs at construction; the real Init comes later.
      return;
    }

    auto data     = std::make_shared<VulkanUniformBuffer>();
    data->context = m_context.get();
    data->buffer =
        VulkanBuffer::CreateHostVisibleMapped(m_context.get(),
                                              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                              (VkDeviceSize) size);
    if (data->buffer.handle == VK_NULL_HANDLE)
    {
      TK_ERR("VulkanBackend::CreateUniformBuffer: failed to allocate %llu byte UBO", (unsigned long long) size);
      return;
    }

    ub->m_gpuData = data;
    ub->m_size    = size;
  }

  void VulkanBackend::DestroyUniformBuffer(UniformBuffer* ub)
  {
    if (ub == nullptr || ub->m_gpuData == nullptr)
    {
      return;
    }
    auto data     = ub->m_gpuData;
    ub->m_gpuData = nullptr;
    DeferDelete([data]() mutable { data.reset(); });
  }

  void VulkanBackend::UpdateUniformBuffer(UniformBuffer* ub, const void* data, uint64 size)
  {
    if (ub == nullptr || data == nullptr || size == 0)
    {
      return;
    }
    auto* gpu = static_cast<VulkanUniformBuffer*>(ub->m_gpuData.get());
    if (gpu == nullptr || gpu->buffer.mapped == nullptr || size > gpu->buffer.size)
    {
      TK_ERR("VulkanBackend::UpdateUniformBuffer: invalid mapped ptr or oversized write (%llu > %llu)",
             (uint64) size,
             (uint64) (gpu != nullptr ? gpu->buffer.size : 0));
      return;
    }

    // Slot 2 is the per-draw UBO; SubmitPerDrawData handles uploads via the ring path. Skip
    // here to avoid recording a no-op vkCmdUpdateBuffer + 2 barriers per draw.
    if (ub->m_slot == ReservedUniformBufferSlots::PerDrawData)
    {
      return;
    }

    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (m_swapchain && m_swapchain->IsFrameActive())
    {
      cb = m_swapchain->GetCurrentCommandBuffer();
    }

    // vkCmdUpdateBuffer + vkCmdPipelineBarrier are illegal inside RP. Close offscreen RP cleanly
    // (next Draw reopens with LOAD). Swapchain RP uses LOAD_OP_CLEAR so we fall back to memcpy.
    if (cb != VK_NULL_HANDLE && m_activePassFb != nullptr && m_rpActive)
    {
      CloseOffscreenRenderPassIfOpen(cb);
    }
    const bool insideRenderPass = m_swapchain && m_swapchain->IsSwapchainPassActive();

    if (cb != VK_NULL_HANDLE && !insideRenderPass)
    {
      // Bracket the UBO update with UNIFORM_READ ↔ TRANSFER_WRITE barriers so in-flight draws
      // see the previous value and subsequent draws see the new one.
      VkMemoryBarrier preBarrier {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
      preBarrier.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
      preBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(cb,
                           VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0,
                           1,
                           &preBarrier,
                           0,
                           nullptr,
                           0,
                           nullptr);

      vkCmdUpdateBuffer(cb, gpu->buffer.handle, 0, size, data);

      VkMemoryBarrier postBarrier {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
      postBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      postBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
      vkCmdPipelineBarrier(cb,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           0,
                           1,
                           &postBarrier,
                           0,
                           nullptr,
                           0,
                           nullptr);

      // No host memcpy here. The previous frame's cb may still be reading this same VkBuffer
      // on the GPU; a parallel host write to HOST_COHERENT mapped memory becomes visible to the
      // device immediately and corrupts those in-flight reads (observed: shadow pass POV
      // bleeding into the main camera viewport at FIF=2). vkCmdUpdateBuffer above writes the new
      // data into the buffer at this cb's GPU execution time, which is gated by the cross-cb
      // BOTTOM_OF_PIPE→TOP_OF_PIPE barrier in BeginFrame — so the previous cb finishes reading
      // before this cb's update writes. HOST_COHERENT semantics still make the new contents
      // observable on the host AFTER this cb retires, so any inter-frame mapped-memory readers
      // see correct values.
    }
    else
    {
      // No active cb — direct host write is safe.
      std::memcpy(gpu->buffer.mapped, data, (size_t) size);
    }

    // Register non-perDraw UBOs in the global registry (Camera, GraphicConsts, etc.) so the
    // descriptor flush can pick them up. Only dirty on real handle change to keep cache hot.
    const int slot = ub->m_slot;
    if (slot >= 0 && slot != ReservedUniformBufferSlots::PerDrawData && slot < kMaxUboSlots)
    {
      GlobalUboEntry& entry = m_globalUboRegistry[slot];
      if (entry.handle != gpu->buffer.handle || entry.size != gpu->buffer.size)
      {
        entry.handle   = gpu->buffer.handle;
        entry.size     = gpu->buffer.size;
        m_shadow.dirty = true;
      }
    }
  }

  // Data-driven descriptor flush. Walks the bound program's declared resources, resolves each
  // to a native handle from m_shadow / m_globalUboRegistry, hashes, and either reuses a cached
  // set or allocates+writes a fresh one. Cache is per-FIF-slot, cleared in BeginFrame alongside
  // the pool reset.
  VkDescriptorSet VulkanBackend::FlushDescriptorState()
  {
    // Same-state fast path: shadow only dirties on real value change. Reuse the last set if
    // program + handles unchanged. Dynamic offset is bound at call site, so per-draw offset
    // changes don't invalidate the cached set.
    if (!m_shadow.dirty && m_lastFlushedSet != VK_NULL_HANDLE && m_lastFlushedProgram == m_boundProgram)
    {
      return m_lastFlushedSet;
    }

    struct Resolved
    {
      uint32_t binding          = 0;
      ShaderResource::Type type = ShaderResource::Type::Texture;
      VkImageView view          = VK_NULL_HANDLE;
      VkSampler sampler         = VK_NULL_HANDLE;
      VkBuffer buffer           = VK_NULL_HANDLE;
      VkDeviceSize bufferSize   = 0;
      bool isPerDrawDynamic     = false;
    };

    std::vector<Resolved> resolved;
    resolved.reserve(m_boundProgram->resources.size());

    for (const ShaderResource& res : m_boundProgram->resources)
    {
      Resolved r {};
      r.type = res.type;

      if (res.type == ShaderResource::Type::Texture)
      {
        if (res.slot < 0 || res.slot >= (int) VulkanBindings::kTextureBindingCount)
        {
          continue;
        }
        r.binding             = VulkanBindings::kTextureBindingBase + (uint32_t) res.slot;

        const TexturePtr& tex = m_shadow.boundTextures[res.slot];
        if (tex && tex->m_gpuData)
        {
          auto* vt  = static_cast<VulkanTexture*>(tex->m_gpuData.get());
          r.view    = vt->view;
          r.sampler = vt->sampler;
        }
        else
        {
          // Declared-but-unbound slot — dummy fallback matching shader's declared ViewType.
          const VulkanTexture* dummy = (res.viewType == ShaderResource::ViewType::TexCube) ? m_dummyCubeTexture.get()
                                       : (res.viewType == ShaderResource::ViewType::Tex2DArray)
                                           ? m_dummy2DArrayTexture.get()
                                           : m_dummyTexture.get();
          r.view                     = dummy->view;
          r.sampler                  = dummy->sampler;
        }
      }
      else // UniformBuffer
      {
        if (res.slot == ReservedUniformBufferSlots::PerDrawData) // per-draw dynamic UBO
        {
          r.binding          = VulkanBindings::kPerDrawUboBinding;
          r.isPerDrawDynamic = true;
          r.buffer           = m_context->GetPerDrawUboBuffer();
          r.bufferSize       = m_shadow.perDrawSize;
          if (r.buffer == VK_NULL_HANDLE || r.bufferSize == 0)
          {
            // Programs declaring the per-draw UBO must receive a SubmitPerDrawData between
            // BindPipeline and Draw. Drop the draw to surface the missing call (else the NVIDIA
            // ICD NULL-derefs in vkCmdDraw).
            TK_ERR("FlushDescriptorState: program %p declares per-draw UBO (binding %u) but "
                   "no SubmitPerDrawData was issued — dropping draw",
                   (void*) m_boundProgram,
                   (unsigned) VulkanBindings::kPerDrawUboBinding);
            return VK_NULL_HANDLE;
          }
        }
        else
        {
          if (res.slot < 0 || res.slot >= kMaxUboSlots)
          {
            continue;
          }
          r.binding = VulkanBindings::UboBindingFor((uint32_t) res.slot);

          // BindUniformBuffer override wins over the global registry.
          if (UniformBuffer* override = m_shadow.boundUniforms[res.slot])
          {
            auto* gpu = static_cast<VulkanUniformBuffer*>(override->m_gpuData.get());
            if (gpu != nullptr)
            {
              r.buffer     = gpu->buffer.handle;
              r.bufferSize = gpu->buffer.size;
            }
          }
          if (r.buffer == VK_NULL_HANDLE)
          {
            const GlobalUboEntry& entry = m_globalUboRegistry[res.slot];
            if (entry.handle != VK_NULL_HANDLE)
            {
              r.buffer     = entry.handle;
              r.bufferSize = entry.size;
            }
          }
          if (r.buffer == VK_NULL_HANDLE)
          {
            continue;
          }
        }
      }
      resolved.push_back(r);
    }

    // Mix the bound program into the hash so unrelated programs with overlapping handles get
    // distinct cache entries.
    auto mix = [](uint64_t h, uint64_t v)
    {
      v ^= v >> 33;
      v *= 0xff51afd7ed558ccdULL;
      v ^= v >> 33;
      v *= 0xc4ceb9fe1a85ec53ULL;
      v ^= v >> 33;
      return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
    };

    uint64_t hash = mix(0, (uint64_t) (uintptr_t) m_boundProgram);
    for (const Resolved& r : resolved)
    {
      hash = mix(hash, (uint64_t) r.binding);
      hash = mix(hash, (uint64_t) r.type);
      hash = mix(hash, (uint64_t) (uintptr_t) r.view);
      hash = mix(hash, (uint64_t) (uintptr_t) r.sampler);
      hash = mix(hash, (uint64_t) (uintptr_t) r.buffer);
      hash = mix(hash, (uint64_t) r.bufferSize);
      hash = mix(hash, r.isPerDrawDynamic ? 1ULL : 0ULL);
    }

    const uint frame = m_swapchain->GetCurrentFrameIndex();
    if (frame >= m_descriptorCache.size())
    {
      return VK_NULL_HANDLE;
    }
    auto& cache = m_descriptorCache[frame];
    for (const DescriptorCacheEntry& e : cache)
    {
      if (e.hash == hash)
      {
        m_shadow.dirty       = false;
        m_lastFlushedSet     = e.set;
        m_lastFlushedProgram = m_boundProgram;
        return e.set;
      }
    }

    VkDescriptorSet set = m_context->AllocateFrameDescriptorSet(frame, m_context->GetGlobalDescriptorSetLayout());
    if (set == VK_NULL_HANDLE)
    {
      return VK_NULL_HANDLE;
    }

    // Batch every binding into a single vkUpdateDescriptorSets. Per-binding helpers
    // (VulkanDescriptor::Write*) each issue their own driver call — a full PBR forward draw
    // declares ~21 resources, so per-miss they used to fire ~21 host-side API calls. The image
    // / buffer info vectors must outlive the update call (pImageInfo / pBufferInfo are pointers
    // into them); reserve up front so push_back can't reallocate and invalidate those pointers.
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    // Reserve before push_back: VkWriteDescriptorSet holds raw pointers into these arrays.
    writes.reserve(resolved.size());
    imageInfos.reserve(resolved.size());
    bufferInfos.reserve(resolved.size());

    for (const Resolved& r : resolved)
    {
      VkWriteDescriptorSet w {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w.dstSet          = set;
      w.dstBinding      = r.binding;
      w.dstArrayElement = 0;
      w.descriptorCount = 1;

      if (r.type == ShaderResource::Type::Texture)
      {
        VkDescriptorImageInfo info {};
        info.sampler     = r.sampler;
        info.imageView   = r.view;
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos.push_back(info);

        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo     = &imageInfos.back();
      }
      else
      {
        VkDescriptorBufferInfo info {};
        info.buffer = r.buffer;
        info.offset = 0;
        info.range  = r.bufferSize;
        bufferInfos.push_back(info);

        w.descriptorType =
            r.isPerDrawDynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &bufferInfos.back();
      }
      writes.push_back(w);
    }

    if (!writes.empty())
    {
      vkUpdateDescriptorSets(m_context->GetDevice(), (uint32_t) writes.size(), writes.data(), 0, nullptr);
    }

    cache.push_back({hash, set});
    m_shadow.dirty       = false;
    m_lastFlushedSet     = set;
    m_lastFlushedProgram = m_boundProgram;
    return set;
  }

  GpuResourceDataPtr VulkanBackend::CreateShader(Shader* shader, const String& source)
  {
    if (shader == nullptr)
    {
      return nullptr;
    }
    if (shader->m_shaderType == ShaderType::IncludeShader)
    {
      // Includes are inlined by the engine before reaching here.
      TK_ERR("Include shader can't be compiled: %s", shader->GetFile().c_str());
      return nullptr;
    }

    const bool isVertex             = (shader->m_shaderType == ShaderType::VertexShader);
    const VulkanShader::Stage stage = isVertex ? VulkanShader::Stage::Vertex : VulkanShader::Stage::Fragment;

    std::vector<uint32_t> spirv     = VulkanShader::CompileGlslToSpirv(stage, source, shader->GetFile());
    if (spirv.empty())
    {
      TK_WRN("CreateShader: compile failed for '%s'", shader->GetFile().c_str());
      return nullptr;
    }

    VkShaderModule module = VulkanShader::CreateShaderModule(m_context->GetDevice(), spirv);
    if (module == VK_NULL_HANDLE)
    {
      return nullptr;
    }

    auto data     = std::make_shared<VulkanShaderModule>();
    data->context = m_context.get();
    data->module  = module;
    return data;
  }

  void VulkanBackend::DestroyShader(GpuResourceData* shaderData)
  {
    // Eager destroy is safe: programs referencing this module have already been torn down by
    // shutdown vkDeviceWaitIdle or by explicit DestroyGpuProgram in hot-reload.
    auto* sm = static_cast<VulkanShaderModule*>(shaderData);
    if (sm == nullptr || sm->context == nullptr || sm->module == VK_NULL_HANDLE)
    {
      return;
    }
    vkDestroyShaderModule(sm->context->GetDevice(), sm->module, nullptr);
    sm->module = VK_NULL_HANDLE;
  }

  void VulkanBackend::CreateGpuProgram(GpuProgram* program, const ShaderResourceBinding* bindings, int bindingCount)
  {
    (void) bindings;
    (void) bindingCount;

    assert(program != nullptr && "CreateGpuProgram: null program");
    assert(program->m_gpuData == nullptr && "CreateGpuProgram: program already has gpu data");

    if (program->m_shaders.size() < 2)
    {
      TK_ERR("CreateGpuProgram: program needs at least vertex+fragment shaders");
      return;
    }

    auto* vertSm = static_cast<VulkanShaderModule*>(program->m_shaders[0]->m_gpuData.get());
    auto* fragSm = static_cast<VulkanShaderModule*>(program->m_shaders[1]->m_gpuData.get());
    if (vertSm == nullptr || fragSm == nullptr || vertSm->module == VK_NULL_HANDLE || fragSm->module == VK_NULL_HANDLE)
    {
      TK_ERR("CreateGpuProgram: missing compiled shader module(s)");
      return;
    }

    auto data                       = std::make_shared<VulkanGpuProgram>();
    data->context                   = m_context.get();
    data->vert                      = vertSm->module;
    data->frag                      = fragSm->module;
    data->resources                 = program->m_resources;

    // Every program references the shared kitchen-sink descriptor set layout. Unused bindings
    // are simply not written. Per-draw data routes through the dynamic UBO, not push constants.
    VkDescriptorSetLayout globalSet = m_context->GetGlobalDescriptorSetLayout();
    if (globalSet == VK_NULL_HANDLE)
    {
      TK_ERR("CreateGpuProgram: global descriptor set layout missing (context not initialized?)");
      return;
    }

    VkPipelineLayoutCreateInfo plci {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &globalSet;
    plci.pushConstantRangeCount = 0;
    plci.pPushConstantRanges    = nullptr;

    if (VkResult r = vkCreatePipelineLayout(m_context->GetDevice(), &plci, nullptr, &data->pipelineLayout);
        r != VK_SUCCESS)
    {
      TK_ERR("CreateGpuProgram: vkCreatePipelineLayout failed: %d", r);
      return;
    }

    program->m_gpuData = data;
  }

  void VulkanBackend::DestroyGpuProgram(GpuProgram* program)
  {
    if (program == nullptr || program->m_gpuData == nullptr)
    {
      return;
    }
    auto data          = program->m_gpuData;
    program->m_gpuData = nullptr;

    // Evict pipelines keyed by this program's layout BEFORE the program dtor destroys it.
    // Pipelines retire in deleter-bucket push order, ahead of the program shared_ptr release.
    auto* progData     = static_cast<VulkanGpuProgram*>(data.get());
    if (progData != nullptr && progData->pipelineLayout != VK_NULL_HANDLE && m_pipelineCache)
    {
      VkDevice device = m_context->GetDevice();
      m_pipelineCache->InvalidateForPipelineLayout(
          progData->pipelineLayout,
          [this, device](VkPipeline pipe)
          { DeferDelete([device, pipe]() { vkDestroyPipeline(device, pipe, nullptr); }); });
    }

    DeferDelete([data]() mutable { data.reset(); });
  }

  int VulkanBackend::GetUniformLocation(GpuProgram* program, const char* name)
  {
    // Not applicable in Vulkan — descriptors only.
    return -1;
  }

  void VulkanBackend::CreateFramebuffer(Framebuffer* fb)
  {
    assert(fb && "CreateFramebuffer: null framebuffer");
    assert(fb->m_gpuData == nullptr && "CreateFramebuffer: framebuffer already has gpu data");

    auto data     = std::make_shared<VulkanFramebuffer>();
    data->context = m_context.get();
    data->width   = (uint32_t) fb->GetSettings().width;
    data->height  = (uint32_t) fb->GetSettings().height;
    data->dirty   = true;
    fb->m_gpuData = data;
  }

  void VulkanBackend::DestroyFramebuffer(Framebuffer* fb)
  {
    if (fb == nullptr)
    {
      return;
    }
    // Defer shared_ptr release until in-flight cb retires. Evict pipelines keyed on this fb's
    // cached RPs first (handle-reuse hazard, same as BuildRpVariant).
    if (auto data = fb->m_gpuData)
    {
      auto* fbData = static_cast<VulkanFramebuffer*>(data.get());
      if (fbData != nullptr && m_pipelineCache)
      {
        VkDevice device = m_context->GetDevice();
        for (VulkanFramebuffer::RpVariant& v : fbData->rpVariants)
        {
          if (v.valid && v.rp != VK_NULL_HANDLE)
          {
            m_pipelineCache->InvalidateForRenderPass(
                v.rp,
                [this, device](VkPipeline pipe)
                { DeferDelete([device, pipe]() { vkDestroyPipeline(device, pipe, nullptr); }); });
          }
        }
      }
      fb->m_gpuData = nullptr;
      DeferDelete([data]() mutable { data.reset(); });
    }
  }

  void VulkanBackend::AttachColorTarget(Framebuffer* fb,
                                        RenderTargetPtr rt,
                                        int attachment,
                                        int mip,
                                        int layer,
                                        int face)
  {
    auto* fbData = static_cast<VulkanFramebuffer*>(fb->m_gpuData.get());
    assert(fbData && "AttachColorTarget: framebuffer has no gpu data");
    assert(attachment >= 0 && attachment < VulkanFramebuffer::kMaxColorAttachments);

    // Slot just borrows view handles; subresource views are owned by VulkanTexture's cache.
    auto& slot = fbData->colorAttachments[attachment];
    slot       = {};
    slot.tex   = static_cast<VulkanTexture*>(rt->m_gpuData.get());

    const bool needsSubresourceView =
        slot.tex != nullptr && (face >= 0 || layer >= 0 || (mip > 0 && slot.tex->mipLevels > 1));

    if (slot.tex == nullptr)
    {
      // Null texture — leave slot cleared.
    }
    else if (needsSubresourceView)
    {
      uint32_t baseArrayLayer = 0;
      uint32_t layerCount     = 1;
      if (face >= 0)
      {
        baseArrayLayer = (uint32_t) face;
        layerCount     = 1;
      }
      else if (layer >= 0)
      {
        baseArrayLayer = (uint32_t) layer;
        layerCount     = 1;
      }
      uint32_t baseMip = (uint32_t) std::max(0, mip);
      if (baseMip >= slot.tex->mipLevels)
      {
        TK_WRN("AttachColorTarget: mip %u >= image mipLevels %u — clamping to %u",
               baseMip,
               slot.tex->mipLevels,
               slot.tex->mipLevels - 1);
        baseMip = slot.tex->mipLevels - 1;
      }

      // Cache key: (mip, layer). Cubemap face is encoded as layer.
      VkImageView resolvedView = VK_NULL_HANDLE;
      for (const auto& e : slot.tex->subresourceViews)
      {
        if (e.valid && e.mip == baseMip && e.layer == baseArrayLayer)
        {
          resolvedView = e.view;
          break;
        }
      }

      if (resolvedView == VK_NULL_HANDLE)
      {
        VkImageViewCreateInfo viewInfo           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image                           = slot.tex->image;
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = slot.tex->format;
        viewInfo.subresourceRange.aspectMask     = slot.tex->aspect;
        viewInfo.subresourceRange.baseMipLevel   = baseMip;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
        viewInfo.subresourceRange.layerCount     = layerCount;
        if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &resolvedView) != VK_SUCCESS)
        {
          TK_ERR("AttachColorTarget: vkCreateImageView failed for face/layer/mip view");
          slot.view = VK_NULL_HANDLE;
          return;
        }
        VulkanTexture::SubresourceViewEntry entry;
        entry.mip   = baseMip;
        entry.layer = baseArrayLayer;
        entry.view  = resolvedView;
        entry.valid = true;
        slot.tex->subresourceViews.push_back(entry);
      }

      slot.view           = resolvedView;
      slot.ownsView       = false;
      slot.baseArrayLayer = baseArrayLayer;
      slot.layerCount     = layerCount;
      slot.baseMipLevel   = baseMip;
    }
    else
    {
      slot.view           = slot.tex->view;
      slot.ownsView       = false;
      slot.baseArrayLayer = 0;
      slot.layerCount     = slot.tex->arrayLayers;
      slot.baseMipLevel   = 0;
    }

    fbData->dirty = true;
  }

  void VulkanBackend::DetachColorTarget(Framebuffer* fb, int attachment)
  {
    auto* fbData = static_cast<VulkanFramebuffer*>(fb->m_gpuData.get());
    if (fbData == nullptr)
    {
      return;
    }
    assert(attachment >= 0 && attachment < VulkanFramebuffer::kMaxColorAttachments);

    auto& slot = fbData->colorAttachments[attachment];
    if (slot.ownsView && slot.view != VK_NULL_HANDLE)
    {
      VkDevice device = m_context->GetDevice();
      VkImageView old = slot.view;
      DeferDelete([device, old]() { vkDestroyImageView(device, old, nullptr); });
    }
    slot          = {};

    fbData->dirty = true;
  }

  void VulkanBackend::AttachDepthTarget(Framebuffer* fb, DepthTexturePtr dt)
  {
    auto* fbData = static_cast<VulkanFramebuffer*>(fb->m_gpuData.get());
    assert(fbData && "AttachDepthTarget: framebuffer has no gpu data");

    auto& slot = fbData->depthAttachment;
    if (slot.ownsView && slot.view != VK_NULL_HANDLE)
    {
      VkDevice device = m_context->GetDevice();
      VkImageView old = slot.view;
      DeferDelete([device, old]() { vkDestroyImageView(device, old, nullptr); });
    }
    slot          = {};
    slot.tex      = static_cast<VulkanTexture*>(dt->m_gpuData.get());
    slot.view     = slot.tex ? slot.tex->view : VK_NULL_HANDLE;

    fbData->dirty = true;
  }

  void VulkanBackend::DetachDepthTarget(Framebuffer* fb)
  {
    auto* fbData = static_cast<VulkanFramebuffer*>(fb->m_gpuData.get());
    if (fbData == nullptr)
    {
      return;
    }
    auto& slot = fbData->depthAttachment;
    if (slot.ownsView && slot.view != VK_NULL_HANDLE)
    {
      VkDevice device = m_context->GetDevice();
      VkImageView old = slot.view;
      DeferDelete([device, old]() { vkDestroyImageView(device, old, nullptr); });
    }
    slot          = {};

    fbData->dirty = true;
  }

  void VulkanBackend::SetUniform4f(int location, const Vec4& value)
  {
    // No-op (no glUniform-style addressing in Vulkan). GetUniformLocation returns -1.
    (void) location;
    (void) value;
  }

  String VulkanBackend::GetBackendRendererString()
  {
    if (m_context == nullptr || m_context->GetPhysicalDevice() == VK_NULL_HANDLE)
    {
      return "Vulkan";
    }
    VkPhysicalDeviceProperties props {};
    vkGetPhysicalDeviceProperties(m_context->GetPhysicalDevice(), &props);
    return String("Vulkan: ") + props.deviceName;
  }

  int VulkanBackend::GetMaxArrayTextureLayers()
  {
    if (m_context == nullptr || m_context->GetPhysicalDevice() == VK_NULL_HANDLE)
    {
      return 256;
    }
    VkPhysicalDeviceProperties props {};
    vkGetPhysicalDeviceProperties(m_context->GetPhysicalDevice(), &props);
    return (int) props.limits.maxImageArrayLayers;
  }

  void VulkanBackend::SetSrgbAutoEncoding(bool enable)
  {
    // Vulkan handles sRGB via swapchain format; no-op here.
    (void) enable;
  }

  void VulkanBackend::Finish()
  {
    if (m_context != nullptr && m_context->GetDevice() != VK_NULL_HANDLE)
    {
      vkDeviceWaitIdle(m_context->GetDevice());
    }
  }

  void VulkanBackend::SetDefaultClearColor(const Vec4& color)
  {
    // Stored on the backend so ClearColorBuffer / ClearBuffer paths and any future implicit
    // backbuffer clear can pick it up. StartPass takes its clear color from PassDesc directly,
    // so this is currently informational; engine uses it for "set once, expect all subsequent
    // passes to use this when they didn't override".
    m_clearColor = color;
  }

  bool VulkanBackend::ValidateBackbufferSrgbEncoding()
  {
    // Reports whether the swapchain format we picked applies sRGB encode in hardware vs UNORM
    // (which would force gamma encoding in-shader).
    if (m_swapchain == nullptr)
    {
      return false;
    }

    switch (m_swapchain->GetFormat())
    {
      case VK_FORMAT_R8G8B8_SRGB:
      case VK_FORMAT_R8G8B8A8_SRGB:
      case VK_FORMAT_B8G8R8_SRGB:
      case VK_FORMAT_B8G8R8A8_SRGB:
      case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return true;
      default:
        return false;
    }
  }

  void VulkanBackend::EnableScissorTest(bool enable)
  {
    // Scissor is dynamic state in Vulkan; "disable" means a full-viewport scissor.
  }

  void VulkanBackend::ReadPixels(int x, int y, int w, int h, GraphicTypes format, GraphicTypes type, void* data)
  {
    // TODO: vkCmdCopyImageToBuffer + map staging.
  }

  void VulkanBackend::UpdateTextureSubRegion(Texture* tex, int x, int y, int w, int h, const void* data)
  {
    if (tex == nullptr || data == nullptr || w <= 0 || h <= 0)
      return;
    auto* vt = static_cast<VulkanTexture*>(tex->m_gpuData.get());
    if (vt == nullptr || vt->image == VK_NULL_HANDLE)
      return;

    const int bpp            = BytesOfFormat(tex->Settings().InternalFormat);
    const VkDeviceSize bytes = (VkDeviceSize) w * (VkDeviceSize) h * (VkDeviceSize) bpp;
    if (bytes == 0)
      return;

    VulkanBuffer::Buffer staging =
        VulkanBuffer::CreateHostVisibleMapped(m_context.get(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, bytes);
    if (staging.handle == VK_NULL_HANDLE)
    {
      TK_ERR("VulkanBackend::UpdateTextureSubRegion - staging buffer alloc failed (%llu bytes)",
             (unsigned long long) bytes);
      return;
    }
    std::memcpy(staging.mapped, data, static_cast<size_t>(bytes));

    const VkImageLayout srcLayout = vt->currentLayout;
    const int32_t ox              = x;
    const int32_t oy              = y;
    const uint32_t ew             = (uint32_t) w;
    const uint32_t eh             = (uint32_t) h;

    m_context->EnqueueGpuWork(
        [staging, vt, srcLayout, ox, oy, ew, eh](VkCommandBuffer cb)
        {
          VkImageMemoryBarrier toTransfer {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
          toTransfer.oldLayout                       = srcLayout;
          toTransfer.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
          toTransfer.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
          toTransfer.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
          toTransfer.image                           = vt->image;
          toTransfer.subresourceRange.aspectMask     = vt->aspect;
          toTransfer.subresourceRange.baseMipLevel   = 0;
          toTransfer.subresourceRange.levelCount     = 1;
          toTransfer.subresourceRange.baseArrayLayer = 0;
          toTransfer.subresourceRange.layerCount     = 1;
          toTransfer.srcAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
          toTransfer.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
          vkCmdPipelineBarrier(cb,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &toTransfer);

          VkBufferImageCopy region {};
          region.imageSubresource.aspectMask     = vt->aspect;
          region.imageSubresource.mipLevel       = 0;
          region.imageSubresource.baseArrayLayer = 0;
          region.imageSubresource.layerCount     = 1;
          region.imageOffset                     = {ox, oy, 0};
          region.imageExtent                     = {ew, eh, 1};
          vkCmdCopyBufferToImage(cb, staging.handle, vt->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

          VkImageMemoryBarrier toRead = toTransfer;
          toRead.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
          toRead.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          toRead.srcAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
          toRead.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
          vkCmdPipelineBarrier(cb,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &toRead);
        },
        [ctx = m_context.get(), staging]() mutable { VulkanBuffer::Destroy(ctx, staging); });

    vt->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }

  void VulkanBackend::PushDebugGroup(StringView name)
  {
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }

    if (m_context->m_vkCmdBeginDebugUtilsLabelEXT != nullptr)
    {
      VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
      VkDebugUtilsLabelEXT label {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
      label.pLabelName = name.data();
      label.color[0]   = 1.0f;
      label.color[1]   = 1.0f;
      label.color[2]   = 1.0f;
      label.color[3]   = 1.0f;
      m_context->m_vkCmdBeginDebugUtilsLabelEXT(cb, &label);
    }
  }

  void VulkanBackend::PopDebugGroup()
  {
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }

    if (m_context->m_vkCmdEndDebugUtilsLabelEXT != nullptr)
    {
      VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
      m_context->m_vkCmdEndDebugUtilsLabelEXT(cb);
    }
  }

  bool VulkanBackend::SupportsFloatTextureLinearFilter()
  {
    // Query the canonical 32-bit float format as a proxy for the rest of the float chain.
    if (m_context == nullptr || m_context->GetPhysicalDevice() == VK_NULL_HANDLE)
    {
      return true;
    }
    VkFormatProperties fp {};
    vkGetPhysicalDeviceFormatProperties(m_context->GetPhysicalDevice(), VK_FORMAT_R32G32B32A32_SFLOAT, &fp);
    return (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
  }

  bool VulkanBackend::IsDepthClampSupported()
  {
    if (m_context == nullptr || m_context->GetPhysicalDevice() == VK_NULL_HANDLE)
    {
      return false;
    }
    VkPhysicalDeviceFeatures supported {};
    vkGetPhysicalDeviceFeatures(m_context->GetPhysicalDevice(), &supported);
    return supported.depthClamp == VK_TRUE;
  }

  void* VulkanBackend::GetNativeTextureHandle(Texture* tex)
  {
    // Returns raw VulkanTexture* so UI layers can pull out sampler/view themselves.
    return tex != nullptr ? tex->m_gpuData.get() : nullptr;
  }

  void VulkanBackend::SetDebugLabel(Texture* tex)
  {
    // TODO
  }

  void VulkanBackend::SetDebugLabel(Framebuffer* fb)
  {
    // TODO
  }

  // Factory function
  IGraphicsBackend* CreateGraphicsBackend() { return new VulkanBackend(); }

} // namespace ToolKit
