/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "StencilPass.h"

#include "Material.h"
#include "Mesh.h"
#include "Shader.h"
#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{

  StencilRenderPass::StencilRenderPass() : Pass("StencilRenderPass")
  {
    // Init sub pass.
    m_copyStencilSubPass    = MakeNewPtr<FullQuadPass>();
    m_unlitFragShader       = GetShaderManager()->Create<Shader>(ShaderPath("unlitFrag.shader", true));
    m_frameBuffer           = MakeNewPtr<Framebuffer>("StencilPassFB");

    m_solidOverrideMaterial = GetMaterialManager()->GetCopyOfUnlitColorMaterial();
  }

  void StencilRenderPass::Render()
  {
    assert(m_params.RenderJobs != nullptr && "Stencil Render Pass Render Jobs Are Not Given!");

    Renderer* renderer = GetRenderer();

    // Stencil pass: write 1 to stencil, suppress color writes.
    for (RenderJob& job : *m_params.RenderJobs)
    {
      job.State.stencilOperation = StencilOperation::AllowAllPixels;
      job.State.colorMaskEnabled = false;
    }

    renderer->Render(*m_params.RenderJobs);

    // Copy pass: draw a fullscreen quad where stencil != 1. The stencil-op is set on the
    // subpass material so it propagates into the quad job's State at job-creation time
    // (FullQuadPass::Render copies material's RenderState into job.State).
    m_copyStencilSubPass->SetFragmentShader(m_unlitFragShader, renderer);
    m_copyStencilSubPass->m_material->GetRenderState()->stencilOperation =
        StencilOperation::AllowPixelsFailingStencil;

    RenderSubPass(m_copyStencilSubPass);
  }

  void StencilRenderPass::PreRender()
  {
    Pass::PreRender();
    Renderer* renderer                   = GetRenderer();

    GpuProgramManager* gpuProgramManager = renderer->GetGpuProgramManager();
    ShaderPtr vert                       = m_solidOverrideMaterial->GetVertexShaderVal();
    ShaderPtr frag                       = m_solidOverrideMaterial->GetFragmentShaderVal();
    m_program                            = gpuProgramManager->CreateProgram(vert, frag);
    renderer->BindProgram(m_program);

    FramebufferSettings settings;
    settings.depthStencil    = true;
    settings.useDefaultDepth = true;
    settings.width           = m_params.OutputTarget->m_width;
    settings.height          = m_params.OutputTarget->m_height;

    m_frameBuffer->ReconstructIfNeeded(settings);
    m_frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_params.OutputTarget);
    m_copyStencilSubPass->m_params.frameBuffer      = m_frameBuffer;
    m_copyStencilSubPass->m_params.clearFrameBuffer = GraphicBitFields::None;

    renderer->SetFramebuffer(m_frameBuffer, GraphicBitFields::AllBits);
    renderer->SetCamera(m_params.Camera, true);
  }

  void StencilRenderPass::PostRender()
  {
    Pass::PostRender();
    GetRenderer()->EndPass();
  }

} // namespace ToolKit