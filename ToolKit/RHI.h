/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Stats.h"
#include "TKOpenGL.h"
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
    static constexpr float ShadowBiasMultiplier          = 0.0001f;

    /** Update shadow.shader MAX_CASCADE_COUNT accordingly. */
    static constexpr uint MaxCascadeCount                = 4;

    /** Update shadow.shader SHADOW_ATLAS_SIZE accordingly. */
    static constexpr uint ShadowAtlasTextureSize         = 2048;

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
    typedef std::unordered_map<int, int> TextureIdSlotMap;

   public:
    /** Sets the given texture to given slot. textureSlot can be between 0 & 31. */
    static void SetTexture(GLenum target, GLuint textureID, GLenum textureSlot = 31);
    static void DeleteTexture(GLuint textureID);
    static void BindVertexArray(GLuint VAO);

   private:
    static void SetFramebuffer(GLenum target, GLuint framebufferID);
    static void DeleteFramebuffers(GLsizei n, const GLuint* framebuffers);
    static void InvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum* attachments);

   private:
    static GLuint m_currentReadFramebufferID;
    static GLuint m_currentDrawFramebufferID;
    static GLuint m_currentFramebufferID;
    static GLuint m_currentVAO;

    static TextureIdSlotMap m_textureIdSlotMap;
  };

} // namespace ToolKit
