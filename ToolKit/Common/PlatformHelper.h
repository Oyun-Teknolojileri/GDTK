/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

// Single entry point for the editor's host-glue helpers (process spawn,
// plugin dlopen, log writer, file-manager open, config dir, etc.).
//
// Callers include this header and use ToolKit::PlatformHelpers::* directly
// -- they never need to know whether the underlying implementation is
// Win32Utils.h or LinuxUtils.h, so the #ifdef _WIN32 dance stays here
// in exactly one place.
//
// The two platform-specific headers still exist as standalone files and
// keep their own self-checks (Win32Utils.h is a no-op when not on _WIN32,
// LinuxUtils.h #errors if you accidentally include it on _WIN32). That
// keeps the existing direct-include paths working for anything that
// hasn't migrated yet, but new code should go through this header.

#ifdef _WIN32
  #include "Win32Utils.h"
#else
  #include "LinuxUtils.h"
#endif
