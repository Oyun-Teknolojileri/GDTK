/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Types.h"

#include <vulkan/vulkan.h>

namespace ToolKit
{

  /** Thin helpers around descriptor layout/set creation + writes. */
  namespace VulkanDescriptor
  {

    /** binding=0, COMBINED_IMAGE_SAMPLER, descriptorCount=1. */
    VkDescriptorSetLayout CreateLayoutSingleSampler(VkDevice device, VkShaderStageFlags stages);

    /** Sampler + UBO two-binding layout. */
    VkDescriptorSetLayout CreateLayoutSamplerAndUbo(VkDevice device,
                                                    uint samplerBinding,
                                                    VkShaderStageFlags samplerStages,
                                                    uint uboBinding,
                                                    VkShaderStageFlags uboStages);

    /** VK_NULL_HANDLE on failure. Pool must have FREE_DESCRIPTOR_SET_BIT if caller intends to
        free the set before pool destruction. */
    VkDescriptorSet AllocateSet(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout);

    void WriteCombinedImageSampler(VkDevice device,
                                   VkDescriptorSet set,
                                   uint32_t binding,
                                   VkImageView view,
                                   VkSampler sampler);

    void WriteUniformBuffer(VkDevice device,
                            VkDescriptorSet set,
                            uint32_t binding,
                            VkBuffer buffer,
                            VkDeviceSize offset,
                            VkDeviceSize range);

    /** UNIFORM_BUFFER_DYNAMIC — per-draw offset comes via pDynamicOffsets at bind time. */
    void WriteUniformBufferDynamic(VkDevice device,
                                   VkDescriptorSet set,
                                   uint32_t binding,
                                   VkBuffer buffer,
                                   VkDeviceSize offset,
                                   VkDeviceSize range);

  } // namespace VulkanDescriptor

} // namespace ToolKit
