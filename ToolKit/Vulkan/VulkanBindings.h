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
   * Stage 7d binding convention — resolves the GL/Vulkan binding-namespace clash without
   * touching shader sources.
   *
   * GL has separate namespaces for sampler texture units (slots 0..31, see
   * RHIConstants::TextureSlotCount) and uniform block bindings (slots 3, 4, 5, 7, 8, 9, 10).
   * Vulkan unifies them into a single descriptor set, so the GL slot numbers collide
   * (e.g. texture slot 11 vs CameraGpuBuffer's UBO slot 3 if both naively map to binding 11).
   *
   * Solution: shaderc compile-time remap. We tell shaderc to shift every UBO binding up by
   * @ref kUboBindingBase before it lands in SPIR-V. Texture bindings stay at their GL slot
   * indices unchanged. With kUboBindingBase = kTextureBindingCount the two ranges never
   * overlap, regardless of which GL UBO slot a shader picks.
   *
   * Binding layout in the SPIR-V / VkDescriptorSetLayout (set = 0):
   *   bindings 0..(kTextureBindingCount-1)   — combined image samplers (GL sampler slot N ? binding N)
   *   bindings kTextureBindingCount..        — uniform buffers (GL UBO slot N ? binding N + kUboBindingBase)
   *
   * Concrete UBO layout for ToolKit's known buffers (resolved via @ref UboBindingFor) with
   * kTextureBindingCount = 32:
   *   GL slot 3  CameraGpuBuffer              ? Vulkan binding 35
   *   GL slot 4  GraphicConstantsGpuBuffer    ? Vulkan binding 36
   *   GL slot 5  Pass-specific UBO (shared)   ? Vulkan binding 37
   *   GL slot 6  PerDrawData (dynamic)        ? Vulkan binding 38   @ref kPerDrawUboBinding
   *   GL slot 7  DirectionalLightBuffer.light ? Vulkan binding 39
   *   GL slot 8  PointLightCache              ? Vulkan binding 40
   *   GL slot 9  SpotLightCache               ? Vulkan binding 41
   *   GL slot 10 DirectionalLightBuffer.pvm   ? Vulkan binding 42
   */
  namespace VulkanBindings
  {

    /** Number of combined image sampler bindings reserved at the start of the descriptor set.
        Mirrors RHIConstants::TextureSlotCount; the value is duplicated here so this header has
        no engine-side dependency. Engine shaders use sampler slots up to 17 (IBL pre-filtered
        cubemap arrays); 32 matches the engine RHI constant and gives headroom. */
    constexpr uint kTextureBindingCount = 32;

    /** Texture bindings start at 0 — GL sampler slot index maps directly. */
    constexpr uint kTextureBindingBase  = 0;

    /** UBO bindings shift by this much. shaderc receives this as the binding base for
        @c shaderc_uniform_kind_buffer; every @c layout(binding=N) UBO in shader source becomes
        binding N + kUboBindingBase in the produced SPIR-V. */
    constexpr uint kUboBindingBase      = kTextureBindingCount; // = 32

    /** Resolves a GL UBO slot to its Vulkan binding (after shaderc remap). */
    constexpr uint UboBindingFor(uint glSlot) { return glSlot + kUboBindingBase; }

    /** Dedicated binding for the per-draw dynamic UBO (PerDrawUniforms ring buffer). Mapped via
        UboBindingFor(6) so it lines up with shaderc's UBO remap. The value is bound as
        UNIFORM_BUFFER_DYNAMIC; the per-draw offset is supplied via vkCmdBindDescriptorSets'
        dynamicOffset rather than rewriting the descriptor. */
    constexpr uint kPerDrawUboBinding   = UboBindingFor(6); // = 38

    /** Total binding count the global descriptor set layout reserves. Must comfortably cover
        bindings 0..42 (highest UBO binding) with headroom; bindings beyond this should never
        appear in shaders. Driver-side limit is typically 4096+ on desktop GPUs. */
    constexpr uint kMaxBindings         = 64;

  } // namespace VulkanBindings

} // namespace ToolKit
