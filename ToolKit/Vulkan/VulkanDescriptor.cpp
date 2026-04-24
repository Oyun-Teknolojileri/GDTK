/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "VulkanDescriptor.h"

#include "../Logger.h"

namespace ToolKit
{
  namespace VulkanDescriptor
  {

    VkDescriptorSetLayout CreateLayoutSingleSampler(VkDevice device, VkShaderStageFlags stages)
    {
      VkDescriptorSetLayoutBinding binding{};
      binding.binding         = 0;
      binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      binding.descriptorCount = 1;
      binding.stageFlags      = stages;

      VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      ci.bindingCount = 1;
      ci.pBindings    = &binding;

      VkDescriptorSetLayout layout = VK_NULL_HANDLE;
      if (VkResult r = vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout); r != VK_SUCCESS)
      {
        TK_ERR("vkCreateDescriptorSetLayout failed: %d", r);
        return VK_NULL_HANDLE;
      }
      return layout;
    }

    VkDescriptorSetLayout CreateLayoutSamplerAndUbo(VkDevice device,
                                                    VkShaderStageFlags samplerStages,
                                                    VkShaderStageFlags uboStages)
    {
      VkDescriptorSetLayoutBinding bindings[2]{};
      bindings[0].binding         = 0;
      bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      bindings[0].descriptorCount = 1;
      bindings[0].stageFlags      = samplerStages;
      bindings[1].binding         = 1;
      bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      bindings[1].descriptorCount = 1;
      bindings[1].stageFlags      = uboStages;

      VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      ci.bindingCount = 2;
      ci.pBindings    = bindings;

      VkDescriptorSetLayout layout = VK_NULL_HANDLE;
      if (VkResult r = vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout); r != VK_SUCCESS)
      {
        TK_ERR("vkCreateDescriptorSetLayout (sampler+ubo) failed: %d", r);
        return VK_NULL_HANDLE;
      }
      return layout;
    }

    VkDescriptorSet AllocateSet(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout)
    {
      VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      ai.descriptorPool     = pool;
      ai.descriptorSetCount = 1;
      ai.pSetLayouts        = &layout;

      VkDescriptorSet set = VK_NULL_HANDLE;
      if (VkResult r = vkAllocateDescriptorSets(device, &ai, &set); r != VK_SUCCESS)
      {
        TK_ERR("vkAllocateDescriptorSets failed: %d", r);
        return VK_NULL_HANDLE;
      }
      return set;
    }

    void WriteCombinedImageSampler(VkDevice device,
                                   VkDescriptorSet set,
                                   uint32_t binding,
                                   VkImageView view,
                                   VkSampler sampler)
    {
      VkDescriptorImageInfo info{};
      info.sampler     = sampler;
      info.imageView   = view;
      info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w.dstSet          = set;
      w.dstBinding      = binding;
      w.dstArrayElement = 0;
      w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      w.descriptorCount = 1;
      w.pImageInfo      = &info;

      vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    }

    void WriteUniformBuffer(VkDevice device,
                            VkDescriptorSet set,
                            uint32_t binding,
                            VkBuffer buffer,
                            VkDeviceSize offset,
                            VkDeviceSize range)
    {
      VkDescriptorBufferInfo info{};
      info.buffer = buffer;
      info.offset = offset;
      info.range  = range;

      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w.dstSet          = set;
      w.dstBinding      = binding;
      w.dstArrayElement = 0;
      w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      w.descriptorCount = 1;
      w.pBufferInfo     = &info;

      vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    }

  } // namespace VulkanDescriptor
} // namespace ToolKit
