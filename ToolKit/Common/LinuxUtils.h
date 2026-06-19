/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
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

#include "Types.h"

extern char** environ;

namespace ToolKit
{
  namespace PlatformHelpers
  {
    namespace UTF8Util
    {
      // Linux paths are already UTF-8 in std::string; this is a no-op
      // identity function kept for API symmetry with Win32Utils.h.
      std::wstring ConvertUTF8ToUTF16(const std::string& utf8String)
      {
        return std::wstring(utf8String.begin(), utf8String.end());
      }
    } // namespace UTF8Util

    // Linux console command execution callback.
    //
    // async=true  -> spawn child, fire `callback` from a detached thread
    //                once the child exits; the call returns 0 immediately.
    // async=false -> wait for the child synchronously and return its
    //                exit status.
    int SysComExec(StringView cmd, bool async, bool showConsole, std::function<void(int)> callback)
    {
      (void) showConsole; // No Win32-style console window concept on Linux.

      // Copy cmd into a mutable buffer; posix_spawn / execvp want a
      // null-terminated string and StringView only guarantees a view.
      std::string cmdStr(cmd.data(), cmd.size());

      if (!async)
      {
        // Synchronous path: block until the child exits.
        int status = 0;
        int rc = std::system(cmdStr.c_str());
        if (rc == -1)
        {
          return -1;
        }

        // system() returns the waitpid-encoded status; extract the
        // exit code the same way the Windows version surfaces it.
        if (WIFEXITED(rc))
        {
          status = WEXITSTATUS(rc);
        }
        else
        {
          status = 128 + WTERMSIG(rc);
        }

        if (callback != nullptr)
        {
          callback(status);
        }
        return status;
      }

      // Async path: fork a child to run the command. The parent returns
      // 0 immediately. The child runs the command via /bin/sh -c to
      // preserve the shell-isms the editor relies on (e.g. "code \"$p\"").
      pid_t pid = fork();
      if (pid < 0)
      {
        TK_ERR("fork failed (%d).", errno);
        return -1;
      }

      if (pid == 0)
      {
        // Child: hand the command to /bin/sh so the editor's existing
        // Windows-style shell commands keep working unmodified.
        execl("/bin/sh", "sh", "-c", cmdStr.c_str(), (char*) nullptr);
        // execl only returns on failure.
        _exit(127);
      }

      // Parent: arm a detached watcher that fires the callback once
      // the child exits, so callers don't have to drive a wait loop.
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
    void OutputLog(int logType, const char* szFormat, ...)
    {
      static const char* logNames[] = {"[Memo]", "[Error]", "[Warning]", "[Command]", "[Success]"};
      const char* tag               = (logType >= 0 && logType < 5) ? logNames[logType] : "[Log]";

      char szBuff[1024] = {0};
      va_list arg;
      va_start(arg, szFormat);
      std::vsnprintf(szBuff, sizeof(szBuff), szFormat, arg);
      va_end(arg);

      std::fprintf(stderr, "%s %s\n", tag, szBuff);
    }

    // Open `utf8Path` in the platform's default file manager via xdg-open.
    void OpenExplorer(const StringView utf8Path)
    {
      std::filesystem::path systemPath = utf8Path;
      std::string systemPathStr        = systemPath.lexically_normal().string();

      pid_t pid = fork();
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
    void HideConsoleWindow() {}

    // Fix working directory when launched from a shortcut (the
    // shortcut's CWD is usually $HOME). Mirror the Win32 helper by
    // resolving the current executable's directory and chdir'ing there
    // so Resources/Config relative paths resolve the same way the
    // Windows binary expects.
    void SetWorkingDirectoryToBinFolder()
    {
      std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
      std::filesystem::path exeDir  = exePath.parent_path(); // .../Bin
      if (!exeDir.empty())
      {
        std::filesystem::current_path(exeDir);
      }
    }

    // Return the file's mtime as a string the same way the Win32 helper
    // does, so the plugin manager's "did the file change" logic keeps
    // working unchanged.
    String GetCreationTime(const String& fullPath)
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
    void* TKLoadModule(StringView fullPath)
    {
      std::string pathStr(fullPath.data(), fullPath.size());
      // RTLD_NOW forces all symbols to resolve at load time, matching
      // the Windows LoadLibraryW behaviour the editor assumes.
      return dlopen(pathStr.c_str(), RTLD_NOW);
    }

    void TKFreeModule(void* module) { dlclose(module); }

    void* TKGetFunction(void* module, StringView func)
    {
      std::string funcStr(func.data(), func.size());
      return dlsym(module, funcStr.c_str());
    }

    // SDL drives the editor window on Linux; there is no separate Win32
    // HWND icon to set after SDL wipes ours. Kept as a no-op for API
    // symmetry.
    void UpdateAppIcon() {}

    // Create a desktop entry that launches the current editor
    // executable. On Linux the "shortcut" is a freedesktop.org .desktop
    // file in $XDG_DESKTOP_DIR (or $HOME/Desktop if unset). The args
    // match the Windows helper so the editor's project dialog can use
    // either platform's API without branching.
    bool CreateProjectShortcutOnDesktop(const String& shortcutName,
                                        const String& arguments,
                                        const String& exePathOverride = "")
    {
      std::string exePath = exePathOverride.empty()
                                ? std::filesystem::read_symlink("/proc/self/exe").string()
                                : exePathOverride;

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
                                   std::filesystem::perms::owner_all |
                                       std::filesystem::perms::group_read |
                                       std::filesystem::perms::others_read,
                                   std::filesystem::perm_options::replace);

      return true;
    }

  } // namespace PlatformHelpers
} // namespace ToolKit
