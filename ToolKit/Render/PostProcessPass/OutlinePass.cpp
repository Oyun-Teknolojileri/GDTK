/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "OutlinePass.h"

#include "Material.h"
#include "Mesh.h"
#include "Shader.h"
#include "ToolKit.h"

#include <DebugNew.h>

namespace ToolKit
{

  OutlinePass::OutlinePass() : Pass("OutlinePass")
  {
    m_stencilPass  = MakeNewPtr<StencilRenderPass>();
    m_stencilAsRt  = MakeNewPtr<RenderTarget>();

    m_outlinePass  = MakeNewPtr<FullQuadPass>();
    m_dilateShader = GetShaderManager()->Create<Shader>(ShaderPath("dilateFrag.shader", true));
  }

  void OutlinePass::Render()
  {
    assert(m_params.RenderJobs != nullptr && "Outline Pass Render Jobs Are Not Given!");

    // Generate stencil binary image.
    RenderSubPass(m_stencilPass);

    // Feed the stencil image into slot 0 as the dilate shader's input.
    m_outlinePass->m_material->SetDiffuseTextureVal(m_stencilAsRt);

    m_outlinePass->SetFragmentShader(m_dilateShader, GetRenderer());

    // Push the outline color through the pass-specific UBO. m_dilateBuffer is lazy-initialized
    // here (rather than in the constructor) so the renderer backend is fully up by the time
    // CreateUniformBuffer runs; OutlinePass instances can be constructed before Init.
    if (!m_dilateBufferInitialized)
    {
      m_dilateBuffer.Init(7);
      m_dilateBufferInitialized = true;
    }
    m_dilateBuffer.m_data.color = m_params.OutlineColor;
    m_dilateBuffer.Invalidate();
    m_dilateBuffer.Map();

    // Stage slot 7 in the quad's PassRequirements so ApplyRequirements re-binds the
    // descriptor set to this buffer (instead of a stale VkBuffer left by an earlier
    // pass sharing the slot — gradient sky, bloom, SSAO, DoF, gamma, etc.).
    m_outlinePass->GetRequirements().customUbos[7] = &m_dilateBuffer.GetBuffer();

    // Draw outline to the viewport.
    m_outlinePass->m_params.frameBuffer            = m_params.FrameBuffer;
    m_outlinePass->m_params.clearFrameBuffer       = GraphicBitFields::None;

    RenderSubPass(m_outlinePass);
  }

  void OutlinePass::PreRender()
  {
    Pass::PreRender();

    // Create stencil image.
    m_stencilPass->m_params.Camera     = m_params.Camera;
    m_stencilPass->m_params.RenderJobs = m_params.RenderJobs;

    // Construct output target.
    const FramebufferSettings& fbs     = m_params.FrameBuffer->GetSettings();
    m_stencilAsRt->ReconstructIfNeeded(fbs.width, fbs.height);
    m_stencilPass->m_params.OutputTarget = m_stencilAsRt;
  }

  void OutlinePass::PostRender() { Pass::PostRender(); }

} // namespace ToolKit