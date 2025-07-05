/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Material.h"
#include "Shader.h"
#include "ShaderUniform.h"
#include "Types.h"

namespace ToolKit
{

  /**
   * Class that holds programs for the gpu's programmable pipeline stages. A program consists of
   * shaders for stages. Vertex and Fragment stages are supported.
   */
  class TK_API GpuProgram
  {
    friend class GpuProgramManager;
    friend class Renderer;

   public:
    GpuProgram();
    GpuProgram(ShaderPtr vertex, ShaderPtr fragment);
    ~GpuProgram();

    /** If caller gives index (different than -1), this function tries to get uniform location as array. */
    int GetDefaultUniformLocation(Uniform uniform, int index = -1);

    /** Returns the location of the custom uniform in the program. */
    int GetCustomUniformLocation(ShaderUniform& shaderUniform);

    /** Updates or adds the given uniform to the uniform cache of the program. */
    void UpdateCustomUniform(const String& name, const UniformValue& val);
    void UpdateCustomUniform(const ShaderUniform& uniform);

   public:
    uint m_handle = 0;
    ShaderPtrArray m_shaders;
    MaterialCacheItem m_cachedMaterial; //!< Cached material data for the program.

   private:
    std::unordered_map<Uniform, int> m_defaultUniformLocation;
    std::unordered_map<Uniform, int> m_defaultArrayUniformLocations;
    std::unordered_map<String, ShaderUniform> m_customUniforms;
  };

  /** Number of programmable pipeline stages. */
  constexpr int TKGpuPipelineStages = 2;

  /** Class that generates the programs from the given shaders and maintains the generated programs. */
  class TK_API GpuProgramManager
  {
   public:
    ~GpuProgramManager();

    /** Sets global gpu buffers used by toolkit. */
    void SetGpuBuffers(struct GlobalGpuBuffers* gpuBuffers) { m_globalGpuBuffers = gpuBuffers; }

   private:
    /**
     * Utility class that hash an array of ULongIDs. Purpose of this class is to provide a hash generator from the
     * shader ids that is used in the program. This hash value will be identifying the program.
     */
    struct IDArrayHash
    {
      std::size_t operator()(const std::array<ObjectId, TKGpuPipelineStages>& data) const
      {
        std::size_t hashValue = 0;

        for (const auto& element : data)
        {
          // Combine the hash value with the hash of each element
          hashValue ^= std::hash<ObjectId>()(element) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
        }

        return hashValue;
      }
    };

   public:
    /**
     * Creates a gpu program that can be binded to renderer to render the objects with.
     * @param vertexShader - is the vertex shader to use in pipeline.
     * @param fragmentShader - is the fragment program to use in pipeline.
     */
    const GpuProgramPtr& CreateProgram(const ShaderPtr vertexShader, const ShaderPtr fragmentShader);

    /**
     * Clears all the created programs, effectively forcing renderer to recreate the programs at next run.
     * Intended usage is to clear all the programs develop by additional modules to prevent memory issues
     * when the modules are destroyed but the editor still running.
     */
    void FlushPrograms();

   private:
    /** Links the given shaders with the program. */
    void LinkProgram(uint program, const ShaderPtr vertexShader, const ShaderPtr fragmentShader);

   private:
    /** Associative array that holds all the programs. */
    std::unordered_map<std::array<ObjectId, TKGpuPipelineStages>, GpuProgramPtr, IDArrayHash> m_programs;

    /** Global gpu buffers used to set uniforms / buffers for each created program. */
    struct GlobalGpuBuffers* m_globalGpuBuffers = nullptr;
  };

} // namespace ToolKit
