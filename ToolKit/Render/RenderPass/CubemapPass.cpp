/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "CubemapPass.h"

#include "Material.h"
#include "Mesh.h"
#include "Stats.h"
#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{

  CubeMapPass::CubeMapPass() : Pass("CubeMapPass")
  {
    m_cube                    = MakeNewPtr<Cube>();

    // Skybox passive default. Defaults for depthTest (on) and depthWrite (on) are fine; the
    // cube renders before any other geometry so writing the far plane is harmless.
    m_passState.depthFunction = CompareFunctions::FuncLequal;
  }

  void CubeMapPass::Render()
  {
    TK_PROFILE_FUNCTION();

    Renderer* renderer            = GetRenderer();

    // Drive the draw through the standard PassRequirements flow. This way the slot-7
    // customUbos entry (e.g. the gradient-skybox UBO staged by the sky callback) gets
    // bound into the descriptor set on Vulkan, instead of being silently ignored by
    // the old RenderWithProgramFromMaterial path which never touched customUbos.
    //
    // CubeMapPass owns no material of its own — the skybox material lives on the cube
    // entity. Pull vert/frag/program from it so ApplyRequirements has the program
    // identity (and so the descriptor set knows what samplers the cube's draw expects).
    MaterialPtr skyMaterial       = m_cube->GetMaterialComponent()->GetFirstMaterial();
    m_requirements.fragmentShader = skyMaterial ? skyMaterial->GetFragmentShaderVal() : nullptr;
    m_requirements.vertexShader   = skyMaterial ? skyMaterial->GetVertexShaderVal() : nullptr;
    m_requirements.program        = nullptr;
    m_requirements.frameBuffer    = m_params.FrameBuffer;
    m_requirements.clearBits      = m_params.clearBuffer;
    m_requirements.passState      = m_passState;

    RenderJobArray jobs;
    RenderJobProcessor::CreateRenderJobs(jobs, m_cube);

    ApplyRequirements(renderer);
    renderer->Render(jobs);
  }

  void CubeMapPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    m_cube->m_node->SetTransform(m_params.Transform);

    MaterialComponentPtr matCom = m_cube->GetMaterialComponent();
    matCom->SetFirstMaterial(m_params.Material);

    Renderer* renderer = GetRenderer();
    renderer->SetCamera(m_params.Cam, false);

    if (m_params.onPreRender)
    {
      m_params.onPreRender();
    }
  }

  void CubeMapPass::PostRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PostRender();
  }

} // namespace ToolKit