/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Pass.h"
#include "ShadowAtlas.h"

namespace ToolKit
{

  struct ShadowPassParams
  {
    ScenePtr scene       = nullptr;
    CameraPtr viewCamera = nullptr;
    LightRawPtrArray lights;
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
    /** Perform all renderings to generate all shadow maps for the given light. */
    void RenderShadowMaps(Light* light);

    /** Renders all shadow casters to light's slot in the atlas. */
    void RenderShadowCasters(Light* light, CameraPtr shadowCamera, CameraPtr cullCamera);

    /** Assigns shadow atlas slots to shadow casting lights using priority-based dynamic packing. */
    void PlaceShadowMapsToShadowAtlas(const LightRawPtrArray& lights);

    /** Creates a shadow atlas for m_params.Lights */
    void InitShadowAtlas();

    /** Applies Gaussian blur to all used layers of the shadow atlas. */
    void BlurShadowAtlas();

   public:
    ShadowPassParams m_params;

   private:
    MaterialPtr m_shadowMatOrtho       = nullptr;
    MaterialPtr m_shadowMatPersp       = nullptr;

    Vec4 m_shadowClearColor            = Vec4(1.0f);

    /** Per-layer framebuffers for the main atlas render path. Each pinned to one atlas layer +
     *  the shared depth attachment. Replaces the old "one framebuffer + repeated
     *  SetColorAttachment" pattern that churned VkFramebuffer handles every cascade switch on
     *  Vulkan. Built once in InitShadowAtlas, alive for the atlas's lifetime. */
    std::array<FramebufferPtr, ShadowAtlas::LayerCount> m_shadowFramebuffers;

    /** Scratch framebuffer used by the blur path only — BlurShadowAtlas / ApplyGaussianBlur*
     *  swap its color attachment between the temp RT and an atlas layer per slot. Backend FB
     *  cache absorbs the churn; not worth restructuring blur for now. */
    FramebufferPtr m_shadowFramebuffer  = nullptr;

    RenderTargetPtr m_shadowAtlas       = nullptr;
    RenderTargetPtr m_shadowBlurTempRT  = nullptr;
    int m_activeCascadeCount            = 0;
    bool m_use2KLayer                   = false;

    /** Tracks which per-layer framebuffer is currently the active pass target inside the main
     *  render loop. Cascades may scatter across atlas layers within one light-group; this lets
     *  RenderShadowMaps detect layer transitions and switch passes without bouncing the same
     *  layer back-to-back. -1 = no pass active. */
    int m_currentRenderLayer            = -1;

    /** At each index, a layer switch occurs in the shadow atlas. */
    IntArray m_atlasLayerSwitchIndices;

    /** Meta data used for storing all shadow map coordinates and layers. */
    ShadowAtlas m_atlas;

    /** Cached list of shadow casting lights with valid atlas slots, sorted by first atlas layer. */
    LightRawPtrArray m_lights;

    /** Rotations for each face of a cubemap used for point light shadows. */
    Quaternion m_cubeMapRotations[6];

    /** Pass-owned passive RenderState. Only passive fields (depth*, stencilOp, colorMask,
     *  depthClamp) are read by ApplyPassState. Active fields are ignored. */
    RenderState m_passState;
  };

  typedef std::shared_ptr<ShadowPass> ShadowPassPtr;

} // namespace ToolKit
