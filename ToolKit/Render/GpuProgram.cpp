/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "GpuProgram.h"

#include "BootStats.h"
#include "Logger.h"
#include "Renderer.h"
#include "Shader.h"

#include <chrono>

#include "DebugNew.h"

namespace ToolKit
{

  namespace
  {
    /** FNV-1a over both stages: the key of the program cache, see GpuProgramManager::CreateProgram. */
    uint64 HashProgramSource(const String& vertexSource, const String& fragmentSource)
    {
      constexpr uint64 offset = 14695981039346656037ull;
      constexpr uint64 prime  = 1099511628211ull;

      uint64 hash = offset;
      for (const String* source : {&vertexSource, &fragmentSource})
      {
        for (const char character : *source)
        {
          hash ^= (uint64) (unsigned char) character;
          hash *= prime;
        }
      }
      return hash;
    }

    /** Milliseconds since a steady clock reading, for the boot stats. */
    float ElapsedMilliseconds(std::chrono::steady_clock::time_point begin)
    {
      return (float) std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    }

    /** Fills the binding list a backend needs to restore a program's sampler and UBO slots. */
    void BuildResourceBindings(GpuProgram* program,
                               const GlobalGpuBuffers* globalBuffers,
                               std::vector<ShaderResourceBinding>& bindings)
    {
      bindings.reserve(program->m_resources.size());
      for (const ShaderResource& res : program->m_resources)
      {
        ShaderResourceBinding b;
        b.type = (res.type == ShaderResource::Type::Texture) ? ShaderResourceBinding::Type::Texture
                                                             : ShaderResourceBinding::Type::UniformBuffer;
        b.name = res.name.c_str();
        b.slot = res.slot;

        if (res.type == ShaderResource::Type::UniformBuffer && globalBuffers != nullptr)
        {
          if (const GlobalBufferInfo* info = globalBuffers->FindGlobalBufferInfo(res.name.c_str()))
          {
            b.buffer = info->buffer;
          }
        }
        bindings.push_back(b);
      }
    }
  } // namespace
  // GpuProgram
  //////////////////////////////////////////

  GpuProgram::GpuProgram() {}

  GpuProgram::GpuProgram(ShaderPtr vertex, ShaderPtr fragment)
  {
    m_shaders.push_back(vertex);
    m_shaders.push_back(fragment);

    // Aggregate resource declarations from all stages, deduplicated by (type, slot, name).
    auto mergeResources = [this](const ShaderPtr& shader)
    {
      if (!shader)
      {
        return;
      }

      for (const ShaderResource& res : shader->m_resources)
      {
        auto same = [&res](const ShaderResource& r)
        { return r.type == res.type && r.slot == res.slot && r.name == res.name; };

        if (std::find_if(m_resources.begin(), m_resources.end(), same) == m_resources.end())
        {
          m_resources.push_back(res);
        }
      }
    };

    mergeResources(vertex);
    mergeResources(fragment);

    // Resolve the samplers once here rather than on the draw path, which asks for the same handful of
    // names for every object it draws. The map is keyed by name content, so a caller that passes a
    // temporary string still gets the right answer.
    for (const ShaderResource& res : m_resources)
    {
      if (res.type == ShaderResource::Type::Texture)
      {
        m_textureSlotsByName[res.name] = res.slot;
      }
    }

    const auto slotOf = [this](const char* name) -> int
    {
      const auto itr = m_textureSlotsByName.find(String(name));
      return itr != m_textureSlotsByName.end() ? itr->second : -1;
    };

    m_materialSamplers.diffuse           = slotOf("s_diffuseColor");
    m_materialSamplers.emissive          = slotOf("s_emissiveColor");
    m_materialSamplers.metallicRoughness = slotOf("s_metallicRoughness");
    m_materialSamplers.normal            = slotOf("s_normalMap");
    m_materialSamplers.brdfLut           = slotOf("s_brdfLut");
    m_materialSamplers.cubeMap           = slotOf("s_cubeMap");
  }

  GpuProgram::~GpuProgram()
  {
    if (m_backend && m_gpuData)
    {
      m_backend->DestroyGpuProgram(this);
    }
  }

  /**
   * Empty program returned when a shader declares an invalid uniform buffer slot. Shared instead
   * of a function local static per error path. It is deliberately never assigned a real program,
   * so it holds no backend pointer and can be destroyed during process exit safely.
   */
  namespace
  {
    const GpuProgramPtr g_nullProgram;
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

    // Android only: the program cache is consulted before anything compiles, because the compile is
    // what a device start spends its time on. Every other backend answers false here and this stays
    // the single path it always was.
    const bool useProgramCache = m_backend->SupportsProgramBinary();
    uint64 sourceHash          = 0;

    if (useProgramCache)
    {
      sourceHash = HashProgramSource(vertexShader->m_source, fragmentShader->m_source);

      const auto cached = m_programsBySourceHash.find(sourceHash);
      if (cached != m_programsBySourceHash.end())
      {
        return cached->second;
      }

      // A hit restores the linked program and leaves both shaders uncompiled: a program that came
      // from a binary needs no shader objects.
      GpuProgramPtr loaded = MakeNewPtr<GpuProgram>(vertexShader, fragmentShader);
      loaded->m_backend    = m_backend;

      const auto restoreBegin = std::chrono::steady_clock::now();

      if (m_backend->LoadProgramBinary(loaded.get(), sourceHash))
      {
        loaded->m_loadedFromCache = true;

        std::vector<ShaderResourceBinding> cacheBindings;
        BuildResourceBindings(loaded.get(), m_globalGpuBuffers, cacheBindings);
        m_backend->CreateGpuProgram(loaded.get(), cacheBindings.data(), (int) cacheBindings.size());

        BootStats::ProgramReady(true, ElapsedMilliseconds(restoreBegin));

        m_programsBySourceHash[sourceHash] = loaded;
        return m_programsBySourceHash[sourceHash];
      }
    }

    vertexShader->Init();
    fragmentShader->Init();

    // Android compiles one variant per shader at start and the rest on demand, so the pair the cache
    // missed is brought in here, right before the program is built from it.
    vertexShader->EnsureVariantCompiled();
    fragmentShader->EnsureVariantCompiled();

    // A shader that failed to compile reports through the backend's log and keeps a null GpuResourceData.
    // Creating a program out of it would dereference that null inside the backend, so stop here and
    // name the shader instead: a compile error must not take the renderer down.
    if (vertexShader->m_gpuData == nullptr || fragmentShader->m_gpuData == nullptr)
    {
      TK_ERR("Can not create a program, a shader failed to compile. Vertex: %s, Fragment: %s",
             vertexShader->GetFile().c_str(),
             fragmentShader->GetFile().c_str());
      return g_nullProgram;
    }

    const auto& progIter = m_programs.find(
        {(ObjectId) (uintptr_t) vertexShader->m_gpuData.get(), (ObjectId) (uintptr_t) fragmentShader->m_gpuData.get()});
    if (progIter == m_programs.end())
    {
      GpuProgramPtr program = MakeNewPtr<GpuProgram>(vertexShader, fragmentShader);
      program->m_backend    = m_backend;

      // Validate uniform buffer slots. Global buffers must match their reserved slot so
      // Vulkan pipeline layouts agree with the SPIR-V binding declared in the shader source.
      for (const ShaderResource& res : program->m_resources)
      {
        if (res.type != ShaderResource::Type::UniformBuffer)
          continue;

        if (const GlobalBufferInfo* info = m_globalGpuBuffers->FindGlobalBufferInfo(res.name.c_str()))
        {
          if (res.slot != info->slot)
          {
            TK_WRN("Shader '%s' declares global UBO '%s' at slot %d, but reserved slot is %d. "
                   "Update the shader XML / GLSL binding to match.",
                   vertexShader->GetFile().c_str(),
                   res.name.c_str(),
                   res.slot,
                   info->slot);
          }
        }
        else
        {
          // Custom (pass-specific) buffer: validate slot is explicitly assigned and outside the
          // reserved global range.
          if (res.slot == -1)
          {
            TK_ERR("Shader '%s' uses uniform block '%s' without specifying a binding slot. "
                   "Add layout(binding = X) to the UBO declaration.",
                   vertexShader->GetFile().c_str(),
                   res.name.c_str());
            return g_nullProgram;
          }

          if (res.slot < ReservedUniformBufferSlots::FirstCustomSlot)
          {
            TK_ERR("Custom uniform buffer '%s' uses slot %d which is in the reserved global range [0, %d). "
                   "Move it to slot >= %d.",
                   res.name.c_str(),
                   res.slot,
                   ReservedUniformBufferSlots::FirstCustomSlot,
                   ReservedUniformBufferSlots::FirstCustomSlot);
            return g_nullProgram;
          }
        }
      }

      // Build explicit binding list for GL backend (VK reads program->m_resources directly).
      std::vector<ShaderResourceBinding> bindings;
      bindings.reserve(program->m_resources.size());

      for (const ShaderResource& res : program->m_resources)
      {
        ShaderResourceBinding b;
        b.type = (res.type == ShaderResource::Type::Texture) ? ShaderResourceBinding::Type::Texture
                                                             : ShaderResourceBinding::Type::UniformBuffer;
        b.name = res.name.c_str();
        b.slot = res.slot;

        if (res.type == ShaderResource::Type::UniformBuffer)
        {
          if (const GlobalBufferInfo* info = m_globalGpuBuffers->FindGlobalBufferInfo(res.name.c_str()))
          {
            b.buffer = info->buffer;
          }
        }

        bindings.push_back(b);
      }

      const auto linkBegin = std::chrono::steady_clock::now();
      m_backend->CreateGpuProgram(program.get(), bindings.data(), (int) bindings.size());
      BootStats::ProgramReady(false, ElapsedMilliseconds(linkBegin));

      if (useProgramCache)
      {
        // Timed on its own: reading a program back forces the driver to finish the program, so the
        // save can cost more than the link it is trying to spare on the next start.
        const auto saveBegin = std::chrono::steady_clock::now();
        m_backend->SaveProgramBinary(program.get(), sourceHash);
        BootStats::ProgramStored(ElapsedMilliseconds(saveBegin));

        m_programsBySourceHash[sourceHash] = program;
      }

      auto key = std::array<ObjectId, TKGpuPipelineStages> {(ObjectId) (uintptr_t) vertexShader->m_gpuData.get(),
                                                            (ObjectId) (uintptr_t) fragmentShader->m_gpuData.get()};
      m_programs[key] = program;
      return m_programs[key];
    }

    return progIter->second;
  }

  void GpuProgramManager::FlushPrograms()
  {
    m_programs.clear();
    m_programsBySourceHash.clear();
  }

} // namespace ToolKit