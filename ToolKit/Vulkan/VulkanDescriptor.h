/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../Types.h"

#include <vulkan/vulkan.h>

namespace ToolKit
{

  /**
   * Stage 4b scaffold — minimum-surface helpers for building a single-binding combined
   * image-sampler descriptor set. Keeps the test pipeline's Init readable without smearing
   * vkCreateDescriptorSetLayout / vkAllocateDescriptorSets / vkUpdateDescriptorSets boilerplate
   * across it. Stage 7 folds this into a real descriptor-set manager keyed off shader reflection.
   */
  namespace VulkanDescriptor
  {

    /** binding=0, type=COMBINED_IMAGE_SAMPLER, descriptorCount=1, shader stage = @p stages. */
    VkDescriptorSetLayout CreateLayoutSingleSampler(VkDevice device, VkShaderStageFlags stages);

    /** Two-binding layout: binding=0 COMBINED_IMAGE_SAMPLER (@p samplerStages), binding=1
     *  UNIFORM_BUFFER (@p uboStages). Used by the Stage 5 test pipeline for the
     *  camera UBO + checkerboard sampler combo. */
    VkDescriptorSetLayout CreateLayoutSamplerAndUbo(VkDevice device,
                                                    VkShaderStageFlags samplerStages,
                                                    VkShaderStageFlags uboStages);

    /** Allocates a single descriptor set of @p layout from @p pool. Returns VK_NULL_HANDLE on
     *  failure (logged). @p pool must have been created with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
     *  if the caller intends to free the set before pool destruction. */
    VkDescriptorSet AllocateSet(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout);

    /** Issues a single vkUpdateDescriptorSets call writing @p view + @p sampler into
     *  (@p set, @p binding) at descriptor type COMBINED_IMAGE_SAMPLER, layout
     *  SHADER_READ_ONLY_OPTIMAL. */
    void WriteCombinedImageSampler(VkDevice device,
                                   VkDescriptorSet set,
                                   uint32_t binding,
                                   VkImageView view,
                                   VkSampler sampler);

    /** Writes (@p buffer, @p offset, @p range) into (@p set, @p binding) as UNIFORM_BUFFER. */
    void WriteUniformBuffer(VkDevice device,
                            VkDescriptorSet set,
                            uint32_t binding,
                            VkBuffer buffer,
                            VkDeviceSize offset,
                            VkDeviceSize range);

  } // namespace VulkanDescriptor

} // namespace ToolKit
