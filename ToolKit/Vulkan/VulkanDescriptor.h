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

    /** Two-binding layout for the Stage 5 test pipeline: a COMBINED_IMAGE_SAMPLER at
     *  @p samplerBinding (e.g. 0) and a UNIFORM_BUFFER at @p uboBinding (e.g.
     *  VulkanBindings::UboBindingFor(N) so it lines up with shaderc's UBO remap base). */
    VkDescriptorSetLayout CreateLayoutSamplerAndUbo(VkDevice device,
                                                    uint samplerBinding,
                                                    VkShaderStageFlags samplerStages,
                                                    uint uboBinding,
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

    /** Same as @ref WriteUniformBuffer but emits a UNIFORM_BUFFER_DYNAMIC descriptor. The
     *  buffer + offset + range here are the static base; the per-draw byte offset travels
     *  through vkCmdBindDescriptorSets' pDynamicOffsets parameter and is added on top of the
     *  static offset by the driver at access time. */
    void WriteUniformBufferDynamic(VkDevice device,
                                   VkDescriptorSet set,
                                   uint32_t binding,
                                   VkBuffer buffer,
                                   VkDeviceSize offset,
                                   VkDeviceSize range);

  } // namespace VulkanDescriptor

} // namespace ToolKit
