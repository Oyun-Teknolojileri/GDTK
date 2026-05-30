/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../Types.h"

namespace ToolKit
{

  /**
   * Binding convention — resolves the GL/Vulkan binding-namespace clash without touching
   * shader sources. GL has separate sampler / UBO namespaces; Vulkan unifies them. shaderc is
   * told to shift UBO bindings up by kUboBindingBase, so:
   *
   *   bindings 0..(kTextureBindingCount-1)   — combined image samplers (GL slot N → N)
   *   bindings kTextureBindingCount..         — uniform buffers (GL slot N → N + kUboBindingBase)
   *
   * UBO mapping for ToolKit's buffers (kTextureBindingCount = 32):
   *   GL 3=Camera→35   4=GraphicConsts→36   5=PassSpecific→37   6=PerDraw(dynamic)→38
   *   GL 7=DirLight→39  8=PointLights→40  9=SpotLights→41  10=DirLightPVM→42
   */
  namespace VulkanBindings
  {

    /** Texture binding count. Matches RHIConstants::TextureSlotCount; duplicated here to keep
        this header dependency-free. */
    constexpr uint kTextureBindingCount = 32;

    constexpr uint kTextureBindingBase  = 0;

    /** UBO binding shift handed to shaderc as the buffer binding base. */
    constexpr uint kUboBindingBase      = kTextureBindingCount;

    constexpr uint UboBindingFor(uint glSlot) { return glSlot + kUboBindingBase; }

    /** Per-draw dynamic UBO binding. Offset travels via vkCmdBindDescriptorSets' dynamicOffset. */
    constexpr uint kPerDrawUboBinding   = UboBindingFor(6); // = 38

    constexpr uint kMaxBindings         = 64;

  } // namespace VulkanBindings

} // namespace ToolKit
