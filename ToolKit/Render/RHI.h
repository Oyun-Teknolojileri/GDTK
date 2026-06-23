/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "IGraphicsBackend.h"
#include "Stats.h"
#include "Types.h"

namespace ToolKit
{

  struct RHIConstants
  {
    static constexpr ubyte TextureSlotCount              = 32;
    static constexpr ubyte MaxLightsPerObject            = 128;
    static constexpr uint ShadowAtlasSlot                = 8;
    static constexpr uint SpecularIBLLods                = 7;
    static constexpr uint BrdfLutTextureSize             = 512;
    static constexpr float ShadowBiasMultiplier          = 0.01f;

    /** Update shadow.shader MAX_CASCADE_COUNT accordingly. */
    static constexpr uint MaxCascadeCount                = 4;

    /** Update drawDataInc.shader DIRECTIONAL_LIGHT_CACHE_ITEM_COUNT accordingly. */
    static constexpr uint DirectionalLightCacheItemCount = 12;

    /** Update drawDataInc.shader MAX_DIRECTIONAL_LIGHT_PER_OBJECT accordingly. */
    static constexpr uint MaxDirectionalLightPerObject   = 8;

    /** Update drawDataInc.shader POINT_LIGHT_CACHE_ITEM_COUNT accordingly. */
    static constexpr uint PointLightCacheItemCount       = 32;

    /** Update drawDataInc.shader MAX_POINT_LIGHT_PER_OBJECT accordingly. */
    static constexpr uint MaxPointLightPerObject         = 24;

    /** Update drawDataInc.shader SPOT_LIGHT_CACHE_ITEM_COUNT accordingly. */
    static constexpr uint SpotLightCacheItemCount        = 32;

    /** Update drawDataInc.shader MAX_SPOT_LIGHT_PER_OBJECT accordingly. */
    static constexpr uint MaxSpotLightPerObject          = 24;
  };

  /** Reserved uniform buffer slot assignments for global engine UBOs.
   *  Custom (pass-specific) UBOs must use slots >= FirstCustomSlot. */
  namespace ReservedUniformBufferSlots
  {
    constexpr int CameraData                = 0;
    constexpr int GraphicConstantsData      = 1;
    constexpr int PerDrawData               = 2;
    constexpr int DirectionalLightBuffer    = 3;
    constexpr int PointLightCache           = 4;
    constexpr int SpotLightCache            = 5;
    constexpr int DirectionalLightPVMBuffer = 6;

    constexpr int GlobalBufferCount         = 7;
    constexpr int FirstCustomSlot           = GlobalBufferCount;
  } // namespace ReservedUniformBufferSlots

} // namespace ToolKit
