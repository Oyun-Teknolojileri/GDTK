/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

// Linux implementation of the PlatformHelpers namespace. The editor's
// main.cpp pulls this in on non-Windows to keep the editor's host glue
// (process spawning, plugin dlopen, log writer, etc.) portable without
// leaking #ifdefs through the rest of the engine. Mirrors the public
// surface of Win32Utils.h.

#ifdef _WIN32
  #error "LinuxUtils.h is the non-Windows implementation; do not include on _WIN32."
#endif

#include "Types.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <thread>

extern char** environ;

namespace ToolKit
{
  namespace PlatformHelpers
  {
    namespace UTF8Util
    {
      // Linux paths are already UTF-8 in std::string; this is a no-op
      // identity function kept for API symmetry with Win32Utils.h.
      inline std::wstring ConvertUTF8ToUTF16(const std::string& utf8String)
      {
        return std::wstring(utf8String.begin(), utf8String.end());
      }
    } // namespace UTF8Util

    // Returns the absolute path of the running executable by
    // resolving the /proc/self/exe symlink. Mirrors the Windows
    // GetModuleFileNameW-based resolver. Implementation detail
    // for GetExecutableDirectory() / GetSiblingExecutablePath().
    inline String GetExecutablePath()
    {
      std::error_code ec;
      std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe", ec);
      if (ec)
      {
        return String();
      }
      return PathToString(exePath.lexically_normal());
    }

    // Returns the directory containing the running executable
    // (with trailing separator). Use to resolve sibling binaries
    // that ship next to the current process (e.g. the editor
    // next to the launcher, both living in Bin/).
    inline String GetExecutableDirectory()
    {
      String exePath = GetExecutablePath();
      if (exePath.empty())
      {
        return String();
      }
      std::filesystem::path p(exePath);
      return PathToString(p.parent_path()) + GetPathSeparator();
    }

    // Returns the absolute path of `name` interpreted as a binary
    // that lives next to the current process. Lets the launcher
    // locate the editor, the editor locate the packer, etc.,
    // without hardcoded Bin/ relative paths or $PATH lookups.
    inline String GetSiblingExecutablePath(const String& name) { return GetExecutableDirectory() + name; }

    // Returns the absolute path of the editor binary. The editor
    // is expected to ship next to the current process (both live
    // in Bin/), so this is just a sibling lookup.
    inline String GetEditorExecutablePath() { return GetSiblingExecutablePath(GetEditorExecutableName()); }

    // Returns the absolute path of the packer binary. Same sibling
    // semantics as GetEditorExecutablePath().
    inline String GetPackerExecutablePath() { return GetSiblingExecutablePath(GetPackerExecutableName()); }

    // Helper: convert StringArray to a null-terminated
    // char** suitable for posix_spawn's argv parameter. Each entry
    // is duplicated so the spawn call can survive argv going out
    // of scope if needed (posix_spawn may copy internally on some
    // libcs, but the spec doesn't guarantee it for the file_actions
    // path on all platforms).
    inline char** ToNullTerminatedArgv(const StringArray& argv)
    {
      char** arr = new char*[argv.size() + 1];
      for (size_t i = 0; i < argv.size(); ++i)
      {
        arr[i] = new char[argv[i].size() + 1];
        std::memcpy(arr[i], argv[i].data(), argv[i].size());
        arr[i][argv[i].size()] = '\0';
      }
      arr[argv.size()] = nullptr;
      return arr;
    }

    inline void FreeNullTerminatedArgv(char** arr, size_t count)
    {
      if (arr == nullptr)
      {
        return;
      }
      for (size_t i = 0; i < count; ++i)
      {
        delete[] arr[i];
      }
      delete[] arr;
    }

    // Linux console command execution callback.
    //
    // Takes a tokenized argv (argv[0] is the executable). No shell
    // is invoked, so each argument reaches the child verbatim -- no
    // need to escape spaces, quotes, $vars, etc. on the caller side.
    //
    // async=true  -> spawn child, fire `callback` from a detached thread
    //                once the child exits; the call returns 0 immediately.
    // async=false -> wait for the child synchronously and return its
    //                exit status.
    inline int SysComExec(const StringArray& argv, bool async, bool showConsole, std::function<void(int)> callback)
    {
      (void) showConsole; // No Win32-style console window concept on Linux.

      if (argv.empty())
      {
        TK_ERR("SysComExec: empty argv.");
        return -1;
      }

      char** argvArr         = ToNullTerminatedArgv(argv);
      const size_t argcCount = argv.size();

      if (!async)
      {
        // Synchronous path: posix_spawnp + waitpid, no shell.
        // posix_spawnp performs $PATH lookup (terminal behaviour),
        // so bare binary names like "code" resolve correctly.
        pid_t pid = -1;
        int rc    = posix_spawnp(&pid,
                                 argvArr[0],
                                 nullptr, // file_actions
                                 nullptr, // attrp
                                 argvArr,
                                 environ);
        if (rc != 0)
        {
          TK_ERR("posix_spawn failed (%d): %s", rc, std::strerror(rc));
          FreeNullTerminatedArgv(argvArr, argcCount);
          return -1;
        }

        int status = 0;
        if (waitpid(pid, &status, 0) < 0)
        {
          TK_ERR("waitpid failed (%d).", errno);
          FreeNullTerminatedArgv(argvArr, argcCount);
          return -1;
        }

        FreeNullTerminatedArgv(argvArr, argcCount);

        int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        if (callback != nullptr)
        {
          callback(code);
        }
        return code;
      }

      // Async path: posix_spawnp, parent returns 0 immediately. Arm
      // a detached watcher thread that reaps and fires the callback.
      // posix_spawnp performs $PATH lookup so names like "code" work.
      pid_t pid = -1;
      int rc    = posix_spawnp(&pid, argvArr[0], nullptr, nullptr, argvArr, environ);
      FreeNullTerminatedArgv(argvArr, argcCount);

      if (rc != 0)
      {
        TK_ERR("posix_spawn failed (%d): %s", rc, std::strerror(rc));
        return -1;
      }

      if (callback != nullptr)
      {
        std::thread t(
            [pid, callback]() -> void
            {
              int status = 0;
              pid_t r    = waitpid(pid, &status, 0);
              if (r < 0)
              {
                callback(-1);
                return;
              }

              int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
              callback(code);
            });
        t.detach();
      }

      return 0;
    }

    // Write a log line to stderr tagged with the same prefixes the
    // Windows OutputDebugStringW path produces.
    inline void OutputLog(int logType, const char* szFormat, ...)
    {
      static const char* logNames[] = {"[Memo]", "[Error]", "[Warning]", "[Command]", "[Success]"};
      const char* tag               = (logType >= 0 && logType < 5) ? logNames[logType] : "[Log]";

      char szBuff[1024]             = {0};
      va_list arg;
      va_start(arg, szFormat);
      std::vsnprintf(szBuff, sizeof(szBuff), szFormat, arg);
      va_end(arg);

      std::fprintf(stderr, "%s %s\n", tag, szBuff);
    }

    // Open `utf8Path` in the platform's default file manager via xdg-open.
    inline void OpenExplorer(const StringView utf8Path)
    {
      std::filesystem::path systemPath = utf8Path;
      std::string systemPathStr        = PathToString(systemPath.lexically_normal());

      pid_t pid                        = fork();
      if (pid < 0)
      {
        TK_ERR("Failed to fork for xdg-open: %s", std::strerror(errno));
        return;
      }
      if (pid == 0)
      {
        // Detach from the editor's stdout/stderr so xdg-open's child
        // doesn't inherit our log streams. The trailing `&` lets the
        // file manager outlive the editor if needed.
        execlp("xdg-open", "xdg-open", systemPathStr.c_str(), (char*) nullptr);
        _exit(127);
      }

      // Reap the xdg-open helper asynchronously so we don't leave a
      // zombie around; ignore the exit code.
      int status = 0;
      waitpid(pid, &status, 0);

      if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
      {
        TK_ERR("Failed to open the folder: %s", utf8Path);
      }
    }

    // No Win32 console window on Linux; this is a no-op kept for API
    // symmetry with Win32Utils.h.
    inline void HideConsoleWindow() {}

    // Fix working directory when launched from a shortcut (the
    // shortcut's CWD is usually $HOME). Mirror the Win32 helper by
    // resolving the current executable's directory and chdir'ing there
    // so Resources/Config relative paths resolve the same way the
    // Windows binary expects.
    inline void SetWorkingDirectoryToBinFolder()
    {
      std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
      std::filesystem::path exeDir  = exePath.parent_path(); // .../Bin
      if (!exeDir.empty())
      {
        std::filesystem::current_path(exeDir);
      }
    }

    // Returns the per-user config directory to use for engine config
    // files (Workspace.settings, Editor.settings, etc.). Follows the
    // freedesktop.org XDG Base Directory spec:
    //   $XDG_CONFIG_HOME  -> use it directly
    //   otherwise         -> $HOME/.config
    // Empty / unset values are treated the same as missing. Returns
    // an empty String when no usable config dir can be resolved; the
    // caller should treat that as a soft failure and skip the
    // config bootstrap rather than abort.
    inline String GetUserConfigDir()
    {
      const char* xdg = getenv("XDG_CONFIG_HOME");
      if (xdg != nullptr && xdg[0] != '\0')
      {
        return xdg;
      }
      const char* home = getenv("HOME");
      if (home == nullptr || home[0] == '\0')
      {
        return String();
      }
      return std::string(home) + "/.config";
    }

    // Return the file's mtime as a string the same way the Win32 helper
    // does, so the plugin manager's "did the file change" logic keeps
    // working unchanged.
    inline String GetCreationTime(const String& fullPath)
    {
      struct stat st;
      if (stat(fullPath.c_str(), &st) != 0)
      {
        return String();
      }
      return std::to_string((long long) st.st_mtime);
    }

    // dlopen-based plugin loader. Returns the opaque module handle
    // (a void* pointing at the link_map chain), which the rest of the
    // editor hands back to TKFreeModule / TKGetFunction.
    inline void* TKLoadModule(StringView fullPath)
    {
      std::string pathStr(fullPath.data(), fullPath.size());
      // RTLD_NOW forces all symbols to resolve at load time, matching
      // the Windows LoadLibraryW behaviour the editor assumes.
      return dlopen(pathStr.c_str(), RTLD_NOW);
    }

    inline void TKFreeModule(void* module) { dlclose(module); }

    inline void* TKGetFunction(void* module, StringView func)
    {
      std::string funcStr(func.data(), func.size());
      return dlsym(module, funcStr.c_str());
    }

    // SDL drives the editor window on Linux; there is no separate Win32
    // HWND icon to set after SDL wipes ours. Kept as a no-op for API
    // symmetry.
    inline void UpdateAppIcon() {}

    // Create a desktop entry that launches the current editor
    // executable. On Linux the "shortcut" is a freedesktop.org .desktop
    // file in $XDG_DESKTOP_DIR (or $HOME/Desktop if unset). The args
    // match the Windows helper so the editor's project dialog can use
    // either platform's API without branching.
    inline bool CreateProjectShortcutOnDesktop(const String& shortcutName,
                                               const String& arguments,
                                               const String& exePathOverride = "")
    {
      std::string exePath =
          exePathOverride.empty() ? PathToString(std::filesystem::read_symlink("/proc/self/exe")) : exePathOverride;

      // Resolve $XDG_DESKTOP_DIR, falling back to $HOME/Desktop.
      std::string desktopDir;
      if (const char* xdg = std::getenv("XDG_DESKTOP_DIR"); xdg != nullptr && xdg[0] != '\0')
      {
        desktopDir = xdg;
      }
      else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
      {
        desktopDir = std::string(home) + "/Desktop";
      }
      else
      {
        return false;
      }

      std::filesystem::path desktopPath(desktopDir);
      if (!std::filesystem::is_directory(desktopPath))
      {
        return false;
      }

      std::filesystem::path shortcutPath = desktopPath / (shortcutName + ".desktop");
      std::ofstream file(shortcutPath);
      if (!file.is_open())
      {
        return false;
      }

      file << "[Desktop Entry]\n";
      file << "Type=Application\n";
      file << "Name=" << shortcutName << "\n";
      file << "Exec=" << exePath;
      if (!arguments.empty())
      {
        file << " " << arguments;
      }
      file << "\n";
      file << "Terminal=false\n";
      file.close();

      // Mark the .desktop as executable so launchers will accept it.
      std::filesystem::permissions(shortcutPath,
                                   std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                       std::filesystem::perms::others_read,
                                   std::filesystem::perm_options::replace);

      return true;
    }

  } // namespace PlatformHelpers
} // namespace ToolKit
