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
    };
    std::unordered_map<const Entity*, JobSlot> m_jobRanges;

    /**
     * Frames in a row the depth grid found an entity hidden.
     *
     * Keyed by entity and kept outside the job ranges on purpose: a range is a position in the cached job
     * array, so any rebuild of that array - a material edit, a prefab linked, an entity added - renumbers
     * every range while the entities themselves did not move a pixel. Holding the count in the slot reset
     * the whole scene's history on such a rebuild, and a cache that rebuilt every frame (the sky pass used
     * to bump the material revision each frame) could never reach the three frames in a row it takes to
     * drop anything. An entity that actually moved is erased from here by the rebuild that renumbers it.
     */
    std::unordered_map<const Entity*, int> m_entityOcclusionStreak;

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

    /**
     * Resolves a multisampled depth attachment into a single sample depth texture, the only form a
     * shader can read it in on GL ES 3.0 / WebGL 2, where a depth attachment that is multisampled is a
     * renderbuffer and there is no multisample sampler to fetch it with.
     * @param renderer is the renderer the blit goes through.
     * @param depthSource is the framebuffer whose depth attachment is resolved.
     * @param depthBuffer is that attachment, read for its size and its stencil flag.
     * @return The single sample depth texture, or null when the resolve is not possible.
     */
    TexturePtr ResolveDepthBuffer(Renderer* renderer, Framebuffer* depthSource, TexturePtr depthBuffer);

    /** Drops the local lights the grid proves are occluded. */
    void CullOccludedLights(LightRawPtrArray& lights);

    /** Drops the entities the grid proves are occluded, from this frame's visible entity list. */
    void CullOccludedEntities(EntityRawPtrArray& entities);

    /**
     * One entity the grid is about to test, with the box to test it against and where it sat in the frame's
     * entity list. The grid test is pure - it reads the grid and the stored camera and writes nothing - so
     * the verdicts are filled on the worker pool, while everything that writes (the streaks, the kept list)
     * stays on the calling thread and walks these in the same order the entities came in.
     */
    struct OcclusionCandidate
    {
      Entity* entity  = nullptr;
      BoundingBox box;
      uint index      = 0; //!< Position in the entity list the frame started with.
      uint8 verdict   = 0; //!< 1 when the grid proved this entity hidden.
    };
    std::vector<OcclusionCandidate> m_occlusionCandidates;

    /**
     * What the grid test needs to project a volume into the frame the grid was reduced from, together with the
     * direction that frame's depth runs. It travels as one piece: a grid compared against a camera other than the
     * one it was reduced with would place every block in the wrong spot.
     */
    struct OcclusionCamera
    {
      Mat4 viewProj      = Mat4(1.0f);
      Mat4 view          = Mat4(1.0f);
      Mat4 projection    = Mat4(1.0f);
      bool reversedDepth = false;
      float nearClip     = 0.1f;
      float farClip      = 1000.0f;
      uint64 id          = 0;
    };

    /** Reads a camera into the form the grid test projects with. */
    static OcclusionCamera CaptureOcclusionCamera(Camera* camera);

    /** Makes the grid test project into @p camera, which is what a grid install does. */
    void ApplyOcclusionCamera(const OcclusionCamera& camera);

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

    /**
     * The grid, carried into the frame's own view, and the blocks of it a source block actually landed on.
     *
     * The grid is reduced in the screen space of the frame that produced it, so a camera that moves between that
     * frame and this one slides the whole picture across the blocks: a block that held a near occluder can end up
     * under a volume that is in front of nothing. Reprojecting each block with its depth into this frame's view
     * puts the depths back where their geometry now is, and leaves a block empty wherever the source blocks do not
     * cover it - which is exactly the region the old frame said nothing about, because whatever used to be there has
     * moved out of the way. A volume that lands on an empty block cannot be proven hidden.
     */
    std::vector<float> m_depthGridWarped;
    std::vector<uint8> m_depthGridCovered;

    /** Size of the warped pair above: half the grid, so the warp costs a quarter and a splat covers more blocks per
     *  write - the disocclusion a camera move leaves behind is far wider than a cell, so it stays a gap either way. */
    UVec2 m_depthGridWarpSize = UVec2(0);

    /** Per block result of the warp's projection: the cell it lands on, and the distance it landed with. Written by
     *  the pool, one index per block, and folded into the pair above on the calling thread. */
    std::vector<int> m_depthGridWarpTarget;
    std::vector<float> m_depthGridWarpDepth;

    /** The view the warped grid was built for, so the warp only runs when the view has actually changed. */
    Mat4 m_depthGridWarpView         = Mat4(1.0f);

    /** Which install of the grid the warp above was built from, so a new grid always rebuilds it. */
    uint64 m_depthGridWarpGeneration = 0;

    /** Bumped every time a read is installed, so the warp can tell a new grid from the one it already warped. */
    uint64 m_depthGridGeneration     = 0;

    /** Counts the warp rebuilds, so the cost is attributable instead of guessed. */
    uint64 m_depthGridWarpCount      = 0;

    /** The view and projection being culled. The grid carries the ones it was reduced with. */
    Mat4 m_occlusionTestView         = Mat4(1.0f);

    /**
     * Carries the grid into the frame's own view: one entry per block, in view space distance, and the mask of the
     * blocks a source block landed on. Cached against the view and the grid it was built from, so a still camera
     * pays nothing for it. Returns false when the grid is not in hand.
     */
    bool WarpDepthGrid();

    /** True when the warped grid in hand describes @p view and the grid generation it was built from. */
    bool WarpedDepthGridReady(const Mat4& view) const { return m_depthGridWarpGeneration == m_depthGridGeneration && m_depthGridWarpView == view; }

    /** Drops the grid and every streak: whatever the grid proved about the last frame is now worthless. */
    void InvalidateDepthGrid();

    /** Clears the per entity streak of every cached range. */
    void ResetEntityOcclusionStreaks();

    /**
     * A grid read the backend is still transferring.
     *
     * The pixels arrive a frame or two after the frame that asked for them, so the view they were reduced with
     * travels with them: comparing them against whichever camera happens to be current when they land would
     * compare two different views.
     */
    struct DepthGridRead
    {
      void* handle        = nullptr;      //!< Backend owned, null when the slot is free.
      bool pending        = false;        //!< Set when the pixels are still to be read on a backend without async.
      FramebufferPtr target = nullptr;    //!< The target a pending read takes its pixels from.
      UVec2 size          = UVec2(0);     //!< Blocks across and down.
      OcclusionCamera camera;             //!< The view those depths were reduced with.
      bool linearDepth    = false;        //!< True when the pixels are view space distances.
      bool discard        = false;        //!< Set when the grid was dropped while this read was in flight.
      uint64 issue        = 0;            //!< Read counter, so an older read cannot overwrite a newer grid.
    };

    /** Two slots: one transferring while the other is being consumed, which is what keeps a read in flight
     *  every frame the grid is refreshed instead of a read every other frame. */
    DepthGridRead m_depthGridReads[2];
    uint64 m_depthGridIssue         = 0;
    uint64 m_depthGridInstalled     = 0;

    /**
     * Polls the reads in flight and installs the newest one that has landed as the grid in hand. Called before
     * the frame culls, so the depths are in hand for the frame that uses them.
     */
    void ConsumeDepthGridReads(Renderer* renderer);

    RenderTargetPtr m_depthGridRt                        = nullptr;
    FramebufferPtr m_depthGridFb                         = nullptr;
    FullQuadPassPtr m_depthGridQuad                      = nullptr;
    ShaderPtr m_depthGridShader                          = nullptr;

    /** Holds the single sample copy of a multisampled frame depth, which is what the grid reduces while
     *  MSAA is on. Built on first use and kept, because the resolve happens every read interval. */
    FramebufferPtr m_depthResolveFb                      = nullptr;
    /** Size and depth format m_depthResolveFb was built for. The attachment is rebuilt when either moves,
     *  which is a viewport resize or a framebuffer that switched between stencil and depth only. */
    UVec2 m_depthResolveSize                             = UVec2(0);
    bool m_depthResolveStencil                           = false;

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

    /** True while m_depthGrid holds view space distances out of the pre process pass's G buffer instead of
     *  depths out of the frame's depth attachment. The two are not comparable, so a change of source drops
     *  the grid and the streaks. */
    bool m_occlusionLinearDepth                          = false;

    /** Camera the grid in m_depthGrid was rendered with. A grid from another view says nothing about
     *  this one, so culling waits for the grid to catch up after a camera change. */
    uint64 m_occlusionCameraId                           = 0;

    /** The view being culled, next to m_occlusionViewProj which is the view the grid was reduced from. The
     *  distance between the two is how far a volume may have drifted on screen since the grid was read. */
    Mat4 m_occlusionTestViewProj = Mat4(1.0f);

    /** The camera the last frame drew with, kept only to notice a switch. Not the camera the grid in hand was
     *  reduced with - that one travels with the read and is applied when it lands - because the frame after a
     *  read is issued legitimately has a grid from an older camera and must not be treated as a switch. */
    uint64 m_lastFrameCameraId = 0;

    /** Set by the frame that changed what is on screen - a moved camera, an entity that moved, a scene
     *  that was edited - and read by the next one, which then re-reads the grid instead of waiting for
     *  the read interval: a mesh behind an occluder that just moved away would otherwise stay dropped
     *  for up to DepthGridReadInterval frames. */
    bool m_sceneChangedLastFrame                         = false;

    /** Last culling decision that was logged, so the log reports changes instead of every frame. */
    uint64 m_loggedCullCameraId                          = 0;
    int m_loggedCullCount                                = -1;
    int m_loggedCulledEntityCount                        = -1;

    /** Whether the depth buffer the grid reduced was multisampled, as last logged. -1 before the first
     *  read, so the first grid build always says which path it took. */
    int m_loggedDepthSource                              = -1;

    /** Whether the empty grid read was last logged as empty, so the recovery is reported too. */
    bool m_loggedEmptyGrid                               = false;

    /** Consecutive frames each light was found hidden behind the grid. */
    std::unordered_map<uint64, int> m_lightOcclusionStreak;
  };

} // namespace ToolKit
