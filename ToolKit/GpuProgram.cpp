/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "GpuProgram.h"

#include "Logger.h"
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
  }

  GpuProgram::~GpuProgram()
  {
    if (m_backend && m_gpuData)
    {
      m_backend->DestroyGpuProgram(this);
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

    const auto& progIter = m_programs.find(
        {(ObjectId) (uintptr_t) vertexShader->m_gpuData.get(), (ObjectId) (uintptr_t) fragmentShader->m_gpuData.get()});
    if (progIter == m_programs.end())
    {
      GpuProgramPtr program = MakeNewPtr<GpuProgram>(vertexShader, fragmentShader);
      program->m_backend    = m_backend;

      // Build explicit binding list from shader declarations.
      std::vector<ShaderResourceBinding> bindings;
      bindings.reserve(program->m_resources.size());

      for (const ShaderResource& res : program->m_resources)
      {
        if (res.type != ShaderResource::Type::UniformBuffer)
          continue;

        ShaderResourceBinding b;
        b.blockName = res.name.c_str();
        b.buffer    = nullptr;

        if (const GlobalBufferInfo* info = m_globalGpuBuffers->FindGlobalBufferInfo(res.name.c_str()))
        {
          // Global buffer: use reserved slot, ignore shader-declared binding.
          b.slot   = info->slot;
          b.buffer = info->buffer;
        }
        else
        {
          // Custom (pass-specific) buffer: use shader-declared slot.
          b.slot = res.slot;

          if (b.slot == -1)
          {
            TK_ERR("Shader '%s' uses uniform block '%s' without specifying a binding slot. "
                   "Add layout(binding = X) to the UBO declaration.",
                   vertexShader->GetFile().c_str(), res.name.c_str());
            return nullptr;
          }

          if (b.slot < ReservedUniformBufferSlots::FirstCustomSlot)
          {
            TK_ERR("Custom uniform buffer '%s' uses slot %d which is in the reserved global range [0, %d). "
                   "Move it to slot >= %d.",
                   res.name.c_str(), b.slot,
                   ReservedUniformBufferSlots::FirstCustomSlot,
                   ReservedUniformBufferSlots::FirstCustomSlot);
            return nullptr;
          }
        }

        bindings.push_back(b);
      }

      m_backend->CreateGpuProgram(program.get(), bindings.data(), (int)bindings.size());

      auto key        = std::array<ObjectId, TKGpuPipelineStages> {(ObjectId) (uintptr_t) vertexShader->m_gpuData.get(),
                                                                   (ObjectId) (uintptr_t) fragmentShader->m_gpuData.get()};
      m_programs[key] = program;
      return m_programs[key];
    }

    return progIter->second;
  }

  void GpuProgramManager::FlushPrograms() { m_programs.clear(); }

} // namespace ToolKit