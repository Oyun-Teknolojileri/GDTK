/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "FullQuadPass.h"

#include "Camera.h"
#include "Material.h"
#include "Mesh.h"
#include "Shader.h"
#include "ToolKit.h"

#include <DebugNew.h>

namespace ToolKit
{

  FullQuadPass::FullQuadPass() : Pass("FullQuadPass")
  {
    m_quad           = MakeNewPtr<Quad>();

    m_material       = MakeNewPtr<Material>();
    ShaderPtr shader = GetShaderManager()->Create<Shader>(ShaderPath("fullQuadVert.shader", true));
    m_material->SetVertexShaderVal(shader);
  }

  void FullQuadPass::Render()
  {
    Renderer* renderer = GetRenderer();
    renderer->SetFramebuffer(m_params.frameBuffer, m_params.clearFrameBuffer);

    RenderJobArray jobs;
    RenderJobProcessor::CreateRenderJobs(jobs, m_quad);
    renderer->Render(jobs);
  }

  void FullQuadPass::PreRender()
  {
    // Gpu Program should be bound before calling FulQuadPass Render

    Pass::PreRender();
    Renderer* renderer = GetRenderer();
    renderer->EnableDepthTest(false);

    MeshComponentPtr mc = m_quad->GetMeshComponent();
    MeshPtr mesh        = mc->GetMeshVal();
    mesh->m_material    = m_material;
    mesh->Init();

    m_material->GetRenderState()->blendFunction = m_params.blendFunc;
    SetFragmentShader(m_material->GetFragmentShaderVal(), renderer);
  }

  void FullQuadPass::PostRender()
  {
    Pass::PostRender();
    GetRenderer()->EnableDepthTest(true);
  }

  void FullQuadPass::SetFragmentShader(ShaderPtr fragmentShader, Renderer* renderer)
  {
    ShaderPtr frag = m_material->GetFragmentShaderVal();
    if (frag != fragmentShader)
    {
      m_material->SetFragmentShaderVal(fragmentShader);
      frag = fragmentShader;
    }

    ShaderPtr vert = m_material->GetVertexShaderVal();

    m_program      = GetGpuProgramManager()->CreateProgram(vert, frag);
    renderer->BindProgram(m_program);
  }

} // namespace ToolKit
