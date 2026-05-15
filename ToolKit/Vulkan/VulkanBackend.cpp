/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanBackend.h"

#include "../EngineSettings.h"
#include "../Framebuffer.h"
#include "../Logger.h"
#include "../Mesh.h"
#include "../Texture.h"
#include "../ToolKit.h"
#include "../Types.h"
#include "../UniformBuffer.h"
#include "VulkanBindings.h"
#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanDescriptor.h"
#include "VulkanPipeline.h"
#include "VulkanPipelineCache.h"
#include "VulkanResources.h"
#include "VulkanShader.h"
#include "VulkanSwapchain.h"

#include "../GpuProgram.h"
#include "../Shader.h"
#include "../Util.h"

namespace
{
  // Uploads @p pixels (byteCount bytes) to VulkanTexture @p vt at (layer, mip) via a
  // single-use staging buffer. Transitions the sub-resource from its current layout to
  // TRANSFER_DST, performs the copy, then transitions back to SHADER_READ_ONLY_OPTIMAL.
  // The actual GPU work runs on the swapchain command buffer at the next BeginFrame; the
  // staging buffer's lifetime is extended via DeferDelete so it survives until the GPU has
  // retired the recorded copy.
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

    // Staging buffer — CPU-visible, written once and discarded after the GPU consumes the copy.
    VulkanBuffer::Buffer staging =
        VulkanBuffer::CreateHostVisibleMapped(ctx, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, byteCount);
    if (staging.handle == VK_NULL_HANDLE)
    {
      TK_ERR("UploadTexelData: staging buffer alloc failed (%llu bytes)", (unsigned long long) byteCount);
      return;
    }
    std::memcpy(staging.mapped, pixels, static_cast<size_t>(byteCount));

    const VkImageLayout srcLayout = vt->currentLayout;
    uint32_t w = std::max(1u, vt->extent.width  >> mip);
    uint32_t h = std::max(1u, vt->extent.height >> mip);

    ctx->EnqueueGpuWork(
        [staging, vt, srcLayout, layer, mip, w, h](VkCommandBuffer cb)
        {
          // Transition: currentLayout → TRANSFER_DST_OPTIMAL
          VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
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
                               0, 0, nullptr, 0, nullptr, 1, &toTransfer);

          // Copy staging → image
          VkBufferImageCopy region{};
          region.imageSubresource.aspectMask     = vt->aspect;
          region.imageSubresource.mipLevel       = mip;
          region.imageSubresource.baseArrayLayer = layer;
          region.imageSubresource.layerCount     = 1;
          region.imageExtent                     = {w, h, 1};
          vkCmdCopyBufferToImage(cb, staging.handle, vt->image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

          // Transition: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
          VkImageMemoryBarrier toRead = toTransfer;
          toRead.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
          toRead.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
          toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
          vkCmdPipelineBarrier(cb,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0, 0, nullptr, 0, nullptr, 1, &toRead);
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
      : m_context(std::make_unique<VulkanContext>()),
        m_swapchain(std::make_unique<VulkanSwapchain>()),
        m_pipelineCache(std::make_unique<VulkanPipelineCache>()),
        m_testPipeline(std::make_unique<VulkanTestPipeline>())
  {
    // One bucket per frame-in-flight. Sized once at construction so DeferDelete can run before
    // InitBackend (e.g., during early resource churn) without bounds checks.
    m_pendingDeleters.resize(VulkanSwapchain::FRAMES_IN_FLIGHT);
  }

  VulkanBackend::~VulkanBackend()
  {
    // Block until every queued submission completes, then run every pending deleter while the
    // device is still alive. Do this BEFORE swapchain.reset()/context.reset() so the lambdas can
    // call vkDestroy* / shared_ptr dtors safely.
    if (m_context && m_context->GetDevice() != VK_NULL_HANDLE)
    {
      vkDeviceWaitIdle(m_context->GetDevice());
    }

    // Drop shadow bindings while the device + VMA allocator are still alive. The shadow holds
    // TexturePtr (shared_ptr) refs that are sticky across passes/frames by design (BindPipeline
    // / FinishPass intentionally do not wipe them). At shutdown those refs are the last owners
    // of engine-side Texture / CubeMap objects; if we let them release during the implicit
    // member-destruction phase that runs after m_context.reset(), VulkanTexture::~VulkanTexture
    // would issue vkDestroySampler / vkDestroyImageView on a dead device and vmaDestroyImage
    // on a destroyed allocator (the VMA_ASSERT_LEAK fires here because VMA sees outstanding
    // allocations when its allocator is freed).
    m_shadow.Reset();
    m_globalUboRegistry.clear();
    for (auto& bucket : m_descriptorCache)
    {
      bucket.clear();
    }

    // Explicitly reset dummy textures while context allocator is valid
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
    if (m_testPipeline)
    {
      m_testPipeline->Destroy();
    }
    m_testPipeline.reset();
    // Pipeline cache AFTER the test pipeline â€” the test pipeline no longer owns any cached
    // VkPipeline directly, but it may hold VkShaderModule / VkPipelineLayout that some cache
    // entries reference. Destroying the cache first keeps destruction order clean when Stage 7
    // starts mixing engine shaders + test scaffolds.
    if (m_pipelineCache && m_context && m_context->GetDevice() != VK_NULL_HANDLE)
    {
      m_pipelineCache->Destroy(m_context->GetDevice());
    }
    m_pipelineCache.reset();
    m_swapchain.reset();

    // Final drain: m_pipelineCache->Destroy / m_swapchain.reset may DeferDelete more lambdas
    // (with shared_ptr captures). Drain them before m_context.reset() takes the device down so
    // they can still issue valid vk* calls and release VMA-backed resources.
    DrainAllDeleters();
    m_context.reset();
  }

  void VulkanBackend::DeferDelete(std::function<void()> fn)
  {
    if (!fn || m_pendingDeleters.empty())
    {
      // Backend not yet constructed (impossible â€” vector is sized in ctor) or no-op lambda.
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
    // Move out so a deleter that itself calls DeferDelete (queueing into the same slot) doesn't
    // invalidate iteration. Anything appended during drain lands in the now-empty bucket and
    // will be drained on the next cycle.
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
    m_dummyTexture = std::make_shared<VulkanTexture>();

    VkFormat vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
    m_dummyTexture->context = m_context.get();
    m_dummyTexture->format = vkFormat;
    m_dummyTexture->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    m_dummyTexture->extent = {1, 1};
    m_dummyTexture->arrayLayers = 1;
    m_dummyTexture->mipLevels = 1;
    m_dummyTexture->isCubemap = false;
    m_dummyTexture->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_dummyTexture->samples = VK_SAMPLE_COUNT_1_BIT;

    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = vkFormat;
    ci.extent = {1, 1, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(m_context->GetAllocator(), &ci, &aci, &m_dummyTexture->image, &m_dummyTexture->allocation, nullptr);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = m_dummyTexture->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = vkFormat;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    vkCreateImageView(m_context->GetDevice(), &vci, nullptr, &m_dummyTexture->view);

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 1.0f;
    vkCreateSampler(m_context->GetDevice(), &sci, nullptr, &m_dummyTexture->sampler);

    m_context->EnqueueGpuWork([img = m_dummyTexture->image](VkCommandBuffer cb) {
      VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = img;
      b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      b.subresourceRange.levelCount = 1;
      b.subresourceRange.layerCount = 1;
      b.srcAccessMask = 0;
      b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    });
    m_dummyTexture->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    const uint32_t whitePixels = 0xFFFFFFFF;
    UploadTexelData(m_context.get(), m_dummyTexture.get(), &whitePixels, 4, 0, 0);

    // Cube dummy
    m_dummyCubeTexture = std::make_shared<VulkanTexture>();
    m_dummyCubeTexture->context = m_context.get();
    m_dummyCubeTexture->format = vkFormat;
    m_dummyCubeTexture->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    m_dummyCubeTexture->extent = {1, 1};
    m_dummyCubeTexture->arrayLayers = 6;
    m_dummyCubeTexture->mipLevels = 1;
    m_dummyCubeTexture->isCubemap = true;
    m_dummyCubeTexture->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_dummyCubeTexture->samples = VK_SAMPLE_COUNT_1_BIT;

    ci.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ci.arrayLayers = 6;
    vmaCreateImage(m_context->GetAllocator(), &ci, &aci, &m_dummyCubeTexture->image, &m_dummyCubeTexture->allocation, nullptr);

    vci.image = m_dummyCubeTexture->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    vci.subresourceRange.layerCount = 6;
    vkCreateImageView(m_context->GetDevice(), &vci, nullptr, &m_dummyCubeTexture->view);

    vkCreateSampler(m_context->GetDevice(), &sci, nullptr, &m_dummyCubeTexture->sampler);

    m_context->EnqueueGpuWork([img = m_dummyCubeTexture->image](VkCommandBuffer cb) {
      VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = img;
      b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      b.subresourceRange.levelCount = 1;
      b.subresourceRange.layerCount = 6;
      b.srcAccessMask = 0;
      b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    });
    m_dummyCubeTexture->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    for (int i = 0; i < 6; i++)
    {
      UploadTexelData(m_context.get(), m_dummyCubeTexture.get(), &whitePixels, 4, i, 0);
    }

    // 2D Array dummy
    m_dummy2DArrayTexture = std::make_shared<VulkanTexture>();
    m_dummy2DArrayTexture->context = m_context.get();
    m_dummy2DArrayTexture->format = vkFormat;
    m_dummy2DArrayTexture->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    m_dummy2DArrayTexture->extent = {1, 1};
    m_dummy2DArrayTexture->arrayLayers = 1;
    m_dummy2DArrayTexture->mipLevels = 1;
    m_dummy2DArrayTexture->isCubemap = false;
    m_dummy2DArrayTexture->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_dummy2DArrayTexture->samples = VK_SAMPLE_COUNT_1_BIT;

    ci.flags = 0;
    ci.arrayLayers = 1;
    vmaCreateImage(m_context->GetAllocator(), &ci, &aci, &m_dummy2DArrayTexture->image, &m_dummy2DArrayTexture->allocation, nullptr);

    vci.image = m_dummy2DArrayTexture->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vci.subresourceRange.layerCount = 1;
    vkCreateImageView(m_context->GetDevice(), &vci, nullptr, &m_dummy2DArrayTexture->view);

    vkCreateSampler(m_context->GetDevice(), &sci, nullptr, &m_dummy2DArrayTexture->sampler);

    m_context->EnqueueGpuWork([img = m_dummy2DArrayTexture->image](VkCommandBuffer cb) {
      VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = img;
      b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      b.subresourceRange.levelCount = 1;
      b.subresourceRange.layerCount = 1;
      b.srcAccessMask = 0;
      b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
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
    if (!m_testPipeline->Init(m_context.get()))
    {
      TK_ERR("VulkanBackend: VulkanTestPipeline init failed");
    }

    // Timer query infra. Skip the whole feature if the device can't time graphics work — leave
    // m_cpuTimeMs/m_gpuTimeMs at their default 1.0 so the Stats window doesn't show inf.
    {
      VkPhysicalDeviceProperties props{};
      vkGetPhysicalDeviceProperties(m_context->GetPhysicalDevice(), &props);
      m_timestampPeriodNs = props.limits.timestampPeriod;

      uint32_t qfCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(m_context->GetPhysicalDevice(), &qfCount, nullptr);
      std::vector<VkQueueFamilyProperties> qfProps(qfCount);
      vkGetPhysicalDeviceQueueFamilyProperties(m_context->GetPhysicalDevice(), &qfCount, qfProps.data());
      uint32_t validBits = (m_context->GetGraphicsQueueFamily() < qfCount)
                               ? qfProps[m_context->GetGraphicsQueueFamily()].timestampValidBits
                               : 0;

      if (m_timestampPeriodNs > 0.0f && validBits > 0)
      {
        VkQueryPoolCreateInfo qci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
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

  void VulkanBackend::DrawTestTriangle()
  {
    if (!m_frameStarted || m_activePassFb == nullptr || m_testPipeline == nullptr)
    {
      // Only valid inside an offscreen pass â€” swapchain pass is reserved for ImGui.
      return;
    }
    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
    VkRenderPass rp    = m_activePassFb->renderPass;
    m_testPipeline->Draw(cb, rp, m_pipelineCache.get());
  }

  void VulkanBackend::BeginFrame()
  {
    // Try to recreate the swapchain only when the surface actually has a presentable extent.
    // Calling Recreate while the window is minimized would just fail (extent 0) every tick — we
    // wait until the window has size again, then rebuild. Until then we keep running frames in
    // "no-present" mode so engine state (uploads, offscreen passes, simulation) keeps advancing.
    if (m_needsRecreate)
    {
      VkSurfaceCapabilitiesKHR caps{};
      if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_context->GetPhysicalDevice(),
                                                    m_context->GetSurface(),
                                                    &caps) == VK_SUCCESS &&
          caps.currentExtent.width != 0 && caps.currentExtent.height != 0 &&
          caps.currentExtent.width != UINT32_MAX)
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
      // We have a cb but no swapchain image. Flag for recreate so the next frame retries acquire
      // (which will succeed once the window is restored).
      m_needsRecreate = true;
    }
    // VulkanSwapchain::BeginFrame waited on m_inFlight[currentFrame] just now â†’ every cmd
    // buffer that recorded into this slot last cycle is fully retired on the GPU. Reap that
    // bucket before any new work this frame can touch the same slot.
    //
    // The very first BeginFrame is special: the slot's fence has never gated a real submission,
    // so anything DeferDelete'd during init (loader code, dummy texture churn) is sitting in
    // this slot but has NOT been consumed by any cb yet. Draining it now would destroy
    // resources that FlushPendingGpuWork below is about to record barriers/copies against.
    // Skip the drain on the first frame; those entries stay queued until the slot rolls around
    // again (frame N + FRAMES_IN_FLIGHT), by which point this frame's cb has retired and the
    // standard fence guarantee holds.
    m_deleterSlot = m_swapchain->GetCurrentFrameIndex();
    if (!m_firstFrame)
    {
      DrainDeleterBucket(m_deleterSlot);
    }
    m_firstFrame = false;

    // Same fence guarantee covers descriptor sets allocated last cycle from this slot's pool â€”
    // resetting it releases every set in one call, ready for fresh BindTexture/SubmitPerDrawData
    // allocations during this frame's recording (Stage 7d-4).
    m_context->ResetFrameDescriptorPool(m_deleterSlot);

    // Phase 3: ResetFrameDescriptorPool just released every set in this frame's pool, so the
    // cache entries that point at those sets are now stale. Drop them before any draw can read.
    if (m_deleterSlot < m_descriptorCache.size())
    {
      m_descriptorCache[m_deleterSlot].clear();
    }

    // The per-draw UBO ring is shared across frames; head=0 reset is also fence-safe because the
    // ring's contents are only read from inside cmd buffers that have now retired (Stage 7d-4b).
    m_context->ResetPerDrawUboRing();
    m_currentDynamicOffset = 0;

    // Cross-frame hygiene: drop every shadow binding so stale TexturePtr / UniformBuffer* refs
    // from last frame don't keep destroyed engine resources alive (e.g. viewport resize releases
    // old RTs, but a sticky boundTextures slot would prolong their lifetime until the next
    // SetTexture overwrites the slot). BindPipeline / FinishPass deliberately only reset per-draw
    // state; the full sweep belongs here.
    m_shadow.Reset();

    // Drain any GPU work queued via EnqueueGpuWork while no frame was active (init-time texture
    // uploads, layout transitions, mip generation) into this frame's command buffer. The
    // cleanup callbacks (e.g. staging buffer destroys) ride DeferDelete so they only fire once
    // the recorded GPU work has retired on the GPU.
    VkCommandBuffer cb              = m_swapchain->GetCurrentCommandBuffer();
    std::vector<std::function<void()>> pendingCleanups = m_context->FlushPendingGpuWork(cb);
    for (std::function<void()>& cleanup : pendingCleanups)
    {
      DeferDelete(std::move(cleanup));
    }

    // From this point on EnqueueGpuWork records inline into cb when no render pass is open, or
    // parks into the during-RP queue when one is (vkCmdPipelineBarrier mid-pass without
    // self-dependency is illegal — VUID-vkCmdPipelineBarrier-None-07889). VulkanBackend flushes
    // the during-RP queue immediately after every render pass closes (per-Draw EndRenderPass
    // for offscreen, EndSwapchainPass for the swapchain pass). Cleared in Present.
    m_context->SetCurrentRecordingCb(
        cb,
        [this](std::function<void()> fn) { DeferDelete(std::move(fn)); },
        [this]() { return m_rpActive || (m_swapchain != nullptr && m_swapchain->IsSwapchainPassActive()); });
  }

  void VulkanBackend::EndFrame()
  {
    // Present() handles the end-of-frame submit. Kept as no-op to match IGraphicsBackend contract.
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

    // SubmitPerDrawData is invoked from FeedUniforms which runs between BindPipeline and Draw.
    // Offscreen render passes are opened/closed inside Draw() itself so m_rpActive is normally
    // false at this point; defensively close one if the engine ever reorders that.
    if (m_rpActive)
    {
      vkCmdEndRenderPass(cb);
      m_rpActive = false;
    }
    // If the swapchain pass is currently open we can't safely flush: the swapchain render pass
    // declares VK_ATTACHMENT_LOAD_OP_CLEAR, so closing and reopening it would clear the image
    // and discard everything drawn so far this frame. Bail with a log; the draw that triggered
    // overflow will hit the validation error, but no rendered content is lost. Realistic
    // overflow today comes from EnvironmentComponent::CaptureEnvironment which renders
    // exclusively to offscreen FBs, so this branch should not fire in practice.
    if (m_swapchain->IsSwapchainPassActive())
    {
      TK_ERR("VulkanBackend::FlushAndResetRing: per-draw ring overflowed inside the swapchain "
             "render pass — flush skipped to avoid losing draw content. The triggering draw "
             "will fail descriptor validation; consider reducing per-frame draw count.");
      return;
    }

    if (!m_swapchain->FlushCommandBuffer())
    {
      // FlushCommandBuffer logged the failure; nothing more we can do here.
      return;
    }

    // Every descriptor set in the current frame's pool was just consumed by the submission we
    // waited on, so it's safe to release them all and clear the cache that pointed at them.
    const uint frameIdx = m_swapchain->GetCurrentFrameIndex();
    m_context->ResetFrameDescriptorPool(frameIdx);
    if (frameIdx < m_descriptorCache.size())
    {
      m_descriptorCache[frameIdx].clear();
    }

    // Ring drained; reuse from the start.
    m_context->ResetPerDrawUboRing();
    m_currentDynamicOffset = 0;

    // Re-issue dynamic state on the new cmd buffer. CPU shadow state (bound textures/UBOs/
    // program) stays valid — FlushDescriptorState will allocate fresh sets from it on the next
    // Draw. The next Draw also re-records vkCmdBindPipeline + vkCmdBindDescriptorSets +
    // vkCmdBindVertexBuffers / vkCmdBindIndexBuffer, so those don't need preservation here.
    if (m_cachedViewport.valid)
    {
      VkViewport vp{};
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
      VkRect2D sc{};
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
    // Stop routing EnqueueGpuWork inline before the cb is submitted: any work enqueued past
    // this point (e.g. resource churn between Present and the next BeginFrame) goes back into
    // the pending queue and replays into the next frame's cb.
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

  bool VulkanBackend::BuildOffscreenRenderPass(const PassDesc& desc, VulkanFramebuffer* fbData)
  {
    VkDevice device = m_context->GetDevice();

    // Clearing is done via vkCmdClear* outside the render pass in StartPass.
    // The render pass always uses loadOp=LOAD so multiple Draw() calls within the
    // same pass accumulate rather than each one clearing the attachments.

    // Old RP+FB may still be referenced by the in-flight command buffer of an earlier frame --
    // queue them for deletion N frames out instead of destroying eagerly. The new ones below
    // overwrite the slots after the lambdas have captured the handles.
    if (fbData->renderPass != VK_NULL_HANDLE)
    {
      VkRenderPass oldRp = fbData->renderPass;
      // Evict every pipeline cache entry keyed by this VkRenderPass BEFORE deferring its destroy.
      // The cache key embeds the raw VkRenderPass handle (uintptr_t) and NVIDIA's ICD recycles
      // handle values after destroy -- without this eviction, a freshly-created VkRenderPass that
      // happens to land on the same handle value as a destroyed one will cache-hit a stale
      // VkPipeline whose internal state still references the dead RP, which manifests as a NULL
      // (or tiny-offset, e.g. 0x105) deref inside nvoglv64.dll mid-vkCmdDraw. Pipeline destroy
      // itself is also deferred (same in-flight cb may still reference it).
      if (m_pipelineCache)
      {
        m_pipelineCache->InvalidateForRenderPass(oldRp,
            [this, device](VkPipeline pipe) {
              DeferDelete([device, pipe]() { vkDestroyPipeline(device, pipe, nullptr); });
            });
      }
      DeferDelete([device, oldRp]() { vkDestroyRenderPass(device, oldRp, nullptr); });
      fbData->renderPass = VK_NULL_HANDLE;
    }
    if (fbData->framebuffer != VK_NULL_HANDLE)
    {
      VkFramebuffer oldFb = fbData->framebuffer;
      DeferDelete([device, oldFb]() { vkDestroyFramebuffer(device, oldFb, nullptr); });
      fbData->framebuffer = VK_NULL_HANDLE;
    }

    std::vector<VkAttachmentDescription> atts;
    std::vector<VkAttachmentReference> colorRefs;
    std::vector<VkImageView> views;
    atts.reserve(VulkanFramebuffer::kMaxColorAttachments + 1);
    colorRefs.reserve(VulkanFramebuffer::kMaxColorAttachments);
    views.reserve(VulkanFramebuffer::kMaxColorAttachments + 1);

    // Stage 10. Each attachment carries the sample count it was created with (CreateTexture
    // mirrors TextureSettings::msaaCount onto VulkanTexture::samples). Vulkan requires every
    // attachment in a subpass to share the same sampleCount, so we sanity-check each color
    // attachment against the first one (or the depth attachment when there are no color
    // slots). Mismatched MSAA framebuffers are an engine-side bug â€” log so the caller sees
    // the broken combo and fix the source-side TextureSettings.
    VkSampleCountFlagBits subpassSamples = VK_SAMPLE_COUNT_1_BIT;
    bool subpassSamplesSet               = false;

    auto adoptSamples = [&](VkSampleCountFlagBits s, const char* label)
    {
      if (!subpassSamplesSet)
      {
        subpassSamples     = s;
        subpassSamplesSet  = true;
      }
      else if (subpassSamples != s)
      {
        TK_ERR("BuildOffscreenRenderPass: attachment '%s' sampleCount %u mismatches subpass %u",
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

      VkAttachmentDescription a{};
      a.format         = slot.tex->format;
      a.samples        = slot.tex->samples;
      a.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
      a.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
      a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      // initialLayout: with LOAD we must declare the actual current layout so the previous
      // contents are preserved. With CLEAR/DONT_CARE the contents are tossed → UNDEFINED is the
      // cheapest start. FinishPass parks every attachment at SHADER_READ_ONLY_OPTIMAL.
      // ClearBuffer in StartPass leaves color in SHADER_READ_ONLY_OPTIMAL.
      a.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      // MSAA color attachments aren't sampled directly (engine resolves through
      // ResolveFramebuffer first). The "shader read only" finalLayout is harmless for them
      // either way â€” vkCmdResolveImage transitions the image to TRANSFER_SRC_OPTIMAL itself
      // before reading, and the engine never binds an MSAA target as a sampled texture.
      a.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      VkAttachmentReference ref{};
      ref.attachment = (uint32_t) atts.size();
      ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      colorRefs.push_back(ref);
      atts.push_back(a);
      views.push_back(slot.view);
    }

    VkAttachmentReference depthRef{};
    bool hasDepth = fbData->depthAttachment.view != VK_NULL_HANDLE;
    if (hasDepth)
    {
      adoptSamples(fbData->depthAttachment.tex->samples, "depth");

      VkAttachmentDescription a{};
      a.format         = fbData->depthAttachment.tex->format;
      a.samples        = fbData->depthAttachment.tex->samples;
      a.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
      a.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
      a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
      a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
      // ClearBuffer in StartPass leaves depth-stencil in DEPTH_STENCIL_READ_ONLY_OPTIMAL.
      a.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
      a.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

      depthRef.attachment = (uint32_t) atts.size();
      depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      atts.push_back(a);
      views.push_back(fbData->depthAttachment.view);
    }
    fbData->subpassSamples = subpassSamples;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = (uint32_t) colorRefs.size();
    subpass.pColorAttachments       = colorRefs.empty() ? nullptr : colorRefs.data();
    subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

    // External subpass dependencies. The engine opens a fresh render pass instance per Draw on
    // the same framebuffer (intermediate buffers like bloom chains, m_resolvedFramebuffer, the
    // shadow atlas etc. are written by many such instances back-to-back), and FRAMES_IN_FLIGHT>1
    // pipelines two cb's on the queue. Both srcStageMask and srcAccessMask therefore have to
    // cover *every* prior access type \u2014 read AND write, depth AND color \u2014 so the implicit
    // queue-order memory dependency hands off cleanly to this pass:
    //   - prior FRAGMENT_SHADER + SHADER_READ (sampling in earlier passes)
    //   - prior COLOR_ATTACHMENT_OUTPUT + COLOR_ATTACHMENT_WRITE (write-after-write on the same
    //     image, intra-cb between back-to-back Draw RPs and inter-cb across frames-in-flight)
    //   - prior LATE_FRAGMENT_TESTS + DEPTH_STENCIL_ATTACHMENT_WRITE (depth write-after-write)
    // dep[1] mirrors that with the post-pass scope so subsequent samplers (e.g. ImGui in the
    // swapchain pass, or the next Draw's color/depth read) observe this pass's writes.
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = (uint32_t) atts.size();
    rpci.pAttachments    = atts.empty() ? nullptr : atts.data();
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies   = deps;

    VkRenderPass newRp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &rpci, nullptr, &newRp) != VK_SUCCESS)
    {
      TK_ERR("BuildOffscreenRenderPass: vkCreateRenderPass failed");
      return false;
    }

    VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbci.renderPass      = newRp;
    fbci.attachmentCount = (uint32_t) views.size();
    fbci.pAttachments    = views.empty() ? nullptr : views.data();
    fbci.width           = fbData->width;
    fbci.height          = fbData->height;
    fbci.layers          = 1;

    VkFramebuffer newFb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device, &fbci, nullptr, &newFb) != VK_SUCCESS)
    {
      TK_ERR("BuildOffscreenRenderPass: vkCreateFramebuffer failed");
      vkDestroyRenderPass(device, newRp, nullptr);
      return false;
    }

    fbData->renderPass      = newRp;
    fbData->framebuffer     = newFb;
      // (cachedClearBits removed: render pass no longer depends on clearBits)
    fbData->dirty           = false;
    return true;
  }

  void VulkanBackend::StartPass(const PassDesc& desc)
  {
    if (!m_frameStarted)
    {
      return;
    }

    // HiÃ§bir pass nest edilmez â€” Ã¶nce hangisi aÃ§Ä±ksa onu kapat.
    FinishPass();

    if (desc.target == nullptr)
    {
      // Backbuffer pass — silently no-op when the swapchain isn't presentable (minimize). Caller
      // is free to record nothing this frame; the engine's frame loop keeps spinning so uploads
      // and offscreen work still flow through DeferDelete / FlushPendingGpuWork normally.
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

    if (fbData->dirty
        || fbData->renderPass == VK_NULL_HANDLE
        || fbData->framebuffer == VK_NULL_HANDLE)
    {
      if (!BuildOffscreenRenderPass(desc, fbData))
      {
        return;
      }
    }

    m_pendingPassDesc = desc;
    m_activePassFb    = fbData;

    // Perform the requested clear ONCE here, outside any render pass, via vkCmdClear*Image.
    // The render pass always uses loadOp=LOAD so multiple Draw() calls within the same pass
    // accumulate rather than each one wiping the attachments.
    if (desc.clearBits != GraphicBitFields::None)
    {
      ClearBuffer(desc.clearBits, desc.clearColor);
    }

    if (VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer(); cb != VK_NULL_HANDLE)
    {
      VkViewport vp{};
      vp.x        = 0.0f;
      vp.y        = (float) fbData->height; // negative-height trick: NDC Y+1 → top of framebuffer
      vp.width    = (float) fbData->width;
      vp.height   = -(float) fbData->height;
      vp.minDepth = 0.0f;
      vp.maxDepth = 1.0f;
      vkCmdSetViewport(cb, 0, 1, &vp);

      VkRect2D sc{};
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
    // Pipeline binding is per-pass: a fresh BindPipeline is required after every FinishPass.
    m_pipelineBound = false;
    m_boundProgram  = nullptr;
    m_rpActive      = false;
    // Per-draw state (perDraw UBO + dynamic offset) is consumed at draw time; reset so the next
    // pipeline binding starts clean. Texture / UBO slot bindings are NOT touched: engine code
    // (BloomPass / DoFPass) stages SetTexture *before* the next SetFramebuffer, and StartPass
    // calls FinishPass unconditionally to close any previously-open pass — wiping the slots
    // here used to drop those just-staged bindings and FlushDescriptorState fell back to the
    // dummy texture. BeginFrame issues the full Reset so cross-frame leaks can't accumulate.
    m_currentDynamicOffset    = 0;
    m_shadow.perDrawSubmitted = false;
    m_shadow.perDrawSize      = 0;
    m_shadow.dirty            = true;
    if (m_activePassFb != nullptr)
    {
      // Offscreen RP instances are closed per-Draw; here we only reset pass tracking state.
      m_activePassFb = nullptr;
      return;
    }
    // Swapchain pass was opened in StartPass and must be explicitly closed here.
    m_swapchain->EndSwapchainPass();

    // Same rationale as the offscreen path above: anything EnqueueGpuWork parked while the
    // swapchain pass was open (e.g. ImGui texture cache uploads from the editor) is now safe
    // to record into the cb before the next pass starts.
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
    // Dynamic state â€” every cached pipeline is built with VK_DYNAMIC_STATE_VIEWPORT (see
    // VulkanPipelineCache::GetOrCreate), so this can be called any time during cmd recording.
    // Cache the latest values regardless of frame-active so FlushAndResetRing can restore them
    // onto the freshly begun cmd buffer after a mid-frame flush.
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
    // Negative viewport height flips Y axis so screen-space matches GL conventions — see the
    // matching code in StartPass for the rationale. ToolKit's engine code passes (x,y,w,h) in
    // a top-left origin; after this flip the actual rasterisation matches GL's bottom-left.
    VkViewport vp{};
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
    VkRect2D sc{};
    sc.offset.x      = (int32_t) x;
    sc.offset.y      = (int32_t) y;
    sc.extent.width  = w;
    sc.extent.height = h;
    vkCmdSetScissor(cb, 0, 1, &sc);
  }

  void VulkanBackend::ClearBuffer(GraphicBitFields fields, const Vec4& color)
  {
    // vkCmdClearColorImage / vkCmdClearDepthStencilImage are only legal outside a render pass.
    // In our per-draw RP architecture RPs are only open inside Draw(), so this is always safe.
    if (!m_frameStarted || m_activePassFb == nullptr || m_rpActive)
    {
      return;
    }
    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
    if (cb == VK_NULL_HANDLE)
    {
      return;
    }

    const uint32_t fieldBits = (uint32_t) fields;
    const bool clearColor    = (fieldBits & (uint32_t) GraphicBitFields::ColorBits)   != 0;
    const bool clearDepth    = (fieldBits & (uint32_t) GraphicBitFields::DepthBits)   != 0;
    const bool clearStencil  = (fieldBits & (uint32_t) GraphicBitFields::StencilBits) != 0;

    // ---- Color attachments ------------------------------------------------------------------
    if (clearColor)
    {
      VkClearColorValue cv{};
      cv.float32[0] = color.r;
      cv.float32[1] = color.g;
      cv.float32[2] = color.b;
      cv.float32[3] = color.a;

      for (int i = 0; i < VulkanFramebuffer::kMaxColorAttachments; ++i)
      {
        auto& slot = m_activePassFb->colorAttachments[i];
        if (slot.tex == nullptr || slot.tex->image == VK_NULL_HANDLE)
        {
          continue;
        }

        VkImageSubresourceRange range{};
        range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel   = slot.baseMipLevel;
        range.levelCount     = 1;
        range.baseArrayLayer = slot.baseArrayLayer;
        range.layerCount     = slot.layerCount;

        const bool isUndef = slot.tex->currentLayout == VK_IMAGE_LAYOUT_UNDEFINED;

        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = slot.tex->image;
        b.subresourceRange    = range;
        b.oldLayout           = slot.tex->currentLayout;
        b.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask       = isUndef ? 0 : (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        b.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb,
                             isUndef ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                     : (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT),
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);

        vkCmdClearColorImage(cb, slot.tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &range);

        b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);

        slot.tex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      }
    }

    // ---- Depth/stencil attachment -----------------------------------------------------------
    if ((clearDepth || clearStencil) && m_activePassFb->depthAttachment.tex != nullptr)
    {
      auto& slot = m_activePassFb->depthAttachment;
      if (slot.tex->image == VK_NULL_HANDLE)
      {
        return;
      }

      VkImageAspectFlags aspect = 0;
      if (clearDepth   && (slot.tex->aspect & VK_IMAGE_ASPECT_DEPTH_BIT))   aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
      if (clearStencil && (slot.tex->aspect & VK_IMAGE_ASPECT_STENCIL_BIT)) aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
      if (aspect == 0)
      {
        return;
      }

      VkImageSubresourceRange range{};
      range.aspectMask     = aspect;
      range.baseMipLevel   = 0;
      range.levelCount     = 1;
      range.baseArrayLayer = 0;
      range.layerCount     = slot.tex->arrayLayers;

      const bool isUndef = slot.tex->currentLayout == VK_IMAGE_LAYOUT_UNDEFINED;

      VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image               = slot.tex->image;
      b.subresourceRange    = range;
      b.oldLayout           = slot.tex->currentLayout;
      b.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      b.srcAccessMask       = isUndef ? 0 : (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
      b.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(cb,
                           isUndef ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                   : (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT),
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);

      VkClearDepthStencilValue dsv{1.0f, 0};
      vkCmdClearDepthStencilImage(cb, slot.tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &dsv, 1, &range);

      b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      b.newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
      b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      b.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &b);

      slot.tex->currentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
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
      // CreateGpuProgram never ran or failed for this program.
      m_pipelineBound = false;
      m_boundProgram  = nullptr;
      return;
    }
    // Cache program + state. The actual VkPipeline is built lazily inside Draw() because the
    // pipeline desc requires the vertex layout (Mesh vs SkinMesh), which only arrives with
    // DrawDesc. Caching avoids allocating per-draw and lets a single BindPipeline serve N
    // consecutive draws with different meshes.
    m_boundProgram  = gp;
    m_boundState    = *state;
    m_pipelineBound = true;

    // Per-draw dynamic UBO contract (binding 6 → vulkan binding 38): perDrawSize is consumed by
    // FlushDescriptorState and must be re-submitted before the next Draw. Resetting it here
    // mirrors the GL backend's per-pipeline-bind invariant for the per-draw UBO and lets the
    // FlushDescriptorState perDrawSize==0 check catch a missing SubmitPerDrawData. Texture /
    // UBO bindings are NOT reset: GL keeps texture state sticky across program changes and
    // engine code (BloomPass / DoFPass) stages SetTexture *before* the RenderSubPass that
    // triggers BindPipeline internally. A blanket reset here used to drop those slots silently
    // — FlushDescriptorState then fell back to the dummy texture (BloomPass ping-pong looked
    // black/dummy). BeginFrame / FinishPass still issue a full Reset so cross-pass leaks can't
    // accumulate.
    m_currentDynamicOffset    = 0;
    m_shadow.perDrawSubmitted = false;
    m_shadow.perDrawSize      = 0;
    m_shadow.dirty            = true;
  }

  void VulkanBackend::SubmitPerDrawData(const void* data, size_t size)
  {
    // Stage 7d-4b. Append @p data to the per-frame UBO ring, write a UNIFORM_BUFFER_DYNAMIC
    // descriptor pointing at the ring base, and stash the slot offset for Draw's bind call.
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
      // Ring full. Drain queued GPU work so it's safe to reuse the ring from offset 0, then
      // retry. AllocatePerDrawSlot already logged the overflow once; if the retry still fails
      // the payload itself is bigger than the ring, which is unrecoverable here.
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

    // Phase 2: descriptor write deferred to FlushDescriptorState. Just record offset + size in
    // shadow state so the upcoming Draw can issue a UNIFORM_BUFFER_DYNAMIC write with the right
    // range and bind dynamicOffsets[0] = m_currentDynamicOffset.
    m_currentDynamicOffset    = (uint32_t) offset;
    m_shadow.perDrawSubmitted = true;
    m_shadow.perDrawSize      = (uint64_t) size;
    m_shadow.dirty            = true;
  }

  void VulkanBackend::BindTexture(ubyte slot, TexturePtr tex)
  {
    // Phase 2: only updates the shadow state. Actual descriptor write happens in
    // FlushDescriptorState (Draw), which folds N BindTexture calls into one allocation and
    // reuses a previously cached set when the same handle combination repeats.
    if (slot >= VulkanBindings::kTextureBindingCount)
    {
      TK_ERR("BindTexture: slot %u beyond reserved texture binding range (%u)",
             (unsigned) slot,
             (unsigned) VulkanBindings::kTextureBindingCount);
      return;
    }
    m_shadow.boundTextures[slot] = tex;
    m_shadow.dirty               = true;
  }

  void VulkanBackend::BindUniformBuffer(const String& name, UniformBuffer* ub)
  {
    // Phase 2: shadow-state only. Indexed by slot since the shared descriptor layout is
    // slot-keyed (UBO binding = slot + kUboBindingBase). The name parameter is accepted for
    // future named lookups but unused here; engine code can still rely on the slot path
    // populated by UpdateUniformBuffer / m_globalUboRegistry as a fallback in flush.
    (void) name;
    if (ub == nullptr || ub->m_slot < 0)
    {
      return;
    }
    m_shadow.boundUniforms[ub->m_slot] = ub;
    m_shadow.dirty                     = true;
  }

  // Fills the vertex-input portion of @p out (vertexStride + attributes + attributeCount) for
  // the given ToolKit VertexLayout. Pipeline cache hits depend on these fields being identical
  // for matching layouts, so the offsets are spelled out as constants matching ToolKit's
  // Vertex / SkinVertex struct layouts in Mesh.h.
  static void FillVertexInput(VertexLayout layout, VulkanPipelineDesc& out)
  {
    // Locations match the GL backend's hardcoded glVertexAttribPointer indices:
    //   0 pos(vec3) | 1 norm(vec3) | 2 tex(vec2) | 3 tan(vec4)  [+ SkinMesh: 4 bones(vec4) | 5 weights(vec4)]
    if (layout == VertexLayout::SkinMesh)
    {
      out.vertexStride   = sizeof(SkinVertex);
      out.attributeCount = 6;
      out.attributes[0]  = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0};
      out.attributes[1]  = {1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12};
      out.attributes[2]  = {2, 0, VK_FORMAT_R32G32_SFLOAT,       24};
      out.attributes[3]  = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32};
      out.attributes[4]  = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 48};
      out.attributes[5]  = {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 64};
    }
    else // VertexLayout::Mesh (default) and VertexLayout::None fall back to plain Vertex.
    {
      out.vertexStride   = sizeof(Vertex);
      out.attributeCount = 6;
      out.attributes[0]  = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0};
      out.attributes[1]  = {1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12};
      out.attributes[2]  = {2, 0, VK_FORMAT_R32G32_SFLOAT,       24};
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
    // Gate 1: command buffer must be in recording state. If nothing called BeginFrame yet
    // (engine init, hot-reload, off-frame Draw probes), recording vkCmd* corrupts the buffer
    // and the next vkBeginCommandBuffer fails.
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }
    // Gate 2: a pass must have been configured via StartPass. The render pass instance itself
    // is opened below, just before vkCmdBindPipeline.
    if (m_activePassFb == nullptr && !m_swapchain->IsSwapchainPassActive())
    {
      return;
    }
    // Gate 3: a pipeline must have been bound (BindPipeline cached program + state).
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

    // ---- Lazy pipeline build ----------------------------------------------------------------
    // Assemble the full VulkanPipelineDesc from cached program + state plus per-draw fields
    // (vertex layout). Same RenderState + program + layout + render pass hits the cache; one
    // VkPipeline is shared across every draw with that combination.
    VulkanPipelineDesc pdesc{};
    pdesc.vert = m_boundProgram->vert;
    pdesc.frag = m_boundProgram->frag;
    FillVertexInput(desc.vertexLayout, pdesc);
    RenderStateToPipelineDesc(m_boundState, pdesc);

    // Render pass + color attachment count come from the active pass. Spec requires the
    // pipeline's color blend attachmentCount to match the subpass's color attachment count,
    // including the depth-only case (Stage 9 shadow map pass writes only to a depth attachment
    // and expects pipeline.colorAttachmentCount == 0). The previous "max(count, 1)" fallback
    // forced a phantom blend attachment that validation rejected against a depth-only RP.
    // Sample count (Stage 10) likewise propagates from the FB's adopted subpassSamples so MSAA
    // and non-MSAA copies of the same recipe end up in distinct cache slots.
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
      pdesc.colorAttachmentCount  = colorCount;
      pdesc.rasterizationSamples  = m_activePassFb->subpassSamples;
    }
    else
    {
      pdesc.renderPass            = m_swapchain->GetRenderPass();
      pdesc.colorAttachmentCount  = 1;
      pdesc.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT; // Swapchain images are non-MSAA.
    }

    VkPipeline pipe = m_pipelineCache->GetOrCreate(m_context.get(), m_boundProgram->pipelineLayout, pdesc);
    if (pipe == VK_NULL_HANDLE)
    {
      // Pipeline build failure already logged inside GetOrCreate.
      return;
    }

    // ---- Begin render pass instance (offscreen only) -------------------------------------
    // For offscreen passes the RP instance is opened here and closed after the draw call so
    // begin/end tightly bracket each set of draw commands. The swapchain pass is opened in
    // StartPass (so ImGui can record directly) and closed in FinishPass.
    if (m_activePassFb != nullptr)
    {
      // If SetColorAttachment was called after StartPass (e.g. shadow atlas layer switch),
      // the attachment view changed but the VkFramebuffer still points to the old layer.
      // Rebuild only the VkFramebuffer — the VkRenderPass is format-stable so the pipeline
      // cache key (pdesc.renderPass) stays valid and no pipeline thrash occurs.
      if (m_activePassFb->dirty && m_activePassFb->renderPass != VK_NULL_HANDLE)
      {
        VkDevice device = m_context->GetDevice();

        if (m_activePassFb->framebuffer != VK_NULL_HANDLE)
        {
          VkFramebuffer old = m_activePassFb->framebuffer;
          DeferDelete([device, old]() { vkDestroyFramebuffer(device, old, nullptr); });
          m_activePassFb->framebuffer = VK_NULL_HANDLE;
        }

        std::vector<VkImageView> fbViews;
        fbViews.reserve(VulkanFramebuffer::kMaxColorAttachments + 1);
        for (int i = 0; i < VulkanFramebuffer::kMaxColorAttachments; ++i)
        {
          if (m_activePassFb->colorAttachments[i].view != VK_NULL_HANDLE)
          {
            fbViews.push_back(m_activePassFb->colorAttachments[i].view);
          }
        }
        if (m_activePassFb->depthAttachment.view != VK_NULL_HANDLE)
        {
          fbViews.push_back(m_activePassFb->depthAttachment.view);
        }

        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass      = m_activePassFb->renderPass;
        fbci.attachmentCount = (uint32_t) fbViews.size();
        fbci.pAttachments    = fbViews.data();
        fbci.width           = m_activePassFb->width;
        fbci.height          = m_activePassFb->height;
        fbci.layers          = 1;

        VkFramebuffer newFb = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(device, &fbci, nullptr, &newFb) != VK_SUCCESS)
        {
          TK_ERR("Draw: vkCreateFramebuffer rebuild failed (dirty attachment)");
          return;
        }

        m_activePassFb->framebuffer = newFb;
        m_activePassFb->dirty       = false;
      }

      std::vector<VkClearValue> clears;
      clears.reserve(VulkanFramebuffer::kMaxColorAttachments + 1);
      for (int i = 0; i < VulkanFramebuffer::kMaxColorAttachments; ++i)
      {
        if (m_activePassFb->colorAttachments[i].view != VK_NULL_HANDLE)
        {
          VkClearValue cv{};
          cv.color = {{m_pendingPassDesc.clearColor.r,
                       m_pendingPassDesc.clearColor.g,
                       m_pendingPassDesc.clearColor.b,
                       m_pendingPassDesc.clearColor.a}};
          clears.push_back(cv);
        }
      }
      if (m_activePassFb->depthAttachment.view != VK_NULL_HANDLE)
      {
        VkClearValue cv{};
        cv.depthStencil = {1.0f, 0};
        clears.push_back(cv);
      }

      VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      rpbi.renderPass        = m_activePassFb->renderPass;
      rpbi.framebuffer       = m_activePassFb->framebuffer;
      rpbi.renderArea.offset = {0, 0};
      rpbi.renderArea.extent = {m_activePassFb->width, m_activePassFb->height};
      rpbi.clearValueCount   = (uint32_t) clears.size();
      rpbi.pClearValues      = clears.empty() ? nullptr : clears.data();
      vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

      // Viewport and scissor were set in StartPass (default: full framebuffer) and may have
      // been overridden by SetViewportRect before this Draw (e.g. shadow slot viewport).
      // Do NOT reset them here — that would silently undo per-slot viewports.
      m_rpActive = true;
    }

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

    // ---- Resolve + bind descriptor set (Phase 3) --------------------------------------------
    // FlushDescriptorState reads m_shadow + m_globalUboRegistry against the bound program's
    // declared resources, hashes the active handles, and either reuses a cached set or
    // allocates + writes a fresh one. The dynamic offset still travels via dynamicOffsets[0].
    VkDescriptorSet flushedSet = FlushDescriptorState();
    if (flushedSet == VK_NULL_HANDLE)
    {
      // Descriptor allocation failed (pool exhausted, or no shadow state was bindable). The
      // pipeline is already bound and the RP already opened; issuing vkCmdDraw without a
      // descriptor set 0 leaves the driver dereferencing an unbound binding and crashes
      // (observed as a NULL access violation inside nvoglv64.dll mid-vkCmdDraw). Close the
      // RP cleanly and bail — the draw is dropped, but the cmd buffer remains valid for
      // subsequent passes. Pool exhaustion already logged inside AllocateFrameDescriptorSet.
      if (m_activePassFb != nullptr)
      {
        vkCmdEndRenderPass(cb);
        m_rpActive = false;
      }
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
    const VkBuffer vbuf      = meshGpu->vertex.handle;
    const VkDeviceSize voff  = 0;
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

    // ---- End render pass instance (offscreen only) ----------------------------------------
    if (m_activePassFb != nullptr)
    {
      vkCmdEndRenderPass(cb);
      // Update cached image layouts to match the render pass's final layout declarations.
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
      m_rpActive = false;

      // Drain anything EnqueueGpuWork parked while this RP was open (lazy uploads triggered
      // during FlushDescriptorState etc.). Now that we're past vkCmdEndRenderPass it's legal
      // to record their barriers + copies into the same cb, ahead of the next Draw.
      auto rpCleanups = m_context->FlushDuringRenderPassWork(cb);
      for (std::function<void()>& cleanup : rpCleanups)
      {
        DeferDelete(std::move(cleanup));
      }
    }
  }

  // Wraps the source + destination color attachments in pre + post layout barriers â€” both end
  // up back at SHADER_READ_ONLY_OPTIMAL afterward. The transfer call between the barriers is
  // chosen by the caller (vkCmdBlitImage for size-mismatched non-MSAA copies, vkCmdResolveImage
  // for an MSAA â†’ single-sample resolve). Caller must guarantee no render pass is active on
  // @p cb (both transfer ops are outside-RP-only).
  static void TransitionForColorTransfer(VkCommandBuffer cb, VulkanTexture* srcTex, VulkanTexture* dstTex)
  {
    VkImageMemoryBarrier pre[2]{};
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
    VkImageMemoryBarrier post[2]{};
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

  // Resolves an MSAA color attachment into its single-sample destination via vkCmdResolveImage.
  // Both src/dst extents must match (resolve has no scaling). Caller already checked
  // src->samples > 1.
  static void ResolveColorAttachment(VkCommandBuffer cb, VulkanTexture* srcTex, VulkanTexture* dstTex)
  {
    TransitionForColorTransfer(cb, srcTex, dstTex);

    VkImageResolve region{};
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

  // Blits @p src's full extent into @p dst's full extent (LINEAR filter, COLOR aspect). The
  // textures' currentLayout fields drive the pre-transition source layout and the
  // post-transition restore target â€” both end up back at SHADER_READ_ONLY_OPTIMAL, the
  // engine's resting state for color render targets. Caller must guarantee no render pass is
  // active on @p cb (vkCmdBlitImage is outside-RP-only) and that src is not multi-sampled
  // (vkCmdBlitImage rejects multi-sample sources â€” use ResolveColorAttachment instead).
  static void BlitColorAttachment(VkCommandBuffer cb, VulkanTexture* srcTex, VulkanTexture* dstTex)
  {
    TransitionForColorTransfer(cb, srcTex, dstTex);

    VkImageBlit region{};
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
    // Stage 10. Per requested attachment index: vkCmdResolveImage if source is multi-sampled,
    // vkCmdBlitImage otherwise. Engine pass code (ForwardPass / ForwardPreProcessPass) calls
    // this gated on `framebuffer->IsMultiSampled()`, so the resolve path is the common one;
    // the blit fallback exists only as a safe behavior when an engine site happens to call
    // ResolveFramebuffer on a single-sample source.
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }
    if (m_rpActive)
    {
      // Spec forbids vkCmdBlitImage / vkCmdResolveImage inside a render pass instance. Engine
      // code calls this between passes (after FinishPass) so this guard is purely defensive
      // log so a misuse surfaces during development.
      TK_ERR("ResolveFramebuffer called inside an active render pass â€” skipped");
      return;
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

      using Attachment    = Framebuffer::Attachment;
      Attachment atcEnum  = (Attachment) ((int) Attachment::ColorAttachment0 + idx);

      // Engine-side lazy creation + m_resolvedTexture wiring. Mirrors GLBackend::ResolveFramebuffer:
      // the destination FB is often a freshly-reconstructed bag with no attachments yet (see
      // ForwardSceneRenderPath::PreRender constructing m_resolvedFramebuffer with no attachments);
      // the resolve site is where we materialize the single-sample target RT and link it back to
      // the source via m_resolvedTexture so GetResolvedTexture() (SsaoPass / EditorViewport /
      // PreviewViewport) finds the resolved twin.
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

      // Re-read the slot — SetColorAttachment routes through AttachColorTarget which populates
      // dstFb->colorAttachments[idx].tex with the freshly-created VulkanTexture pointer.
      auto* srcTex = srcFb->colorAttachments[idx].tex;
      auto* dstTex = dstFb->colorAttachments[idx].tex;
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
    // Stage 8. Color-only blit between two framebuffers â€” used by post-process chains and other
    // intermediate-target copies. Each color slot present in both source and destination is
    // blit'd in turn (vkCmdBlitImage handles size mismatch by linear-filtering, matching GL's
    // glBlitFramebuffer semantics).
    //
    // BilinÃ§li ertelemeler:
    //   - dst == nullptr (GL'in "default framebuffer = ekran" yolu): Vulkan'da viewport
    //     texture'Ä± ImGui'ye `ImGui_ImplVulkan_AddTexture` ile veriliyor; "ekrana blit" yolu
    //     Stage 11'de gerÃ§ek engine pass'leri Vulkan Ã¼zerinden baÄŸlandÄ±ÄŸÄ±nda deÄŸerlendirilir.
    //   - DepthBits / StencilBits: vkCmdBlitImage'in depth-aspect blit'i NEAREST + format-eÅŸ
    //     gerektirir, stencil ise vkCmdCopyImage'le kopyalanÄ±r. Mevcut Ã§aÄŸrÄ± yerleri hep
    //     ColorBits geÃ§iyor (GameRenderer, SplashScreenRenderPath, post-process zinciri),
    //     bu yÃ¼zden depth/stencil yollarÄ± Stage 11'e bÄ±rakÄ±ldÄ±.
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }
    if (m_rpActive)
    {
      TK_ERR("CopyFramebuffer called inside an active render pass — skipped");
      return;
    }
    if (src == nullptr)
    {
      return;
    }

    const uint mask = (uint) fields;
    if ((mask & (uint) GraphicBitFields::ColorBits) == 0)
    {
      // Nothing to do for the color path; depth/stencil paths aren't wired yet.
      static bool s_warnedDepthOnly = false;
      if (!s_warnedDepthOnly && (mask & ((uint) GraphicBitFields::DepthBits | (uint) GraphicBitFields::StencilBits)) != 0)
      {
        TK_WRN("CopyFramebuffer: depth/stencil-only copy not implemented yet (Stage 11 follow-up)");
        s_warnedDepthOnly = true;
      }
      return;
    }

    if (dst == nullptr)
    {
      // GL equivalent: glBlitFramebuffer into FBO 0 (the default framebuffer = swapchain).
      // Used by SplashScreenRenderPath::PostRender and GameRenderer to push the final composed
      // image straight to the backbuffer when no ImGui pass runs afterward to do it for us.
      if (!m_swapchain->IsPresentable())
      {
        // No swapchain image acquired this frame (minimize) — nothing to blit to. The engine's
        // upstream work already ran; just skip the final present step.
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

      // Pre-blit barriers. src goes to TRANSFER_SRC; swapchain image goes UNDEFINED→TRANSFER_DST
      // (we discard whatever the previous frame left there — the blit covers the full extent).
      VkImageMemoryBarrier pre[2]{};
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

      VkImageBlit region{};
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

      // Post-blit barriers. src returns to SHADER_READ_ONLY (engine's resting layout for color
      // RTs). Swapchain goes to PRESENT_SRC_KHR so vkQueuePresentKHR doesn't trip the validator.
      VkImageMemoryBarrier post[2]{};
      post[0]                             = pre[0];
      post[0].oldLayout                   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      post[0].newLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      post[0].srcAccessMask               = VK_ACCESS_TRANSFER_READ_BIT;
      post[0].dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;

      post[1]                             = pre[1];
      post[1].oldLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      post[1].newLayout                   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      post[1].srcAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
      post[1].dstAccessMask               = 0;

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
    // Intentional no-op (matches GLBackend::BlitToScreen, which is also empty). The IGraphicsBackend
    // entry exists for legacy GL paths; ToolKit's actual "blit to screen" flow is
    // CopyFramebuffer(src, nullptr, ColorBits), which is the call site engine code already uses.
    // Vulkan currently routes viewport pixels through ImGui's image binding instead â€” no blit
    // needed at this layer.
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
      // GL parity: gate new cycles until the previous result has been consumed.
      return;
    }
    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
    if (cb == VK_NULL_HANDLE)
    {
      return;
    }
    // Reset the pool ahead of the new cycle (must be outside a render pass). RenderPath::PreRender
    // runs before any StartPass for the path, so we're guaranteed to be RP-free here.
    vkCmdResetQueryPool(cb, m_timestampPool, 0, 2);
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

    if (m_timerQueryWaiting)
    {
      // Non-blocking poll. The GPU usually needs at least one frame to finish; until then we keep
      // returning the previous m_gpuTimeMs (default 1.0 on first cycle), matching GL.
      uint64_t results[2] = {0, 0};
      VkResult r          = vkGetQueryPoolResults(m_context->GetDevice(),
                                                  m_timestampPool,
                                                  0,
                                                  2,
                                                  sizeof(results),
                                                  results,
                                                  sizeof(uint64_t),
                                                  VK_QUERY_RESULT_64_BIT);
      if (r == VK_SUCCESS)
      {
        // timestampPeriod is ns/tick. Result delta * period → ns → /1e6 → ms. Clamp to 1ms so
        // the FPS column stays finite even when the GPU reports a near-zero frame.
        double deltaTicks = (double) (results[1] - results[0]);
        double deltaMs    = (deltaTicks * (double) m_timestampPeriodNs) / 1.0e6;
        m_gpuTimeMs       = (float) std::max(1.0, deltaMs);
        m_timerQueryWaiting = false;
      }
    }
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
    case GraphicTypes::UVRepeat:        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case GraphicTypes::UVClampToEdge:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case GraphicTypes::UVClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    default:                            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
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
      TK_ERR("VulkanBackend::CreateTexture called before VulkanContext was initialized â€” "
             "check InitGraphics ordering / VulkanContext::Init failure logs");
      return;
    }

    if (tex->m_width <= 0 || tex->m_height <= 0)
    {
      TK_ERR("VulkanBackend::CreateTexture - invalid dimensions (%d x %d)", tex->m_width, tex->m_height);
      return;
    }

    const TextureSettings& settings = tex->Settings();

    uint32_t arrayLayers = 1;
    bool isCubemap       = false;
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    VkImageCreateFlags imageFlags = 0;

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

    // DepthTexture leaves its TextureSettings::InternalFormat at the Texture default (a color
    // format) â€” its real depth format lives on the subclass via GetDepthFormat() (mirrors what
    // GLBackend does with dt->As<DepthTexture>()). Without this branch we'd allocate a color
    // image and then bind it to a depth attachment slot â†’ vkCreateRenderPass / vkCreateFramebuffer
    // validation errors and an unrenderable framebuffer.
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

    const bool isDepth = IsDepthFormat(vkFormat);
    const bool isStencil = IsStencilFormat(vkFormat);

    auto data          = std::make_shared<VulkanTexture>();
    data->context      = m_context.get();
    data->format       = vkFormat;
    data->aspect       = 0;
    if (isDepth) data->aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (isStencil) data->aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if (!isDepth && !isStencil) data->aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    data->extent       = {(uint32_t) tex->m_width, (uint32_t) tex->m_height};
    data->arrayLayers  = arrayLayers;
    // Allocate the full mip chain when either the texture opted in (GenerateMipMap) or this is a
    // cubemap â€” the IBL prefilter pipeline always writes to every mip of the environment cubemap
    // regardless of the flag, so we must back it with real mip storage. Depth targets don't need
    // mip chains.
    const bool wantsMipChain = !isDepth && settings.GenerateMipMap;
    data->mipLevels    = wantsMipChain ? (uint32_t) tex->CalculateMipmapLevels() : 1u;
    data->isCubemap    = isCubemap;
    data->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Stage 10. MSAA sample count comes from TextureSettings::msaaCount. The enum's integer
    // values (1/2/4/8) are deliberately VK_SAMPLE_COUNT_*_BIT-compatible, so we cast directly.
    // Vulkan spec forbids samples > 1 with mipLevels > 1; the same is forbidden for some
    // image types (cubemaps practically never go MSAA). Demote to 1 when geometry of the
    // image would make MSAA invalid â€” engine rarely asks for those combos but the guard keeps
    // a misconfigured asset from blowing up validation.
    VkSampleCountFlagBits requestedSamples = (VkSampleCountFlagBits) (uint32_t) settings.msaaCount;
    if (requestedSamples == 0)
    {
      requestedSamples = VK_SAMPLE_COUNT_1_BIT;
    }
    if (requestedSamples != VK_SAMPLE_COUNT_1_BIT && (data->mipLevels > 1 || isCubemap))
    {
      TK_WRN("CreateTexture: MSAA sampleCount %u demoted to 1 â€” incompatible with mipLevels=%u "
             "or cubemap target",
             (unsigned) requestedSamples,
             (unsigned) data->mipLevels);
      requestedSamples = VK_SAMPLE_COUNT_1_BIT;
    }

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    usage |= isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // Per-format MSAA support varies. Some formats (FormatRGBA32F on many GPUs, depth+stencil
    // combos on tilers, etc.) advertise only VK_SAMPLE_COUNT_1_BIT for the OPTIMAL tiling /
    // usage / flags combination we're requesting. Asking for an unsupported sample count makes
    // vmaCreateImage fail with VK_ERROR_FORMAT_NOT_SUPPORTED; instead query the supported
    // sampleCounts mask now and demote to the largest supported bit that is <= requested.
    if (requestedSamples != VK_SAMPLE_COUNT_1_BIT)
    {
      VkImageFormatProperties props{};
      VkResult fpRes = vkGetPhysicalDeviceImageFormatProperties(m_context->GetPhysicalDevice(),
                                                                vkFormat,
                                                                VK_IMAGE_TYPE_2D,
                                                                VK_IMAGE_TILING_OPTIMAL,
                                                                usage,
                                                                imageFlags,
                                                                &props);
      if (fpRes != VK_SUCCESS)
      {
        TK_WRN("CreateTexture: vkGetPhysicalDeviceImageFormatProperties failed (%d) for format %d "
               "â€” demoting MSAA to 1",
               (int) fpRes,
               (int) vkFormat);
        requestedSamples = VK_SAMPLE_COUNT_1_BIT;
      }
      else if ((props.sampleCounts & requestedSamples) == 0)
      {
        VkSampleCountFlagBits demoted = VK_SAMPLE_COUNT_1_BIT;
        for (VkSampleCountFlagBits cand : {VK_SAMPLE_COUNT_8_BIT,
                                           VK_SAMPLE_COUNT_4_BIT,
                                           VK_SAMPLE_COUNT_2_BIT,
                                           VK_SAMPLE_COUNT_1_BIT})
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
    data->samples       = requestedSamples;

    VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.flags             = imageFlags;
    imageInfo.imageType         = VK_IMAGE_TYPE_2D;
    imageInfo.format            = vkFormat;
    imageInfo.extent            = {data->extent.width, data->extent.height, 1};
    imageInfo.mipLevels         = data->mipLevels;
    imageInfo.arrayLayers       = data->arrayLayers;
    imageInfo.samples           = data->samples;
    imageInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage             = usage;
    imageInfo.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(m_context->GetAllocator(),
                       &imageInfo,
                       &allocInfo,
                       &data->image,
                       &data->allocation,
                       nullptr) != VK_SUCCESS)
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

    // Default sampler â€” color targets are sampled by ImGui / future post passes.
    // Depth targets get sampler lazily via ApplyTextureSettings when needed.
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
      // Expose the full mip chain to sampling. Values above the actual mip count clamp safely.
      samplerInfo.maxLod              = (float) data->mipLevels;
      vkCreateSampler(m_context->GetDevice(), &samplerInfo, nullptr, &data->sampler);
    }

    // Transition the freshly-created image out of UNDEFINED to a sensible sampling-ready layout.
    // Without this, any code path that samples the texture before a render pass has written to it
    // (ImGui showing a default/black target, thumbnail previews, the backing RT of an uninitialized
    // viewport, etc.) hits a layout-mismatch validation error. Render passes are free to perform
    // their own transitions from this state onward.
    // Upload pixel data if the texture was loaded from CPU memory.
    // has data → UNDEFINED→SHADER_READ_ONLY then SHADER_READ_ONLY→TRANSFER_DST→copy→SHADER_READ_ONLY
    // no data  → UNDEFINED→SHADER_READ_ONLY  (render target / will be filled by a render pass)
    tex->m_gpuData = data;  // set before helpers so they can access context via VulkanTexture

    const bool hasData2D      = !isDepth && (tex->m_image != nullptr || tex->m_imagef != nullptr);
    CubeMap* cubeMapTex       = tex->As<CubeMap>();
    const bool hasCubemapData = cubeMapTex != nullptr
                                && cubeMapTex->m_images.size() == 6
                                && cubeMapTex->m_images[0] != nullptr;

    // All paths start with an UNDEFINED→SHADER_READ_ONLY barrier for the whole image so that
    // UploadTexelData can safely transition individual sub-resources from SHADER_READ_ONLY.
    {
      const VkImageLayout targetLayout = isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                 : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      m_context->EnqueueGpuWork(
          [img        = data->image,
           aspect     = data->aspect,
           mipLevels  = data->mipLevels,
           layerCount = data->arrayLayers,
           targetLayout](VkCommandBuffer cb)
          {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
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
                                 0, 0, nullptr, 0, nullptr, 1, &b);
          });
      data->currentLayout = targetLayout;
    }

    if (hasData2D)
    {
      const void* pixels      = tex->m_imagef ? (const void*) tex->m_imagef
                                              : (const void*) tex->m_image;
      const int bpp           = BytesOfFormat(tex->Settings().InternalFormat);
      const VkDeviceSize bytes = (VkDeviceSize) tex->m_width * tex->m_height * bpp;
      UploadTexelData(m_context.get(), data.get(), pixels, bytes, 0, 0);
    }
    else if (hasCubemapData)
    {
      // CubeMap::Load always produces RGBA8 (4 bytes/pixel) via stb_image.
      const VkDeviceSize faceBytes = (VkDeviceSize) tex->m_width * tex->m_height * 4;
      for (int face = 0; face < 6; ++face)
      {
        UploadTexelData(m_context.get(), data.get(),
                        cubeMapTex->m_images[face], faceBytes, (uint32_t) face, 0);
      }
    }
  }

  void VulkanBackend::DestroyTexture(Texture* tex)
  {
    if (tex == nullptr)
    {
      return;
    }
    // Hand the gpu data to the deletion queue: the lambda holds the only remaining shared_ptr
    // ref, so the VulkanTexture dtor (which calls vkDestroyImage/View/Sampler) only fires once
    // the deleter bucket is drained â€” i.e., after the GPU has finished any cmd buffer that may
    // still reference these handles. Editor-side ImGui descriptor cache observes the same
    // shared_ptr via weak_ptr and sweeps expired entries on the next frame.
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

    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter    = ToVkFilter(s.MagFilter);
    info.minFilter    = ToVkFilter(s.MinFilter);
    info.mipmapMode   = ToVkMipmapMode(s.MinFilter);
    info.addressModeU = ToVkAddressMode(s.WarpS);
    info.addressModeV = ToVkAddressMode(s.WarpT);
    info.addressModeW = ToVkAddressMode(s.WarpR);
    info.minLod       = 0.0f;
    info.maxLod       = (float) vt->mipLevels;

    // Anisotropy: only meaningful for sampled color 2D textures. Depth samplers and special
    // 1D-style targets (e.g., bone transform texture) skip it. Engine setting clamped to the
    // physical device's maxSamplerAnisotropy limit.
    if (!isDepth && s.Target == GraphicTypes::Target2D)
    {
      EngineSettings& engSettings = GetEngineSettings();
      int anisoVal = engSettings.m_graphics->GetAnisotropicTextureFilteringVal().GetValue<int>();
      if (anisoVal > 1)
      {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_context->GetPhysicalDevice(), &props);
        float maxAniso       = props.limits.maxSamplerAnisotropy;
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

    // DeferDelete the previous sampler — in-flight cmd buffers may still reference it via
    // their bound descriptor sets. ImGui's (view, sampler) descriptor cache observes the change
    // through GetNativeTextureHandle's shared_ptr indirection on the next frame.
    if (vt->sampler != VK_NULL_HANDLE)
    {
      VkDevice device = m_context->GetDevice();
      VkSampler old   = vt->sampler;
      DeferDelete([device, old]() { vkDestroySampler(device, old, nullptr); });
    }
    vt->sampler = newSampler;

    // Note: SwizzleAlphaToOne would require recreating VkImageView with VK_COMPONENT_SWIZZLE_ONE
    // on the alpha channel — handled by the dedicated SetTextureSwizzleAlpha() entry point.
  }

  void VulkanBackend::SetTextureSwizzleAlpha(Texture* tex, bool swizzleToOne, bool setLastBindBack)
  {
    // TODO change the view of the texture to swizzle alpha to 1.0 if swizzleToOne is true, or to the original alpha
    // channel if false.
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
          for (uint32_t mip = 1; mip < mipLevels; ++mip)
          {
            // Transition mip-1 (all layers): SHADER_READ_ONLY → TRANSFER_SRC
            VkImageMemoryBarrier toSrc{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toSrc.oldLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toSrc.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toSrc.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            toSrc.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            toSrc.image                           = img;
            toSrc.subresourceRange.aspectMask     = aspect;
            toSrc.subresourceRange.baseMipLevel   = mip - 1;
            toSrc.subresourceRange.levelCount     = 1;
            toSrc.subresourceRange.layerCount     = arrayLayers;
            toSrc.srcAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
            toSrc.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSrc);

            // Transition mip (all layers): SHADER_READ_ONLY → TRANSFER_DST
            VkImageMemoryBarrier toDst = toSrc;
            toDst.oldLayout             = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toDst.newLayout             = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toDst.subresourceRange.baseMipLevel = mip;
            toDst.srcAccessMask         = VK_ACCESS_SHADER_READ_BIT;
            toDst.dstAccessMask         = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

            // Blit mip-1 → mip for all layers
            int32_t srcW = std::max(1, (int32_t)(extent.width  >> (mip - 1)));
            int32_t srcH = std::max(1, (int32_t)(extent.height >> (mip - 1)));
            int32_t dstW = std::max(1, (int32_t)(extent.width  >> mip));
            int32_t dstH = std::max(1, (int32_t)(extent.height >> mip));

            VkImageBlit blit{};
            blit.srcSubresource.aspectMask     = aspect;
            blit.srcSubresource.mipLevel       = mip - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount     = arrayLayers;
            blit.srcOffsets[1]                 = {srcW, srcH, 1};
            blit.dstSubresource               = blit.srcSubresource;
            blit.dstSubresource.mipLevel       = mip;
            blit.dstOffsets[1]                 = {dstW, dstH, 1};
            vkCmdBlitImage(cb,
                           img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, VK_FILTER_LINEAR);

            // Transition mip-1 back: TRANSFER_SRC → SHADER_READ_ONLY
            VkImageMemoryBarrier backToRead = toSrc;
            backToRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            backToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            backToRead.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            backToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &backToRead);

            // Transition current mip: TRANSFER_DST → SHADER_READ_ONLY (always, not just last).
            // This keeps every mip in SHADER_READ_ONLY so the next iteration's
            // SHADER_READ_ONLY→TRANSFER_SRC barrier always sees the correct old layout.
            VkImageMemoryBarrier dstToRead = toDst;
            dstToRead.oldLayout    = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dstToRead.newLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            dstToRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &dstToRead);
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
    // TODO: Recreate VkImageView with limited mip range, or no-op if handled at creation.
  }

  void VulkanBackend::AllocateCubemapMipStorage(Texture* tex)
  {
    // TODO: Vulkan allocates all mips at image creation time likely no-op.
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

    // 1. Kaynak (Source) İmajı Transfer'e Geçir
    VkImageMemoryBarrier srcBarrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    srcBarrier.oldLayout           = vSrc->currentLayout; // Render'dan çıktığı için genelde SHADER_READ_ONLY olur
    srcBarrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.image               = vSrc->image;
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

    // 3. Kopyalama İşlemi
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

    // 4. Kaynağı Eski Haline (Veya Okunabilir Hale) Geri Döndür
    srcBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    srcBarrier.newLayout     = vSrc->currentLayout; // Veya VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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

    // 5. Hedefi (Küp Yüzeyini) Shader'da Okunacak Hale Getir
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

    // Re-upload path: drop any existing GPU data first (matches GLBackend behavior).
    DestroyMesh(mesh);

    const void* vertexData    = mesh->GetClientVertexData();
    const size_t vertexCount  = mesh->GetClientVertexCount();
    const int vertexStride    = mesh->GetVertexSize();

    if (vertexData == nullptr || vertexCount == 0 || vertexStride <= 0)
    {
      // Empty mesh â€” nothing to upload. Leave m_gpuData null; Draw() guards against this.
      mesh->m_vertexCount = 0;
      mesh->m_indexCount  = 0;
      return;
    }

    auto data     = std::make_shared<VulkanMesh>();
    data->context = m_context.get();

    const VkDeviceSize vertexBytes = (VkDeviceSize) vertexStride * (VkDeviceSize) vertexCount;
    data->vertex                   = VulkanBuffer::UploadDeviceLocal(m_context.get(),
                                                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                                     vertexData,
                                                                     vertexBytes);
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
        TK_ERR("VulkanBackend::CreateMesh: index upload failed (%llu bytes)",
               (unsigned long long) indexBytes);
        // Vertex buffer already alive â€” drop the half-built mesh; shared_ptr dtor cleans up.
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
    // Defer the shared_ptr release: the in-flight cmd buffer may still reference these vertex /
    // index buffers. Once the frame slot's fence retires, DrainDeleterBucket runs the lambda and
    // ~VulkanMesh frees the VMA allocations.
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
      // ToolKit allows constructing a UniformBuffer with size 0 and binding it later via
      // GpuBufferBase::Init(); mirror that by creating no GPU resource yet. The next Init
      // call comes back here with the real size.
      return;
    }

    auto data     = std::make_shared<VulkanUniformBuffer>();
    data->context = m_context.get();
    data->buffer  = VulkanBuffer::CreateHostVisibleMapped(m_context.get(),
                                                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                          (VkDeviceSize) size);
    if (data->buffer.handle == VK_NULL_HANDLE)
    {
      TK_ERR("VulkanBackend::CreateUniformBuffer: failed to allocate %llu byte UBO",
             (unsigned long long) size);
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
    // Defer the shared_ptr release so the underlying VkBuffer + VMA allocation outlive any
    // in-flight command buffer that might still reference this UBO via a descriptor set.
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
    // HOST_COHERENT memory â€” write becomes visible to the device on the next vkQueueSubmit
    // without an explicit vkFlushMappedMemoryRanges call.
    // Check if we are actively recording a frame.
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (m_swapchain && m_swapchain->IsFrameActive())
    {
      cb = m_swapchain->GetCurrentCommandBuffer();
    }

    // vkCmdUpdateBuffer and vkCmdPipelineBarrier (without self-dependency) are illegal inside
    // a render pass instance. If any pass is currently recording, fall through to the memcpy
    // path — HOST_COHERENT mapped memory is visible to the GPU on the next queue submission
    // without any explicit flush, so the data is guaranteed to arrive before the draw.
    const bool insideRenderPass = m_rpActive || (m_swapchain && m_swapchain->IsSwapchainPassActive());

    if (cb != VK_NULL_HANDLE && !insideRenderPass)
    {
      // Synchronize the UBO update to the draw calls currently in flight in this command buffer.
      // Barrier 1: Ensure previous shader reads of this UBO finish before the update overwrites it.
      VkMemoryBarrier preBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
      preBarrier.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
      preBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &preBarrier, 0, nullptr, 0, nullptr);

      // Perform the update inline in the command buffer. This handles multiple updates to the same UBO in one frame gracefully.
      vkCmdUpdateBuffer(cb, gpu->buffer.handle, 0, size, data);

      // Barrier 2: Ensure the transfer finishes before any subsequent draw call reads from this UBO.
      VkMemoryBarrier postBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
      postBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      postBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &postBarrier, 0, nullptr, 0, nullptr);

      // Also keep the mapped memory up to date just in case it is read back or used when frame is inactive.
      std::memcpy(gpu->buffer.mapped, data, (size_t) size);
    }
    else
    {
      // Not recording: just write directly to the mapped host memory.
      std::memcpy(gpu->buffer.mapped, data, (size_t) size);
    }

    // Register non-perDraw UBOs for global descriptor write.
    // Slot 6 = per-draw dynamic UBO (ring buffer path); all other slots are static global UBOs
    // (Camera, GraphicConsts, DirectionalLight, etc.) that must be written into every new set.
    const int slot = ub->m_slot;
    if (slot >= 0 && slot != 6)
    {
      m_globalUboRegistry[slot] = {gpu->buffer.handle, gpu->buffer.size};
    }
  }

  // Phase 3: data-driven descriptor flush. Walks the bound program's declared resources,
  // resolves each to a native handle from m_shadow / m_globalUboRegistry, hashes the result,
  // and either reuses a cached set or allocates + writes a fresh one.
  //
  // Caching policy: the cache is per-frame-in-flight. BeginFrame resets the entire descriptor
  // pool for that slot (releasing every set), so we also clear the cache there. Within a frame
  // identical (program, handle-set) tuples reuse the same VkDescriptorSet — multiple draws of
  // the same material no longer trigger fresh allocations.
  VkDescriptorSet VulkanBackend::FlushDescriptorState()
  {
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return VK_NULL_HANDLE;
    }
    if (m_boundProgram == nullptr || m_boundProgram->pipelineLayout == VK_NULL_HANDLE)
    {
      return VK_NULL_HANDLE;
    }

    // ---- 1. Resolve declared resources to native handles ------------------------------------
    struct Resolved
    {
      uint32_t binding             = 0;
      ShaderResource::Type type    = ShaderResource::Type::Texture;
      VkImageView view             = VK_NULL_HANDLE;
      VkSampler sampler            = VK_NULL_HANDLE;
      VkBuffer buffer              = VK_NULL_HANDLE;
      VkDeviceSize bufferSize      = 0;
      bool isPerDrawDynamic        = false;
    };
    std::vector<Resolved> resolved;
    resolved.reserve(m_boundProgram->resources.size());

    for (const ShaderResource& res : m_boundProgram->resources)
    {
      Resolved r{};
      r.type = res.type;

      if (res.type == ShaderResource::Type::Texture)
      {
        if (res.slot < 0 || res.slot >= (int) VulkanBindings::kTextureBindingCount)
        {
          continue;
        }
        r.binding = VulkanBindings::kTextureBindingBase + (uint32_t) res.slot;

        const TexturePtr& tex = m_shadow.boundTextures[res.slot];
        if (tex && tex->m_gpuData)
        {
          auto* vt = static_cast<VulkanTexture*>(tex->m_gpuData.get());
          if (vt != nullptr && vt->view != VK_NULL_HANDLE && vt->sampler != VK_NULL_HANDLE)
          {
            r.view    = vt->view;
            r.sampler = vt->sampler;
          }
        }
        // Fallback: declared-but-unbound texture slots get the dummy so a shader access into
        // a slot the engine forgot to bind doesn't trip validation.
        if (r.view == VK_NULL_HANDLE)
        {
          bool isCube    = (res.viewType == ShaderResource::ViewType::TexCube);
          bool is2DArray = (res.viewType == ShaderResource::ViewType::Tex2DArray);

          if (isCube && m_dummyCubeTexture && m_dummyCubeTexture->view != VK_NULL_HANDLE)
          {
            r.view    = m_dummyCubeTexture->view;
            r.sampler = m_dummyCubeTexture->sampler;
          }
          else if (is2DArray && m_dummy2DArrayTexture && m_dummy2DArrayTexture->view != VK_NULL_HANDLE)
          {
            r.view    = m_dummy2DArrayTexture->view;
            r.sampler = m_dummy2DArrayTexture->sampler;
          }
          else if (m_dummyTexture && m_dummyTexture->view != VK_NULL_HANDLE)
          {
            r.view    = m_dummyTexture->view;
            r.sampler = m_dummyTexture->sampler;
          }
        }
        if (r.view == VK_NULL_HANDLE)
        {
          continue; // No way to satisfy this binding; skip rather than write garbage.
        }
      }
      else // UniformBuffer
      {
        if (res.slot == 6) // per-draw dynamic UBO
        {
          r.binding             = VulkanBindings::kPerDrawUboBinding;
          r.isPerDrawDynamic    = true;
          r.buffer              = m_context->GetPerDrawUboBuffer();
          r.bufferSize          = m_shadow.perDrawSize;
          if (r.buffer == VK_NULL_HANDLE || r.bufferSize == 0)
          {
            // Contract enforcement: a program that declares the per-draw UBO (GL slot 6)
            // MUST receive a SubmitPerDrawData call between BindPipeline and Draw. Reaching
            // here means a Draw landed without one — the freshly allocated set would leave
            // binding 38 unwritten and the shader would read an unbound dynamic UBO. The
            // NVIDIA Vulkan ICD has been observed to NULL-deref inside vkCmdDraw in exactly
            // this scenario (Access violation @ 0x0 in nvoglv64.dll). Bail with a diagnostic
            // so the offending pass surfaces in the log instead of producing the crash.
            TK_ERR("FlushDescriptorState: program %p declares per-draw UBO (binding %u) but "
                   "no SubmitPerDrawData was issued for this draw (perDrawSize=0). "
                   "Dropping draw — fix the engine path to call SubmitPerDrawData first.",
                   (void*) m_boundProgram,
                   (unsigned) VulkanBindings::kPerDrawUboBinding);
            return VK_NULL_HANDLE;
          }
        }
        else
        {
          if (res.slot < 0)
          {
            continue;
          }
          r.binding = VulkanBindings::UboBindingFor((uint32_t) res.slot);

          // BindUniformBuffer override wins over the global registry.
          auto it = m_shadow.boundUniforms.find(res.slot);
          if (it != m_shadow.boundUniforms.end() && it->second != nullptr && it->second->m_gpuData != nullptr)
          {
            auto* gpu = static_cast<VulkanUniformBuffer*>(it->second->m_gpuData.get());
            if (gpu != nullptr)
            {
              r.buffer     = gpu->buffer.handle;
              r.bufferSize = gpu->buffer.size;
            }
          }
          if (r.buffer == VK_NULL_HANDLE)
          {
            auto git = m_globalUboRegistry.find(res.slot);
            if (git != m_globalUboRegistry.end())
            {
              r.buffer     = git->second.handle;
              r.bufferSize = git->second.size;
            }
          }
          if (r.buffer == VK_NULL_HANDLE)
          {
            continue; // No backing buffer; skip the binding.
          }
        }
      }
      resolved.push_back(r);
    }

    // ---- 2. Hash native handles -------------------------------------------------------------
    // Mix in the bound program pointer too so unrelated programs that happen to share handles
    // get distinct cache entries; the resource declaration order is stable per program so the
    // walk order alone is enough to disambiguate.
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

    // ---- 3. Cache lookup --------------------------------------------------------------------
    const uint frame = m_swapchain->GetCurrentFrameIndex();
    if (frame >= m_descriptorCache.size())
    {
      return VK_NULL_HANDLE; // shouldn't happen with FRAMES_IN_FLIGHT = 2.
    }
    auto& cache = m_descriptorCache[frame];
    for (const DescriptorCacheEntry& e : cache)
    {
      if (e.hash == hash)
      {
        return e.set;
      }
    }

    // ---- 4. Allocate + write a fresh set ----------------------------------------------------
    VkDescriptorSet set = m_context->AllocateFrameDescriptorSet(frame, m_context->GetGlobalDescriptorSetLayout());
    if (set == VK_NULL_HANDLE)
    {
      return VK_NULL_HANDLE; // pool exhaustion already logged.
    }

    for (const Resolved& r : resolved)
    {
      if (r.type == ShaderResource::Type::Texture)
      {
        VulkanDescriptor::WriteCombinedImageSampler(m_context->GetDevice(), set, r.binding, r.view, r.sampler);
      }
      else if (r.isPerDrawDynamic)
      {
        VulkanDescriptor::WriteUniformBufferDynamic(
            m_context->GetDevice(), set, r.binding, r.buffer, 0, r.bufferSize);
      }
      else
      {
        VulkanDescriptor::WriteUniformBuffer(
            m_context->GetDevice(), set, r.binding, r.buffer, 0, r.bufferSize);
      }
    }

    cache.push_back({hash, set});
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
      // Include shaders are textually inlined by the engine before reaching here in the GL path;
      // no compilation target. Return null as GLBackend does.
      TK_ERR("Include shader can't be compiled: %s", shader->GetFile().c_str());
      return nullptr;
    }

    const bool isVertex             = (shader->m_shaderType == ShaderType::VertexShader);
    const VulkanShader::Stage stage = isVertex ? VulkanShader::Stage::Vertex : VulkanShader::Stage::Fragment;

    // Phase C (Step 5): real compile path.
    // Engine GLSL is now Vulkan-clean: bare uniforms removed, TK_UBO_BINDING / TK_SAMPLER_BINDING
    // macros inject binding qualifiers when VULKAN is defined (vulkanCompatInc.shader).
    // CompileGlslToSpirv applies SetForcedVersionProfile(450,core) + VULKAN macro +
    // SetAutoMapLocations â€” source is passed as-is, no runtime string manipulation.
    std::vector<uint32_t> spirv = VulkanShader::CompileGlslToSpirv(stage, source, shader->GetFile());
    if (spirv.empty())
    {
      TK_WRN("CreateShader: compile failed for '%s' â€” shader will produce no output",
             shader->GetFile().c_str());
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
    // GpuResourceData* arrives as a raw pointer here (matching GLBackend's signature). The
    // owning shared_ptr is held by the Shader instance â€” we cannot defer-delete via a captured
    // shared_ptr because we don't have one. Eager destroy is acceptable: by the time the engine
    // calls DestroyShader, the program(s) referencing the module have already been torn down
    // (vkDeviceWaitIdle on shutdown, or explicit DestroyGpuProgram in hot-reload paths).
    auto* sm = static_cast<VulkanShaderModule*>(shaderData);
    if (sm == nullptr || sm->context == nullptr || sm->module == VK_NULL_HANDLE)
    {
      return;
    }
    vkDestroyShaderModule(sm->context->GetDevice(), sm->module, nullptr);
    sm->module = VK_NULL_HANDLE;
  }

  void VulkanBackend::CreateGpuProgram(GpuProgram* program, GlobalGpuBuffers* buffers)
  {
    assert(program != nullptr && "CreateGpuProgram: null program");
    assert(program->m_gpuData == nullptr && "CreateGpuProgram: program already has gpu data");

    if (program->m_shaders.size() < 2)
    {
      TK_ERR("CreateGpuProgram: program needs at least vertex+fragment shaders");
      return;
    }

    auto* vertSm = static_cast<VulkanShaderModule*>(program->m_shaders[0]->m_gpuData.get());
    auto* fragSm = static_cast<VulkanShaderModule*>(program->m_shaders[1]->m_gpuData.get());
    if (vertSm == nullptr || fragSm == nullptr || vertSm->module == VK_NULL_HANDLE ||
        fragSm->module == VK_NULL_HANDLE)
    {
      TK_ERR("CreateGpuProgram: missing compiled shader module(s)");
      return;
    }

    auto data       = std::make_shared<VulkanGpuProgram>();
    data->context   = m_context.get();
    data->vert      = vertSm->module;
    data->frag      = fragSm->module;
    data->resources = program->m_resources;

    // Stage 7d-3: every program references the context's shared kitchen-sink descriptor set
    // layout. Programs whose shaders touch only a subset of bindings still work â€” unused entries
    // are simply not written to. Push constants stay at zero (per-draw data routes through the
    // dynamic UBO at VulkanBindings::kPerDrawUboBinding instead).
    VkDescriptorSetLayout globalSet = m_context->GetGlobalDescriptorSetLayout();
    if (globalSet == VK_NULL_HANDLE)
    {
      TK_ERR("CreateGpuProgram: global descriptor set layout missing (context not initialized?)");
      return;
    }

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
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
    // Defer the shared_ptr release: in-flight cmd buffers may have bound a VkPipeline built off
    // this program's layout. ~VulkanGpuProgram destroys the layout once the deleter runs.
    auto data          = program->m_gpuData;
    program->m_gpuData = nullptr;

    // Evict every cached pipeline that was built using this program's pipelineLayout BEFORE the
    // dtor destroys the layout. ~VulkanGpuProgram (line VulkanResources.cpp:179) calls
    // vkDestroyPipelineLayout unconditionally; any cached pipeline still referencing that layout
    // becomes a spec violation (Vulkan: VkPipeline must be destroyed before its VkPipelineLayout)
    // and the next vkCmdDraw that picks up the orphaned pipeline NULL-derefs inside nvoglv64.dll
    // at a small struct offset (0x104/0x105). Pipeline destroys go through DeferDelete so they
    // share the same deleter bucket as the program shared_ptr release; bucket drains in push
    // order, so pipelines retire before the layout dtor fires.
    auto* progData = static_cast<VulkanGpuProgram*>(data.get());
    if (progData != nullptr && progData->pipelineLayout != VK_NULL_HANDLE && m_pipelineCache)
    {
      VkDevice device = m_context->GetDevice();
      m_pipelineCache->InvalidateForPipelineLayout(progData->pipelineLayout,
          [this, device](VkPipeline pipe) {
            DeferDelete([device, pipe]() { vkDestroyPipeline(device, pipe, nullptr); });
          });
    }

    DeferDelete([data]() mutable { data.reset(); });
  }

  int VulkanBackend::GetUniformLocation(GpuProgram* program, const char* name)
  {
    // Vulkan doesn't have uniform locations  push constants / descriptors handle this.
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
    // Same pattern as DestroyTexture: defer the shared_ptr release so the VulkanFramebuffer
    // dtor (which destroys the cached VkRenderPass + VkFramebuffer + any owned attachment views)
    // runs after the in-flight cmd buffer has retired. Attached texture pointers are non-owning;
    // their lifetime is governed by their owning RenderTarget/DepthTexture.
    if (auto data = fb->m_gpuData)
    {
      // Evict pipeline cache entries keyed by this framebuffer's VkRenderPass before letting
      // the dtor destroy it -- same handle-reuse hazard as BuildOffscreenRenderPass. Pipelines
      // go through DeferDelete so they retire on the in-flight cb before the RP itself
      // (within the same deleter bucket, dispatched in push order).
      auto* fbData = static_cast<VulkanFramebuffer*>(data.get());
      if (fbData != nullptr && fbData->renderPass != VK_NULL_HANDLE && m_pipelineCache)
      {
        VkDevice device = m_context->GetDevice();
        m_pipelineCache->InvalidateForRenderPass(fbData->renderPass,
            [this, device](VkPipeline pipe) {
              DeferDelete([device, pipe]() { vkDestroyPipeline(device, pipe, nullptr); });
            });
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

    auto& slot = fbData->colorAttachments[attachment];

    // Defer the previously owned view â€” the in-flight cmd buffer's RP+FB still references it
    // until the next BuildOffscreenRenderPass swaps them out (which itself defers the old RP+FB).
    if (slot.ownsView && slot.view != VK_NULL_HANDLE)
    {
      VkDevice device  = m_context->GetDevice();
      VkImageView old  = slot.view;
      DeferDelete([device, old]() { vkDestroyImageView(device, old, nullptr); });
    }
    slot      = {};
    slot.tex  = static_cast<VulkanTexture*>(rt->m_gpuData.get());

    const bool needsSubresourceView =
        slot.tex != nullptr && (face >= 0 || layer >= 0 || (mip > 0 && slot.tex->mipLevels > 1));

    if (slot.tex == nullptr)
    {
      // Attach with a null texture â€” caller error; leave slot cleared.
    }
    else if (needsSubresourceView)
    {
      uint32_t baseArrayLayer = 0;
      uint32_t layerCount     = 1;
      if (face >= 0)
      {
        // Cubemap face as 2D render target.
        baseArrayLayer = (uint32_t) face;
        layerCount     = 1;
      }
      else if (layer >= 0)
      {
        baseArrayLayer = (uint32_t) layer;
        layerCount     = 1;
      }

      VkImageViewCreateInfo viewInfo       = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      viewInfo.image                       = slot.tex->image;
      viewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format                      = slot.tex->format;
      viewInfo.subresourceRange.aspectMask = slot.tex->aspect;
      uint32_t baseMip = (uint32_t) std::max(0, mip);
      if (baseMip >= slot.tex->mipLevels)
      {
        TK_WRN("AttachColorTarget: mip %u >= image mipLevels %u â€” clamping to %u",
               baseMip,
               slot.tex->mipLevels,
               slot.tex->mipLevels - 1);
        baseMip = slot.tex->mipLevels - 1;
      }
      viewInfo.subresourceRange.baseMipLevel   = baseMip;
      viewInfo.subresourceRange.levelCount     = 1;
      viewInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
      viewInfo.subresourceRange.layerCount     = layerCount;
      if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &slot.view) != VK_SUCCESS)
      {
        TK_ERR("AttachColorTarget: vkCreateImageView failed for face/layer/mip view");
        slot.view = VK_NULL_HANDLE;
      }
      else
      {
        slot.ownsView        = true;
        slot.baseArrayLayer  = baseArrayLayer;
        slot.layerCount      = layerCount;
        slot.baseMipLevel    = baseMip;
      }
    }
    else
    {
      slot.view           = slot.tex->view;
      slot.ownsView       = false;
      slot.baseArrayLayer = 0;
      slot.layerCount     = slot.tex->arrayLayers;
      slot.baseMipLevel   = 0;
    }

    // Don't eager-destroy the cached RP+FB â€” BuildOffscreenRenderPass will defer them on the
    // next StartPass. Eager destroy here would invalidate the in-flight cmd buffer.
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
      VkDevice device  = m_context->GetDevice();
      VkImageView old  = slot.view;
      DeferDelete([device, old]() { vkDestroyImageView(device, old, nullptr); });
    }
    slot = {};

    fbData->dirty = true;
  }

  void VulkanBackend::AttachDepthTarget(Framebuffer* fb, DepthTexturePtr dt)
  {
    auto* fbData = static_cast<VulkanFramebuffer*>(fb->m_gpuData.get());
    assert(fbData && "AttachDepthTarget: framebuffer has no gpu data");

    auto& slot = fbData->depthAttachment;
    if (slot.ownsView && slot.view != VK_NULL_HANDLE)
    {
      VkDevice device  = m_context->GetDevice();
      VkImageView old  = slot.view;
      DeferDelete([device, old]() { vkDestroyImageView(device, old, nullptr); });
    }
    slot      = {};
    slot.tex  = static_cast<VulkanTexture*>(dt->m_gpuData.get());
    slot.view = slot.tex ? slot.tex->view : VK_NULL_HANDLE;
    // Depth attachments currently always use the texture's primary view (no face/layer selection).

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
      VkDevice device  = m_context->GetDevice();
      VkImageView old  = slot.view;
      DeferDelete([device, old]() { vkDestroyImageView(device, old, nullptr); });
    }
    slot = {};

    fbData->dirty = true;
  }

  void VulkanBackend::SetUniform4f(int location, const Vec4& value)
  {
    // No-op. Vulkan has no glUniform-style "location" addressing; the GL backend uses this for
    // ad-hoc per-program uniforms but the Vulkan path will route equivalent data through the
    // per-material UBO once descriptor sets land. GetUniformLocation already returns -1 here,
    // so well-behaved engine code shouldn't reach this with a real location anyway.
    (void) location;
    (void) value;
  }

  String VulkanBackend::GetBackendRendererString()
  {
    if (m_context == nullptr || m_context->GetPhysicalDevice() == VK_NULL_HANDLE)
    {
      return "Vulkan";
    }
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_context->GetPhysicalDevice(), &props);
    return String("Vulkan: ") + props.deviceName;
  }

  int VulkanBackend::GetMaxArrayTextureLayers()
  {
    if (m_context == nullptr || m_context->GetPhysicalDevice() == VK_NULL_HANDLE)
    {
      return 256;
    }
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_context->GetPhysicalDevice(), &props);
    return (int) props.limits.maxImageArrayLayers;
  }

  void VulkanBackend::SetSrgbAutoEncoding(bool enable)
  {
    // Vulkan handles sRGB via swapchain format  likely no-op.
    (void) enable;
  }

  void VulkanBackend::Finish()
  {
    // GL's glFinish equivalent â€” flush + wait until every queued GPU op is retired. Engine
    // calls this at shutdown / context teardown / certain readback paths. vkDeviceWaitIdle
    // covers all queues this device owns, which matches the GL semantics on a single-context
    // setup.
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
    // Unlike GL — where the SRGB-capable bit is a request the driver may silently ignore, forcing
    // us to clear+readback to verify — on Vulkan we picked the swapchain format ourselves in
    // VulkanSwapchain::PickSurfaceFormat. The "validation" is just reporting whether that pick
    // resolved to an *_SRGB format (HW does the encode automatically) vs a UNORM one (we must
    // gamma-encode in-shader / signal ImGui to do so).
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
    // Vulkan: scissor is always enabled as dynamic state. Disable = set scissor to full viewport.
  }

  void VulkanBackend::ReadPixels(int x, int y, int w, int h, GraphicTypes format, GraphicTypes type, void* data)
  {
    // TODO: vkCmdCopyImageToBuffer + map staging buffer.
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
    const int32_t  ox = x;
    const int32_t  oy = y;
    const uint32_t ew = (uint32_t) w;
    const uint32_t eh = (uint32_t) h;

    m_context->EnqueueGpuWork(
        [staging, vt, srcLayout, ox, oy, ew, eh](VkCommandBuffer cb)
        {
          VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
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
                               0, 0, nullptr, 0, nullptr, 1, &toTransfer);

          VkBufferImageCopy region{};
          region.imageSubresource.aspectMask     = vt->aspect;
          region.imageSubresource.mipLevel       = 0;
          region.imageSubresource.baseArrayLayer = 0;
          region.imageSubresource.layerCount     = 1;
          region.imageOffset                     = {ox, oy, 0};
          region.imageExtent                     = {ew, eh, 1};
          vkCmdCopyBufferToImage(cb, staging.handle, vt->image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

          VkImageMemoryBarrier toRead = toTransfer;
          toRead.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
          toRead.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
          toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
          vkCmdPipelineBarrier(cb,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0, 0, nullptr, 0, nullptr, 1, &toRead);
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
      VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
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
    // Engine uses this to decide between linear-sampled HDR targets and a NEAREST fallback for
    // older GPUs. On Vulkan we query format properties for the canonical 32-bit float color
    // format; if linear filter is missing on that, the rest of the float chain is unlikely to
    // support it either.
    if (m_context == nullptr || m_context->GetPhysicalDevice() == VK_NULL_HANDLE)
    {
      return true;
    }
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(m_context->GetPhysicalDevice(),
                                        VK_FORMAT_R32G32B32A32_SFLOAT,
                                        &fp);
    return (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
  }

  bool VulkanBackend::IsDepthClampSupported()
  {
    // VulkanContext::CreateLogicalDevice only enables depthClamp when the device advertises it
    // (see the supported/features split there), so querying the physical device here gives the
    // same answer as "did we actually turn the feature on?".
    if (m_context == nullptr || m_context->GetPhysicalDevice() == VK_NULL_HANDLE)
    {
      return false;
    }
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(m_context->GetPhysicalDevice(), &supported);
    return supported.depthClamp == VK_TRUE;
  }

  void* VulkanBackend::GetNativeTextureHandle(Texture* tex)
  {
    // Return the raw VulkanTexture* so UI layers (editor, etc.) can pull out sampler/view and
    // register them with their own texture systems (ImGui descriptor cache, debug viewers, ...).
    // Keeping the backend UI-framework agnostic means zero ImGui / SDL includes inside ToolKit.
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
