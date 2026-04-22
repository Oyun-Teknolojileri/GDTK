/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "LauncherBackendBindings.h"

#include <SDL.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/imgui.h>

namespace ToolKit
{
  namespace Launcher
  {
    namespace LauncherBackendBindings
    {

      void InitImGui(SDL_Window* window, void* context)
      {
        ImGui_ImplSDL2_InitForOpenGL(window, context);
        ImGui_ImplOpenGL3_Init("#version 300 es");
      }

      void ShutdownImGui() { ImGui_ImplOpenGL3_Shutdown(); }

      void ImGuiNewFrame() { ImGui_ImplOpenGL3_NewFrame(); }

      void ImGuiRenderDrawData() { ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); }

    } // namespace LauncherBackendBindings
  }   // namespace Launcher
} // namespace ToolKit
