/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "GLBackend.h"

#include "Framebuffer.h"
#include "GpuProgram.h"
#include "Mesh.h"
#include "RHI.h"
#include "TKOpenGL.h"
#include "DebugNew.h"

namespace ToolKit
{

  GLBackend::GLBackend()  {}

  GLBackend::~GLBackend() {}

  void GLBackend::BeginFrame() {}

  void GLBackend::EndFrame() {}

  void GLBackend::Present() {}

  void GLBackend::BeginPass(const PassDesc& desc)
  {
    if (desc.target != nullptr)
    {
      RHI::SetFramebuffer(GL_FRAMEBUFFER, desc.target->GetFboId());
      desc.target->SetDrawBuffers();
    }
    else
    {
      RHI::SetFramebuffer(GL_FRAMEBUFFER, 0);
    }

    if (desc.clearBits != GraphicBitFields::None)
    {
      ClearBuffer(desc.clearBits, desc.clearColor);
    }
  }

  void GLBackend::EndPass() {}

  void GLBackend::SetViewport(uint x, uint y, uint w, uint h)
  {
    glViewport(x, y, w, h);
  }

  void GLBackend::SetScissor(uint x, uint y, uint w, uint h)
  {
    glScissor(x, y, w, h);
  }

  void GLBackend::ClearBuffer(GraphicBitFields fields, const Vec4& color)
  {
    glClearColor(color.x, color.y, color.z, color.w);
    glClear((GLbitfield) fields);
  }

  void GLBackend::ClearColorBuffer(const Vec4& color)
  {
    glClearColor(color.x, color.y, color.z, color.w);
    glClear((GLbitfield) GraphicBitFields::ColorBits);
  }

  void GLBackend::BindPipeline(const GpuProgramPtr& program, const RenderState* state)
  {
    if (program)
    {
      glUseProgram(program->m_handle);
    }
  }

  void GLBackend::SubmitPerDrawData(const void* data, size_t size) {}

  void GLBackend::BindTexture(ubyte slot, TexturePtr tex) {}

  void GLBackend::Draw(const DrawDesc& desc)
  {
    if (desc.mesh == nullptr)
    {
      return;
    }

    RHI::BindVertexArray(desc.mesh->m_vaoId);

    if (desc.indexed)
    {
      glDrawElements((GLenum) desc.type, desc.elementCount, GL_UNSIGNED_INT, nullptr);
    }
    else
    {
      glDrawArrays((GLenum) desc.type, 0, desc.elementCount);
    }
  }


  void GLBackend::ResolveFramebuffer(FramebufferPtr src, FramebufferPtr dst, const IntArray& attachments) {}

  void GLBackend::CopyFramebuffer(FramebufferPtr src, FramebufferPtr dst, GraphicBitFields fields) {}

  void GLBackend::BlitToScreen(FramebufferPtr src) {}

  void GLBackend::StartTimerQuery() {}

  void GLBackend::EndTimerQuery() {}

  void GLBackend::GetElapsedTime(float& cpu, float& gpu) {}

} // namespace ToolKit
