/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include <Texture.h>
#include <Types.h>

namespace ToolKit
{
  namespace Editor
  {

    /**
     * Editor-owned cache that hands out ImGui-ready texture handles backed by the active
     * graphics backend. Keeps ToolKit free of any ImGui dependency: ToolKit hands us the raw
     * native handle (GL texture name, or VulkanTexture*) and we do whatever wrapping ImGui
     * needs (in Vulkan: registering a descriptor via ImGui_ImplVulkan_AddTexture).
     *
     * Returned values are uint64 to keep call sites pointer-width agnostic and convertible
     * to ImTextureID via ConvertUIntImGuiTexture.
     */
    namespace EditorImGuiTextureCache
    {

      /**
       * Returns an ImGui-ready handle for the given texture. Safe to pass through
       * ConvertUIntImGuiTexture and into ImGui::Image / UI::ImageButton helpers.
       *
       * - GL build: forwards to Renderer::GetNativeTextureHandle(tex).
       * - VK build: looks up (or lazily creates) the ImGui_ImplVulkan_AddTexture descriptor
       *             for the texture's VulkanTexture, returns the descriptor cast to uint64.
       *
       * Returns 0 if the texture is null or has no GPU data yet.
       */
      uint64 Acquire(const TexturePtr& tex, bool swizzleAlphaOne = false);

      /**
       * Frame-start sweep: removes cache entries whose backing GpuResourceData has expired
       * (texture destroyed). Calls ImGui_ImplVulkan_RemoveTexture on each removed entry.
       * No-op on the GL build.
       */
      void Sweep();

      /**
       * Tears down all cached descriptors. Must be called before ImGui_ImplVulkan_Shutdown
       * because the descriptors are owned by the ImGui Vulkan backend's pool.
       * No-op on the GL build.
       */
      void Clear();

    } // namespace EditorImGuiTextureCache

  } // namespace Editor
} // namespace ToolKit
