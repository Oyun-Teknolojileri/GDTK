/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "GizmoPass.h"

#include <AABBOverrideComponent.h>
#include <Material.h>
#include <Mesh.h>

namespace ToolKit
{
  namespace Editor
  {

    GizmoPass::GizmoPass() : Pass("GizmoPass")
    {
      m_depthMaskSphere = MakeNewPtr<Sphere>();
      m_depthMaskSphere->SetRadiusVal(0.95f);

      MeshComponentPtr mc                   = m_depthMaskSphere->GetMeshComponent();
      MeshPtr mesh                          = mc->GetMeshVal();
      mesh->m_material->cullMode            = CullingType::Front;

      // Passive overrides used by the two non-default sub-draws (depth-mask sphere, guide meshes).
      // Both inherit the rest from the default RenderState construction; only the highlighted
      // field is the meaningful change in each case.
      m_depthMaskPassState.colorMaskEnabled = false;
      m_guideMeshPassState.depthFunction    = CompareFunctions::FuncAlways;
    }

    GizmoPass::GizmoPass(const GizmoPassParams& params) : GizmoPass() { m_params = params; }

    void GizmoPass::Render()
    {
      Renderer* renderer                   = GetRenderer();
      GpuProgramManager* gpuProgramManager = renderer->GetGpuProgramManager();

      for (EditorBillboardPtr billboard : m_params.GizmoArray)
      {
        if (billboard->GetBillboardType() == EditorBillboardBase::BillboardType::Rotate)
        {
          Mat4 ts = billboard->m_node->GetTransform();
          m_depthMaskSphere->m_node->SetTransform(ts, TransformationSpace::TS_WORLD);

          // Depth-mask sphere: write only depth, no color.
          RenderJobArray jobs;
          RenderJobProcessor::CreateRenderJobs(jobs, m_depthMaskSphere);
          renderer->SetPassState(m_depthMaskPassState);
          renderer->RenderWithProgramFromMaterial(jobs);

          jobs.clear();
          RenderJobProcessor::CreateRenderJobs(jobs, billboard);

          Gizmo* gizmo = static_cast<Gizmo*>(billboard.get());
          if (!gizmo->m_noDepthMeshes.empty())
          {
            RenderJobArray guideJobs;
            RenderJobArray normalJobs;
            for (const RenderJob& job : jobs)
            {
              if (contains(gizmo->m_noDepthMeshes, job.Mesh->GetIdVal()))
              {
                guideJobs.push_back(job);
              }
              else
              {
                normalJobs.push_back(job);
              }
            }

            // Reset passive state to defaults before drawing the normal jobs — the depth-mask
            // pass left colorMaskEnabled=false in m_passiveState.
            renderer->SetPassState(m_defaultPassState);
            renderer->RenderWithProgramFromMaterial(normalJobs);

            // Guide meshes draw on top regardless of depth.
            renderer->SetPassState(m_guideMeshPassState);
            renderer->RenderWithProgramFromMaterial(guideJobs);
          }
          else
          {
            renderer->SetPassState(m_defaultPassState);
            renderer->RenderWithProgramFromMaterial(jobs);
          }
        }
        else
        {
          RenderJobArray jobs;
          RenderJobProcessor::CreateRenderJobs(jobs, billboard);
          renderer->SetPassState(m_defaultPassState);
          renderer->RenderWithProgramFromMaterial(jobs);
        }
      }
    }

    void GizmoPass::PreRender()
    {
      Pass::PreRender();
      Renderer* renderer = GetRenderer();

      m_camera           = m_params.Viewport->GetCamera();
      renderer->SetFramebuffer(m_params.Viewport->m_framebuffer, GraphicBitFields::DepthBits);
      renderer->SetCamera(m_camera, true);

      // Update.
      BillboardPtrArray& gizmoArray = m_params.GizmoArray;
      gizmoArray.erase(std::remove_if(gizmoArray.begin(),
                                      gizmoArray.end(),
                                      [this](EditorBillboardPtr bb) -> bool
                                      {
                                        if (bb == nullptr)
                                        {
                                          return true;
                                        }

                                        bb->LookAt(m_camera, m_params.Viewport->GetBillboardScale());
                                        return false;
                                      }),
                       gizmoArray.end());
    }

    void GizmoPass::PostRender() { Pass::PostRender(); }

  } // namespace Editor
} // namespace ToolKit