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
    m_copyStencilSubPass              = MakeNewPtr<FullQuadPass>();
    m_unlitFragShader                 = GetShaderManager()->Create<Shader>(ShaderPath("unlitFrag.shader", true));
    m_frameBuffer                     = MakeNewPtr<Framebuffer>("StencilPassFB");

    m_solidOverrideMaterial           = GetMaterialManager()->GetCopyOfUnlitColorMaterial();

    // Stencil-write passive defaults. Defaults for depthTest/depthWrite/depthFunction are
    // correct here. colorMaskEnabled is unimplemented in the Vulkan backend; the OpenGL path
    // takes the masked write, Vulkan keeps current behavior. Tracked separately.
    m_writePassState.stencilOperation = StencilOperation::AllowAllPixels;
    m_writePassState.colorMaskEnabled = false;
  }

  void StencilRenderPass::Render()
  {
    assert(m_params.RenderJobs != nullptr && "Stencil Render Pass Render Jobs Are Not Given!");

    Renderer* renderer = GetRenderer();

    // Stencil pass: write 1 to stencil, suppress color writes.
    renderer->SetPassState(m_writePassState);
    renderer->Render(*m_params.RenderJobs);

    // Copy pass: draw a fullscreen quad where stencil != 1. The stencil-op is now an explicit
    // FullQuadPass parameter rather than smuggled through the subpass material.
    m_copyStencilSubPass->SetFragmentShader(m_unlitFragShader, renderer);
    m_copyStencilSubPass->m_params.stencilOp = StencilOperation::AllowPixelsFailingStencil;

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
    GetRenderer()->FinishPass();
  }

} // namespace ToolKit