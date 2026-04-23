/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "EditorImGuiTextureCache.h"

#include <Renderer.h>
#include <RenderSystem.h>
#include <ToolKit.h>

#ifdef TK_VULKAN
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
        // Tracks the lifetime of the backing VulkanTexture indirectly via Texture::m_gpuData.
        // When the texture is destroyed (or its gpu data swapped) the weak_ptr expires and
        // Sweep() drops the dangling cache row before any caller can re-use the raw pointer.
        std::weak_ptr<GpuResourceData> guard;
      };

      // Keyed by the raw VulkanTexture* identity. This is stable for the lifetime of the
      // owning shared_ptr — the weak_ptr above is the safety net against dangling lookups.
      static std::unordered_map<VulkanTexture*, Entry> g_cache;

      uint64 Acquire(const TexturePtr& tex)
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
          g_cache.erase(it);
        }

        VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(vt->sampler,
                                                         vt->view,
                                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (ds == VK_NULL_HANDLE)
        {
          return 0;
        }

        Entry e;
        e.descriptor = ds;
        e.guard      = std::weak_ptr<GpuResourceData>(tex->m_gpuData);
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
        }
        g_cache.clear();
      }

#else // !TK_VULKAN

      uint64 Acquire(const TexturePtr& tex) { return Renderer::GetNativeTextureHandle(tex); }

      void Sweep() {}

      void Clear() {}

#endif

    } // namespace EditorImGuiTextureCache
  } // namespace Editor
} // namespace ToolKit
