/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Serialize.h"

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

  class TK_API RenderState : public Serializable
  {
   public:
    // Active state values.
    // Changing these settings will modify the renderer's state.
    CullingType cullMode        = CullingType::Back;
    BlendFunction blendFunction = BlendFunction::NONE;
    DrawType drawType           = DrawType::Triangle;
    float alphaMaskTreshold     = 0.001f;
    float lineWidth             = 1.0f;

    // Passive state values.
    // Renderer changes or updates these values.
    bool depthTestEnabled             = true;
    bool depthWriteEnabled            = true;
    CompareFunctions depthFunction    = CompareFunctions::FuncLess;
    StencilOperation stencilOperation = StencilOperation::None;
    bool colorMaskEnabled             = true;
    bool depthClampEnabled            = false;
    bool blendOverride                = false;
    BlendFunction blendOverrideFunc   = BlendFunction::NONE;

   protected:
    virtual XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const;
    virtual XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent);
  };

} // namespace ToolKit
