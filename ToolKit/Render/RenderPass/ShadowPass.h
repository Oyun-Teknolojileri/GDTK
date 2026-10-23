/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Pass.h"
#include "ShadowAtlas.h"
#include "Texture.h"

#include <vector>

namespace ToolKit
{

  struct ShadowPassParams
  {
    ScenePtr scene       = nullptr;
    CameraPtr viewCamera = nullptr;
    LightRawPtrArray lights;

    /** Set by the render path on the frames where an entity changed, because the casters it built
     *  against are then stale even though the scene version did not move. */
    bool castersDirty = false;
  };

  /** Create shadow map buffers for all given lights. */
  class TK_API ShadowPass : public Pass
  {
   public:
    ShadowPass();
    ShadowPass(const ShadowPassParams& params);
    ~ShadowPass();

    void Render() override;
    void PreRender() override;
    void PostRender() override;

    RenderTargetPtr GetShadowAtlas();

   private:
    /**
     * A run of caster jobs that share mesh, material and cull side, drawn as one instanced
     * draw. irstJob is also the index of the batch's first entry in the instance table,
     * because the table holds one entry per job in job order.
     */
    struct ShadowBatch
    {
      Mesh* mesh         = nullptr;
      Material* material = nullptr;
      bool masked        = false;
      int firstJob       = 0;
      int jobCount       = 0;
      BoundingBox bbox   = BoundingBox(Vec3(FLT_MAX), Vec3(-FLT_MAX));
    };

    /** Sorts the caster jobs into batches, packs the instance table and uploads it. */
    void BuildShadowBatches();

    /** Rebuilds m_casterRanges from the current job order. Called after the batch sort, which
     *  reorders the jobs and would otherwise leave the ranges pointing at the wrong ones. */
    void UpdateCasterRanges();

    /** Perform all renderings to generate all shadow maps for the given light. */
    void RenderShadowMaps(Light* light);

    /** Renders all shadow casters to light's slot in the atlas. */
    void RenderShadowCasters(Light* light, CameraPtr shadowCamera, CameraPtr cullCamera);

    /**
     * Builds the caster job list for this frame, once.
     *
     * The jobs do not depend on the light or the cascade: mesh, material, transform and
     * world bounding box are the same for every shadow map. Only the *set* of casters a
     * cascade keeps differs, so the list is built once and each cascade is filtered with
     * a bounding box test instead of rebuilding every job (five times a frame with four
     * directional cascades plus a spot).
     */
    void PrepareShadowCasterJobs();

    /** Assigns shadow atlas slots to shadow casting lights using priority-based dynamic packing. */
    void PlaceShadowMapsToShadowAtlas(const LightRawPtrArray& lights);

    /** Creates a shadow atlas for m_params.Lights */
    void InitShadowAtlas();

    /** Applies Gaussian blur to all used layers of the shadow atlas. */
    void BlurShadowAtlas();

   public:
    ShadowPassParams m_params;

   private:
    MaterialPtr m_shadowMatOrtho = nullptr;
    MaterialPtr m_shadowMatPersp = nullptr;

    /** Point lights project each hemisphere onto a paraboloid, two maps instead of six cube faces.
     *  The projection cannot be a matrix, so it lives in its own vertex shader. */
    MaterialPtr m_shadowMatParaboloid = nullptr;

    /** Turns the shadow camera around for the second hemisphere of a point light: half a turn about Y.
     *  (w, x, y, z) = (0, 0, 1, 0). */
    Quaternion m_paraboloidBackRotation = Quaternion(0.0f, 0.0f, 1.0f, 0.0f);

    /** The depth program without the instance table. Used on the frames where instanced batching is
     *  switched off, where every caster is one draw and reads the per draw block again. */
    GpuProgramPtr m_plainProgram = nullptr;

    Vec4 m_shadowClearColor      = Vec4(1.0f);

    /** Per-layer framebuffers for the main atlas render path. Each pinned to one atlas layer +
     *  the shared depth attachment. Replaces the old "one framebuffer + repeated
     *  SetColorAttachment" pattern that churned VkFramebuffer handles every cascade switch on
     *  Vulkan. Built once in InitShadowAtlas, alive for the atlas's lifetime. */
    std::array<FramebufferPtr, ShadowAtlas::LayerCount> m_shadowFramebuffers;

    /** Scratch framebuffer used by the blur path only - BlurShadowAtlas / ApplyGaussianBlur*
     *  swap its color attachment between the temp RT and an atlas layer per slot. Backend FB
     *  cache absorbs the churn; not worth restructuring blur for now. */
    FramebufferPtr m_shadowFramebuffer = nullptr;

    RenderTargetPtr m_shadowAtlas      = nullptr;
    RenderTargetPtr m_shadowBlurTempRT = nullptr;
    int m_activeCascadeCount           = 0;
    bool m_use2KLayer                  = false;

    /** Tracks which per-layer framebuffer is currently the active pass target inside the main
     *  render loop. Cascades may scatter across atlas layers within one light-group; this lets
     *  RenderShadowMaps detect layer transitions and switch passes without bouncing the same
     *  layer back-to-back. -1 = no pass active. */
    int m_currentRenderLayer           = -1;

    /** At each index, a layer switch occurs in the shadow atlas. */
    IntArray m_atlasLayerSwitchIndices;

    /** Slot density the atlas was placed with last frame. A change rebuilds the layout, and the
     *  slot indices in m_placedLights name the old grid, so the placement starts over. */
    int m_atlasSlotScale = 1;

    /** Meta data used for storing all shadow map coordinates and layers. */
    ShadowAtlas m_atlas;

    /** Where a local light was placed last frame. A light that still wants the same tier gets its old
     *  slots back, so a light entering or leaving the view does not reshuffle the atlas under the
     *  others and their shadows do not jump between resolutions. */
    struct PlacedLight
    {
      ShadowAtlas::SlotSize tier = ShadowAtlas::SlotSize::Eighth;
      int slotIndices[6]         = {-1, -1, -1, -1, -1, -1};
      int count                  = 0;
    };
    std::unordered_map<uint64, PlacedLight> m_placedLights;

    /** Cached list of shadow casting lights with valid atlas slots, sorted by first atlas layer. */
    LightRawPtrArray m_lights;

    /** Shadow caster jobs for this frame, partitioned opaque / alpha masked / translucent.
     *  Built once in PrepareShadowCasterJobs and shared by every light and cascade. */
    RenderData m_shadowRenderData;

    /** Scene version and light set the caster jobs were built with. Building them walks every
     *  entity in the scene, which measured 2.8 ms a frame, so they are kept until the scene
     *  version moves, the light set changes, or the render path reports a dirty entity. */
    uint64 m_casterVersion        = ~0ull;
    uint64 m_casterLightSignature = 0;

    /// Half open [begin, end) range of m_shadowRenderData.jobs that belongs to an entity, with the
    /// spatial generation it was built at. This is what makes a caster rebuild incremental: a frame
    /// where objects move rebuilds their ranges, not the whole scene.
    struct CasterSlot
    {
      uint64 generation = 0;
      uint begin        = 0;
      uint end          = 0;
    };
    std::unordered_map<const Entity*, CasterSlot> m_casterRanges;

    /// Scratch jobs for the casters that changed since the last frame.
    RenderJobArray m_dirtyCasterJobs;
    EntityRawPtrArray m_dirtyCasterEntities;

    /// Casters whose geometry moves without their entity moving: a skinned mesh deforms, an animated
    /// one is driven through its clip. Their ranges are rebuilt every frame, the rest stays cached.
    std::unordered_set<const Entity*> m_animatedCasterEntities;

    /** Batched runs of m_shadowRenderData.jobs; see ShadowBatch. */
    std::vector<ShadowBatch> m_shadowBatches;

    /** Per instance model matrices, eight texels per job, in job order. */
    DataTexturePtr m_instanceTable = nullptr;
    std::vector<float> m_instanceData;

    /** Same size as m_instanceData. Per cascade, only the casters that survive the cascade
     *  frustum are packed into it and uploaded, so a batch no longer drags in its off screen
     *  instances. */
    std::vector<float> m_cascadeInstanceData;

    /** Must match INSTANCE_TEX_WIDTH and INSTANCE_TEXELS_PER_ITEM in instanceDataInc.shader. */
    static constexpr int InstanceTexWidth       = 256;
    static constexpr int InstanceTexelsPerItem  = 8;

    /** Rotations for each face of a cubemap used for point light shadows. */
    Quaternion m_cubeMapRotations[6];

    /** Pass-owned passive RenderState. Only passive fields (depth*, stencilOp, colorMask,
     *  depthClamp) are read by ApplyPassState. Active fields are ignored. */
    RenderState m_passState;
  };

  typedef std::shared_ptr<ShadowPass> ShadowPassPtr;

} // namespace ToolKit
