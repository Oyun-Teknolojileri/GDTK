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

  /** Depth-of-field pass UBO (`depthOfFieldFrag.shader`). 4 floats + 1 vec2 packed into 32 bytes
      across two vec4s. */
  struct DofPassDataLayout
  {
    /** .xy = uPixelSize (1/width, 1/height). */
    Vec4 pixelSizeAndPad;
    /** .x = focusPoint, .y = focusScale, .z = blurSize, .w = radiusScale. */
    Vec4 focusAndBlur;
  };

  typedef GpuBufferBase<DofPassDataLayout> DofPassDataBuffer;

  enum class DoFQuality
  {
    Low,    // Radius Scale = 2.0f
    Normal, // Radius Scale = 0.8f
    High    // Radius Scale = 0.2f
  };

  struct DoFPassParams
  {
    RenderTargetPtr ColorRt = nullptr;
    RenderTargetPtr DepthRt = nullptr;

    float focusPoint        = 0.0f;
    float focusScale        = 0.0f;
    DoFQuality blurQuality  = DoFQuality::Normal;
  };

  class TK_API DoFPass : public Pass
  {
   public:
    DoFPass();

    void Render() override;
    void PreRender() override;
    void PostRender() override;

   public:
    DoFPassParams m_params;

   private:
    FullQuadPassPtr m_quadPass    = nullptr;
    ShaderPtr m_dofShader         = nullptr;
    RenderTargetPtr m_copyTexture = nullptr;

    /** Slot-5 UBO for `depthOfFieldFrag.shader`. Lazy-init on first PreRender. */
    DofPassDataBuffer m_passDataBuffer;
    bool m_passDataBufferInitialized = false;
  };

  typedef std::shared_ptr<DoFPass> DoFPassPtr;

} // namespace ToolKit