/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "BillboardPass.h"

#include "Entity.h"
#include "Material.h"

#include <DebugNew.h>

namespace ToolKit
{
  BillboardPass::BillboardPass() : Pass("BillboardPass") {}

  void BillboardPass::Render()
  {
    Renderer* renderer = GetRenderer();
    Viewport* vp       = m_params.Viewport;

    renderer->SetFramebuffer(vp->m_framebuffer, GraphicBitFields::None);
    CameraPtr cam = vp->GetCamera();
    renderer->SetCamera(cam, true);

    GpuProgramManager* gpuProgramManager = renderer->GetGpuProgramManager();

    auto renderBillboardsFn              = [this, cam, renderer, gpuProgramManager](EntityPtrArray& billboards,
                                                                       bool depthTest) -> void
    {
      m_renderData.jobs.clear();

      EntityRawPtrArray rawBillboards = ToEntityRawPtrArray(billboards);
      RenderJobProcessor::CreateRenderJobs(m_renderData.jobs, rawBillboards);
      RenderJobProcessor::SeperateRenderData(m_renderData, true);

      // depthTestEnabled is the only passive field that varies per billboard group.
      m_passState.depthTestEnabled = depthTest;
      renderer->SetPassState(m_passState);

      renderer->RenderWithProgramFromMaterial(m_renderData.jobs);
    };

    renderBillboardsFn(m_noDepthBillboards, false);
    renderBillboardsFn(m_params.Billboards, true);
  }

  void BillboardPass::PreRender()
  {
    Pass::PreRender();

    // Process billboards.
    float vpScale = m_params.Viewport->GetBillboardScale();
    CameraPtr cam = m_params.Viewport->GetCamera();
    m_noDepthBillboards.clear();

    // Separate functions that does not require depth test.
    move_values(m_params.Billboards,
                m_noDepthBillboards,
                [this, vpScale, cam](EntityPtr bb) -> bool
                {
                  // Update billboards.
                  BillboardPtr cbb = Cast<Billboard>(bb);
                  cbb->LookAt(cam, vpScale);

                  // Return separation condition.
                  return cbb->m_settings.bypassDepthTest;
                });
  }

  void BillboardPass::PostRender()
  {
    Pass::PostRender();
    GetRenderer()->FinishPass();
  }

} // namespace ToolKit
