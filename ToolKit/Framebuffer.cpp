/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Framebuffer.h"

#include "EngineSettings.h"
#include "Logger.h"
#include "RHI.h"
#include "Stats.h"
#include "TKOpenGL.h"
#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{

  TKDefineClass(Framebuffer, Resource);

  Framebuffer::Framebuffer()
  {
    for (int i = 0; i < m_maxColorAttachmentCount; ++i)
    {
      m_colorAtchs[i] = nullptr;
    }

    m_depthAtch = nullptr;
  }

  Framebuffer::~Framebuffer() { UnInit(); }

  void Framebuffer::NativeConstruct(StringView label)
  {
    Super::NativeConstruct();
    m_label = label;
  }

  void Framebuffer::NativeConstruct(const FramebufferSettings& settings, StringView label)
  {
    Super::NativeConstruct();
    m_settings = settings;
    m_label    = label;
  }

  void Framebuffer::Init(bool flushClientSideArray)
  {
    if (m_initiated)
    {
      return;
    }

    if constexpr (GraphicSettings::disableMSAA)
    {
      m_settings.multiSampleFrameBuffer = 0;
    }

    // Create framebuffer object
    glGenFramebuffers(1, &m_fboId);
    RHI::SetFramebuffer(GL_FRAMEBUFFER, m_fboId);

    Stats::SetGpuResourceLabel(m_label, GpuResourceType::FrameBuffer, m_fboId);

    if (m_settings.width == 0)
    {
      m_settings.width = 1024;
    }

    if (m_settings.height == 0)
    {
      m_settings.height = 1024;
    }

    if (m_settings.useDefaultDepth)
    {
      m_depthAtch = MakeNewPtr<DepthTexture>();
      m_depthAtch->Init(m_settings.width,
                        m_settings.height,
                        m_settings.depthStencil,
                        m_settings.multiSampleFrameBuffer);

      AttachDepthTexture(m_depthAtch);
    }

    m_initiated = true;
  }

  void Framebuffer::UnInit()
  {
    if (!m_initiated)
    {
      return;
    }

    if (m_depthAtch != nullptr)
    {
      if (m_depthAtch.use_count() == 1)
      {
        // Only uninit, if its not shared.
        m_depthAtch->UnInit();
      }

      m_depthAtch = nullptr;
    }

    for (int i = 0; i < m_maxColorAttachmentCount; i++)
    {
      m_colorAtchs[i] = nullptr;
    }

    RHI::DeleteFramebuffers(1, &m_fboId);
    m_fboId     = 0;
    m_initiated = false;
  }

  void Framebuffer::Load() {}

  bool Framebuffer::Initialized() { return m_initiated; }

  void Framebuffer::ReconstructIfNeeded(int width, int height)
  {
    if (!m_initiated || m_settings.width != width || m_settings.height != height)
    {
      UnInit();

      m_settings.width  = width;
      m_settings.height = height;

      Init();
    }
  }

  void Framebuffer::ReconstructIfNeeded(const FramebufferSettings& settings)
  {
    if (!m_initiated || settings != m_settings)
    {
      UnInit();

      m_settings = settings;
      Init();
    }
  }

  void Framebuffer::AttachDepthTexture(DepthTexturePtr dt)
  {
    m_depthAtch = dt;

    RHI::SetFramebuffer(GL_FRAMEBUFFER, m_fboId);

    // Attach depth buffer to FBO
    GLenum attachment = dt->m_stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, m_depthAtch->m_textureId);

    // Check if framebuffer is complete
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
      GetLogger()->Log("Error: Framebuffer incomplete!");
    }
  }

  DepthTexturePtr Framebuffer::GetDepthTexture() { return m_depthAtch; }

  RenderTargetPtr Framebuffer::SetColorAttachment(Attachment atc,
                                                  RenderTargetPtr rt,
                                                  int mip,
                                                  int layer,
                                                  CubemapFace face)
  {
    GLenum attachment = GL_COLOR_ATTACHMENT0 + (int) atc;

    if (rt->m_width <= 0 || rt->m_height <= 0 || rt->m_textureId == 0)
    {
      assert(false && "Render target can't be bind.");
      return nullptr;
    }

    RenderTargetPtr oldRt = m_colorAtchs[(int) atc];

    RHI::SetFramebuffer(GL_FRAMEBUFFER, m_fboId);

    // Set attachment
    if (face != CubemapFace::NONE)
    {
      glFramebufferTexture2D(GL_FRAMEBUFFER,
                             attachment,
                             GL_TEXTURE_CUBE_MAP_POSITIVE_X + (int) face,
                             rt->m_textureId,
                             mip);
    }
    else
    {
      if (layer != -1)
      {
        assert(layer < rt->Settings().Layers);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, rt->m_textureId, mip, layer);
      }
      else
      {
        if (m_settings.multiSampleFrameBuffer > 0)
        {
          if (glFramebufferTexture2DMultisampleEXT != nullptr)
          {
            glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
                                                 attachment,
                                                 GL_TEXTURE_2D,
                                                 rt->m_textureId,
                                                 mip,
                                                 m_settings.multiSampleFrameBuffer);
          }
          else
          {
            // Fall back to single sample frame buffer.
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, rt->m_textureId, mip);
            m_settings.multiSampleFrameBuffer = 0;
          }
        }
        else
        {
          glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, rt->m_textureId, mip);
        }
      }
    }

    m_colorAtchs[(int) atc] = rt;

    m_settings.width        = rt->m_width;
    m_settings.height       = rt->m_height;

    SetDrawBuffers();
    CheckFramebufferComplete();

    return oldRt;
  }

  RenderTargetPtr Framebuffer::GetColorAttachment(Attachment atc)
  {
    if (atc < Attachment::DepthAttachment)
    {
      return m_colorAtchs[(int) atc];
    }

    return nullptr;
  }

  RenderTargetPtr Framebuffer::DetachColorAttachment(Attachment atc)
  {
    RenderTargetPtr rt = m_colorAtchs[(int) atc];
    if (rt == nullptr)
    {
      return nullptr;
    }

    RHI::SetFramebuffer(GL_FRAMEBUFFER, m_fboId);

    GLenum attachment = GL_COLOR_ATTACHMENT0 + (int) atc;
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0); // Detach

    m_colorAtchs[(int) atc] = nullptr;
    SetDrawBuffers();

    return rt;
  }

  uint Framebuffer::GetFboId() { return m_fboId; }

  const FramebufferSettings& Framebuffer::GetSettings() { return m_settings; }

  void Framebuffer::CheckFramebufferComplete()
  {
    RHI::SetFramebuffer(GL_FRAMEBUFFER, m_fboId);

    GLenum check = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    assert(check == GL_FRAMEBUFFER_COMPLETE && "Framebuffer incomplete");
  }

  void Framebuffer::SetDrawBuffers()
  {
    RHI::SetFramebuffer(GL_FRAMEBUFFER, m_fboId);

    GLenum colorAttachments[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int count                  = 0;

    for (int i = 0; i < m_maxColorAttachmentCount; i++)
    {
      RenderTargetPtr attachment = m_colorAtchs[i];
      if (attachment != nullptr && attachment->m_textureId != 0)
      {
        // All attachments must be the same size.
        assert(attachment->m_width == m_settings.width);
        assert(attachment->m_height == m_settings.height);

        colorAttachments[i] = GL_COLOR_ATTACHMENT0 + i;
        count++;
      }
    }

    if (count == 0)
    {
      glDrawBuffers(0, nullptr);
    }
    else
    {
      glDrawBuffers(count, colorAttachments);
    }
  }

  bool Framebuffer::IsColorAttachment(Attachment atc)
  {
    if (atc == Attachment::DepthAttachment || atc == Attachment::DepthStencilAttachment)
    {
      return false;
    }
    else
    {
      return true;
    }
  }

} // namespace ToolKit
