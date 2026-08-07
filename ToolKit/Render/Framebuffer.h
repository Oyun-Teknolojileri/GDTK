/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "IGraphicsBackend.h"
#include "Resource.h"
#include "Texture.h"

namespace ToolKit
{

  struct FramebufferSettings
  {
    /**Height of the frame buffer. */
    int width                 = 128;
    /** Width of the frame buffer. */
    int height                = 128;
    /** States whether the default depth has stencil or not. */
    bool depthStencil         = false;
    /** Creates a default depth attachment. */
    bool useDefaultDepth      = true;
    /** Creates multi sample frame buffers if greater than x0. */
    MsaaSampleCount msaaCount = MsaaSampleCount::x0;

    bool operator==(const FramebufferSettings& other) const
    {
      return memcmp(this, &other, sizeof(FramebufferSettings)) == 0;
    }

    bool operator!=(const FramebufferSettings& other) const { return !(*this == other); }
  };

  class TK_API Framebuffer : public Resource
  {
   public:
    TKDeclareClass(Framebuffer, Resource);

   public:
    enum class Attachment
    {
      ColorAttachment0       = 0,
      ColorAttachment1       = 1,
      ColorAttachment2       = 2,
      ColorAttachment3       = 3,
      ColorAttachment4       = 4,
      ColorAttachment5       = 5,
      ColorAttachment6       = 6,
      ColorAttachment7       = 7,
      DepthAttachment        = 100,
      DepthStencilAttachment = 101
    };

    enum class CubemapFace
    {
      POS_X = 0,
      NEG_X = 1,
      POS_Y = 2,
      NEG_Y = 3,
      POS_Z = 4,
      NEG_Z = 5,
      NONE
    };

   public:
    Framebuffer();
    virtual ~Framebuffer();

    virtual void NativeConstruct(StringView label);
    virtual void NativeConstruct(const FramebufferSettings& settings, StringView label = "");

    void Init(bool flushClientSideArray = false) override;
    void UnInit() override;
    void Load() override;

    bool Initialized();

    RenderTargetPtr SetColorAttachment(Attachment atc,
                                       RenderTargetPtr rt,
                                       int mip          = 0,
                                       int layer        = -1,
                                       CubemapFace face = CubemapFace::NONE);

    RenderTargetPtr GetColorAttachment(Attachment atc);
    RenderTargetPtr DetachColorAttachment(Attachment atc);

    DepthTexturePtr GetDepthTexture();
    void AttachDepthTexture(DepthTexturePtr rt);
    DepthTexturePtr DetachDepthTexture();

    const FramebufferSettings& GetSettings();
    void ReconstructIfNeeded(int width, int height);
    void ReconstructIfNeeded(const FramebufferSettings& settings);

    /** Returns if the framebuffer is multi sampled. */
    bool IsMultiSampled();

   private:
    bool IsColorAttachment(Attachment atc);

   public:
    static const int m_maxColorAttachmentCount = 8;
    StringView m_label; //!< Debug label which appears in the gpu debuggers.
    GpuResourceDataPtr m_gpuData;

   private:
    FramebufferSettings m_settings;

    RenderTargetPtr m_colorAtchs[m_maxColorAttachmentCount];
    DepthTexturePtr m_depthAtch = nullptr;
  };

}; // namespace ToolKit
