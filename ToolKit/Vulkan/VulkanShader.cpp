/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanShader.h"

#include "../Logger.h"
#include "../ToolKit.h"
#include "VulkanBindings.h"

#include <filesystem>
#include <fstream>
#include <shaderc/shaderc.hpp>

namespace ToolKit
{
  namespace VulkanShader
  {

    static shaderc_shader_kind ToShadercKind(Stage stage)
    {
      switch (stage)
      {
      case Stage::Vertex:   return shaderc_glsl_vertex_shader;
      case Stage::Fragment: return shaderc_glsl_fragment_shader;
      }
      return shaderc_glsl_vertex_shader;
    }

    // SPIR-V disk cache helpers.
    // Key  = FNV-1a 64-bit hash of source bytes + stage (0=vert, 1=frag).
    // Path = <ConfigPath>/SpirVCache/<hex>_v.spv  or  _f.spv
    // GL drivers have a built-in shader cache; shaderc is a pure CPU library with no cache, so
    // without this every variant is re-compiled from scratch on each launch.

    static uint64_t SpvCacheHash(const std::string& src, Stage stage)
    {
      uint64_t h = 14695981039346656037ULL;
      for (unsigned char c : src) { h ^= c; h *= 1099511628211ULL; }
      h ^= (stage == Stage::Vertex ? 0u : 1u);
      h *= 1099511628211ULL;
      return h;
    }

    static std::string SpvCachePath(uint64_t hash, Stage stage)
    {
      char buf[32];
      snprintf(buf, sizeof(buf), "%016llx_%c.spv",
               (unsigned long long) hash,
               stage == Stage::Vertex ? 'v' : 'f');
      return ConcatPaths({ConfigPath(), "SpirVCache", buf});
    }

    static std::vector<uint32_t> LoadSpv(const std::string& path)
    {
      std::ifstream f(path, std::ios::binary | std::ios::ate);
      if (!f) return {};
      auto sz = f.tellg();
      if (sz <= 0 || sz % 4 != 0) return {};
      f.seekg(0);
      std::vector<uint32_t> spv(static_cast<size_t>(sz) / 4);
      f.read(reinterpret_cast<char*>(spv.data()), sz);
      return f ? spv : std::vector<uint32_t>{};
    }

    static void SaveSpv(const std::string& path, const std::vector<uint32_t>& spv)
    {
      namespace fs = std::filesystem;
      std::error_code ec;
      fs::create_directories(fs::path(path).parent_path(), ec);
      if (ec) return;
      std::ofstream f(path, std::ios::binary);
      if (!f) return;
      f.write(reinterpret_cast<const char*>(spv.data()), spv.size() * 4);
    }

    std::vector<uint32_t> CompileGlslToSpirv(Stage stage, const std::string& source, const std::string& debugName)
    {
      // Cache lookup — skip shaderc if we already have SPIR-V for this exact source.
      const uint64_t hash      = SpvCacheHash(source, stage);
      const std::string cached = SpvCachePath(hash, stage);
      if (auto spv = LoadSpv(cached); !spv.empty())
        return spv;

      shaderc::Compiler compiler;
      shaderc::CompileOptions options;
      options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
      options.SetSourceLanguage(shaderc_source_language_glsl);

      // Engine GLSL source files use `#version 300 es` for the GL ES 3.00 path. Vulkan/SPIR-V
      // requires 450 core (layout qualifiers, etc.). SetForcedVersionProfile makes shaderc
      // ignore the in-source #version directive and treat the source as 450 core instead.
      options.SetForcedVersionProfile(450, shaderc_profile_core);

      // Activates the VULKAN preprocessor macro. Engine shader sources use:
      //   TK_UBO_BINDING(n)     -> layout(std140, binding = n)   (Vulkan)
      //   TK_SAMPLER_BINDING(n) -> layout(binding = n)            (Vulkan)
      // defined in vulkanCompatInc.shader via `#ifdef VULKAN`.
      // NOTE: shaderc automatically predefines VULKAN (= Vulkan version, e.g. 100) when
      // SetTargetEnvironment is Vulkan, so we must NOT call AddMacroDefinition("VULKAN")
      // here — that would redefine it with a different value and cause a compile error.

      // Engine GLSL declares inter-stage varyings without explicit layout(location=N)
      // (GLSL ES 3.00 style matched by name). SPIR-V requires explicit locations; shaderc
      // auto-assigns them using name-based matching between vertex outputs and fragment inputs.
      options.SetAutoMapLocations(true);

      // Stage 7d-1 binding remap: shift every UBO binding up by kUboBindingBase so GL UBO slots
      // (3, 4, 7, ...) land outside the texture binding range (0..7). Textures are not shifted.
      // See VulkanBindings.h for the full convention + concrete layout.
      options.SetBindingBase(shaderc_uniform_kind_buffer, VulkanBindings::kUboBindingBase);

#ifdef TK_DEBUG
      options.SetGenerateDebugInfo();
      options.SetOptimizationLevel(shaderc_optimization_level_zero);
#else
      options.SetOptimizationLevel(shaderc_optimization_level_performance);
#endif

      shaderc::SpvCompilationResult result =
          compiler.CompileGlslToSpv(source, ToShadercKind(stage), debugName.c_str(), options);
      if (result.GetCompilationStatus() != shaderc_compilation_status_success)
      {
        TK_ERR("VulkanShader: GLSL compile failed for '%s' (%d errors): %s",
               debugName.c_str(),
               (int) result.GetNumErrors(),
               result.GetErrorMessage().c_str());
        return {};
      }
      std::vector<uint32_t> spv(result.cbegin(), result.cend());
      SaveSpv(cached, spv);
      return spv;
    }

    VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t>& spirv)
    {
      if (device == VK_NULL_HANDLE || spirv.empty())
      {
        return VK_NULL_HANDLE;
      }
      VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      ci.codeSize    = spirv.size() * sizeof(uint32_t);
      ci.pCode       = spirv.data();

      VkShaderModule mod = VK_NULL_HANDLE;
      if (VkResult r = vkCreateShaderModule(device, &ci, nullptr, &mod); r != VK_SUCCESS)
      {
        TK_ERR("vkCreateShaderModule failed: %d", r);
        return VK_NULL_HANDLE;
      }
      return mod;
    }

  } // namespace VulkanShader
} // namespace ToolKit
