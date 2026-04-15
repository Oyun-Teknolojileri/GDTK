/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "GLBackend.h"

#include "DebugNew.h"

namespace ToolKit
{

  GLBackend::GLBackend()  {}

  GLBackend::~GLBackend() {}

  void GLBackend::BeginFrame() {}

  void GLBackend::EndFrame() {}

  void GLBackend::Present() {}

  void GLBackend::BeginPass(const PassDesc& desc) {}

  void GLBackend::EndPass() {}

  void GLBackend::SetViewport(uint x, uint y, uint w, uint h) {}

  void GLBackend::SetScissor(uint x, uint y, uint w, uint h) {}

  void GLBackend::BindPipeline(const GpuProgramPtr& program, const RenderState* state) {}

  void GLBackend::SubmitPerDrawData(const void* data, size_t size) {}

  void GLBackend::BindTexture(ubyte slot, TexturePtr tex) {}

  void GLBackend::Draw(const DrawDesc& desc) {}

  void GLBackend::ResolveFramebuffer(FramebufferPtr src, FramebufferPtr dst, const IntArray& attachments) {}

  void GLBackend::CopyFramebuffer(FramebufferPtr src, FramebufferPtr dst, GraphicBitFields fields) {}

  void GLBackend::BlitToScreen(FramebufferPtr src) {}

  void GLBackend::StartTimerQuery() {}

  void GLBackend::EndTimerQuery() {}

  void GLBackend::GetElapsedTime(float& cpu, float& gpu) {}

} // namespace ToolKit
