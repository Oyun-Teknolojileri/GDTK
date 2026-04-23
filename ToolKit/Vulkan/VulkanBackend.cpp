/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanBackend.h"

#include "../Framebuffer.h"
#include "../Logger.h"
#include "../Texture.h"
#include "VulkanContext.h"
#include "VulkanResources.h"
#include "VulkanSwapchain.h"

#include <vma/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace ToolKit
{

  VulkanBackend::VulkanBackend()
      : m_context(std::make_unique<VulkanContext>()), m_swapchain(std::make_unique<VulkanSwapchain>())
  {
  }

  VulkanBackend::~VulkanBackend()
  {
    m_swapchain.reset();
    m_context.reset();
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
      // Swapchain out-of-date or minimized — flag for recreate and skip.
      m_needsRecreate = true;
    }
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

  void VulkanBackend::BeginPass(const PassDesc& desc)
  {
    if (!m_frameStarted)
    {
      return;
    }

    if (desc.target == nullptr)
    {
      // Backbuffer pass — drive the swapchain's render pass with the caller's clear color.
      m_swapchain->BeginSwapchainPass(desc.clearColor);
      return;
    }

    // TODO (1f.3 next mini-step): build/lookup VkRenderPass + VkFramebuffer for the offscreen
    // target and vkCmdBeginRenderPass on it.
  }

  void VulkanBackend::EndPass()
  {
    if (!m_frameStarted)
    {
      return;
    }
    // TODO (1f.3 next mini-step): if an offscreen pass is active, end it; otherwise close
    // the swapchain pass.
    m_swapchain->EndSwapchainPass();
  }

  void VulkanBackend::SetViewport(uint x, uint y, uint w, uint h)
  {
    // TODO: vkCmdSetViewport.
  }

  void VulkanBackend::SetScissor(uint x, uint y, uint w, uint h)
  {
    // TODO: vkCmdSetScissor.
  }

  void VulkanBackend::ClearBuffer(GraphicBitFields fields, const Vec4& color)
  {
    // TODO: Record clear attachment commands.
  }

  void VulkanBackend::ClearColorBuffer(const Vec4& color)
  {
    // TODO: Record clear color attachment command.
  }

  void VulkanBackend::BindPipeline(const GpuProgramPtr& program, const RenderState* state)
  {
    // TODO: Build PipelineKey from program + state, lookup/create VkPipeline, vkCmdBindPipeline.
  }

  void VulkanBackend::SubmitPerDrawData(const void* data, size_t size)
  {
    // TODO: Push constants or dynamic UBO update.
  }

  void VulkanBackend::BindTexture(ubyte slot, TexturePtr tex)
  {
    // TODO: Update descriptor set with texture's VkImageView + VkSampler.
  }

  void VulkanBackend::Draw(const DrawDesc& desc)
  {
    // TODO: vkCmdBindVertexBuffers + vkCmdBindIndexBuffer + vkCmdDrawIndexed / vkCmdDraw.
  }

  void VulkanBackend::ResolveFramebuffer(FramebufferPtr src, FramebufferPtr dst, const IntArray& attachments)
  {
    // TODO: vkCmdResolveImage.
  }

  void VulkanBackend::CopyFramebuffer(FramebufferPtr src, FramebufferPtr dst, GraphicBitFields fields)
  {
    // TODO: vkCmdCopyImage / vkCmdBlitImage.
  }

  void VulkanBackend::BlitToScreen(FramebufferPtr src)
  {
    // TODO: Blit to swapchain image.
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
      TK_ERR("VulkanBackend::CreateTexture called before VulkanContext was initialized — "
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

    VkFormat vkFormat = ToVkFormat(settings.InternalFormat);
    if (vkFormat == VK_FORMAT_UNDEFINED)
    {
      TK_ERR("VulkanBackend::CreateTexture - unsupported format (%d)", (int) settings.InternalFormat);
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
    // cubemap — the IBL prefilter pipeline always writes to every mip of the environment cubemap
    // regardless of the flag, so we must back it with real mip storage. Depth targets don't need
    // mip chains.
    const bool wantsMipChain = !isDepth && (settings.GenerateMipMap || isCubemap);
    data->mipLevels    = wantsMipChain ? (uint32_t) tex->CalculateMipmapLevels() : 1u;
    data->isCubemap    = isCubemap;
    data->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

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
    imageInfo.samples           = VK_SAMPLE_COUNT_1_BIT; // MSAA Stage 6.
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

    // Default sampler — color targets are sampled by ImGui / future post passes.
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
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &b);
        });
    data->currentLayout = targetLayout;

    tex->m_gpuData = data;
  }

  void VulkanBackend::DestroyTexture(Texture* tex)
  {
    if (tex == nullptr)
    {
      return;
    }
    // VulkanTexture dtor releases image/view/sampler via VulkanContext.
    // ImGui descriptor (if any) is released here since we need the ImGui header.
    if (auto* data = static_cast<VulkanTexture*>(tex->m_gpuData.get()))
    {
      if (data->imguiDescriptor != nullptr)
      {
        // NOTE: ImGui binding is handled in Stage 1f.3 — descriptor is nullptr until then.
        // When enabled, call ImGui_ImplVulkan_RemoveTexture here.
        data->imguiDescriptor = nullptr;
      }
    }
    tex->m_gpuData = nullptr;
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
    // TODO: vkCmdBlitImage chain for mip generation.
  }

  void VulkanBackend::UpdateTextureRegion(Texture* tex, const void* data)
  {
    // TODO: Staging buffer + vkCmdCopyBufferToImage.
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
    // TODO: vkCmdCopyImage from framebuffer attachment to cubemap face+mip.
  }

  void VulkanBackend::CreateMesh(Mesh* mesh)
  {
    // TODO: VkBuffer (vertex + index) + VMA allocation + staging upload.
  }

  void VulkanBackend::DestroyMesh(Mesh* mesh)
  {
    // TODO: vkDestroyBuffer + VMA free.
  }

  void VulkanBackend::CreateUniformBuffer(UniformBuffer* ub, uint64 size)
  {
    // TODO: VkBuffer (uniform) + VMA allocation, persistently mapped.
  }

  void VulkanBackend::DestroyUniformBuffer(UniformBuffer* ub)
  {
    // TODO: vkDestroyBuffer + VMA free.
  }

  void VulkanBackend::UpdateUniformBuffer(UniformBuffer* ub, const void* data, uint64 size)
  {
    // TODO: memcpy to persistently mapped pointer (or staging + copy).
  }

  GpuResourceDataPtr VulkanBackend::CreateShader(Shader* shader, const String& source)
  {
    // TODO: Compile GLSL to SPIR-V (glslang/shaderc), vkCreateShaderModule.
    return nullptr;
  }

  void VulkanBackend::DestroyShader(GpuResourceData* shaderData)
  {
    // TODO: vkDestroyShaderModule.
  }

  void VulkanBackend::CreateGpuProgram(GpuProgram* program, GlobalGpuBuffers* buffers)
  {
    // TODO: Create pipeline layout, descriptor set layouts. Actual VkPipeline created lazily in BindPipeline.
  }

  void VulkanBackend::DestroyGpuProgram(GpuProgram* program)
  {
    // TODO: Destroy pipeline layout, cached pipelines, descriptor set layouts.
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
    // VulkanFramebuffer dtor releases VkRenderPass + VkFramebuffer via VulkanContext.
    // Attached texture pointers are non-owning — they live on their RenderTarget/DepthTexture.
    fb->m_gpuData = nullptr;
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

    // Release previously owned view if we're about to replace it.
    if (slot.ownsView && slot.view != VK_NULL_HANDLE)
    {
      vkDestroyImageView(m_context->GetDevice(), slot.view, nullptr);
    }
    slot      = {};
    slot.tex  = static_cast<VulkanTexture*>(rt->m_gpuData.get());

    const bool needsSubresourceView =
        slot.tex != nullptr && (face >= 0 || layer >= 0 || (mip > 0 && slot.tex->mipLevels > 1));

    if (slot.tex == nullptr)
    {
      // Attach with a null texture — caller error; leave slot cleared.
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
        TK_WRN("AttachColorTarget: mip %u >= image mipLevels %u — clamping to %u",
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

    fbData->ReleaseLazyObjects();
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
      vkDestroyImageView(m_context->GetDevice(), slot.view, nullptr);
    }
    slot = {};

    fbData->ReleaseLazyObjects();
    fbData->dirty = true;
  }

  void VulkanBackend::AttachDepthTarget(Framebuffer* fb, DepthTexturePtr dt)
  {
    auto* fbData = static_cast<VulkanFramebuffer*>(fb->m_gpuData.get());
    assert(fbData && "AttachDepthTarget: framebuffer has no gpu data");

    auto& slot = fbData->depthAttachment;
    if (slot.ownsView && slot.view != VK_NULL_HANDLE)
    {
      vkDestroyImageView(m_context->GetDevice(), slot.view, nullptr);
    }
    slot      = {};
    slot.tex  = static_cast<VulkanTexture*>(dt->m_gpuData.get());
    slot.view = slot.tex ? slot.tex->view : VK_NULL_HANDLE;
    // Depth attachments currently always use the texture's primary view (no face/layer selection).

    fbData->ReleaseLazyObjects();
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
      vkDestroyImageView(m_context->GetDevice(), slot.view, nullptr);
    }
    slot = {};

    fbData->ReleaseLazyObjects();
    fbData->dirty = true;
  }

  void VulkanBackend::SubmitCustomUniforms(const GpuProgramPtr& program,
                                           std::unordered_map<String, ShaderUniform>& uniforms)
  {
    // TODO: Write uniforms into push constant range or material UBO.
  }

  void VulkanBackend::SetUniform4f(int location, const Vec4& value)
  {
    // TODO: Push constant update (or no-op  Vulkan doesn't use locations).
  }

  String VulkanBackend::GetBackendRendererString()
  {
    // TODO: Return VkPhysicalDeviceProperties::deviceName.
    return "Vulkan (stub)";
  }

  int VulkanBackend::GetMaxArrayTextureLayers()
  {
    // TODO: Return VkPhysicalDeviceLimits::maxImageArrayLayers.
    return 256;
  }

  void VulkanBackend::SetSrgbAutoEncoding(bool enable)
  {
    // Vulkan handles sRGB via swapchain format  likely no-op.
  }

  void VulkanBackend::Finish()
  {
    // TODO: vkDeviceWaitIdle.
  }

  void VulkanBackend::SetDefaultClearColor(const Vec4& color)
  {
    // TODO: Store default clear color for render passes.
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
    // TODO: Query VkFormatProperties for VK_FORMAT_R32G32B32A32_SFLOAT.
    return true;
  }

  void* VulkanBackend::GetNativeTextureHandle(Texture* tex)
  {
    // TODO: Return (void*)VkDescriptorSet for ImGui integration.
    return nullptr;
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