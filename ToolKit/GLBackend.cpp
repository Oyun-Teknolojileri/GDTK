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
#include "PerDrawUniforms.h"
#include "RHI.h"
#include "ShaderUniform.h"
#include "TKOpenGL.h"
#include "Texture.h"
#include "DebugNew.h"

namespace ToolKit
{

  GLBackend::GLBackend()  {}

  GLBackend::~GLBackend() {}

  void GLBackend::BeginFrame()
  {
    m_firstBind = true;
  }

  void GLBackend::EndFrame() {}

  void GLBackend::Present() {}

  void GLBackend::BeginPass(const PassDesc& desc)
  {
    if (desc.target != nullptr)
    {
      RHI::SetFramebuffer(GL_FRAMEBUFFER, desc.target->m_glImpl.fboId);
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
    // OpenGL clears are affected by masks. Force them to true to ensure
    // the clear operation succeeds regardless of current pipeline state.
    // This matches Vulkan behavior where clears are independent of pipeline state.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);

    glClearColor(color.x, color.y, color.z, color.w);
    glClear((GLbitfield) fields);

    // Invalidate state cache to force re-application of masks on next draw.
    m_firstBind = true;
  }

  void GLBackend::ClearColorBuffer(const Vec4& color)
  {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(color.x, color.y, color.z, color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    m_firstBind = true;
  }

  void GLBackend::BindPipeline(const GpuProgramPtr& program, const RenderState* state)
  {
    if (program)
    {
      glUseProgram(program->m_handle);
    }

    if (state == nullptr)
    {
      return;
    }

    // Culling
    if (m_firstBind || m_lastAppliedState.cullMode != state->cullMode)
    {
      if (state->cullMode == CullingType::TwoSided)
      {
        glDisable(GL_CULL_FACE);
      }
      else
      {
        glEnable(GL_CULL_FACE);
        glCullFace(state->cullMode == CullingType::Front ? GL_FRONT : GL_BACK);
      }
      m_lastAppliedState.cullMode = state->cullMode;
    }

    // Depth Test
    if (m_firstBind || m_lastAppliedState.depthTestEnabled != state->depthTestEnabled)
    {
      if (state->depthTestEnabled)
      {
        glEnable(GL_DEPTH_TEST);
      }
      else
      {
        glDisable(GL_DEPTH_TEST);
      }
      m_lastAppliedState.depthTestEnabled = state->depthTestEnabled;
    }

    if (state->depthTestEnabled && (m_firstBind || m_lastAppliedState.depthFunction != state->depthFunction))
    {
      glDepthFunc((GLenum) state->depthFunction);
      m_lastAppliedState.depthFunction = state->depthFunction;
    }

    // Depth Write
    if (m_firstBind || m_lastAppliedState.depthWriteEnabled != state->depthWriteEnabled)
    {
      glDepthMask(state->depthWriteEnabled ? GL_TRUE : GL_FALSE);
      m_lastAppliedState.depthWriteEnabled = state->depthWriteEnabled;
    }

    // Depth Clamp
    if (m_firstBind || m_lastAppliedState.depthClampEnabled != state->depthClampEnabled)
    {
      if (TK_GL_EXT_depth_clamp)
      {
        if (state->depthClampEnabled)
        {
          glEnable(GL_DEPTH_CLAMP_EXT);
        }
        else
        {
          glDisable(GL_DEPTH_CLAMP_EXT);
        }
      }
      m_lastAppliedState.depthClampEnabled = state->depthClampEnabled;
    }

    // Blending
    if (m_firstBind || m_lastAppliedState.blendFunction != state->blendFunction)
    {
      if (state->blendFunction == BlendFunction::NONE)
      {
        glDisable(GL_BLEND);
      }
      else
      {
        glEnable(GL_BLEND);
        switch (state->blendFunction)
        {
          case BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
          case BlendFunction::ONE_TO_ONE:
            glBlendFunc(GL_ONE, GL_ONE);
            glBlendEquation(GL_FUNC_ADD);
            break;
          default:
            glDisable(GL_BLEND);
            break;
        }
      }
      m_lastAppliedState.blendFunction = state->blendFunction;
    }

    // Stencil
    if (m_firstBind || m_lastAppliedState.stencilOperation != state->stencilOperation)
    {
      switch (state->stencilOperation)
      {
        case StencilOperation::None:
          glDisable(GL_STENCIL_TEST);
          glStencilMask(0x00);
          break;
        case StencilOperation::AllowAllPixels:
          glEnable(GL_STENCIL_TEST);
          glStencilMask(0xFF);
          glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
          glStencilFunc(GL_ALWAYS, 0xFF, 0xFF);
          break;
        case StencilOperation::AllowPixelsPassingStencil:
          glEnable(GL_STENCIL_TEST);
          glStencilFunc(GL_EQUAL, 0xFF, 0xFF);
          glStencilMask(0x00);
          break;
        case StencilOperation::AllowPixelsFailingStencil:
          glEnable(GL_STENCIL_TEST);
          glStencilFunc(GL_NOTEQUAL, 0xFF, 0xFF);
          glStencilMask(0x00);
          break;
      }
      m_lastAppliedState.stencilOperation = state->stencilOperation;
    }

    // Color Mask
    if (m_firstBind || m_lastAppliedState.colorMaskEnabled != state->colorMaskEnabled)
    {
      GLboolean m = state->colorMaskEnabled ? GL_TRUE : GL_FALSE;
      glColorMask(m, m, m, m);
      m_lastAppliedState.colorMaskEnabled = state->colorMaskEnabled;
    }

    m_firstBind = false;
  }

  void GLBackend::SubmitPerDrawData(const void* data, size_t size)
  {
    if (data == nullptr || size != sizeof(PerDrawUniforms))
    {
      return;
    }

    const PerDrawUniforms* pdu = reinterpret_cast<const PerDrawUniforms*>(data);
    GLuint currentProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, (GLint*) &currentProgram);
    if (currentProgram == 0)
    {
      return;
    }

    // Built-in uniforms.
    auto setMat4 = [&](Uniform u, const Mat4& m)
    {
      GLint loc = glGetUniformLocation(currentProgram, GetUniformName(u));
      if (loc != -1)
      {
        glUniformMatrix4fv(loc, 1, false, reinterpret_cast<const float*>(&m));
      }
    };

    setMat4(Uniform::MODEL, pdu->model);
    setMat4(Uniform::MODEL_WITHOUT_TRANSLATE, pdu->modelWithoutTranslate);
    setMat4(Uniform::INVERSE_MODEL, pdu->inverseModel);
    setMat4(Uniform::INVERSE_TRANSPOSE_MODEL, pdu->inverseTransposeModel);
    setMat4(Uniform::IBL_ROTATION, pdu->iblRotation);
    setMat4(Uniform::IBL_SECONDARY_ROTATION, pdu->iblSecondaryRotation);

    GLint vpLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::VIEWPORT_SIZE));
    if (vpLoc != -1)
    {
      glUniform2f(vpLoc, pdu->viewportSize.x, pdu->viewportSize.y);
    }

    // DrawCommand.
    GLint dcLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::DRAW_COMMAND));
    if (dcLoc != -1)
    {
      glUniform4fv(dcLoc, sizeof(DrawCommand) / sizeof(Vec4), (const float*) &pdu->drawCommand);
    }

    // Light indices.
    GLint plLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::ACTIVE_POINT_LIGHT_INDEXES));
    if (plLoc != -1 && pdu->activePointLightCount > 0)
    {
      glUniform1iv(plLoc, pdu->activePointLightCount, pdu->activePointLightIndices);
    }

    GLint slLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::ACTIVE_SPOT_LIGHT_INDEXES));
    if (slLoc != -1 && pdu->activeSpotLightCount > 0)
    {
      glUniform1iv(slLoc, pdu->activeSpotLightCount, pdu->activeSpotLightIndices);
    }

    // Material data.
    GLint matLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::MATERIAL_CACHE));
    if (matLoc != -1)
    {
      glUniform4fv(matLoc, sizeof(MaterialCacheItem::Data) / sizeof(Vec4), (const float*) &pdu->materialData);
    }

    // Animation data.
    GLint kfLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::KEY_FRAME_DATA));
    if (kfLoc != -1)
    {
      glUniform4fv(kfLoc, 1, (const float*) &pdu->keyFrameData);
    }

    GLint bkfLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::BLEND_FRAME_DATA));
    if (bkfLoc != -1)
    {
      glUniform4fv(bkfLoc, 1, (const float*) &pdu->blendFrameData);
    }

    GLint bfLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::BLEND_FACTOR));
    if (bfLoc != -1)
    {
      glUniform1f(bfLoc, pdu->animationBlendFactor);
    }

    GLint spLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::SKIN_PARAMS));
    if (spLoc != -1)
    {
      glUniform4fv(spLoc, 1, (const float*) &pdu->skinParams);
    }
  }

  void GLBackend::BindTexture(ubyte slot, TexturePtr tex)
  {
    if (tex != nullptr)
    {
      RHI::SetTexture((GLenum) tex->Settings().Target, tex->m_glImpl.textureId, slot);
    }
    else
    {
      RHI::SetTexture(GL_TEXTURE_2D, 0, slot);
    }
  }

  void GLBackend::Draw(const DrawDesc& desc)
  {
    if (desc.mesh == nullptr)
    {
      return;
    }

    RHI::BindVertexArray(desc.mesh->m_glImpl.vaoId);

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
