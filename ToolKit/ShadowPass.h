/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "ShadowAtlas.h"
#include "Pass.h"

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

    /** Performs a single render that generates a single shadow map of a cascade, or a face of a cube etc...*/
    void RenderShadowMap(Light* light, CameraPtr shadowCamera, CameraPtr cullCamera);

    /** Assigns shadow atlas slots to all shadow casting lights using fixed layout. */
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

    const Vec4 m_shadowClearColor      = Vec4(1.0f);
    FramebufferPtr m_shadowFramebuffer = nullptr;
    RenderTargetPtr m_shadowAtlas      = nullptr;
    RenderTargetPtr m_shadowBlurTempRT = nullptr;
    int m_activeCascadeCount           = 0;
    bool m_use32BitShadowMap           = true;
    bool m_use2KLayer                  = false;

    Quaternion m_cubeMapRotations[6];
    ShadowAtlas m_atlas;

    LightRawPtrArray m_lights; // Shadow casters in scene.
  };

  typedef std::shared_ptr<ShadowPass> ShadowPassPtr;

} // namespace ToolKit
