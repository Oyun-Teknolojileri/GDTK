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
#include "FullQuadPass.h"
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

      /** Frames in a row the depth grid found this entity hidden. Kept here rather than in a map of
       *  its own because it has the same lifetime as the range: a rebuilt range means the entity
       *  moved or was replaced, and a moved entity starts its count over. */
      int occlusionStreak = 0;
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

    /** Signature of the set of lights the frame was built with. A different set - the editor switching
     *  between its own lights and the scene ones, a light added or removed - invalidates every object's
     *  light assignment, because the list they were assigned from no longer exists. */
    uint64 m_lightSetSignature = ~0ull;

    /** Entities whose jobs have to be rebuilt every frame because their geometry moves without the
     *  entity moving: a skinned mesh deforms, an animated one can be driven through its clip. Their
     *  ranges are rebuilt while everything around them stays cached. */
    std::unordered_set<const Entity*> m_animatedJobEntities;

    //////////////////////////////////////////
    // Depth grid occlusion culling
    //
    // A coarse grid of the farthest depth per block is built from the depth buffer the previous frame
    // left behind, before the passes of this frame overwrite it. Anything the grid proves is behind the
    // geometry already drawn contributes nothing to the picture, so it is dropped from the frame: a local
    // light leaves it entirely - no atlas slot and no shadow map - and a mesh is left out of the visible
    // job list, so it is neither culled, batched, nor drawn. The grid is a frame behind by construction,
    // so a light or a mesh has to stay hidden for a few frames before it is dropped, and a camera that
    // moves invalidates it.

    /** Reduces the depth buffer to one value per block and reads it back every few frames. */
    void BuildDepthGrid(Renderer* renderer);

    /** Drops the local lights the grid proves are occluded. */
    void CullOccludedLights(LightRawPtrArray& lights);

    /** Drops the entities the grid proves are occluded, from this frame's visible entity list. */
    void CullOccludedEntities(EntityRawPtrArray& entities);

    /** Remembers the camera the current grid was rendered with, for the next frame to project into. */
    void StoreOcclusionCamera();

    /**
     * True when the grid in hand holds depths that were reduced from the view this frame draws. A camera
     * change drops the grid and every streak, since a grid from another view proves nothing about this
     * one, and the frame after a change culls nothing.
     */
    bool OcclusionCullingReady();

    /**
     * Projects the world space box and tests the grid blocks it can cover. The volume counts as occluded
     * when every one of those blocks is entirely nearer than the volume's nearest point, which the caller
     * passes because the box can only ever enclose the volume it is asked about: a light's nearest point
     * is on its sphere, not on a corner of the box around it.
     * @param boxMin is the minimum corner of the world space box.
     * @param boxMax is the maximum corner of the world space box.
     * @param nearestDepth is the nearest distance of the volume from the camera along the view direction.
     * @return True only when the volume provably contributes no pixel to the frame.
     */
    bool IsOccludedByGrid(const Vec3& boxMin, const Vec3& boxMax, float nearestDepth) const;

    /** Same test for a box, using the box's own nearest point as the volume's. */
    bool IsOccludedByGrid(const BoundingBox& box) const;

    /** Drops the grid and every streak: whatever the grid proved about the last frame is now worthless. */
    void InvalidateDepthGrid();

    /** Clears the per entity streak of every cached range. */
    void ResetEntityOcclusionStreaks();

    RenderTargetPtr m_depthGridRt                        = nullptr;
    FramebufferPtr m_depthGridFb                         = nullptr;
    FullQuadPassPtr m_depthGridQuad                      = nullptr;
    ShaderPtr m_depthGridShader                          = nullptr;

    /** Row major max depth per block, as read back from the grid. */
    std::vector<float> m_depthGrid;
    /** Read back pixels of the grid, four floats per block. Kept so the read does not allocate. */
    std::vector<float> m_depthGridScratch;
    UVec2 m_depthGridSize                                = UVec2(0);
    bool m_depthGridValid                                = false;
    uint m_depthGridFrame                                = 0;

    Mat4 m_occlusionViewProj                             = Mat4(1.0f);
    Mat4 m_occlusionView                                 = Mat4(1.0f);
    Mat4 m_occlusionProjection                          = Mat4(1.0f);
    bool m_occlusionReversedDepth                        = false;
    float m_occlusionNear                                = 0.1f;
    float m_occlusionFar                                 = 1000.0f;

    /** Camera the grid in m_depthGrid was rendered with. A grid from another view says nothing about
     *  this one, so culling waits for the grid to catch up after a camera change. */
    uint64 m_occlusionCameraId                           = 0;

    /** Set by the frame that changed what is on screen - a moved camera, an entity that moved, a scene
     *  that was edited - and read by the next one, which then re-reads the grid instead of waiting for
     *  the read interval: a mesh behind an occluder that just moved away would otherwise stay dropped
     *  for up to DepthGridReadInterval frames. */
    bool m_sceneChangedLastFrame                         = false;

    /** Last culling decision that was logged, so the log reports changes instead of every frame. */
    uint64 m_loggedCullCameraId                          = 0;
    int m_loggedCullCount                                = -1;
    int m_loggedCulledEntityCount                        = -1;

    /** Consecutive frames each light was found hidden behind the grid. */
    std::unordered_map<uint64, int> m_lightOcclusionStreak;
  };

} // namespace ToolKit
