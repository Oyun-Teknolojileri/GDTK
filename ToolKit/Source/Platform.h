/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/** If vulkan render backend is desired, define TK_VULKAN */
// #define TK_VULKAN

namespace ToolKit
{

  enum class PLATFORM
  {
    TKWindows,
    TKWeb,
    TKAndroid,
    TKLinux
  };

#ifdef TK_DEBUG
  static constexpr int TKDebug = 1;
#else
  static constexpr int TKDebug = 0;
#endif

#ifdef TK_VULKAN
  static constexpr int TKVulkan = 1;
#else
  static constexpr int TKVulkan = 0;
#endif

#ifdef _WIN32
  #define TK_PLATFORM PLATFORM::TKWindows
  #define TK_WIN
#elif __ANDROID__
  #define TK_PLATFORM PLATFORM::TKAndroid
  #define TK_ANDROID
#elif __EMSCRIPTEN__
  #define TK_PLATFORM PLATFORM::TKWeb
  #define TK_WEB
#elif __linux__
  #define TK_PLATFORM PLATFORM::TKLinux
  #define TK_LINUX
#endif

#ifdef TK_WIN // Windows.
  #define TK_STDCAL __stdcall
  #ifdef TK_DLL_EXPORT // Dynamic binding.
    #define TK_API __declspec(dllexport)
  #elif defined(TK_DLL_IMPORT)
    #define TK_API __declspec(dllimport)
  #else // Static binding.
    #define TK_API
  #endif
#else
  #define TK_API __attribute__((visibility("default")))
  #define TK_STDCAL
#endif

#ifdef TK_WIN // Windows.
  #define TK_PLUGIN_API __declspec(dllexport)
#else // Other OS.
  #define TK_PLUGIN_API
#endif

// Native file extension for dynamically loaded plugins. PluginManager::Load
// appends this to the binary path before dlopen()/LoadLibrary(), so it must
// match the file name the build system actually produces (xyzd.so / xyz.dll).
// The debug variant carries the same "d" postfix the build system appends in
// Debug (CMAKE_DEBUG_POSTFIX on the game / OUTPUT_NAME "ToolKit$<...:d>" on
// the engine), so a Debug editor finds the Debug plugin.
#ifdef TK_WIN
  #define TK_PLUGIN_EXT ".dll"
  #define TK_PLUGIN_DEBUG_EXT "d.dll"
#else
  #define TK_PLUGIN_EXT ".so"
  #define TK_PLUGIN_DEBUG_EXT "d.so"
#endif

} // namespace ToolKit
