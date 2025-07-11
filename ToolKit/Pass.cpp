/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Pass.h"

#include "AABBOverrideComponent.h"
#include "Camera.h"
#include "DirectionComponent.h"
#include "Material.h"
#include "MathUtil.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "Threads.h"
#include "ToolKit.h"
#include "Viewport.h"

#include "DebugNew.h"

namespace ToolKit
{

  Pass::Pass(StringView name) : m_name(name) {}

  Pass::~Pass() {}

  void Pass::PreRender() { Stats::BeginGpuScope(m_name); }

  void Pass::PostRender() { Stats::EndGpuScope(); }

  void Pass::RenderSubPass(const PassPtr& pass)
  {
    Renderer* renderer = GetRenderer();
    pass->SetRenderer(renderer);
    pass->PreRender();
    pass->Render();
    pass->PostRender();
  }

  Renderer* Pass::GetRenderer() { return m_renderer; }

  void Pass::SetRenderer(Renderer* renderer) { m_renderer = renderer; }

  void Pass::UpdateUniform(const ShaderUniform& shaderUniform)
  {
    if (m_program != nullptr)
    {
      m_program->UpdateCustomUniform(shaderUniform);
    }
  }

  void RenderJobProcessor::CreateRenderJobs(RenderJobArray& jobArray,
                                            EntityRawPtrArray& entities,
                                            bool ignoreVisibility,
                                            int dirLightEndIndex,
                                            const LightRawPtrArray& lights,
                                            const EnvironmentComponentPtrArray& environments)
  {
    // Each entity can contain several meshes. This submeshIndexLookup array will be used
    // to find the index of the submesh for a given entity index.
    // Ex: Entity index is 4 and it has 3 submesh,
    // its submesh indexes would be = {4, 5, 6}
    // to look them up: {nttIndex + 0, nttIndex + 1, nttIndex + 3} formula is used.
    IntArray submeshIndexLookup;
    int size = 0;

    // Apply ntt visibility check.
    erase_if(entities,
             [&](Entity* ntt) -> bool
             {
               if (ntt->IsVisible() || ignoreVisibility)
               {
                 if (MeshComponent* meshComp = ntt->GetComponentFast<MeshComponent>())
                 {
                   meshComp->Init(false);
                   submeshIndexLookup.push_back(size);
                   size += meshComp->GetMeshVal()->GetMeshCount();
                   return false;
                 }
               }

               return true;
             });

    jobArray.clear();
    jobArray.resize(size); // at least.

    if (entities.empty())
    {
      return;
    }

    // Construct jobs.
    using poolstl::iota_iter;
    std::for_each(TKExecByConditional(entities.size() > 1000, WorkerManager::FramePool),
                  iota_iter<size_t>(0),
                  iota_iter<size_t>(entities.size()),
                  [&](size_t nttIndex)
                  {
                    Entity* ntt                    = entities[nttIndex];
                    MaterialPtrArray* materialList = nullptr;
                    if (MaterialComponent* matComp = ntt->GetComponentFast<MaterialComponent>())
                    {
                      materialList = &matComp->GetMaterialList();
                    }

                    MeshComponent* meshComp   = ntt->GetComponentFast<MeshComponent>();
                    const MeshPtr& parentMesh = meshComp->GetMeshVal();

                    MeshRawPtrArray allMeshes;
                    parentMesh->GetAllMeshes(allMeshes);

                    bool cullFlip  = ntt->m_node->RequireCullFlip();
                    Mat4 transform = ntt->m_node->GetTransform();
                    for (int subMeshIndx = 0; subMeshIndx < (int) allMeshes.size(); subMeshIndx++)
                    {
                      Mesh* mesh           = allMeshes[subMeshIndx];
                      MaterialPtr material = nullptr;

                      // Pick the material for submesh.
                      if (materialList != nullptr)
                      {
                        if (subMeshIndx < materialList->size())
                        {
                          material = (*materialList)[subMeshIndx];
                        }
                      }

                      // if material is still null, pick from mesh.
                      if (material == nullptr)
                      {
                        if (mesh->m_material)
                        {
                          material = mesh->m_material;
                        }
                      }

                      // Worst case, no material found pick a copy of default.
                      if (material == nullptr)
                      {
                        material = GetMaterialManager()->GetDefaultMaterial();
                        TK_WRN("Material component for entity: \"%s\" has less material than mesh count. Default "
                               "material used for meshes with missing material.",
                               ntt->GetNameVal().c_str());
                      }

                      // Translate nttIndex to corresponding job index.
                      int jobIndex        = submeshIndexLookup[nttIndex] + subMeshIndx;

                      RenderJob& job      = jobArray[jobIndex];
                      job.Entity          = ntt;
                      job.Mesh            = mesh;
                      job.Material        = material.get();
                      job.requireCullFlip = cullFlip;
                      job.ShadowCaster    = meshComp->GetCastShadowVal();
                      job.WorldTransform  = ntt->m_node->GetTransform();
                      job.BoundingBox     = ntt->GetBoundingBox(true);

                      // Assign skeletal animations.
                      if (SkeletonComponent* skComp = ntt->GetComponentFast<SkeletonComponent>())
                      {
                        job.animData = skComp->GetAnimData(); // copy
                      }

                      // push directional lights.
                      AssignLight(job, lights, dirLightEndIndex);
                      AssignEnvironment(job, environments);
                    }
                  });
  }

  void RenderJobProcessor::CreateRenderJobs(RenderJobArray& jobArray, EntityPtr entity)
  {
    EntityRawPtrArray singleNtt = {entity.get()};
    CreateRenderJobs(jobArray, singleNtt, true);
  }

  void RenderJobProcessor::SeperateRenderData(RenderData& renderData, bool forwardOnly)
  {
    // Group culled.
    RenderJobItr beginItr   = renderData.jobs.begin();
    RenderJobItr forwardItr = beginItr;
    RenderJobItr translucentItr;
    RenderJobItr deferredAlphaMaskedItr;
    RenderJobItr forwardAlphaMaskedItr;

    if (!forwardOnly)
    {
      // Group opaque deferred - forward.
      forwardItr = std::partition(beginItr,
                                  renderData.jobs.end(),
                                  [](const RenderJob& job)
                                  { return !job.Material->IsShaderMaterial() && !job.Material->IsTranslucent(); });

      deferredAlphaMaskedItr =
          std::partition(beginItr, forwardItr, [](const RenderJob& job) { return !job.Material->IsAlphaMasked(); });
    }

    // Group translucent.
    translucentItr = std::partition(forwardItr,
                                    renderData.jobs.end(),
                                    [](const RenderJob& job) { return !job.Material->IsTranslucent(); });

    forwardAlphaMaskedItr =
        std::partition(forwardItr, translucentItr, [](const RenderJob& job) { return !job.Material->IsAlphaMasked(); });

    if (forwardOnly)
    {
      renderData.deferredJobsStartIndex            = -1;
      renderData.deferredAlphaMaskedJobsStartIndex = -1;
    }
    else
    {
      renderData.deferredJobsStartIndex = (int) std::distance(renderData.jobs.begin(), beginItr);
      renderData.deferredAlphaMaskedJobsStartIndex =
          (int) std::distance(renderData.jobs.begin(), deferredAlphaMaskedItr);
    }

    renderData.forwardOpaqueStartIndex          = (int) std::distance(renderData.jobs.begin(), forwardItr);
    renderData.forwardAlphaMaskedJobsStartIndex = (int) std::distance(renderData.jobs.begin(), forwardAlphaMaskedItr);
    renderData.forwardTranslucentStartIndex     = (int) std::distance(renderData.jobs.begin(), translucentItr);
  }

  void RenderJobProcessor::AssignLight(RenderJob& job, const LightRawPtrArray& lights, int startIndex)
  {
    // Add all directional lights.
    for (int i = 0; i < startIndex; i++)
    {
      job.lights.push_back(lights[i]);
      if (i >= RHIConstants::MaxLightsPerObject)
      {
        break;
      }
    }

    // No more lights to assign.
    if (lights.size() == job.lights.size())
    {
      // Possibly editor lighting. All directional lights assigned to job.
      return;
    }

    for (size_t i = startIndex; i < lights.size(); i++)
    {
      Light* light = lights[i];
      if (job.lights.size() >= RHIConstants::MaxLightsPerObject)
      {
        return;
      }

      if (light->GetLightType() == Light::LightType::Spot)
      {
        SpotLight* spot = static_cast<SpotLight*>(light);
        if (FrustumBoxIntersection(spot->m_frustumCache, job.BoundingBox) != IntersectResult::Outside)
        {
          job.lights.push_back(light);
        }
      }
      else
      {
        // The only light type that remains is point light.
        // lights must be presorted, check it.
        assert(light->IsA<PointLight>());
        PointLight* point = static_cast<PointLight*>(light);
        if (SphereBoxIntersection(point->m_boundingSphereCache, job.BoundingBox))
        {
          job.lights.push_back(light);
        }
      }
    }
  }

  int RenderJobProcessor::PreSortLights(LightRawPtrArray& lights)
  {
    auto dirEndItr =
        std::partition(lights.begin(),
                       lights.end(),
                       [](Light* light) -> bool { return light->GetLightType() == Light::LightType::Directional; });

    return (int) std::distance(lights.begin(), dirEndItr);
  }

  void RenderJobProcessor::SortByDistanceToCamera(RenderJobItr begin, RenderJobItr end, const CameraPtr& cam)
  {
    Vec3 camLoc = cam->m_node->GetTranslation(TransformationSpace::TS_WORLD);

    std::function<bool(const RenderJob&, const RenderJob&)> sortFn = [&camLoc](const RenderJob& j1,
                                                                               const RenderJob& j2) -> bool
    {
      const BoundingBox& bb1 = j1.BoundingBox;
      const BoundingBox& bb2 = j2.BoundingBox;

      float first            = glm::length2(bb1.GetCenter() - camLoc);
      float second           = glm::length2(bb2.GetCenter() - camLoc);

      return second < first;
    };

    if (cam->IsOrtographic())
    {
      sortFn = [cam](const RenderJob& j1, const RenderJob& j2) -> bool
      {
        float first  = glm::column(j1.WorldTransform, 3).z;
        float second = glm::column(j2.WorldTransform, 3).z;
        return first < second;
      };
    }

    std::sort(begin, end, sortFn);
  }

  void RenderJobProcessor::SortByMaterial(RenderData& renderData)
  {
    auto sortRangeFn = [](RenderJobItr begin, RenderJobItr end) -> void
    {
      std::sort(begin,
                end,
                [](const RenderJob& a, const RenderJob& b) -> bool
                { return a.Material->GetIdVal() < b.Material->GetIdVal(); });
    };

    RenderJobItr begin, end;

    if (renderData.deferredJobsStartIndex != -1)
    {
      begin = renderData.GetDefferedBegin();
      end   = renderData.GetDeferredAlphaMaskedBegin();
      sortRangeFn(begin, end);

      begin = renderData.GetDeferredAlphaMaskedBegin();
      end   = renderData.GetForwardOpaqueBegin();
      sortRangeFn(begin, end);
    }

    begin = renderData.GetForwardOpaqueBegin();
    end   = renderData.GetForwardAlphaMaskedBegin();
    sortRangeFn(begin, end);

    begin = renderData.GetForwardAlphaMaskedBegin();
    end   = renderData.GetForwardTranslucentBegin();
    sortRangeFn(begin, end);

    begin = renderData.GetForwardTranslucentBegin();
    end   = renderData.jobs.end();
    sortRangeFn(begin, end);
  }

  void RenderJobProcessor::AssignEnvironment(RenderJob& job, const EnvironmentComponentPtrArray& environments)
  {
    BoundingBox bestBox;
    job.EnvironmentVolume = nullptr;
    for (const EnvironmentComponentPtr& volume : environments)
    {
      if (volume->GetIlluminateVal() == false)
      {
        continue;
      }

      // Pick the smallest volume intersecting with job.
      const BoundingBox& vbb = volume->GetBoundingBox();
      if (BoxBoxIntersection(vbb, job.BoundingBox) != IntersectResult::Outside)
      {
        if (bestBox.Volume() > vbb.Volume() || job.EnvironmentVolume == nullptr)
        {
          bestBox               = vbb;
          job.EnvironmentVolume = volume.get();
        }
      }
    }
  }

  void RenderJobProcessor::CalculateStdev(const RenderJobArray& rjVec, float& stdev, Vec3& mean)
  {
    int n = (int) rjVec.size();

    // Calculate mean position
    Vec3 sum(0.0f);
    for (int i = 0; i < n; i++)
    {
      Vec3 pos  = rjVec[i].WorldTransform[3];
      sum      += pos;
    }
    mean      = sum / (float) n;

    // Calculate standard deviation of position
    float ssd = 0.0f;
    for (int i = 0; i < n; i++)
    {
      Vec3 pos   = rjVec[i].WorldTransform[3];
      Vec3 diff  = pos - mean;
      ssd       += glm::dot(diff, diff);
    }
    stdev = std::sqrt(ssd / (float) n);
  }

  bool RenderJobProcessor::IsOutlier(const RenderJob& rj, float sigma, const float stdev, const Vec3& mean)
  {
    Vec3 pos   = rj.WorldTransform[3];
    Vec3 diff  = pos - mean;
    float dist = glm::length(diff) / stdev;

    return (dist > sigma);
  }

} // namespace ToolKit
