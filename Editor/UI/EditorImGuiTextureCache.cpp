/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "EditorImGuiTextureCache.h"

#include <RenderSystem.h>
#include <Renderer.h>
#include <ToolKit.h>

#ifdef TK_VULKAN
  #include <Vulkan/VulkanContext.h>
  #include <Vulkan/VulkanResources.h>
  #include <imgui/backends/imgui_impl_vulkan.h>
  #include <vulkan/vulkan.h>

  #include <memory>
  #include <unordered_map>
#endif

namespace ToolKit
{
  namespace Editor
  {
    namespace EditorImGuiTextureCache
    {

#ifdef TK_VULKAN

      struct Entry
      {
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        VkImageView swizzledView   = VK_NULL_HANDLE; ///< View with alpha swizzle=ONE; owned by this entry.
        // Tracks the lifetime of the backing VulkanTexture indirectly via Texture::m_gpuData.
        // When the texture is destroyed (or its gpu data swapped) the weak_ptr expires and
        // Sweep() drops the dangling cache row before any caller can re-use the raw pointer.
        std::weak_ptr<GpuResourceData> guard;
      };

      // Keyed by the raw VulkanTexture* identity. This is stable for the lifetime of the
      // owning shared_ptr — the weak_ptr above is the safety net against dangling lookups.
      static std::unordered_map<VulkanTexture*, Entry> g_cache;

      uint64 Acquire(const TexturePtr& tex, bool swizzleAlphaOne)
      {
        if (tex == nullptr || tex->m_gpuData == nullptr)
        {
          return 0;
        }

        auto* vt = static_cast<VulkanTexture*>(tex->m_gpuData.get());
        if (vt == nullptr || vt->view == VK_NULL_HANDLE || vt->sampler == VK_NULL_HANDLE)
        {
          return 0;
        }

        auto it = g_cache.find(vt);
        if (it != g_cache.end())
        {
          // Same VulkanTexture* could in theory be re-used after a destroy/recreate cycle if
          // the allocator hands out the same heap slot. The weak_ptr guard catches this — if
          // it expired, treat the slot as stale and rebuild.
          if (!it->second.guard.expired())
          {
            return (uint64) (uintptr_t) it->second.descriptor;
          }
          ImGui_ImplVulkan_RemoveTexture(it->second.descriptor);
          if (it->second.swizzledView != VK_NULL_HANDLE)
          {
            vkDestroyImageView(vt->context->GetDevice(), it->second.swizzledView, nullptr);
          }
          g_cache.erase(it);
        }

        // Optionally create a view with alpha swizzle = ONE. This is only needed for viewport
        // textures so ImGui always sees alpha=1.0 when blending the viewport onto the swapchain.
        // Icon / UI textures use their natural alpha, so swizzle is skipped for them.
        VkImageView swizzledView = VK_NULL_HANDLE;
        if (swizzleAlphaOne)
        {
          VkImageViewCreateInfo vci {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
          vci.image                       = vt->image;
          vci.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
          vci.format                      = vt->format;
          vci.components.r                = VK_COMPONENT_SWIZZLE_R;
          vci.components.g                = VK_COMPONENT_SWIZZLE_G;
          vci.components.b                = VK_COMPONENT_SWIZZLE_B;
          vci.components.a                = VK_COMPONENT_SWIZZLE_ONE; // force alpha=1
          vci.subresourceRange.aspectMask = vt->aspect;
          vci.subresourceRange.levelCount = vt->mipLevels;
          vci.subresourceRange.layerCount = vt->arrayLayers;
          vkCreateImageView(vt->context->GetDevice(), &vci, nullptr, &swizzledView);
        }

        VkImageView viewToUse = (swizzledView != VK_NULL_HANDLE) ? swizzledView : vt->view;
        VkDescriptorSet ds =
            ImGui_ImplVulkan_AddTexture(vt->sampler, viewToUse, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (ds == VK_NULL_HANDLE)
        {
          if (swizzledView != VK_NULL_HANDLE)
          {
            vkDestroyImageView(vt->context->GetDevice(), swizzledView, nullptr);
          }
          return 0;
        }

        Entry e;
        e.descriptor   = ds;
        e.swizzledView = swizzledView;
        e.guard        = std::weak_ptr<GpuResourceData>(tex->m_gpuData);
        g_cache.emplace(vt, e);
        return (uint64) (uintptr_t) ds;
      }

      void Sweep()
      {
        for (auto it = g_cache.begin(); it != g_cache.end();)
        {
          if (it->second.guard.expired())
          {
            ImGui_ImplVulkan_RemoveTexture(it->second.descriptor);
            if (it->second.swizzledView != VK_NULL_HANDLE)
            {
              vkDestroyImageView(it->first->context->GetDevice(), it->second.swizzledView, nullptr);
            }
            it = g_cache.erase(it);
          }
          else
          {
            ++it;
          }
        }
      }

      void Clear()
      {
        for (auto& kv : g_cache)
        {
          ImGui_ImplVulkan_RemoveTexture(kv.second.descriptor);
          if (kv.second.swizzledView != VK_NULL_HANDLE)
          {
            vkDestroyImageView(kv.first->context->GetDevice(), kv.second.swizzledView, nullptr);
          }
        }
        g_cache.clear();
      }

#else // !TK_VULKAN

      uint64 Acquire(const TexturePtr& tex, bool /*swizzleAlphaOne*/) { return Renderer::GetNativeTextureHandle(tex); }

      void Sweep() {}

      void Clear() {}

#endif

    } // namespace EditorImGuiTextureCache
  } // namespace Editor
} // namespace ToolKit
