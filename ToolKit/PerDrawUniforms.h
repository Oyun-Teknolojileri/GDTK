/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Material.h"
#include "Renderer.h"
#include "Types.h"

namespace ToolKit
{

  /**
   * This struct holds all the data required to update built-in uniforms for a single draw call.
   * It's designed to be used with IGraphicsBackend::SubmitPerDrawData.
   */
  struct PerDrawUniforms
  {
    // Transform matrices
    Mat4 model;
    Mat4 modelWithoutTranslate;
    Mat4 inverseModel;
    Mat4 inverseTransposeModel;
    Mat4 iblRotation;
    Mat4 iblSecondaryRotation;

    // Viewport size
    Vec2 viewportSize;

    // Draw command data
    DrawCommand drawCommand;

    // Material data
    MaterialCacheItem::Data materialData;

    // Light indices
    int activePointLightIndices[RHIConstants::MaxPointLightPerObject];
    int activeSpotLightIndices[RHIConstants::MaxSpotLightPerObject];
    int activePointLightCount;
    int activeSpotLightCount;

    // Animation data
    Vec4 keyFrameData;   // x: kf1, y: kf2, z: interpTime, w: kfCount
    Vec4 blendFrameData; // x: blendKf1, y: blendKf2, z: blendInterpTime, w: blendKfCount
    float animationBlendFactor;
    Vec4 skinParams; // x: boneCount, y: isSkinned, z: isAnimated, w: hasBlend
  };

} // namespace ToolKit
