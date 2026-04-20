/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

struct SDL_Window;

namespace ToolKit
{
  namespace Launcher
  {

    namespace LauncherGPU
    {
      void InitImGui(SDL_Window* window, void* context);
      void ShutdownImGui();
      void ImGuiNewFrame();
      void ImGuiRenderDrawData();
    } // namespace LauncherGPU

  } // namespace Launcher
} // namespace ToolKit
