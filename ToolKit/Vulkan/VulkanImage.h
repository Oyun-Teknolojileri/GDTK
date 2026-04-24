/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../Types.h"

#include <vulkan/vulkan.h>

#include <memory>

namespace ToolKit
{

  class VulkanContext;
  struct VulkanTexture;

  /**
   * Standalone helpers that build sampleable VkImages from raw pixel data. Distinct from
   * VulkanBackend::CreateTexture (which wraps engine `Texture` resources and leaves the image
   * empty until a render pass writes to it) — these allocate, upload, and transition in one shot.
   * Stage 4 scaffold; Stage 7 generalizes this into the engine `Texture` upload path.
   */
  namespace VulkanImage
  {

    /**
     * Allocates a 2D, single-mip, color-only image with @p pixels uploaded via a transient staging
     * buffer. Final layout is SHADER_READ_ONLY_OPTIMAL. Sampler is linear, REPEAT addressing.
     *
     * @param byteCount must equal width * height * bytes-per-pixel for @p format.
     * Returns nullptr on failure (logged).
     */
    std::shared_ptr<VulkanTexture> CreateSampled2DFromData(VulkanContext* ctx,
                                                           VkFormat format,
                                                           uint32_t width,
                                                           uint32_t height,
                                                           const void* pixels,
                                                           VkDeviceSize byteCount);

  } // namespace VulkanImage

} // namespace ToolKit
