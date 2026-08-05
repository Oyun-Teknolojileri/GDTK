/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "NullBackend.h"

#include "Logger.h"
#include "ToolKit.h"

namespace ToolKit
{

  // ---- lifecycle ----
  void NullBackend::InitBackend(const BackendInitParams&) { GetLogger()->Log("NullBackend initialized (headless)"); }

  void NullBackend::BeginFrame() {}
  void NullBackend::EndFrame() {}
  void NullBackend::Present() {}

  // ---- passes ----
  void NullBackend::StartPass(const PassDesc&) {}
  void NullBackend::FinishPass() {}

  // ---- viewport / scissor ----
  void NullBackend::SetViewport(uint, uint, uint, uint) {}
  void NullBackend::SetScissor(uint, uint, uint, uint) {}

  // ---- clear ----
  void NullBackend::ClearBuffer(GraphicBitFields, const Vec4&) {}
  void NullBackend::ClearColorBuffer(const Vec4&) {}

  // ---- pipeline ----
  void NullBackend::BindPipeline(const GpuProgramPtr&, const RenderState*) {}

  // ---- draw ----
  void NullBackend::SubmitPerDrawData(const void*, size_t) {}
  void NullBackend::BindTexture(ubyte, TexturePtr) {}
  void NullBackend::BindUniformBuffer(const String&, UniformBuffer*) {}
  void NullBackend::BindUniformBuffer(UniformBuffer*, int) {}
  void NullBackend::Draw(const DrawDesc&) {}

  // ---- framebuffer ops ----
  void NullBackend::ResolveFramebuffer(FramebufferPtr, FramebufferPtr, const IntArray&) {}
  void NullBackend::CopyFramebuffer(FramebufferPtr, FramebufferPtr, GraphicBitFields) {}
  void NullBackend::BlitToScreen(FramebufferPtr) {}

  // ---- timer queries ----
  void NullBackend::StartTimerQuery() {}
  void NullBackend::EndTimerQuery() {}
  void NullBackend::GetElapsedTime(float& cpu, float& gpu)
  {
    cpu = 0.0f;
    gpu = 0.0f;
  }

  // ---- textures ----
  void NullBackend::CreateTexture(Texture*) {}
  void NullBackend::DestroyTexture(Texture*) {}
  void NullBackend::ApplyTextureSettings(Texture*) {}
  void NullBackend::GenerateMipmaps(Texture*) {}
  void NullBackend::UpdateTextureRegion(Texture*, const void*, int, int, int, int) {}
  void NullBackend::SetTextureMaxMipLevel(Texture*, int) {}
  void NullBackend::AllocateCubemapMipStorage(Texture*) {}
  void NullBackend::CopyCubemapFaceFromFramebuffer(Texture*, int, int, int, int, Framebuffer*, Framebuffer*) {}

  // ---- meshes ----
  void NullBackend::CreateMesh(Mesh*) {}
  void NullBackend::DestroyMesh(Mesh*) {}

  // ---- uniform buffers ----
  void NullBackend::CreateUniformBuffer(UniformBuffer*, uint64) {}
  void NullBackend::DestroyUniformBuffer(UniformBuffer*) {}
  void NullBackend::UpdateUniformBuffer(UniformBuffer*, const void*, uint64) {}

  // ---- shaders ----
  GpuResourceDataPtr NullBackend::CreateShader(Shader*, const String&) { return nullptr; }
  void NullBackend::DestroyShader(GpuResourceData*) {}

  // ---- gpu programs ----
  void NullBackend::CreateGpuProgram(GpuProgram*, const ShaderResourceBinding*, int) {}
  void NullBackend::DestroyGpuProgram(GpuProgram*) {}
  int NullBackend::GetUniformLocation(GpuProgram*, const char*) { return -1; }

  // ---- framebuffers ----
  void NullBackend::CreateFramebuffer(Framebuffer*) {}
  void NullBackend::DestroyFramebuffer(Framebuffer*) {}
  void NullBackend::AttachColorTarget(Framebuffer*, RenderTargetPtr, int, int, int, int) {}
  void NullBackend::DetachColorTarget(Framebuffer*, int) {}
  void NullBackend::AttachDepthTarget(Framebuffer*, DepthTexturePtr) {}
  void NullBackend::DetachDepthTarget(Framebuffer*) {}

  // ---- uniform setters ----
  void NullBackend::SetUniform4f(int, const Vec4&) {}

  // ---- queries ----
  String NullBackend::GetBackendRendererString() { return "Null Backend (headless)"; }
  int NullBackend::GetMaxArrayTextureLayers() { return 0; }

  // ---- srgb ----
  void NullBackend::SetSrgbAutoEncoding(bool) {}

  // ---- misc ----
  void NullBackend::Finish() {}
  void NullBackend::SetDefaultClearColor(const Vec4&) {}
  bool NullBackend::ValidateBackbufferSrgbEncoding() { return false; }
  void NullBackend::EnableScissorTest(bool) {}
  void NullBackend::ReadPixels(int, int, int, int, GraphicTypes, GraphicTypes, void*) {}
  void NullBackend::UpdateTextureSubRegion(Texture*, int, int, int, int, const void*) {}

  void NullBackend::PushDebugGroup(StringView) {}
  void NullBackend::PopDebugGroup() {}

  void NullBackend::SetDebugLabel(Texture*) {}
  void NullBackend::SetDebugLabel(Framebuffer*) {}

  bool NullBackend::SupportsFloatTextureLinearFilter() { return false; }
  bool NullBackend::IsDepthClampSupported() { return false; }

  void* NullBackend::GetNativeTextureHandle(Texture*) { return nullptr; }

} // namespace ToolKit
