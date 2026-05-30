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

  /** GLSL → SPIR-V + VkShaderModule helpers. Caller owns lifetimes. */
  namespace VulkanShader
  {

    enum class Stage
    {
      Vertex,
      Fragment
    };

    /** Compiles GLSL to SPIR-V; empty on failure (logged). @p debugName feeds shaderc diagnostics. */
    std::vector<uint32_t> CompileGlslToSpirv(Stage stage, const std::string& source, const std::string& debugName);

    /** VK_NULL_HANDLE on failure. */
    VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t>& spirv);

  } // namespace VulkanShader

} // namespace ToolKit
