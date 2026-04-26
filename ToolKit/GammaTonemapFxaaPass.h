/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "FullQuadPass.h"
#include "Renderer.h"

namespace ToolKit
{

  enum class TonemapMethod
  {
    Reinhard,
    Aces
  };

  struct GammaTonemapFxaaPassParams
  {
    FramebufferPtr frameBuffer  = nullptr;
    bool enableGammaCorrection  = true;
    bool enableTonemapping      = true;
    bool enableFxaa             = true;
    TonemapMethod tonemapMethod = TonemapMethod::Aces;
    float gamma                 = 2.2f;
    Vec2 screenSize;
  };

  class TK_API GammaTonemapFxaaPass : public Pass
  {
   public:
    GammaTonemapFxaaPass();

    void PreRender() override;
    void Render() override;
    void PostRender() override;

    /** Returns true if any of the sub passes (Tonemap, Fxaa, Gamma) are required. */
    bool IsEnabled();

   public:
    GammaTonemapFxaaPassParams m_params;

   private:
    /** Processed result is stored in this texture. */
    RenderTargetPtr m_processTexture;

    /** Shader to be used in this post process. */
    ShaderPtr m_postProcessShader = nullptr;

    /** Full quad that applies this shader to frame buffer. */
    FullQuadPassPtr m_quadPass    = nullptr;

    /** Pass-specific UBO (slot 5) holding enable flags + screenSize + tonemap/gamma params.
        Lazily initialized on the first PreRender so the renderer backend is alive when
        CreateUniformBuffer runs. */
    GammaTonemapFxaaPassDataBuffer m_passDataBuffer;
    bool m_passDataBufferInitialized = false;
  };

} // namespace ToolKit