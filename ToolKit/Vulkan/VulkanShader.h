/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../Types.h"

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace ToolKit
{

  /**
   * GLSL → SPIR-V compilation + VkShaderModule creation helpers.
   * Stateless: callers own the returned SPIR-V buffer and shader module and are responsible for
   * destroying the module. (Stage 2b will introduce a pipeline cache that owns module lifetimes.)
   */
  namespace VulkanShader
  {

    enum class Stage
    {
      Vertex,
      Fragment
    };

    /**
     * Compiles GLSL source to SPIR-V. Logs and returns empty vector on failure.
     * @param stage     - shader stage (drives shaderc kind).
     * @param source    - GLSL source text (must contain `#version` directive).
     * @param debugName - free-form name used only in shaderc diagnostics + log messages.
     */
    std::vector<uint32_t> CompileGlslToSpirv(Stage stage, const std::string& source, const std::string& debugName);

    /** Creates a VkShaderModule from a SPIR-V word buffer. Returns VK_NULL_HANDLE on failure. */
    VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t>& spirv);

  } // namespace VulkanShader

} // namespace ToolKit
