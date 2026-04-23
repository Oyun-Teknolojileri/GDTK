/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanBackend.h"

#include "../Logger.h"

namespace ToolKit
{

  VulkanBackend::VulkanBackend() {}

  VulkanBackend::~VulkanBackend() {}

  void VulkanBackend::InitBackend(const BackendInitParams& params)
  {
    TK_LOG("VulkanBackend: InitBackend stub");
    // TODO: Create VkInstance, pick physical device, create logical device, queues, command pool, etc.
  }

  void VulkanBackend::BeginFrame()
  {
    // TODO: Acquire swapchain image, begin command buffer.
  }

  void VulkanBackend::EndFrame()
  {
    // TODO: End command buffer, submit to queue.
  }

  void VulkanBackend::Present()
  {
    // TODO: vkQueuePresentKHR.
  }

  void VulkanBackend::BeginPass(const PassDesc& desc)
  {
    // TODO: Begin dynamic rendering / render pass.
  }

  void VulkanBackend::EndPass()
  {
    // TODO: End dynamic rendering / render pass.
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
    // TODO: vkCreateImage + vkAllocateMemory + vkCreateImageView.
  }

  void VulkanBackend::DestroyTexture(Texture* tex)
  {
    // TODO: vkDestroyImageView + vkDestroyImage + vkFreeMemory.
  }

  void VulkanBackend::ApplyTextureSettings(Texture* tex)
  {
    // TODO: Create/update VkSampler based on TextureSettings.
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
    // TODO: Create VkImageViews for attachments (dynamic rendering  no VkFramebuffer object needed).
  }

  void VulkanBackend::DestroyFramebuffer(Framebuffer* fb)
  {
    // TODO: Destroy associated image views if any.
  }

  void VulkanBackend::AttachColorTarget(Framebuffer* fb,
                                        RenderTargetPtr rt,
                                        int attachment,
                                        int mip,
                                        int layer,
                                        int face)
  {
    // TODO: Record attachment info for dynamic rendering.
  }

  void VulkanBackend::DetachColorTarget(Framebuffer* fb, int attachment)
  {
    // TODO: Remove attachment info.
  }

  void VulkanBackend::AttachDepthTarget(Framebuffer* fb, DepthTexturePtr dt)
  {
    // TODO: Record depth attachment info.
  }

  void VulkanBackend::DetachDepthTarget(Framebuffer* fb)
  {
    // TODO: Remove depth attachment info.
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