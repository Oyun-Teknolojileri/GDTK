/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "GammaTonemapFxaaPass.h"

#include "Material.h"
#include "Stats.h"

#include <DebugNew.h>

namespace ToolKit
{

  GammaTonemapFxaaPass::GammaTonemapFxaaPass() : Pass("GammaTonemapFxaaPass")
  {
    m_quadPass          = MakeNewPtr<FullQuadPass>();
    m_processTexture    = MakeNewPtr<RenderTarget>();
    m_postProcessShader = GetShaderManager()->Create<Shader>(ShaderPath("gammaTonemapFxaa.shader", true));
  }

  void GammaTonemapFxaaPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    Renderer* renderer         = GetRenderer();
    FramebufferPtr framebuffer = m_params.frameBuffer;

    RenderTargetPtr srcTexture = framebuffer->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);
    m_processTexture->ReconstructIfNeeded(srcTexture->m_width, srcTexture->m_height, &srcTexture->Settings());

    renderer->CopyTexture(srcTexture, m_processTexture, true);

    m_quadPass->m_material->SetDiffuseTextureVal(m_processTexture);
    m_quadPass->SetFragmentShader(m_postProcessShader, renderer);

    m_quadPass->m_params.frameBuffer      = m_params.frameBuffer;
    m_quadPass->m_params.clearFrameBuffer = GraphicBitFields::AllBits;

    // Push parameters through the pass-specific UBO. Lazy-init the buffer on the first PreRender
    //   the renderer/backend may not be live in the constructor.
    if (!m_passDataBufferInitialized)
    {
      m_passDataBuffer.Init(7);
      m_passDataBufferInitialized = true;
    }
    GammaTonemapFxaaPassDataLayout& ubo = m_passDataBuffer.m_data;
    ubo.enableFlags =
        IVec4((int) m_params.enableFxaa, (int) m_params.enableTonemapping, (int) m_params.enableGammaCorrection, 0);
    ubo.screenSizeAndPad = Vec4(m_params.screenSize, 0.0f, 0.0f);
    ubo.tonemapParams    = Vec4((float) (uint) m_params.tonemapMethod, m_params.gamma, 0.0f, 0.0f);
    m_passDataBuffer.Invalidate();
    m_passDataBuffer.Map();
  }

  void GammaTonemapFxaaPass::Render()
  {
    TK_PROFILE_FUNCTION();

    // Stage slot 7's UBO into the sub-pass's PassRequirements so the descriptor set
    // picks up this buffer when the quad's program binds. Without this, earlier passes
    // (ForwardPreProcess, SSAO, DoF) that share slot 7 with their own buffers can
    // leave the Vulkan descriptor set pointing at the wrong VkBuffer by the time
    // GammaTonemapFxaa runs.
    PassRequirements& qreq            = m_quadPass->GetRequirements();
    qreq.customUbos[7]                = &m_passDataBuffer.GetBuffer();

    RenderSubPass(m_quadPass);
  }

  void GammaTonemapFxaaPass::PostRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PostRender();
  }

  bool GammaTonemapFxaaPass::IsEnabled()
  { return m_params.enableFxaa || m_params.enableGammaCorrection || m_params.enableTonemapping; }

} // namespace ToolKit