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
#include "GpuProgram.h"
#include "Material.h"
#include "MathUtil.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "Shader.h"
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

  void Pass::GatherRequirements(PassRequirements& reqs)
  {
    // Default: no-op. Subclasses override to populate state specific to their draw.
    (void) reqs;
  }

  void Pass::ApplyRequirements(Renderer* renderer)
  {
    // Step 1: shader defines (before program build, so defines are baked into SPIR-V/GLSL).
    if (m_requirements.fragmentShader && !m_requirements.defines.empty())
    {
      for (const auto& [key, val] : m_requirements.defines)
      {
        m_requirements.fragmentShader->SetDefine(key, val);
      }
    }

    // Step 2: program — use the pre-built one or create from shaders via manager.
    GpuProgramPtr program = m_requirements.program;
    if (program == nullptr)
    {
      ShaderPtr frag = m_requirements.fragmentShader;
      assert(frag != nullptr && "PassRequirements must supply a fragment shader or a program.");

      // Vertex shader is owned by the material/quad pass. Pull it from m_program if set
      // (subclass convention) or use the default fullQuad vert.
      ShaderPtr vert = nullptr;
      if (m_program != nullptr && !m_program->m_shaders.empty())
      {
        vert = m_program->m_shaders.front();
      }

      if (vert == nullptr)
      {
        vert = GetShaderManager()->Create<Shader>(ShaderPath("fullQuadVert.shader", true));
      }

      program = renderer->GetGpuProgramManager()->CreateProgram(vert, frag);
    }
    m_program = program;
    renderer->BindProgram(program);

    // Step 3: framebuffer + clear.
    if (m_requirements.frameBuffer != nullptr)
    {
      renderer->SetFramebuffer(m_requirements.frameBuffer, m_requirements.clearBits, m_requirements.clearColor);
    }

    // Step 4: passive pipeline state.
    renderer->SetPassState(m_requirements.passState);

    // Step 5: scissor (optional).
    if (m_requirements.scissorEnabled)
    {
      renderer->SetScissor(m_requirements.scissor.x,
                           m_requirements.scissor.y,
                           m_requirements.scissor.z,
                           m_requirements.scissor.w);
    }

    // Step 6: custom UBOs (pass-specific). Mark m_invalid on backend side, then Map.
    for (auto& [slot, ubo] : m_requirements.customUbos)
    {
      if (ubo != nullptr)
      {
        renderer->BindUniformBuffer(slot, ubo);
      }
    }

    // Step 7a: textures bound by slot number directly.
    for (auto& [slot, tex] : m_requirements.textures)
    {
      if (tex != nullptr)
      {
        renderer->SetTexture((ubyte) slot, tex);
      }
    }

    // Step 7b: textures bound by semantic name. Program is already bound — slot lookup
    // is guaranteed to succeed. This is the path that fixes the SSAO bug: previously
    // SetTexture("s_normalDepth", ...) was called BEFORE the program was bound and
    // silently no-op'd because m_currentProgram was null.
    for (auto& [name, tex] : m_requirements.semanticTextures)
    {
      if (tex != nullptr)
      {
        renderer->SetTexture(name.c_str(), tex);
      }
    }
  }

  Renderer* Pass::GetRenderer() { return m_renderer; }

  void Pass::SetRenderer(Renderer* renderer) { m_renderer = renderer; }

  void RenderJobProcessor::CreateRenderJobs(RenderJobArray& jobArray,
                                            EntityRawPtrArray& entities,
                                            bool ignoreVisibility,
                                            int dirLightEndIndex,
                                            const LightRawPtrArray& lights,
                                            const EnvironmentComponentPtrArray& environments)
  {
    struct EntityRenderCache
    {
      Entity* entity                   = nullptr;
      MeshComponent* meshComp          = nullptr;
      MaterialComponent* matComp       = nullptr;
      SkeletonComponent* skelComp      = nullptr;
      const MeshRawPtrArray* allMeshes = nullptr;
      MaterialPtrArray* materialList   = nullptr;
      Mat4 worldTransform;
      BoundingBox bbox;
      bool cullFlip     = false;
      bool shadowCaster = true;
      bool hasAnimData  = false;
      AnimData animData;
    };

    IntArray submeshIndexLookup;
    submeshIndexLookup.reserve(entities.size());

    std::vector<EntityRenderCache> cache;
    cache.reserve(entities.size());

    int size = 0;

    // Single-pass filter + component cache.
    // All component lookups and mesh queries happen here once per entity.
    for (Entity* ntt : entities)
    {
      if (!ntt->IsVisible() && !ignoreVisibility)
        continue;

      MeshComponent* meshComp = ntt->GetComponentFast<MeshComponent>();
      if (meshComp == nullptr)
        continue;

      meshComp->Init(false);

      const MeshPtr& parentMesh = meshComp->GetMeshVal();
      int meshCount             = parentMesh->GetMeshCount();
      if (meshCount == 0)
        continue;

      submeshIndexLookup.push_back(size);
      size += meshCount;

      EntityRenderCache rd;
      rd.entity         = ntt;
      rd.meshComp       = meshComp;
      rd.allMeshes      = &parentMesh->GetAllMeshes();
      rd.worldTransform = ntt->m_node->GetTransform();
      rd.bbox           = ntt->GetBoundingBox(true);
      rd.cullFlip       = ntt->m_node->RequireCullFlip();
      rd.shadowCaster   = meshComp->GetCastShadowVal();

      if (MaterialComponent* matComp = ntt->GetComponentFast<MaterialComponent>())
      {
        rd.matComp      = matComp;
        rd.materialList = &matComp->GetMaterialList();
      }

      if (SkeletonComponent* skComp = ntt->GetComponentFast<SkeletonComponent>())
      {
        rd.skelComp    = skComp;
        rd.animData    = skComp->GetAnimData();
        rd.hasAnimData = true;
      }

      cache.push_back(std::move(rd));
    }

    entities.clear();
    entities.reserve(cache.size());
    for (const EntityRenderCache& rd : cache)
    {
      entities.push_back(rd.entity);
    }

    if (entities.empty())
    {
      jobArray.clear();
      return;
    }

    if (jobArray.size() != size)
    {
      jobArray.clear();
      jobArray.resize(size);
    }
    else
    {
      for (RenderJob& job : jobArray)
      {
        job.lights.clear();
      }
    }

    using poolstl::iota_iter;
    std::for_each(TKExecByConditional(entities.size() > 1000, WorkerManager::FramePool),
                  iota_iter<size_t>(0),
                  iota_iter<size_t>(entities.size()),
                  [&](size_t nttIndex)
                  {
                    const EntityRenderCache& rd = cache[nttIndex];
                    Entity* ntt                 = rd.entity;

                    const int meshCount         = static_cast<int>(rd.allMeshes->size());
                    const int jobBase           = submeshIndexLookup[nttIndex];

                    // Lights and environment are identical for all submeshes of the same entity.
                    RenderJob& firstJob         = jobArray[jobBase];
                    firstJob.BoundingBox        = rd.bbox;
                    AssignLight(firstJob, lights, dirLightEndIndex);
                    AssignEnvironment(firstJob, environments);

                    for (int subMeshIndx = 0; subMeshIndx < meshCount; subMeshIndx++)
                    {
                      Mesh* mesh           = (*rd.allMeshes)[subMeshIndx];
                      MaterialPtr material = nullptr;

                      if (rd.materialList != nullptr && subMeshIndx < (int) rd.materialList->size())
                      {
                        material = (*rd.materialList)[subMeshIndx];
                      }

                      if (material == nullptr && mesh->m_material)
                      {
                        material = mesh->m_material;
                      }

                      if (material == nullptr)
                      {
                        material = GetMaterialManager()->GetDefaultMaterial();
                        TK_WRN("Material component for entity: \"%s\" has less material than mesh count. Default "
                               "material used for meshes with missing material.",
                               ntt->GetNameVal().c_str());
                      }

                      RenderJob& job      = jobArray[jobBase + subMeshIndx];
                      job.Entity          = ntt;
                      job.Mesh            = mesh;
                      job.Material        = material.get();
                      job.requireCullFlip = rd.cullFlip;
                      job.ShadowCaster    = rd.shadowCaster;
                      job.WorldTransform  = rd.worldTransform;
                      job.BoundingBox     = rd.bbox;

                      if (rd.hasAnimData)
                      {
                        job.animData = rd.animData;
                      }

                      if (subMeshIndx > 0)
                      {
                        job.lights                     = firstJob.lights;
                        job.EnvironmentVolume          = firstJob.EnvironmentVolume;
                        job.SecondaryEnvironmentVolume = firstJob.SecondaryEnvironmentVolume;
                      }
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
    BoundingBox secondBestBox;
    job.EnvironmentVolume          = nullptr;
    job.SecondaryEnvironmentVolume = nullptr;

    // Assign the two smallest local (non-sky) volumes that intersect the object.
    for (const EnvironmentComponentPtr& volume : environments)
    {
      if (!volume->GetIlluminateVal())
      {
        continue;
      }

      // Sky is handled globally as fallback, skip it here.
      const EntityPtr& owner = volume->OwnerEntity();
      if (owner != nullptr && owner->IsA<SkyBase>())
      {
        continue;
      }

      const BoundingBox& vbb = volume->GetBoundingBox();
      if (BoxBoxIntersection(vbb, job.BoundingBox) != IntersectResult::Outside)
      {
        if (job.EnvironmentVolume == nullptr || vbb.Volume() < bestBox.Volume())
        {
          // Current best becomes secondary.
          if (job.EnvironmentVolume != nullptr)
          {
            secondBestBox                  = bestBox;
            job.SecondaryEnvironmentVolume = job.EnvironmentVolume;
          }

          bestBox               = vbb;
          job.EnvironmentVolume = volume.get();
        }
        else if (job.SecondaryEnvironmentVolume == nullptr || vbb.Volume() < secondBestBox.Volume())
        {
          secondBestBox                  = vbb;
          job.SecondaryEnvironmentVolume = volume.get();
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
