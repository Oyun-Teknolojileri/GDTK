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
    FramebufferPtr m_shadowFramebuffer = nullptr;
    RenderTargetPtr m_shadowAtlas      = nullptr;
    RenderTargetPtr m_shadowBlurTempRT = nullptr;
    int m_activeCascadeCount           = 0;
    bool m_use2KLayer                  = false;

    /** At each index, a layer switch occurs in the shadow atlas. */
    IntArray m_atlasLayerSwitchIndices;

    /** Meta data used for storing all shadow map coordinates and layers. */
    ShadowAtlas m_atlas;

    /** Cached list of shadow casting lights with valid atlas slots, sorted by first atlas layer. */
    LightRawPtrArray m_lights;

    /** Rotations for each face of a cubemap used for point light shadows. */
    Quaternion m_cubeMapRotations[6];
  };

  typedef std::shared_ptr<ShadowPass> ShadowPassPtr;

} // namespace ToolKit
