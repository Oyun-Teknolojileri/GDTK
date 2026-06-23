/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include <IGraphicsBackend.h>

#include <cstdint>

struct SDL_Window;

namespace ToolKit
{
  namespace Editor
  {

    /**
     * All window / graphics-context / present / ImGui-backend operations that differ between GL and
     * Vulkan builds live here. Editor code above this layer (main.cpp, App.cpp, UI.cpp) calls these
     * helpers without knowing which backend is active. The TK_VULKAN preprocessor selects the
     * implementation at compile time.
     */
    namespace EditorBackendBindings
    {
      /** SDL_WINDOW_OPENGL or SDL_WINDOW_VULKAN. OR with other flags when calling SDL_CreateWindow. */
      uint32_t GetSDLWindowFlags();

      /** Sets SDL_GL_SetAttribute values (GL build) before SDL_CreateWindow. No-op on Vulkan. */
      void PrepareWindowAttributes();

      /** Creates the rendering context. Returns opaque handle (SDL_GLContext for GL, nullptr for VK). */
      void* CreateGraphicsContext(SDL_Window* window);

      /** Destroys a context previously returned by CreateGraphicsContext. */
      void DestroyGraphicsContext(void* context);

      /** Fills backend-specific fields of BackendInitParams (getProcAddress / windowHandle). */
      void FillBackendInitParams(IGraphicsBackend::BackendInitParams& params, SDL_Window* window);

      /** Whether the default framebuffer is sRGB-encoded. */
      bool IsBackbufferSrgb();

      /** Swap interval: 0 = off, 1 = vsync, -1 = adaptive (GL only). */
      void SetSwapInterval(int interval);

      /** Presents the backbuffer for the given window. */
      void PresentBackbuffer(SDL_Window* window);

      // ImGui backend wrappers — implementations vary per graphics backend.
      void InitImGui(SDL_Window* window, void* context);
      void ShutdownImGui();
      void ImGuiNewFrame();
      void ImGuiRenderDrawData();
      void MakeContextCurrent(SDL_Window* window, void* context);
    } // namespace EditorBackendBindings

  } // namespace Editor
} // namespace ToolKit
