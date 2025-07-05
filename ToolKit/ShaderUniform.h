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

  /**
   * Predefined uniforms. When used in shaders, engine feeds the values with the right frequency.
   *
   * DEPRECATED !! DO NOT ADD HERE UNIFORMS.
   * ALTERNATIVES METHODS
   * Create UniformBuffers based on update frequency and create an include shader representing the buffer.
   * Example: CameraGpuBuffer.
   * Passes can set shader uniforms. Set a gpu program for the pass and call pass->UpdateUniform
   */
  enum class Uniform
  {
    /* Order is important.Don't change for backward compatible resource files. */

    // Draw data
    //////////////////////////////////////////
    MODEL = 0,
    MODEL_WITHOUT_TRANSLATE,
    INVERSE_MODEL,
    INVERSE_TRANSPOSE_MODEL,
    IBL_ROTATION,
    ACTIVE_POINT_LIGHT_INDEXES,
    ACTIVE_SPOT_LIGHT_INDEXES,
    NORMAL_MAP_IN_USE,
    MATERIAL_CACHE,
    DRAW_COMMAND,

    // Animation & Skinning
    //////////////////////////////////////////
    IS_SKINNED = 54,
    NUM_BONES,
    KEY_FRAME_1,
    KEY_FRAME_2,
    KEY_FRAME_INT_TIME,
    KEY_FRAME_COUNT,
    IS_ANIMATED,
    BLEND_ANIMATION,
    BLEND_FACTOR,
    BLEND_KEY_FRAME_1,
    BLEND_KEY_FRAME_2,
    BLEND_KEY_FRAME_INT_TIME,
    BLEND_KEY_FRAME_COUNT,

    UNIFORM_MAX_INVALID
  };

  extern const char* GetUniformName(Uniform u);

  // ShaderUniform
  //////////////////////////////////////////

  using UniformValue = std::variant<bool, float, int, uint, Vec2, Vec3, Vec4, Mat3, Mat4>;

  class TK_API ShaderUniform
  {
    friend class Renderer;
    friend class GpuProgram;

   public:
    enum class UpdateFrequency
    {
      PerDraw,
      PerFrame
    };

    enum class UniformType
    {
      // Caveat, order must match the type declaration in IniformValue
      Bool,
      Float,
      Int,
      UInt,
      Vec2,
      Vec3,
      Vec4,
      Mat3,
      Mat4,
      Undefined,
    };

   public:
    ShaderUniform();
    ShaderUniform(const String& name, UniformValue value, UpdateFrequency frequency = UpdateFrequency::PerDraw);
    ShaderUniform(const ShaderUniform& other);
    ShaderUniform(ShaderUniform&& other) noexcept;

    template <typename T>
    T& GetVal()
    {
      return std::get<T>(m_value);
    }

    UniformType GetType();

    bool operator==(const UniformValue& other) const;
    bool operator!=(const UniformValue& other) const;

    ShaderUniform& operator=(const UniformValue& other);
    ShaderUniform& operator=(const ShaderUniform& other);
    ShaderUniform& operator=(ShaderUniform&& other) noexcept;

   public:
    String m_name;
    UpdateFrequency m_updateFrequency;
    UniformValue m_value;

   private:
    int m_locInGPUProgram                    = -1;
    bool m_thisUniformIsSearchedInGPUProgram = false;
  };

} // namespace ToolKit