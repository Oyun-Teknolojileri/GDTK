/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Types.h"

namespace ToolKit
{

  enum class ShadingMode
  {
    None,
    Lighting,
    Albedo,
    Normal,
    Metallic,
    Roughness
  };

  enum class GraphicBitFields
  {
    None             = 0x00,
    ColorBits        = 0x01,
    DepthBits        = 0x02,
    StencilBits      = 0x04,
    ColorDepthBits   = ColorBits | DepthBits,
    ColorStencilBits = ColorBits | StencilBits,
    DepthStencilBits = DepthBits | StencilBits,
    AllBits          = ColorBits | DepthBits | StencilBits
  };

  enum class CompareFunctions
  {
    FuncNever,
    FuncLess,
    FuncEqual,
    FuncLequal,
    FuncGreater,
    FuncNEqual,
    FuncGEqual,
    FuncAlways
  };

  enum class BlendFunction
  {
    NONE,
    SRC_ALPHA_ONE_MINUS_SRC_ALPHA,
    ALPHA_MASK,
    ONE_TO_ONE // Additive
  };

  enum class DrawType
  {
    Point,
    Line,
    LineLoop,
    LineStrip,
    Triangle
  };

  enum class CullingType
  {
    TwoSided, // No culling
    Front,
    Back
  };

  enum class VertexLayout
  {
    None,
    Mesh,
    SkinMesh
  };

  /**
   * Simple binary stencil test operations.
   */
  enum class StencilOperation
  {
    /**
     * Stencil write and operations are disabled.
     */
    None,
    /**
     * All pixels are drawn and stencil value of the corresponding pixel set
     * to 1.
     */
    AllowAllPixels,
    /**
     * Pixels whose stencil value is 1 are drawn.
     */
    AllowPixelsPassingStencil,
    /**
     * Pixels whose stencil value is 0 are drawn.
     */
    AllowPixelsFailingStencil
  };

  /**
   * Rasterizer state composed at draw time and handed to the backend's BindPipeline.
   * Passive bits (depth, stencil, color mask, depth clamp, blend override) are set once per pass
   * via Renderer::SetPassState. Active bits (cull, blend, draw type, alpha mask threshold,
   * line width) come from the Material that owns them — Renderer copies them in at each draw.
   * Materials no longer store a RenderState; this struct is a transient draw-call descriptor.
   */
  class TK_API RenderState
  {
   public:
    // Active fields. Sourced from Material at draw time; not pass-level state.
    CullingType cullMode              = CullingType::Back;
    BlendFunction blendFunction       = BlendFunction::NONE;
    DrawType drawType                 = DrawType::Triangle;
    float alphaMaskTreshold           = 0.001f;
    float lineWidth                   = 1.0f;

    // Passive fields. Set by passes through Renderer::SetPassState.
    bool depthTestEnabled             = true;
    bool depthWriteEnabled            = true;
    CompareFunctions depthFunction    = CompareFunctions::FuncLess;
    StencilOperation stencilOperation = StencilOperation::None;
    bool colorMaskEnabled             = true;
    bool depthClampEnabled            = false;

    /** When true, Renderer::Render replaces the material's blendFunction with blendOverrideFunc.
     *  Lets a pass force a single blend mode across every draw (e.g. shadow casters always = NONE)
     *  without mutating each material asset. */
    bool blendOverride                = false;
    BlendFunction blendOverrideFunc   = BlendFunction::NONE;

    /** Same mechanism for cull. Used by two-sided translucent rendering to do a back-then-front
     *  two-pass draw of a single job by toggling cullOverrideMode between draws. */
    bool cullOverride                 = false;
    CullingType cullOverrideMode      = CullingType::Back;
  };

} // namespace ToolKit
