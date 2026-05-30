/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
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
    m_cube                        = MakeNewPtr<Cube>();

    // Skybox passive default. Defaults for depthTest (on) and depthWrite (on) are fine; the
    // cube renders before any other geometry so writing the far plane is harmless.
    m_passState.depthFunction     = CompareFunctions::FuncLequal;
  }

  void CubeMapPass::Render()
  {
    TK_PROFILE_FUNCTION();

    Renderer* renderer = GetRenderer();
    renderer->SetFramebuffer(m_params.FrameBuffer, m_params.clearBuffer);

    RenderJobArray jobs;
    RenderJobProcessor::CreateRenderJobs(jobs, m_cube);

    renderer->SetPassState(m_passState);
    renderer->RenderWithProgramFromMaterial(jobs);
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
    GetRenderer()->FinishPass();
  }

} // namespace ToolKit