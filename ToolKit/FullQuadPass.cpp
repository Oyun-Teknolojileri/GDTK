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

    // Fullscreen quad passive defaults: write nothing to depth, accept every fragment.
    m_passState.depthTestEnabled  = false;
    m_passState.depthWriteEnabled = false;
    m_passState.depthFunction     = CompareFunctions::FuncAlways;
  }

  void FullQuadPass::Render()
  {
    // Pure draw call. State was bound by ApplyRequirements in PreRender.
    Renderer* renderer = GetRenderer();
    RenderJobArray jobs;
    RenderJobProcessor::CreateRenderJobs(jobs, m_quad);
    renderer->Render(jobs);
  }

  void FullQuadPass::PreRender()
  {
    // Gpu Program should be bound before calling FulQuadPass Render
    Pass::PreRender();
    Renderer* renderer  = GetRenderer();

    MeshComponentPtr mc = m_quad->GetMeshComponent();
    MeshPtr mesh        = mc->GetMeshVal();
    mesh->m_material    = m_material;
    mesh->Init();

    m_material->blendFunction = m_params.blendFunc;
    SetFragmentShader(m_material->GetFragmentShaderVal(), renderer);

    // Build the declarative requirements for the upcoming draw. Only fill in fields the
    // subclass didn't already populate — caller (e.g. SSAOPass) injects customUbos /
    // semanticTextures through m_requirements before RenderSubPass runs.
    GatherRequirements(m_requirements);

    // Apply: program/framebuffer/state/UBOs/textures bind in correct order.
    ApplyRequirements(renderer);
  }

  void FullQuadPass::GatherRequirements(PassRequirements& reqs)
  {
    // Default merge: pull vert/frag/program/framebuffer/state from the quad's own state.
    // Caller can override any of these by populating m_requirements first.
    if (reqs.fragmentShader == nullptr)
    {
      reqs.fragmentShader = m_material->GetFragmentShaderVal();
    }
    if (reqs.vertexShader == nullptr)
    {
      reqs.vertexShader = m_material->GetVertexShaderVal();
    }
    if (reqs.program == nullptr)
    {
      reqs.program = m_program;
    }
    if (reqs.frameBuffer == nullptr)
    {
      reqs.frameBuffer = m_params.frameBuffer;
      reqs.clearBits   = m_params.clearFrameBuffer;
    }

    // Re-derive passive state from defaults + the caller's stencil op. Always overlay our
    // depth-off / FuncAlways defaults so a caller can opt out per-field by changing
    // m_passState first.
    m_passState.stencilOperation = m_params.stencilOp;
    reqs.passState               = m_passState;

    // Inherit any textures the outer pass pushed into the material (e.g. diffuse slot 0).
    if (m_material->GetDiffuseTextureVal() != nullptr && reqs.textures.find(0) == reqs.textures.end())
    {
      reqs.textures[0] = m_material->GetDiffuseTextureVal();
    }
  }

  void FullQuadPass::PostRender()
  {
    Pass::PostRender();
    Renderer* renderer = GetRenderer();
    renderer->FinishPass();
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

    m_program      = renderer->GetGpuProgramManager()->CreateProgram(vert, frag);
    renderer->BindProgram(m_program);
  }

} // namespace ToolKit
