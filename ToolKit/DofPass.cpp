/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "DofPass.h"

#include "Shader.h"
#include "Stats.h"
#include "ToolKit.h"

#include <DebugNew.h>

namespace ToolKit
{

  DoFPass::DoFPass() : Pass("DoFPass")
  {
    m_quadPass                       = MakeNewPtr<FullQuadPass>();
    m_quadPass->m_params.frameBuffer = MakeNewPtr<Framebuffer>("DofFB");
    m_dofShader                      = GetShaderManager()->Create<Shader>(ShaderPath("depthOfFieldFrag.shader", true));
    m_copyTexture                    = MakeNewPtr<RenderTarget>();
  }

  void DoFPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();
    if (m_params.ColorRt == nullptr)
    {
      return;
    }

    const TextureSettings& colorRTSet = m_params.ColorRt->Settings();
    m_copyTexture->ReconstructIfNeeded(m_params.ColorRt->m_width, m_params.ColorRt->m_height, &colorRTSet);

    GetRenderer()->CopyTexture(m_params.ColorRt, m_copyTexture);

    m_quadPass->SetFragmentShader(m_dofShader, GetRenderer());

    if (!m_passDataBufferInitialized)
    {
      m_passDataBuffer.Init();
      m_passDataBufferInitialized = true;
    }

    float blurRadiusScale = 0.5f;
    switch (m_params.blurQuality)
    {
      case DoFQuality::Low:
        blurRadiusScale = 2.0f;
        break;
      case DoFQuality::Normal:
        blurRadiusScale = 0.7f;
        break;
      case DoFQuality::High:
        blurRadiusScale = 0.2f;
        break;
    }

    IVec2 size(m_params.ColorRt->m_width, m_params.ColorRt->m_height);
    m_quadPass->m_params.frameBuffer->ReconstructIfNeeded({size.x, size.y, false, false});

    m_passDataBuffer.m_data.pixelSizeAndPad = Vec4(1.0f / float(size.x), 1.0f / float(size.y), 0.0f, 0.0f);
    m_passDataBuffer.m_data.focusAndBlur    = Vec4(m_params.focusPoint, m_params.focusScale, 5.0f, blurRadiusScale);

    m_quadPass->m_params.blendFunc        = BlendFunction::NONE;
    m_quadPass->m_params.clearFrameBuffer = GraphicBitFields::None;
    m_quadPass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_params.ColorRt);
  }

  void DoFPass::Render()
  {
    TK_PROFILE_FUNCTION();

    Renderer* renderer = GetRenderer();
    if (m_params.ColorRt == nullptr)
    {
      return;
    }

    renderer->SetTexture(0, m_copyTexture);
    renderer->SetTexture(1, m_params.DepthRt);

    // Map UBO into slot 5 immediately before draw — earlier passes (bloom etc.) may have left
    // a different buffer there. Pattern matches BloomPass / SSAOPass / GaussBlur.
    m_passDataBuffer.Invalidate();
    m_passDataBuffer.Map();
    RenderSubPass(m_quadPass);
  }

  void DoFPass::PostRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PostRender();
  }

} // namespace ToolKit