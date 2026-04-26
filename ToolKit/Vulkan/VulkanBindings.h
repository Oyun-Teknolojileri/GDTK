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
   * Stage 7d binding convention ? resolves the GL/Vulkan binding-namespace clash without
   * touching shader sources.
   *
   * GL has separate namespaces for sampler texture units (slots 0..7) and uniform block bindings
   * (slots 3, 4, 7, 8, 9, 10). Vulkan unifies them into a single descriptor set, so the GL
   * slot numbers collide (e.g. texture slot 3 vs CameraGpuBuffer at UBO slot 3).
   *
   * Solution: shaderc compile-time remap. We tell shaderc to shift every UBO binding up by
   * @ref kUboBindingBase before it lands in SPIR-V. Texture bindings stay at their GL slot
   * indices unchanged. With kUboBindingBase = 8 (matching texture slot count) the two ranges
   * never overlap, regardless of which GL UBO slot a shader picks.
   *
   * Binding layout in the SPIR-V / VkDescriptorSetLayout (set = 0):
   *   bindings 0..7   ? combined image samplers (texture slots, GL slot N \u2192 binding N)
   *   bindings 8..    ? uniform buffers (GL UBO slot N \u2192 binding N + kUboBindingBase)
   *
   * Concrete UBO layout for ToolKit's known buffers (resolved via @ref UboBindingFor):
   *   GL slot 3  CameraGpuBuffer              \u2192 Vulkan binding 11
   *   GL slot 4  GraphicConstantsGpuBuffer    \u2192 Vulkan binding 12
   *   GL slot 7  DirectionalLightBuffer.light \u2192 Vulkan binding 15
   *   GL slot 8  PointLightCache              \u2192 Vulkan binding 16
   *   GL slot 9  SpotLightCache               \u2192 Vulkan binding 17
   *   GL slot 10 DirectionalLightBuffer.pvm   \u2192 Vulkan binding 18
   *
   * Future per-draw UBO uses @ref kPerDrawUboBinding (Stage 7d-4).
   */
  namespace VulkanBindings
  {

    /** Number of combined image sampler bindings reserved at the start of the descriptor set.
        Mirrors RHIConstants::TextureSlotCount; the value is duplicated here so this header has
        no engine-side dependency. */
    constexpr uint kTextureBindingCount = 8;

    /** Texture bindings start at 0 \u2014 GL sampler slot index maps directly. */
    constexpr uint kTextureBindingBase  = 0;

    /** UBO bindings shift by this much. shaderc receives this as the binding base for
        @c shaderc_uniform_kind_buffer; every @c layout(binding=N) UBO in shader source becomes
        binding N + kUboBindingBase in the produced SPIR-V. */
    constexpr uint kUboBindingBase      = kTextureBindingCount; // = 8

    /** Resolves a GL UBO slot to its Vulkan binding (after shaderc remap). */
    constexpr uint UboBindingFor(uint glSlot) { return glSlot + kUboBindingBase; }

    /** Dedicated binding for the per-draw dynamic UBO (PerDrawUniforms ring buffer). Mapped via
        UboBindingFor(6) so it lines up with shaderc's UBO remap; GL UBO slot 6 is unused, so
        no shader will ever generate the same binding. The value is bound as
        UNIFORM_BUFFER_DYNAMIC; the per-draw offset is supplied via vkCmdBindDescriptorSets'
        dynamicOffset rather than rewriting the descriptor. */
    constexpr uint kPerDrawUboBinding   = UboBindingFor(6); // = 14

    /** Total binding count the global descriptor set layout reserves. Fits every entry in the
        binding table above with headroom; bindings beyond this should never appear in shaders. */
    constexpr uint kMaxBindings         = 32;

  } // namespace VulkanBindings

} // namespace ToolKit
