/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Framebuffer.h"

#include "EngineSettings.h"
#include "IGraphicsBackend.h"
#include "Logger.h"
#include "RenderSystem.h"
#include "Renderer.h"
#include "Stats.h"
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
      m_settings.msaaCount = MsaaSampleCount::x0;
    }

    if (m_settings.width == 0)
    {
      m_settings.width = 1024;
    }

    if (m_settings.height == 0)
    {
      m_settings.height = 1024;
    }

    IGraphicsBackend* backend = GetRenderSystem()->GetBackend();
    assert(backend && "Graphics backend not available during Framebuffer::Init");

    backend->CreateFramebuffer(this);

    if (m_settings.useDefaultDepth)
    {
      m_depthAtch = MakeNewPtr<DepthTexture>();
      m_depthAtch->Init(m_settings.width, m_settings.height, m_settings.depthStencil, m_settings.msaaCount);

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

    m_depthAtch = nullptr;

    for (int i = 0; i < m_maxColorAttachmentCount; i++)
    {
      m_colorAtchs[i] = nullptr;
    }

    if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
    {
      backend->DestroyFramebuffer(this);
    }
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

  bool Framebuffer::IsMultiSampled() { return m_settings.msaaCount > MsaaSampleCount::x0; }

  void Framebuffer::AttachDepthTexture(DepthTexturePtr dt)
  {
    assert(dt != nullptr && "Depth texture can't be null.");
    m_depthAtch = dt;

    if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
    {
      backend->AttachDepthTarget(this, dt);
    }
  }

  DepthTexturePtr Framebuffer::DetachDepthTexture()
  {
    if (m_depthAtch == nullptr)
    {
      return nullptr;
    }

    if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
    {
      backend->DetachDepthTarget(this);
    }

    DepthTexturePtr dt = m_depthAtch;
    m_depthAtch        = nullptr;
    return dt;
  }

  DepthTexturePtr Framebuffer::GetDepthTexture() { return m_depthAtch; }

  RenderTargetPtr Framebuffer::SetColorAttachment(Attachment atc,
                                                  RenderTargetPtr rt,
                                                  int mip,
                                                  int layer,
                                                  CubemapFace face)
  {
    if (rt->m_width <= 0 || rt->m_height <= 0 || rt->m_gpuData == nullptr)
    {
      assert(false && "Render target can't be bind.");
      return nullptr;
    }

    RenderTargetPtr oldRt   = m_colorAtchs[(int) atc];
    m_colorAtchs[(int) atc] = rt;
    m_settings.width        = rt->m_width;
    m_settings.height       = rt->m_height;

    if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
    {
      int faceIdx = (face != CubemapFace::NONE) ? (int) face : -1;
      backend->AttachColorTarget(this, rt, (int) atc, mip, layer, faceIdx);
    }

    return oldRt;
  }

  RenderTargetPtr Framebuffer::GetColorAttachment(Attachment atc)
  {
    if (atc >= Attachment::ColorAttachment0 && atc <= Attachment::ColorAttachment7)
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

    m_colorAtchs[(int) atc] = nullptr;

    if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
    {
      backend->DetachColorTarget(this, (int) atc);
    }

    return rt;
  }

  const FramebufferSettings& Framebuffer::GetSettings() { return m_settings; }

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
