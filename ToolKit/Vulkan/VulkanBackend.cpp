/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanBackend.h"

#include "../Framebuffer.h"
#include "../Logger.h"
#include "../Mesh.h"
#include "../Texture.h"
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

namespace
{
  // Uploads @p pixels (byteCount bytes) to VulkanTexture @p vt at (layer, mip) via a
  // single-use staging buffer. Transitions the sub-resource from its current layout to
  // TRANSFER_DST, performs the copy, then transitions back to SHADER_READ_ONLY_OPTIMAL.
  // Blocks until the GPU copy completes (SubmitOneShot waits for queue idle).
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

    // Staging buffer — CPU-visible, written once and discarded after copy.
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

    ctx->SubmitOneShot(
        [&](VkCommandBuffer cb)
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
        });

    vt->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VulkanBuffer::Destroy(ctx, staging);
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
    DrainAllDeleters();
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
    if (!m_testPipeline->Init(m_context.get()))
    {
      TK_ERR("VulkanBackend: VulkanTestPipeline init failed");
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
    if (m_needsRecreate)
    {
      m_swapchain->Recreate();
      m_needsRecreate = false;
    }
    m_frameStarted = m_swapchain->BeginFrame();
    if (!m_frameStarted)
    {
      // Swapchain out-of-date or minimized â€” flag for recreate and skip.
      m_needsRecreate = true;
      return;
    }
    // VulkanSwapchain::BeginFrame waited on m_inFlight[currentFrame] just now â†’ every cmd
    // buffer that recorded into this slot last cycle is fully retired on the GPU. Reap that
    // bucket before any new work this frame can touch the same slot.
    m_deleterSlot = m_swapchain->GetCurrentFrameIndex();
    DrainDeleterBucket(m_deleterSlot);

    // Same fence guarantee covers descriptor sets allocated last cycle from this slot's pool â€”
    // resetting it releases every set in one call, ready for fresh BindTexture/SubmitPerDrawData
    // allocations during this frame's recording (Stage 7d-4).
    m_context->ResetFrameDescriptorPool(m_deleterSlot);
    m_currentDescriptorSet = VK_NULL_HANDLE;

    // The per-draw UBO ring is shared across frames; head=0 reset is also fence-safe because the
    // ring's contents are only read from inside cmd buffers that have now retired (Stage 7d-4b).
    m_context->ResetPerDrawUboRing();
    m_currentDynamicOffset = 0;
  }

  void VulkanBackend::EndFrame()
  {
    // Present() handles the end-of-frame submit. Kept as no-op to match IGraphicsBackend contract.
  }

  void VulkanBackend::Present()
  {
    if (!m_frameStarted)
    {
      return;
    }
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
    (void) desc;
    VkDevice device = m_context->GetDevice();

    // Old RP+FB may still be referenced by the in-flight command buffer of an earlier frame --
    // queue them for deletion N frames out instead of destroying eagerly. The new ones below
    // overwrite the slots after the lambdas have captured the handles.
    if (fbData->renderPass != VK_NULL_HANDLE)
    {
      VkRenderPass oldRp = fbData->renderPass;
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
      a.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
      a.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
      a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      a.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
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
      a.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
      a.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
      a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      a.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
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

    // Basit dependency: external read \u2192 attachment write, attachment write \u2192 external sample.
    // ImGui sonraki frame'de bu RT'yi sample edecek.
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

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

    fbData->renderPass  = newRp;
    fbData->framebuffer = newFb;
    fbData->dirty       = false;
    return true;
  }

  void VulkanBackend::BeginPass(const PassDesc& desc)
  {
    if (!m_frameStarted)
    {
      return;
    }

    // HiÃ§bir pass nest edilmez â€” Ã¶nce hangisi aÃ§Ä±ksa onu kapat.
    EndPass();

    if (desc.target == nullptr)
    {
      // Backbuffer pass â€” drive the swapchain's render pass with the caller's clear color.
      m_swapchain->BeginSwapchainPass(desc.clearColor);
      return;
    }

    auto* fbData = static_cast<VulkanFramebuffer*>(desc.target->m_gpuData.get());
    if (fbData == nullptr)
    {
      TK_ERR("BeginPass: target framebuffer has no gpu data");
      return;
    }

    if (fbData->dirty || fbData->renderPass == VK_NULL_HANDLE || fbData->framebuffer == VK_NULL_HANDLE)
    {
      if (!BuildOffscreenRenderPass(desc, fbData))
      {
        return;
      }
    }

    // Clear value array â€” color attachments sÄ±rasÄ±nda, sonra (varsa) depth.
    std::vector<VkClearValue> clears;
    clears.reserve(VulkanFramebuffer::kMaxColorAttachments + 1);
    for (int i = 0; i < VulkanFramebuffer::kMaxColorAttachments; ++i)
    {
      if (fbData->colorAttachments[i].view != VK_NULL_HANDLE)
      {
        VkClearValue cv{};
        cv.color = {{desc.clearColor.r, desc.clearColor.g, desc.clearColor.b, desc.clearColor.a}};
        clears.push_back(cv);
      }
    }
    if (fbData->depthAttachment.view != VK_NULL_HANDLE)
    {
      VkClearValue cv{};
      cv.depthStencil = {1.0f, 0};
      clears.push_back(cv);
    }

    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();

    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass        = fbData->renderPass;
    rpbi.framebuffer       = fbData->framebuffer;
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = {fbData->width, fbData->height};
    rpbi.clearValueCount   = (uint32_t) clears.size();
    rpbi.pClearValues      = clears.empty() ? nullptr : clears.data();
    vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = (float) fbData->width;
    vp.height   = (float) fbData->height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &vp);

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = {fbData->width, fbData->height};
    vkCmdSetScissor(cb, 0, 1, &sc);

    m_activePassFb = fbData;
  }

  void VulkanBackend::EndPass()
  {
    if (!m_frameStarted)
    {
      return;
    }
    // Pipeline binding is per-pass: a fresh BindPipeline is required after every EndPass.
    m_pipelineBound = false;
    m_boundProgram  = nullptr;
    if (m_activePassFb != nullptr)
    {
      VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
      vkCmdEndRenderPass(cb);
      // RP final layout'larÄ±nÄ± cache'ledikleri texture'lara yansÄ±t.
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
      m_activePassFb = nullptr;
      return;
    }
    m_swapchain->EndSwapchainPass();
  }

  void VulkanBackend::SetViewport(uint x, uint y, uint w, uint h)
  {
    // Dynamic state â€” every cached pipeline is built with VK_DYNAMIC_STATE_VIEWPORT (see
    // VulkanPipelineCache::GetOrCreate), so this can be called any time during cmd recording.
    // Bail when no frame is active to avoid recording into a non-recording cmd buffer.
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }
    VkCommandBuffer cb = m_swapchain->GetCurrentCommandBuffer();
    if (cb == VK_NULL_HANDLE)
    {
      return;
    }
    // Vulkan's framebuffer Y axis matches GL's after a flip; ToolKit's viewport coords are
    // already in the "Y goes down" convention (top-left origin), so we map x/y directly.
    VkViewport vp{};
    vp.x        = (float) x;
    vp.y        = (float) y;
    vp.width    = (float) w;
    vp.height   = (float) h;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &vp);
  }

  void VulkanBackend::SetScissor(uint x, uint y, uint w, uint h)
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
    VkRect2D sc{};
    sc.offset.x      = (int32_t) x;
    sc.offset.y      = (int32_t) y;
    sc.extent.width  = w;
    sc.extent.height = h;
    vkCmdSetScissor(cb, 0, 1, &sc);
  }

  void VulkanBackend::ClearBuffer(GraphicBitFields fields, const Vec4& color)
  {
    // Engine pass code clears via PassDesc::clearBits + clearColor at BeginPass time, which is
    // mapped to VkRenderPass loadOp=CLEAR â€” the GPU clear happens implicitly at pass start. A
    // mid-pass ClearBuffer is rare; if a pass actually needs it, vkCmdClearAttachments inside
    // the active render pass is the right call. Until a concrete pass needs that path we keep
    // this a no-op so accidental engine-side calls don't generate validation noise.
    // TODO(stage 11): wire vkCmdClearAttachments when an engine pass demands mid-pass clear.
    (void) fields;
    (void) color;
  }

  void VulkanBackend::ClearColorBuffer(const Vec4& color)
  {
    // See ClearBuffer note. ColorOnly variant maps to the same eventual vkCmdClearAttachments
    // call with the color aspect, again deferred until needed.
    (void) color;
  }

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

    // Drop any in-progress descriptor set; the next BindTexture / SubmitPerDrawData kicks off
    // a fresh allocation. Keeping a stale set across BindPipeline boundaries is unsafe \u2014 we
    // can't tell whether the new pipeline expects different bindings.
    m_currentDescriptorSet = VK_NULL_HANDLE;
    m_currentDynamicOffset = 0;
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
      // Ring full â€” already logged once. Skip this draw's per-draw payload; shader ends up
      // reading the previous slot's contents which is wrong, but no crash. A bigger ring fixes.
      return;
    }
    std::memcpy(mapped, data, size);

    if (m_currentDescriptorSet == VK_NULL_HANDLE)
    {
      const uint frame = m_swapchain->GetCurrentFrameIndex();
      m_currentDescriptorSet =
          m_context->AllocateFrameDescriptorSet(frame, m_context->GetGlobalDescriptorSetLayout());
      if (m_currentDescriptorSet == VK_NULL_HANDLE)
      {
        return; // pool exhaustion already logged.
      }
      WriteGlobalUbosToSet(m_currentDescriptorSet);
    }

    // Static descriptor write points at the ring base
    // dynamic offset on bind. Range = size (the actual payload) so the shader sees only its
    // own block via std140 access; the ring's leftover bytes are out-of-range on read.
    VulkanDescriptor::WriteUniformBufferDynamic(
        m_context->GetDevice(),
        m_currentDescriptorSet,
        VulkanBindings::kPerDrawUboBinding,
        m_context->GetPerDrawUboBuffer(),
        0,
        size);

    m_currentDynamicOffset = (uint32_t) offset;
  }

  void VulkanBackend::BindTexture(ubyte slot, TexturePtr tex)
  {
    // Stage 7d-4. Writes a COMBINED_IMAGE_SAMPLER into the per-draw descriptor set at
    // binding=slot (texture slots are NOT shaderc-remapped, so GL slot index = Vulkan binding).
    // Multiple BindTexture calls between BindPipeline / Draw fold into the same set since we
    // only allocate when m_currentDescriptorSet is null \u2014 the actual bind happens later in Draw.
    if (m_swapchain == nullptr || !m_swapchain->IsFrameActive())
    {
      return;
    }
    if (m_boundProgram == nullptr || m_boundProgram->pipelineLayout == VK_NULL_HANDLE)
    {
      // No pipeline bound \u2192 nowhere to attach the descriptor. Engine pass code that calls
      // BindTexture before BindPipeline (uncommon but possible) silently drops the binding.
      return;
    }
    if (slot >= VulkanBindings::kTextureBindingCount)
    {
      TK_ERR("BindTexture: slot %u beyond reserved texture binding range (%u)",
             (unsigned) slot,
             (unsigned) VulkanBindings::kTextureBindingCount);
      return;
    }
    if (tex == nullptr)
    {
      // Engine occasionally clears a slot by binding nullptr; we model that as "no write" \u2014
      // descriptor set keeps whatever previous binding it had (or nothing). A real "unbind"
      // pattern can be added if engine code starts depending on it.
      return;
    }
    auto* vt = static_cast<VulkanTexture*>(tex->m_gpuData.get());
    if (vt == nullptr || vt->view == VK_NULL_HANDLE || vt->sampler == VK_NULL_HANDLE)
    {
      return;
    }

    if (m_currentDescriptorSet == VK_NULL_HANDLE)
    {
      const uint frame = m_swapchain->GetCurrentFrameIndex();
      m_currentDescriptorSet =
          m_context->AllocateFrameDescriptorSet(frame, m_context->GetGlobalDescriptorSetLayout());
      if (m_currentDescriptorSet == VK_NULL_HANDLE)
      {
        return; // pool exhaustion already logged.
      }
      WriteGlobalUbosToSet(m_currentDescriptorSet);
    }

    VulkanDescriptor::WriteCombinedImageSampler(
        m_context->GetDevice(),
        m_currentDescriptorSet,
        VulkanBindings::kTextureBindingBase + (uint) slot,
        vt->view,
        vt->sampler);
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
      out.attributeCount = 4;
      out.attributes[0]  = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0};
      out.attributes[1]  = {1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12};
      out.attributes[2]  = {2, 0, VK_FORMAT_R32G32_SFLOAT,       24};
      out.attributes[3]  = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32};
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
    // Gate 2: a render pass instance must be active. vkCmdDrawIndexed outside a render pass
    // is a validation error.
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
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

    // ---- Bind descriptor set (Stage 7d-4) ---------------------------------------------------
    // The global layout includes one UNIFORM_BUFFER_DYNAMIC binding (kPerDrawUboBinding), so
    // every vkCmdBindDescriptorSets call that touches this layout MUST supply exactly one
    // dynamic offset. If SubmitPerDrawData ran this draw cycle, m_currentDynamicOffset points
    // at the freshly-written ring slot; otherwise it stays 0 (shaders that don't read the
    // per-draw UBO are unaffected; ones that do would see stale data \u2014 acceptable until real
    // engine shaders land).
    if (m_currentDescriptorSet != VK_NULL_HANDLE)
    {
      const uint32_t dyn = m_currentDynamicOffset;
      vkCmdBindDescriptorSets(cb,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_boundProgram->pipelineLayout,
                              0,
                              1,
                              &m_currentDescriptorSet,
                              1,
                              &dyn);
      m_currentDescriptorSet = VK_NULL_HANDLE;
    }

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
    if (m_activePassFb != nullptr || m_swapchain->IsSwapchainPassActive())
    {
      // Spec forbids vkCmdBlitImage / vkCmdResolveImage inside a render pass instance. Engine
      // code calls this between passes (after EndPass) so this guard is purely defensive â€”
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
    if (m_activePassFb != nullptr || m_swapchain->IsSwapchainPassActive())
    {
      TK_ERR("CopyFramebuffer called inside an active render pass â€” skipped");
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
      // See note above â€” Vulkan path doesn't need this until Stage 11. Logged once to keep an
      // unexpected runtime call visible without spamming.
      static bool s_warnedNullDst = false;
      if (!s_warnedNullDst)
      {
        TK_WRN("CopyFramebuffer(dst=nullptr) deferred to Stage 11 â€” call ignored");
        s_warnedNullDst = true;
      }
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
    // TODO: vkCmdWriteTimestamp.
  }

  void VulkanBackend::EndTimerQuery()
  {
    // TODO: vkCmdWriteTimestamp + read back.
  }

  void VulkanBackend::GetElapsedTime(float& cpu, float& gpu)
  {
    cpu = 0.0f;
    gpu = 0.0f;
    // TODO: Read timestamp query results.
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

    auto data          = std::make_shared<VulkanTexture>();
    data->context      = m_context.get();
    data->format       = vkFormat;
    data->aspect       = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    data->extent       = {(uint32_t) tex->m_width, (uint32_t) tex->m_height};
    data->arrayLayers  = arrayLayers;
    // Allocate the full mip chain when either the texture opted in (GenerateMipMap) or this is a
    // cubemap â€” the IBL prefilter pipeline always writes to every mip of the environment cubemap
    // regardless of the flag, so we must back it with real mip storage. Depth targets don't need
    // mip chains.
    const bool wantsMipChain = !isDepth && (settings.GenerateMipMap || isCubemap);
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
    data->samples       = requestedSamples;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    usage |= isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

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
      samplerInfo.addressModeU        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      samplerInfo.addressModeV        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      samplerInfo.addressModeW        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
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
      m_context->SubmitOneShot(
          [&](VkCommandBuffer cb)
          {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout                   = targetLayout;
            b.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            b.image                       = data->image;
            b.subresourceRange.aspectMask = data->aspect;
            b.subresourceRange.levelCount = data->mipLevels;
            b.subresourceRange.layerCount = data->arrayLayers;
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
    // Sampler is created once in CreateTexture with sensible defaults for Stage 1f.
    // Re-creating on every ApplyTextureSettings call is wasteful and breaks the cached ImGui
    // descriptor. Stage 7 will add proper sampler-cache keyed on TextureSettings.
    (void) tex;
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

    m_context->SubmitOneShot(
        [&](VkCommandBuffer cb)
        {
          for (uint32_t mip = 1; mip < vt->mipLevels; ++mip)
          {
            // Transition mip-1 (all layers): SHADER_READ_ONLY → TRANSFER_SRC
            VkImageMemoryBarrier toSrc{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toSrc.oldLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toSrc.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toSrc.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            toSrc.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            toSrc.image                           = vt->image;
            toSrc.subresourceRange.aspectMask     = vt->aspect;
            toSrc.subresourceRange.baseMipLevel   = mip - 1;
            toSrc.subresourceRange.levelCount     = 1;
            toSrc.subresourceRange.layerCount     = vt->arrayLayers;
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
            int32_t srcW = std::max(1, (int32_t)(vt->extent.width  >> (mip - 1)));
            int32_t srcH = std::max(1, (int32_t)(vt->extent.height >> (mip - 1)));
            int32_t dstW = std::max(1, (int32_t)(vt->extent.width  >> mip));
            int32_t dstH = std::max(1, (int32_t)(vt->extent.height >> mip));

            VkImageBlit blit{};
            blit.srcSubresource.aspectMask     = vt->aspect;
            blit.srcSubresource.mipLevel       = mip - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount     = vt->arrayLayers;
            blit.srcOffsets[1]                 = {srcW, srcH, 1};
            blit.dstSubresource               = blit.srcSubresource;
            blit.dstSubresource.mipLevel       = mip;
            blit.dstOffsets[1]                 = {dstW, dstH, 1};
            vkCmdBlitImage(cb,
                           vt->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           vt->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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
    if (cubemap == nullptr || readFb == nullptr)
      return;
    auto* vDst = static_cast<VulkanTexture*>(cubemap->m_gpuData.get());
    if (vDst == nullptr || vDst->image == VK_NULL_HANDLE)
      return;

    // Source is color attachment 0 of readFb — a 2D render target, layer 0.
    auto* vFb = static_cast<VulkanFramebuffer*>(readFb->m_gpuData.get());
    if (vFb == nullptr || vFb->colorAttachments[0].tex == nullptr)
      return;
    VulkanTexture* vSrc = vFb->colorAttachments[0].tex;
    if (vSrc->image == VK_NULL_HANDLE)
      return;

    m_context->SubmitOneShot(
        [&](VkCommandBuffer cb)
        {
          // Transition source: SHADER_READ_ONLY → TRANSFER_SRC
          VkImageMemoryBarrier srcBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
          srcBarrier.oldLayout                       = vSrc->currentLayout;
          srcBarrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
          srcBarrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
          srcBarrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
          srcBarrier.image                           = vSrc->image;
          srcBarrier.subresourceRange.aspectMask     = vSrc->aspect;
          srcBarrier.subresourceRange.levelCount     = 1;
          srcBarrier.subresourceRange.layerCount     = 1;
          srcBarrier.srcAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
          srcBarrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
          vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0, 0, nullptr, 0, nullptr, 1, &srcBarrier);

          // Transition dst face+mip: SHADER_READ_ONLY → TRANSFER_DST
          VkImageMemoryBarrier dstBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
          dstBarrier.oldLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          dstBarrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
          dstBarrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
          dstBarrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
          dstBarrier.image                           = vDst->image;
          dstBarrier.subresourceRange.aspectMask     = vDst->aspect;
          dstBarrier.subresourceRange.baseMipLevel   = (uint32_t) mip;
          dstBarrier.subresourceRange.levelCount     = 1;
          dstBarrier.subresourceRange.baseArrayLayer = (uint32_t) face;
          dstBarrier.subresourceRange.layerCount     = 1;
          dstBarrier.srcAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
          dstBarrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
          vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

          // Copy
          VkImageCopy region{};
          region.srcSubresource.aspectMask     = vSrc->aspect;
          region.srcSubresource.layerCount     = 1;
          region.dstSubresource.aspectMask     = vDst->aspect;
          region.dstSubresource.mipLevel       = (uint32_t) mip;
          region.dstSubresource.baseArrayLayer = (uint32_t) face;
          region.dstSubresource.layerCount     = 1;
          region.extent                        = {(uint32_t) width, (uint32_t) height, 1};
          vkCmdCopyImage(cb,
                         vSrc->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         vDst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1, &region);

          // Transition source back
          srcBarrier.oldLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
          srcBarrier.newLayout    = vSrc->currentLayout;
          srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
          srcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
          vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0, 0, nullptr, 0, nullptr, 1, &srcBarrier);

          // Transition dst back: TRANSFER_DST → SHADER_READ_ONLY
          dstBarrier.oldLayout    = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
          dstBarrier.newLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
          dstBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
          vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0, 0, nullptr, 0, nullptr, 1, &dstBarrier);
        });
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
                                                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
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
    std::memcpy(gpu->buffer.mapped, data, (size_t) size);

    // Register non-perDraw UBOs for global descriptor write.
    // Slot 6 = per-draw dynamic UBO (ring buffer path); all other slots are static global UBOs
    // (Camera, GraphicConsts, DirectionalLight, etc.) that must be written into every new set.
    const int slot = ub->m_slot;
    if (slot >= 0 && slot != 6)
    {
      m_globalUboRegistry[slot] = {gpu->buffer.handle, gpu->buffer.size};
    }
  }

  void VulkanBackend::WriteGlobalUbosToSet(VkDescriptorSet set)
  {
    for (auto& [slot, entry] : m_globalUboRegistry)
    {
      if (entry.handle == VK_NULL_HANDLE)
        continue;
      VulkanDescriptor::WriteUniformBuffer(m_context->GetDevice(),
                                           set,
                                           VulkanBindings::UboBindingFor((uint) slot),
                                           entry.handle,
                                           0,
                                           entry.size);
    }
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

    auto data     = std::make_shared<VulkanGpuProgram>();
    data->context = m_context.get();
    data->vert    = vertSm->module;
    data->frag    = fragSm->module;

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
        slot.ownsView = true;
      }
    }
    else
    {
      slot.view     = slot.tex->view;
      slot.ownsView = false;
    }

    // Don't eager-destroy the cached RP+FB â€” BuildOffscreenRenderPass will defer them on the
    // next BeginPass. Eager destroy here would invalidate the in-flight cmd buffer.
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
    // backbuffer clear can pick it up. BeginPass takes its clear color from PassDesc directly,
    // so this is currently informational; engine uses it for "set once, expect all subsequent
    // passes to use this when they didn't override".
    m_clearColor = color;
  }

  bool VulkanBackend::ValidateBackbufferSrgbEncoding()
  {
    // Vulkan swapchain format explicitly defines sRGB  always valid if configured correctly.
    return true;
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
    // TODO: Staging buffer + vkCmdCopyBufferToImage with offset region.
  }

  void VulkanBackend::PushDebugGroup(StringView name)
  {
    // TODO: vkCmdBeginDebugUtilsLabelEXT.
  }

  void VulkanBackend::PopDebugGroup()
  {
    // TODO: vkCmdEndDebugUtilsLabelEXT.
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