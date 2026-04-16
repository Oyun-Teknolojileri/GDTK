/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

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

  class TK_API RHI
  {
    friend class Renderer;
    friend class Framebuffer;
    friend class RenderSystem;
    friend class Mesh;
    friend class Main;

   public:
    // Framebuffer helpers
    static void SetFramebuffer(uint target, uint framebufferID);
    static void DeleteFramebuffers(int n, const uint* framebuffers);

    static void StoreFramebufferBindings();
    static void RestoreFramebufferBindings();

    // Texture helpers
    static void SetTexture(uint target, uint textureID, uint textureSlot = 0);
    static void DeleteTexture(uint textureID);

    // Vertex array helpers
    static void BindVertexArray(uint VAO);

   private:
    static uint m_currentReadFramebufferID;
    static uint m_currentDrawFramebufferID;
    static uint m_currentVAO;

    // Stacks to support nested Store/Restore calls
    static IntArray m_storedReadFramebufferStack;
    static IntArray m_storedDrawFramebufferStack;
  };

} // namespace ToolKit
