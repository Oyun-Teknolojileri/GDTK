/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

namespace ToolKit
{

  enum class PLATFORM
  {
    TKWindows,
    TKWeb,
    TKAndroid,
    TKMac
  };

// Debug flag.
#ifdef TK_DEBUG
  static constexpr int TKDebug = 1;
#else
  static constexpr int TKDebug = 0;
#endif

// Detect platform.
#if defined(_WIN32) || defined(_WIN64)
  #define TK_PLATFORM PLATFORM::TKWindows
  #define TK_WIN
#elif defined(__ANDROID__)
  #define TK_PLATFORM PLATFORM::TKAndroid
  #define TK_ANDROID
#elif defined(__EMSCRIPTEN__)
  #define TK_PLATFORM PLATFORM::TKWeb
  #define TK_WEB
#elif defined(__APPLE__)
  #include <TargetConditionals.h>
  #define TK_PLATFORM PLATFORM::TKMac
  #define TK_MAC
#else
  #error "Unknown platform!"
#endif

// Calling convention
#if defined(TK_WIN)
  #define TK_STDCAL __stdcall
#else
  #define TK_STDCAL
#endif

// Export/import macros for main API
#if defined(TK_WIN)
  #if defined(TK_DLL_EXPORT)
    #define TK_API __declspec(dllexport)
  #elif defined(TK_DLL_IMPORT)
    #define TK_API __declspec(dllimport)
  #else
    #define TK_API
  #endif
#else
  // On macOS, Linux, Web, Android: no special attributes needed for static library
  #define TK_API
#endif

// Plugin API
#if defined(TK_WIN)
  #define TK_PLUGIN_API __declspec(dllexport)
#else
  #define TK_PLUGIN_API
#endif

} // namespace ToolKit
