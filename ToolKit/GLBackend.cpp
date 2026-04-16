/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "GLBackend.h"

#include "EngineSettings.h"
#include "Framebuffer.h"
#include "GpuProgram.h"
#include "Mesh.h"
#include "PerDrawUniforms.h"
#include "RHI.h"
#include "ShaderUniform.h"
#include "TKOpenGL.h"
#include "Texture.h"
#include "ToolKit.h"
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
      BindFramebuffer(GL_FRAMEBUFFER, desc.target->m_glImpl.fboId);
      desc.target->SetDrawBuffers();
    }
    else
    {
      BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    if (desc.clearBits != GraphicBitFields::None)
    {
      ClearBuffer(desc.clearBits, desc.clearColor);
    }
  }

  void GLBackend::EndPass() {}

  void GLBackend::StoreFboBindings()
  {
    m_storedReadFboStack.push_back(m_currentReadFboId);
    m_storedDrawFboStack.push_back(m_currentDrawFboId);
  }

  void GLBackend::RestoreFboBindings()
  {
    if (!m_storedReadFboStack.empty())
    {
      BindFramebuffer(GL_READ_FRAMEBUFFER, m_storedReadFboStack.back());
      m_storedReadFboStack.pop_back();
    }

    if (!m_storedDrawFboStack.empty())
    {
      BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_storedDrawFboStack.back());
      m_storedDrawFboStack.pop_back();
    }
  }

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
    // OpenGL clears are affected by masks and scissor test. 
    // Force them to safe defaults to ensure the clear operation 
    // succeeds regardless of current pipeline state.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);
    glDisable(GL_SCISSOR_TEST);

    glClearColor(color.x, color.y, color.z, color.w);
    glClear((GLbitfield) fields);

    // Invalidate state cache to force re-application of masks and scissor on next draw.
    m_firstBind = true;
  }

  void GLBackend::ClearColorBuffer(const Vec4& color)
  {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
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
      BindTextureDirect((uint) tex->Settings().Target, tex->m_glImpl.textureId, slot);
    }
    else
    {
      BindTextureDirect(GL_TEXTURE_2D, 0, slot);
    }
  }

  void GLBackend::Draw(const DrawDesc& desc)
  {
    if (desc.mesh == nullptr)
    {
      return;
    }

    BindVAO(desc.mesh->m_glImpl.vaoId);

    if (desc.indexed)
    {
      glDrawElements((GLenum) desc.type, desc.elementCount, GL_UNSIGNED_INT, nullptr);
    }
    else
    {
      glDrawArrays((GLenum) desc.type, 0, desc.elementCount);
    }
  }

  // -----------------------------------------------------------------------
  // Texture resource management
  // -----------------------------------------------------------------------

  void GLBackend::CreateTexture(Texture* tex)
  {
    if (tex == nullptr)
    {
      return;
    }

    const TextureSettings& s = tex->Settings();

    // DepthTexture — backed by a renderbuffer
    if (DepthTexture* dt = tex->As<DepthTexture>())
    {
      glGenRenderbuffers(1, &tex->m_textureId);
      glBindRenderbuffer(GL_RENDERBUFFER, tex->m_textureId);

      GLenum fmt = (GLenum) dt->GetDepthFormat();
      if (s.msaaCount > MsaaSampleCount::x0)
      {
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, (int) s.msaaCount, fmt, tex->m_width, tex->m_height);
      }
      else
      {
        glRenderbufferStorage(GL_RENDERBUFFER, fmt, tex->m_width, tex->m_height);
      }

      tex->m_glImpl.textureId = tex->m_textureId;
      return;
    }

    // RenderTarget MSAA — also a renderbuffer
    if (s.msaaCount > MsaaSampleCount::x0 && s.Target == GraphicTypes::Target2D)
    {
      glGenRenderbuffers(1, &tex->m_textureId);
      glBindRenderbuffer(GL_RENDERBUFFER, tex->m_textureId);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER,
                                       (int) s.msaaCount,
                                       (GLenum) s.InternalFormat,
                                       tex->m_width,
                                       tex->m_height);
      tex->m_glImpl.textureId = tex->m_textureId;
      return;
    }

    // All other textures — regular GL texture
    glGenTextures(1, &tex->m_textureId);
    tex->m_glImpl.textureId = tex->m_textureId;
    BindTextureDirect((uint) s.Target, tex->m_textureId, 0);

    if (s.Target == GraphicTypes::Target2D)
    {
      // Texture or RenderTarget (non-MSAA 2D)
      void* data = tex->m_imagef ? (void*) tex->m_imagef : (void*) tex->m_image;
      glTexImage2D(GL_TEXTURE_2D,
                   0,
                   (GLint) s.InternalFormat,
                   tex->m_width,
                   tex->m_height,
                   0,
                   (GLenum) s.Format,
                   (GLenum) s.Type,
                   data);
    }
    else if (s.Target == GraphicTypes::TargetCubeMap)
    {
      // CubeMap — check if it has 6 loaded images
      if (CubeMap* cm = tex->As<CubeMap>())
      {
        const auto& images = cm->m_images;
        if (images.size() == 6)
        {
          uint sides[6] = {GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                           GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
                           GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
                           GL_TEXTURE_CUBE_MAP_NEGATIVE_Z};
          for (int i = 0; i < 6; i++)
          {
            glTexImage2D(sides[i], 0, GL_RGBA, tex->m_width, tex->m_width, 0, GL_RGBA, GL_UNSIGNED_BYTE, images[i]);
          }
        }
        else
        {
          // RenderTarget cube map — allocate empty faces
          for (uint i = 0; i < 6; i++)
          {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                         0,
                         (GLint) s.InternalFormat,
                         tex->m_width,
                         tex->m_height,
                         0,
                         (GLenum) s.Format,
                         (GLenum) s.Type,
                         nullptr);
          }
        }
      }
      else
      {
        // Generic cubemap (e.g. RenderTarget with TargetCubeMap)
        for (uint i = 0; i < 6; i++)
        {
          glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                       0,
                       (GLint) s.InternalFormat,
                       tex->m_width,
                       tex->m_height,
                       0,
                       (GLenum) s.Format,
                       (GLenum) s.Type,
                       nullptr);
        }
      }
    }
    else if (s.Target == GraphicTypes::Target2DArray)
    {
      assert(s.Layers > 0 && "Layer count must be at least 1");
      glTexImage3D(GL_TEXTURE_2D_ARRAY,
                   0,
                   (GLint) s.InternalFormat,
                   tex->m_width,
                   tex->m_height,
                   s.Layers,
                   0,
                   (GLenum) s.Format,
                   (GLenum) s.Type,
                   nullptr);
    }
  }

  void GLBackend::DestroyTexture(Texture* tex)
  {
    if (tex == nullptr || tex->m_textureId == 0)
    {
      return;
    }

    const TextureSettings& s = tex->Settings();
    bool isRenderbuffer      = false;

    if (tex->IsA<DepthTexture>())
    {
      isRenderbuffer = true;
    }
    else if (s.msaaCount > MsaaSampleCount::x0 && s.Target == GraphicTypes::Target2D)
    {
      isRenderbuffer = true;
    }

    if (isRenderbuffer)
    {
      glDeleteRenderbuffers(1, &tex->m_textureId);
    }
    else
    {
      InvalidateTextureCache(tex->m_textureId);
      glDeleteTextures(1, &tex->m_textureId);
    }

    tex->m_textureId        = 0;
    tex->m_glImpl.textureId = 0;
  }

  void GLBackend::ApplyTextureSettings(Texture* tex)
  {
    if (tex == nullptr)
    {
      return;
    }

    const TextureSettings& s = tex->Settings();
    GLenum target            = (GLenum) s.Target;

    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, (GLint) s.MinFilter);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, (GLint) s.MagFilter);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, (GLint) s.WarpS);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, (GLint) s.WarpT);

    if (s.Target == GraphicTypes::TargetCubeMap)
    {
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, (GLint) s.WarpR);
    }

    // Anisotropic filtering for 2D textures
    if (s.Target == GraphicTypes::Target2D && TK_GL_EXT_texture_filter_anisotropic == 1)
    {
      float maxAniso = 1.0f;
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);

      EngineSettings& engSettings = GetEngineSettings();
      int anisoVal = engSettings.m_graphics->GetAnisotropicTextureFilteringVal().GetValue<int>();
      float aniso  = glm::min(maxAniso, glm::max(1.0f, float(anisoVal)));
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    }
  }

  void GLBackend::GenerateMipmaps(Texture* tex)
  {
    if (tex == nullptr || tex->m_textureId == 0)
    {
      return;
    }

    BindTextureDirect((uint) tex->Settings().Target, tex->m_textureId, 0);
    glGenerateMipmap((GLenum) tex->Settings().Target);
  }

  void GLBackend::UpdateTextureRegion(Texture* tex, const void* data)
  {
    if (tex == nullptr || tex->m_textureId == 0)
    {
      return;
    }

    const TextureSettings& s = tex->Settings();
    BindTextureDirect((uint) s.Target, tex->m_textureId, 0);

    glTexSubImage2D((GLenum) s.Target,
                    0,
                    0,
                    0,
                    tex->m_width,
                    tex->m_height,
                    (GLenum) s.Format,
                    (GLenum) s.Type,
                    data);
  }

  void GLBackend::SetTextureMaxMipLevel(Texture* tex, int maxLevel)
  {
    if (tex == nullptr || tex->m_textureId == 0)
    {
      return;
    }

    GLenum target = (GLenum) tex->Settings().Target;
    BindTextureDirect(target, tex->m_textureId, 0);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, maxLevel);
  }

  void GLBackend::AllocateCubemapMipStorage(Texture* tex)
  {
    if (tex == nullptr || tex->m_textureId == 0)
    {
      return;
    }

    const TextureSettings& s = tex->Settings();
    BindTextureDirect(GL_TEXTURE_CUBE_MAP, tex->m_textureId, 0);

    const int numMipLevels = tex->CalculateMipmapLevels();
    for (int mip = 1; mip < numMipLevels; mip++)
    {
      int mipW = glm::max(1, tex->m_width >> mip);
      int mipH = glm::max(1, tex->m_height >> mip);

      for (int face = 0; face < 6; face++)
      {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                     mip,
                     (GLint) s.InternalFormat,
                     mipW,
                     mipH,
                     0,
                     (GLenum) s.Format,
                     (GLenum) s.Type,
                     nullptr);
      }
    }
  }

  void GLBackend::CopyCubemapFaceFromFramebuffer(Texture* cubemap, int face, int mip, int width, int height)
  {
    if (cubemap == nullptr || cubemap->m_textureId == 0)
    {
      return;
    }

    BindTextureDirect(GL_TEXTURE_CUBE_MAP, cubemap->m_textureId, 0);
    glCopyTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, 0, 0, 0, 0, width, height);
  }

  // -----------------------------------------------------------------------
  // Framebuffer resource management
  // -----------------------------------------------------------------------

  void GLBackend::CreateFramebuffer(Framebuffer* fb)
  {
    if (fb == nullptr)
    {
      return;
    }

    uint fboId = 0;
    glGenFramebuffers(1, &fboId);
    fb->m_fboId        = fboId;
    fb->m_glImpl.fboId = fboId;
    BindFramebuffer(GL_FRAMEBUFFER, fboId);
  }

  void GLBackend::DestroyFramebuffer(Framebuffer* fb)
  {
    if (fb == nullptr || fb->m_fboId == 0)
    {
      return;
    }

    uint id = fb->m_fboId;
    InvalidateFboCache(id);
    glDeleteFramebuffers(1, &id);
    fb->m_fboId        = 0;
    fb->m_glImpl.fboId = 0;
  }

  void GLBackend::AttachColorTarget(Framebuffer* fb, RenderTargetPtr rt, int attachment, int mip, int layer, int face)
  {
    if (fb == nullptr || rt == nullptr)
    {
      return;
    }

    BindFramebuffer(GL_FRAMEBUFFER, fb->m_fboId);
    GLenum glAttachment = GL_COLOR_ATTACHMENT0 + attachment;

    if (rt->Settings().msaaCount > MsaaSampleCount::x0)
    {
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, glAttachment, GL_RENDERBUFFER, rt->m_textureId);
    }
    else if (face >= 0)
    {
      glFramebufferTexture2D(GL_FRAMEBUFFER, glAttachment, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, rt->m_textureId, mip);
    }
    else if (layer >= 0)
    {
      glFramebufferTextureLayer(GL_FRAMEBUFFER, glAttachment, rt->m_textureId, mip, layer);
    }
    else
    {
      glFramebufferTexture2D(GL_FRAMEBUFFER, glAttachment, GL_TEXTURE_2D, rt->m_textureId, mip);
    }
  }

  void GLBackend::DetachColorTarget(Framebuffer* fb, int attachment)
  {
    if (fb == nullptr || fb->m_fboId == 0)
    {
      return;
    }

    BindFramebuffer(GL_FRAMEBUFFER, fb->m_fboId);
    GLenum glAttachment = GL_COLOR_ATTACHMENT0 + attachment;
    glFramebufferTexture2D(GL_FRAMEBUFFER, glAttachment, GL_TEXTURE_2D, 0, 0);
  }

  void GLBackend::AttachDepthTarget(Framebuffer* fb, DepthTexturePtr dt)
  {
    if (fb == nullptr || dt == nullptr)
    {
      return;
    }

    BindFramebuffer(GL_FRAMEBUFFER, fb->m_fboId);
    GLenum attachment = dt->m_stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, dt->m_textureId);
  }

  void GLBackend::DetachDepthTarget(Framebuffer* fb)
  {
    if (fb == nullptr || fb->m_fboId == 0)
    {
      return;
    }

    BindFramebuffer(GL_FRAMEBUFFER, fb->m_fboId);
    // Detach both possible depth attachment types
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
  }

  void GLBackend::SetDrawBuffers(Framebuffer* fb)
  {
    if (fb == nullptr || fb->m_fboId == 0)
    {
      return;
    }

    BindFramebuffer(GL_FRAMEBUFFER, fb->m_fboId);

    GLenum colorAttachments[8] = {GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE};
    int maxAttachment          = -1;

    for (int i = 0; i < Framebuffer::m_maxColorAttachmentCount; i++)
    {
      RenderTargetPtr rt = fb->GetColorAttachment((Framebuffer::Attachment) i);
      if (rt != nullptr && rt->m_textureId != 0)
      {
        colorAttachments[i] = GL_COLOR_ATTACHMENT0 + i;
        maxAttachment       = i;
      }
    }

    if (maxAttachment >= 0)
    {
      glDrawBuffers(maxAttachment + 1, colorAttachments);
    }
    else
    {
      glDrawBuffers(0, nullptr);
    }
  }

  void GLBackend::CheckFramebufferComplete(Framebuffer* fb)
  {
    if (fb == nullptr || fb->m_fboId == 0)
    {
      return;
    }

    BindFramebuffer(GL_FRAMEBUFFER, fb->m_fboId);
    GLenum check = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    assert(check == GL_FRAMEBUFFER_COMPLETE && "Framebuffer incomplete");
  }

  void GLBackend::ResolveFramebuffer(FramebufferPtr src, FramebufferPtr dst, const IntArray& attachments) {}

  void GLBackend::CopyFramebuffer(FramebufferPtr src, FramebufferPtr dst, GraphicBitFields fields) {}

  void GLBackend::BlitToScreen(FramebufferPtr src) {}

  void GLBackend::StartTimerQuery() {}

  void GLBackend::EndTimerQuery() {}

  void GLBackend::GetElapsedTime(float& cpu, float& gpu) {}

  void GLBackend::InvalidateFboCache(uint id)
  {
    if (m_currentReadFboId == id)
    {
      m_currentReadFboId = (uint) -1;
    }
    if (m_currentDrawFboId == id)
    {
      m_currentDrawFboId = (uint) -1;
    }
  }

  void GLBackend::InvalidateTextureCache(uint id)
  {
    for (int i = 0; i < 32; i++)
    {
      if (m_textureSlotCache[i].textureID == id)
      {
        m_textureSlotCache[i].textureID = 0;
        m_textureSlotCache[i].target    = 0;
      }
    }
  }

  void GLBackend::BindFramebuffer(uint target, uint id)
  {
    if (target == GL_FRAMEBUFFER)
    {
      if (m_currentReadFboId != id || m_currentDrawFboId != id)
      {
        glBindFramebuffer(GL_FRAMEBUFFER, id);
        m_currentReadFboId = id;
        m_currentDrawFboId = id;
      }
      return;
    }

    if (target == GL_READ_FRAMEBUFFER && m_currentReadFboId != id)
    {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, id);
      m_currentReadFboId = id;
    }

    if (target == GL_DRAW_FRAMEBUFFER && m_currentDrawFboId != id)
    {
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, id);
      m_currentDrawFboId = id;
    }
  }

  void GLBackend::BindTextureDirect(uint target, uint id, uint slot)
  {
    if (slot >= 32)
    {
      return;
    }

    if (m_textureSlotCache[slot].textureID != id || m_textureSlotCache[slot].target != target)
    {
      glActiveTexture(GL_TEXTURE0 + slot);
      glBindTexture(target, id);
      m_textureSlotCache[slot].textureID = id;
      m_textureSlotCache[slot].target    = target;
    }
  }

  void GLBackend::BindVAO(uint vao)
  {
    if (m_currentVAO != vao)
    {
      glBindVertexArray(vao);
      m_currentVAO = vao;
    }
  }

} // namespace ToolKit
