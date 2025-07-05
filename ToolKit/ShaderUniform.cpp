/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "ShaderUniform.h"

#include "TKAssert.h"

#include "DebugNew.h"

namespace ToolKit
{

  const char* GetUniformName(Uniform u)
  {
    switch (u)
    {
    case Uniform::MODEL:
      return "model";
    case Uniform::MODEL_WITHOUT_TRANSLATE:
      return "modelWithoutTranslate";
    case Uniform::INVERSE_MODEL:
      return "inverseModel";
    case Uniform::INVERSE_TRANSPOSE_MODEL:
      return "inverseTransposeModel";
    case Uniform::IBL_ROTATION:
      return "iblRotation";
    case Uniform::ACTIVE_POINT_LIGHT_INDEXES:
      return "activePointLightIndexes";
    case Uniform::ACTIVE_SPOT_LIGHT_INDEXES:
      return "activeSpotLightIndexes";
    case Uniform::NORMAL_MAP_IN_USE:
      return "normalMapInUse";
    case Uniform::MATERIAL_CACHE:
      return "materialCache";
    case Uniform::DRAW_COMMAND:
      return "drawCommand";
    case Uniform::IS_SKINNED:
      return "isSkinned";
    case Uniform::NUM_BONES:
      return "numBones";
    case Uniform::KEY_FRAME_1:
      return "keyFrame1";
    case Uniform::KEY_FRAME_2:
      return "keyFrame2";
    case Uniform::KEY_FRAME_INT_TIME:
      return "keyFrameIntepolationTime";
    case Uniform::KEY_FRAME_COUNT:
      return "keyFrameCount";
    case Uniform::IS_ANIMATED:
      return "isAnimated";
    case Uniform::BLEND_ANIMATION:
      return "blendAnimation";
    case Uniform::BLEND_FACTOR:
      return "blendFactor";
    case Uniform::BLEND_KEY_FRAME_1:
      return "blendKeyFrame1";
    case Uniform::BLEND_KEY_FRAME_2:
      return "blendKeyFrame2";
    case Uniform::BLEND_KEY_FRAME_INT_TIME:
      return "blendKeyFrameIntepolationTime";
    case Uniform::BLEND_KEY_FRAME_COUNT:
      return "blendKeyFrameCount";
    case Uniform::UNIFORM_MAX_INVALID:
    default:
      return "";
    }
  }

  // ShaderUniform
  //////////////////////////////////////////

  ShaderUniform::ShaderUniform() : m_updateFrequency(UpdateFrequency::PerDraw) {}

  ShaderUniform::ShaderUniform(const String& name, UniformValue value, UpdateFrequency frequency)
      : m_updateFrequency(UpdateFrequency::PerDraw)
  {
    m_name            = name;
    m_value           = value;
    m_updateFrequency = frequency;
  }

  ShaderUniform::ShaderUniform(const ShaderUniform& other)
      : m_name(other.m_name), m_updateFrequency(other.m_updateFrequency), m_value(other.m_value)
  {
  }

  ShaderUniform::ShaderUniform(ShaderUniform&& other) noexcept
      : m_name(std::move(other.m_name)), m_updateFrequency(other.m_updateFrequency), m_value(std::move(other.m_value))
  {
  }

  ShaderUniform& ShaderUniform::operator=(const UniformValue& other)
  {
    m_value = other;
    return *this;
  }

  ShaderUniform& ShaderUniform::operator=(const ShaderUniform& other)
  {
    if (this != &other)
    {
      m_name            = other.m_name;
      m_updateFrequency = other.m_updateFrequency;
      m_value           = other.m_value;
    }
    return *this;
  }

  ShaderUniform& ShaderUniform::operator=(ShaderUniform&& other) noexcept
  {
    if (this != &other)
    {
      m_name            = other.m_name;
      m_updateFrequency = other.m_updateFrequency;
      m_value           = other.m_value;
    }
    return *this;
  }

  ShaderUniform::UniformType ShaderUniform::GetType() { return (UniformType) m_value.index(); }

  bool ShaderUniform::operator==(const UniformValue& other) const
  {
    if (m_value.index() != other.index())
    {
      // Different variant types, not equal
      return false;
    }

    switch (m_value.index())
    {
    case 0: // bool
      return std::get<bool>(m_value) == std::get<bool>(other);
    case 1: // float
      return std::get<float>(m_value) == std::get<float>(other);
    case 2: // int
      return std::get<int>(m_value) == std::get<int>(other);
    case 3: // uint
      return std::get<uint>(m_value) == std::get<uint>(other);
    case 4: // glm::vec2
      return glm::all(glm::equal(std::get<glm::vec2>(m_value), std::get<glm::vec2>(other)));
    case 5: // glm::vec3
      return glm::all(glm::equal(std::get<glm::vec3>(m_value), std::get<glm::vec3>(other)));
    case 6: // glm::vec4
      return glm::all(glm::equal(std::get<glm::vec4>(m_value), std::get<glm::vec4>(other)));
    case 7: // glm::mat3
      return glm::all(glm::equal(std::get<glm::mat3>(m_value), std::get<glm::mat3>(other)));
    case 8: // glm::mat4
      return glm::all(glm::equal(std::get<glm::mat4>(m_value), std::get<glm::mat4>(other)));
    default:
      // Unsupported type, not equal
      return false;
    }
  }

  bool ShaderUniform::operator!=(const UniformValue& other) const { return !(*this == other); }

} // namespace ToolKit