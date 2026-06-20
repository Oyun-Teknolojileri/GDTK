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

#include <vector>

namespace ToolKit
{
  namespace PlatformHelpers
  {
    // Returns the editor executable name for the current build
    // configuration.
    //
    // The trailing "d" suffix is a Windows/MSVC convention (debug
    // builds are named "Editord.exe" via the .vcxproj TargetName).
    // It does NOT apply on Linux: the CMake build always names the
    // binary "Editor" (see add_executable(Editor ...) in
    // Editor/CMakeLists.txt), regardless of build type. CMake's
    // TK_DEBUG define is still set on Linux, but the resulting
    // binary on disk is just "Editor" -- appending a "d" there
    // would point at a file that does not exist.
    //
    // Declared ABOVE the platform-header include so that the
    // inline implementations of GetEditorExecutablePath() and
    // GetPackerExecutablePath() (which live in the platform
    // headers) can compose them via GetSiblingExecutablePath(name).
    inline String GetEditorExecutableName()
    {
#ifdef _WIN32
  #ifdef TK_DEBUG
      return "Editord.exe";
  #else
      return "Editor.exe";
  #endif
#else
      return "Editor";
#endif
    }

    // Same convention as GetEditorExecutableName but for the Packer
    // sibling binary. Used by PublishManager to locate the packer
    // without a hardcoded .exe suffix.
    inline String GetPackerExecutableName()
    {
#ifdef _WIN32
      return "Packer.exe";
#else
      return "Packer";
#endif
    }

  } // namespace PlatformHelpers
} // namespace ToolKit

#ifdef _WIN32
  #include "Win32Utils.h"
#else
  #include "LinuxUtils.h"
#endif

namespace ToolKit
{
  namespace PlatformHelpers
  {
    // Resolves the absolute path of the running executable itself.
    // Implementation lives in the platform-specific header
    // (LinuxUtils.h / Win32Utils.h) -- this single entry point keeps
    // the #ifdef dance in one place so callers never need to know
    // the platform. Mostly a building block for GetSiblingExecutablePath.
    inline String GetExecutablePath();

    // Returns the directory containing the running executable
    // (with trailing separator). Building block for sibling lookups.
    inline String GetExecutableDirectory();

    // Returns the absolute path of `name` interpreted as a binary
    // that lives next to the current process. The launcher uses
    // this to locate the editor (both ship in Bin/), the editor
    // uses it to locate the packer, etc. No $PATH lookup, no
    // hardcoded Bin/ relative path -- just a sibling of the
    // currently running binary.
    inline String GetSiblingExecutablePath(const String& name);

    // Returns the absolute path of the editor binary. The editor
    // ships next to the current process (both in Bin/), so this
    // is just GetSiblingExecutablePath(GetEditorExecutableName()).
    // When invoked from the launcher, returns the editor's path,
    // not the launcher's -- the launcher's argv[0] would otherwise
    // recursively re-launch the launcher.
    inline String GetEditorExecutablePath();

    // Same as GetEditorExecutablePath but for the packer sibling.
    inline String GetPackerExecutablePath();

    // Builds the editor command-line fragment that selects a workspace
    // and project: "--workspace "<ws>" --project-name "<proj>"".
    // Kept as a String for files that need a single argument blob
    // (e.g. Windows .lnk SetArguments, Linux .desktop Exec=). Code
    // that actually launches the editor should prefer the argv form
    // below so the executor can avoid shell parsing.
    inline String BuildEditorLaunchArgs(const String& workspacePath,
                                        const String& projectName)
    {
      return "--workspace \"" + workspacePath + "\" --project-name \"" + projectName + "\"";
    }

    // Same args as BuildEditorLaunchArgs but as a tokenized argv that
    // can be handed straight to SysComExec(const StringArray&,
    // ...). Avoids the shell-escaping pitfalls the string form has
    // (paths with spaces, quotes, dollar signs, etc.) because each
    // token reaches the child process verbatim.
    inline StringArray BuildEditorLaunchArgv(const String& workspacePath,
                                             const String& projectName)
    {
      return {String("--workspace"), workspacePath, String("--project-name"), projectName};
    }
  } // namespace PlatformHelpers
} // namespace ToolKit
