/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "GpuProgram.h"

#include "Renderer.h"
#include "Shader.h"

#include "DebugNew.h"

namespace ToolKit
{

  // GpuProgram
  //////////////////////////////////////////

  GpuProgram::GpuProgram() {}

  GpuProgram::GpuProgram(ShaderPtr vertex, ShaderPtr fragment)
  {
    m_shaders.push_back(vertex);
    m_shaders.push_back(fragment);
  }

  GpuProgram::~GpuProgram()
  {
    if (m_backend && m_gpuData)
    {
      m_backend->DestroyGpuProgram(this);
    }
  }

  int GpuProgram::GetDefaultUniformLocation(Uniform uniform, int index)
  {
    if (index == -1)
    {
      const auto& itr = m_defaultUniformLocation.find(uniform);
      if (itr != m_defaultUniformLocation.end())
      {
        return itr->second;
      }
    }
    else
    {
      // Uniform is an array
      const auto& itr = m_defaultArrayUniformLocations.find(uniform);
      if (itr != m_defaultArrayUniformLocations.end())
      {
        return itr->second;
      }
    }

    return -1;
  }

  int GpuProgram::GetCustomUniformLocation(ShaderUniform& shaderUniform)
  {
    if (!shaderUniform.m_thisUniformIsSearchedInGPUProgram)
    {
      shaderUniform.m_thisUniformIsSearchedInGPUProgram = true;

      int loc = m_backend ? m_backend->GetUniformLocation(this, shaderUniform.m_name.c_str()) : -1;
      if (loc == -1)
      {
        TK_WRN("Uniform: \"%s\" does not exist in program!", shaderUniform.m_name.c_str());
      }

      shaderUniform.m_locInGPUProgram = loc;
    }

    return shaderUniform.m_locInGPUProgram;
  }

  void GpuProgram::UpdateCustomUniform(const String& uniformName, const UniformValue& val)
  {
    auto paramItr = m_customUniforms.find(uniformName);
    if (paramItr == m_customUniforms.end())
    {
      m_customUniforms[uniformName] = ShaderUniform(uniformName, val);
    }
    else
    {
      paramItr->second = val;
    }
  }

  void GpuProgram::UpdateCustomUniform(const ShaderUniform& uniform)
  {
    auto paramItr = m_customUniforms.find(uniform.m_name);
    if (paramItr == m_customUniforms.end())
    {
      m_customUniforms[uniform.m_name] = uniform;
    }
    else
    {
      paramItr->second = uniform.m_value;
    }
  }

  // GpuProgramManager
  //////////////////////////////////////////

  GpuProgramManager::~GpuProgramManager() { FlushPrograms(); }

  const GpuProgramPtr& GpuProgramManager::CreateProgram(const ShaderPtr vertexShader, const ShaderPtr fragmentShader)
  {
    assert(vertexShader);
    assert(fragmentShader);
    assert(m_globalGpuBuffers != nullptr);
    assert(m_backend != nullptr);

    vertexShader->Init();
    fragmentShader->Init();

    const auto& progIter = m_programs.find({(ObjectId)(uintptr_t)vertexShader->m_gpuData.get(),
                                            (ObjectId)(uintptr_t)fragmentShader->m_gpuData.get()});
    if (progIter == m_programs.end())
    {
      GpuProgramPtr program  = MakeNewPtr<GpuProgram>(vertexShader, fragmentShader);
      program->m_backend     = m_backend;

      m_backend->CreateGpuProgram(program.get(), m_globalGpuBuffers);

      auto key = std::array<ObjectId, TKGpuPipelineStages>{(ObjectId)(uintptr_t)vertexShader->m_gpuData.get(),
                                                            (ObjectId)(uintptr_t)fragmentShader->m_gpuData.get()};
      m_programs[key] = program;
      return m_programs[key];
    }

    return progIter->second;
  }

  void GpuProgramManager::FlushPrograms() { m_programs.clear(); }

} // namespace ToolKit