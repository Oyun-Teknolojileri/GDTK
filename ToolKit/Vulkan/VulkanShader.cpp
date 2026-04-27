/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanShader.h"

#include "../Logger.h"
#include "VulkanBindings.h"

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

    std::vector<uint32_t> CompileGlslToSpirv(Stage stage, const std::string& source, const std::string& debugName)
    {
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
      return std::vector<uint32_t>(result.cbegin(), result.cend());
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
