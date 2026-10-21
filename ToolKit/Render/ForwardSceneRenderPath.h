/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "BloomPass.h"
#include "CubemapPass.h"
#include "DofPass.h"
#include "EngineSettings.h"
#include "ForwardPass.h"
#include "ForwardPreProcessPass.h"
#include "GammaTonemapFxaaPass.h"
#include "Pass.h"
#include "RenderSystem.h"
#include "ShadowPass.h"
#include "SsaoPass.h"
#include "SsrPass.h"

namespace ToolKit
{

  struct TK_API SceneRenderPathParams
  {
    SceneRenderPathParams()
    {
      MainFramebuffer     = nullptr;
      Cam                 = nullptr;
      Scene               = nullptr;
      grid                = nullptr;
      postProcessSettings = MakeNewPtr<PostProcessingSettings>();
    }

    LightPtrArray overrideLights;
    ScenePtr Scene;
    CameraPtr Cam;
    FramebufferPtr MainFramebuffer;
    EntityPtr grid;
    PostProcessingSettingsPtr postProcessSettings;
  };

  /** Forward scene render path. All objects are drawn in forward manner. Bandwidth optimized. */
  class TK_API ForwardSceneRenderPath : public RenderPath
  {
   public:
    ForwardSceneRenderPath();
    virtual ~ForwardSceneRenderPath();

    void Render(Renderer* renderer) override;
    void PreRender(Renderer* renderer) override;
    void PostRender(Renderer* renderer) override;

   protected:
    void SetPassParams(Renderer* renderer);
    bool RequiresForwardPreProcessPass();

   public:
    SceneRenderPathParams m_params;

   public:
    ShadowPassPtr m_shadowPass                       = nullptr;
    ForwardRenderPassPtr m_forwardRenderPass         = nullptr;
    ForwardPreProcessPassPtr m_forwardPreProcessPass = nullptr;
    CubeMapPassPtr m_skyPass                         = nullptr;
    SSAOPassPtr m_ssaoPass                           = nullptr;
    SsrPassPtr m_ssrPass                             = nullptr;
    BloomPassPtr m_bloomPass                         = nullptr;
    DoFPassPtr m_dofPass                             = nullptr;
    GammaTonemapFxaaPassPtr m_gammaTonemapFxaaPass   = nullptr;
    FramebufferPtr m_resolvedFramebuffer             = nullptr;

   protected:
    bool m_drawSky   = false;
    SkyBasePtr m_sky = nullptr;

    // Cached variables
    RenderData m_renderData;

    /**
     * Render jobs built once for the whole scene and reused until the scene's cache version moves.
     *
     * Building them is 1.8 ms on a city, 82 percent of SetPassParams, and it does not depend on
     * the camera: only which of them are visible does. The version covers entity add / remove and
     * spatial invalidation, which is how a transform or mesh change reports itself.
     */
    RenderJobArray m_cachedJobs;
    uint64 m_jobCacheVersion = ~0ull;

    /** Half open [begin, end) range of m_cachedJobs that belongs to an entity, with the spatial
     *  generation the jobs were built at. CreateRenderJobs lays the submeshes of one entity out
     *  contiguously, so this maps the entities the AABB tree query returns onto the cached jobs
     *  without a frustum test per job, and it is what makes a rebuild per entity possible. */
    struct JobSlot
    {
      uint64 generation      = 0;
      uint64 lightGeneration = 0;
      uint begin             = 0;
      uint end               = 0;
    };
    std::unordered_map<const Entity*, JobSlot> m_jobRanges;

    /** Scratch jobs for the entities that changed since the last frame. */
    RenderJobArray m_dirtyJobs;
    EntityRawPtrArray m_dirtyEntities;

    /** Entities whose light list is stale because a light moved over them, without any change to
     *  the entities themselves. */
    EntityRawPtrArray m_lightDirtyEntities;

    /** Signature of the positions the cached per object light and environment assignment was built
     *  with. It moves when a light or an environment volume does, and only then is the assignment
     *  recomputed: doing that every frame measured 3.5 ms on the city. */
    uint64 m_assignmentSignature = ~0ull;

    /** Entities whose jobs have to be rebuilt every frame because their geometry moves without the
     *  entity moving: a skinned mesh deforms, an animated one can be driven through its clip. Their
     *  ranges are rebuilt while everything around them stays cached. */
    std::unordered_set<const Entity*> m_animatedJobEntities;
  };

} // namespace ToolKit
