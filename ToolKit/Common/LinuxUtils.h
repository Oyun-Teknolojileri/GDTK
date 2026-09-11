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

#include "Image.h"
#include "Types.h"

#include <SDL.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <sstream>
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

    // Single-quote shell-escape one argv entry so it survives being embedded
    // in a `sh -c` command string verbatim. Wraps in single quotes and rewrites
    // any embedded single quote via the standard '"'"' idiom.
    inline String ShellQuote(const String& s)
    {
      String out = "'";
      for (char c : s)
      {
        if (c == '\'')
        {
          out += "'\\''";
        }
        else
        {
          out += c;
        }
      }
      out += "'";
      return out;
    }

    // Resolve `bin` to an executable: absolute/relative names are tested as-is,
    // bare names are searched on $PATH the way a shell would. Fills `out` with
    // the resolved path and returns true on success.
    inline bool FindInPath(const String& bin, String& out)
    {
      if (bin.find('/') != String::npos)
      {
        if (access(bin.c_str(), X_OK) == 0)
        {
          out = bin;
          return true;
        }
        return false;
      }
      const char* pathEnv = std::getenv("PATH");
      if (pathEnv == nullptr || pathEnv[0] == '\0')
      {
        return false;
      }
      String path(pathEnv);
      size_t start = 0;
      while (start <= path.size())
      {
        size_t colon = path.find(':', start);
        String dir   = (colon == String::npos) ? path.substr(start) : path.substr(start, colon - start);
        String cand  = dir.empty() ? bin : (dir + "/" + bin);
        if (access(cand.c_str(), X_OK) == 0)
        {
          out = cand;
          return true;
        }
        if (colon == String::npos)
        {
          break;
        }
        start = colon + 1;
      }
      return false;
    }

    // Pick a terminal emulator and the flag it uses to run a command. $TERMINAL
    // wins if it resolves (assumed to take -e); otherwise the common desktop
    // emulators are tried in rough popularity order. Each takes either -e
    // (pass-through argv list) or -- (gnome/mate's modern separator). Returns
    // false when none of them is installed.
    inline bool ResolveTerminal(String& bin, String& flag)
    {
      struct Spec
      {
        const char* name;
        const char* execFlag;
      };
      static const Spec specs[] = {
        {"x-terminal-emulator", "-e"},
        {"gnome-terminal", "--"},
        {"konsole", "-e"},
        {"xfce4-terminal", "-e"},
        {"mate-terminal", "--"},
        {"lxterminal", "-e"},
        {"qterminal", "-e"},
        {"alacritty", "-e"},
        {"kitty", "-e"},
        {"terminator", "-e"},
        {"urxvt", "-e"},
        {"xterm", "-e"},
      };

      if (const char* envTerm = std::getenv("TERMINAL"))
      {
        if (envTerm[0] != '\0')
        {
          String resolved;
          if (FindInPath(envTerm, resolved))
          {
            bin  = envTerm;
            flag = "-e";
            return true;
          }
        }
      }
      for (const Spec& s : specs)
      {
        String resolved;
        if (FindInPath(s.name, resolved))
        {
          bin  = s.name;
          flag = s.execFlag;
          return true;
        }
      }
      return false;
    }

    // Wrap `argv` so it runs inside a visible terminal window:
    //   <emulator> <execFlag> sh -c "<shell-quoted argv>; hold prompt"
    // The trailing read keeps the window open after the command exits so the
    // user can read the output; while the command runs, sh's stdin is the
    // terminal's, so interactive prompts (sudo password, y/n, ...) work too.
    // Falls back to `argv` unchanged when no emulator is installed.
    inline StringArray WrapInTerminal(const StringArray& argv)
    {
      String termBin;
      String termFlag;
      if (!ResolveTerminal(termBin, termFlag))
      {
        TK_LOG("SysComExec: showConsole requested but no terminal emulator "
               "found on $PATH; running the command hidden instead.");
        return argv;
      }

      String cmdString;
      for (size_t i = 0; i < argv.size(); ++i)
      {
        if (i > 0)
        {
          cmdString += ' ';
        }
        cmdString += ShellQuote(argv[i]);
      }
      cmdString += "; echo; echo '[Process finished - press Enter to close]'; read -r _";

      return {termBin, termFlag, "sh", "-c", cmdString};
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
      if (argv.empty())
      {
        TK_ERR("SysComExec: empty argv.");
        return -1;
      }

      // showConsole asks for a visible console. On Linux there is no kernel
      // "new console" concept like Win32's CREATE_NEW_CONSOLE, so we run the
      // command inside a terminal-emulator window instead. Without this, a
      // desktop-launched editor has no inherited terminal: the child's
      // stdout/stdin go nowhere, its output is invisible, and any prompt it
      // emits hangs the call forever. When no emulator is installed, the
      // wrapper falls back to running `argv` directly (hidden).
      StringArray effectiveArgv = argv;
      if (showConsole)
      {
        effectiveArgv = WrapInTerminal(argv);
      }

      char** argvArr         = ToNullTerminatedArgv(effectiveArgv);
      const size_t argcCount = effectiveArgv.size();

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

      // Clear any previous error so dlerror() is reliable.
      dlerror();

      void* handle = dlopen(pathStr.c_str(), RTLD_NOW);
      if (handle == nullptr)
      {
        const char* err = dlerror();
        TK_ERR("dlopen failed for \"%s\": %s",
               pathStr.c_str(),
               err ? err : "unknown error");
      }
      return handle;
    }

    inline void TKFreeModule(void* module) { dlclose(module); }

    inline void* TKGetFunction(void* module, StringView func)
    {
      std::string funcStr(func.data(), func.size());
      return dlsym(module, funcStr.c_str());
    }

    // Absolute path of the PNG used as the application icon: the Linux
    // counterpart of the icon Windows embeds in the executable through
    // Editor.rc (MAIN_ICON / app.ico).
    //
    // Resolved from the running executable instead of the process
    // working directory so it keeps working when a host is started from
    // a .desktop entry or a file manager:
    //   <exe dir>/../Resources/Engine/Textures/Icons/<iconName>
    // which is the engine asset root in both the build tree (Bin<Config>
    // next to Resources/) and a staged `cmake --install` tree.
    //
    // The names come from PlatformHelper.h: the editor publishes
    // app_big.png, the launcher the blue variant app_big_blue.png.
    // app_big.png is the same artwork as app.png at 2.8x the resolution
    // (270x248 against 96x96), which is what keeps the icon sharp at the
    // sizes a task bar or a dock asks for (64 to 256 px on a HiDPI
    // desktop). It is a tight crop rather than a padded square, so its
    // aspect is 1.089; window managers scale a window icon with the
    // aspect kept, so that only costs a sliver of transparent pixels on
    // one axis. app.png next to it is the same logo padded into a square
    // 96x96 canvas -- swap the name if the padded framing is worth more
    // than the resolution.
    inline String GetAppIconFile(const String& iconName = EditorAppIconFile)
    {
      std::filesystem::path icon = std::filesystem::path(GetExecutableDirectory()) / ".." / "Resources" / "Engine" /
                                   "Textures" / "Icons" / iconName;
      return PathToString(icon.lexically_normal());
    }

    // Publishes the application icon on the SDL window the host created.
    //
    // Windows carries the icon in the executable's resource section, so
    // re-sending WM_SETICON is enough. A Linux executable has no icon in
    // the ELF image: the icon is a runtime window property
    // (_NET_WM_ICON), which is what both the window manager's title bar
    // decoration and the task bar render. SDL_SetWindowIcon is the API
    // that writes it, so the PNG is loaded here and handed to SDL.
    //
    // nativeWindow is the SDL_Window* the host created. SDL copies the
    // icon data into the window, so the surface and the pixel buffer are
    // released right after the call. iconName selects which PNG of the
    // engine's Icons folder is published, so each host can carry its own
    // (see PlatformHelper.h).
    //
    // Wayland note: xdg-shell has no window icon request, so on a Wayland
    // session SDL ignores the call and the compositor matches the window
    // against a .desktop file instead. That is a protocol limitation, not
    // a failure of this helper, so no warning is emitted.
    inline void UpdateAppIcon(void* nativeWindow = nullptr, const String& iconName = EditorAppIconFile)
    {
      SDL_Window* window = static_cast<SDL_Window*>(nativeWindow);
      if (window == nullptr)
      {
        return;
      }

      const String iconFile = GetAppIconFile(iconName);
      int width             = 0;
      int height            = 0;
      int channels          = 0;

      // 4 channels: ImageLoad always hands back tightly packed, non
      // premultiplied RGBA data.
      // ImageLoadTopDown, not ImageLoad: RenderSystem::InitGraphics turns the
      // engine's vertical flip switch on for the GL texture path (bottom-left
      // origin), and an icon published on a window has to keep the top row the
      // file starts with.
      ubyte* pixels         = ImageLoadTopDown(iconFile.c_str(), &width, &height, &channels, 4);
      if (pixels == nullptr)
      {
        TK_WRN("UpdateAppIcon: cannot load the application icon \"%s\".", iconFile.c_str());
        return;
      }

      // SDL_PIXELFORMAT_RGBA32 is the format whose byte order matches the
      // R,G,B,A sequence ImageLoad produces, on both endiannesses.
      SDL_Surface* icon =
          SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
      if (icon != nullptr)
      {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
      }
      else
      {
        TK_WRN("UpdateAppIcon: cannot create an SDL surface for \"%s\": %s.", iconFile.c_str(), SDL_GetError());
      }

      ImageFree(pixels);
    }

    // Publishes a desktop entry for the running host, so the desktop
    // environment can tie the application to its icon.
    //
    // An ELF executable carries no icon: SDL_SetWindowIcon puts one on the
    // window (see UpdateAppIcon), but the dock, the application menu and the
    // file manager read Icon= from a .desktop entry instead, and they tie a
    // running window to that entry through WM_CLASS. SDL derives WM_CLASS from
    // the executable name (SDL_x11video.c, get_classname), so the entry is
    // named after the executable and repeats the class in StartupWMClass.
    //
    // The entry name is lowercased: GNOME canonicalizes the window's WM_CLASS
    // before it looks the entry up (Shell.AppSystem.lookup_desktop_wmclass:
    // "the .desktop file, without the extension and properly canonicalized,
    // matches wmclass"), while StartupWMClass carries the class as SDL
    // registered it, which is what KDE matches on.
    //
    // XDG_DATA_HOME/applications holds per user entries; without an installer
    // step this is the only place a build tree can put one, which is why the
    // hosts publish it at startup. The file is rewritten only when its content
    // actually changed -- a rewrite on every launch would churn the mtime and
    // re-trigger the desktop database and the file managers watching the
    // folder. Two builds of the same host (BinDebug and BinRelWithDebInfo)
    // share one entry name, so the last one launched is the one that stays;
    // that is the trade-off for not needing an installer.
    //
    // Returns true when the entry is present and current, false when it could
    // not be written (no HOME, no permissions). The result is informational:
    // a host that cannot publish the entry still runs, it just shows the
    // generic icon where the desktop environment matches by entry.
    inline bool RegisterAppDesktopEntry(const String& appName, const String& iconName = EditorAppIconFile)
    {
      std::string dataDir;
      if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && xdg[0] != '\0')
      {
        dataDir = xdg;
      }
      else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
      {
        dataDir = std::string(home) + "/.local/share";
      }
      else
      {
        return false;
      }

      const std::string exePath = GetExecutablePath();
      if (exePath.empty())
      {
        return false;
      }

      // SDL registers this exact string as WM_CLASS.
      const std::string wmClass = PathToString(std::filesystem::path(exePath).stem());

      std::string entryName     = wmClass;
      std::transform(entryName.begin(),
                     entryName.end(),
                     entryName.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

      std::ostringstream entry;
      entry << "[Desktop Entry]\n";
      entry << "Type=Application\n";
      entry << "Version=1.0\n";
      entry << "Name=" << appName << "\n";
      entry << "Exec=" << exePath << "\n";
      entry << "Icon=" << GetAppIconFile(iconName) << "\n";
      entry << "Terminal=false\n";
      entry << "StartupNotify=false\n";
      entry << "StartupWMClass=" << wmClass << "\n";
      entry << "Categories=Development;\n";

      std::error_code ec;
      const std::filesystem::path dir = std::filesystem::path(dataDir) / "applications";
      std::filesystem::create_directories(dir, ec);
      if (ec)
      {
        TK_WRN("RegisterAppDesktopEntry: cannot create \"%s\": %s.", dir.string().c_str(), ec.message().c_str());
        return false;
      }

      const std::filesystem::path file = dir / (entryName + ".desktop");

      {
        std::ifstream current(file);
        if (current.is_open())
        {
          std::stringstream buffer;
          buffer << current.rdbuf();
          if (buffer.str() == entry.str())
          {
            return true;
          }
        }
      }

      std::ofstream out(file, std::ios::trunc);
      if (!out.is_open())
      {
        TK_WRN("RegisterAppDesktopEntry: cannot write \"%s\".", file.string().c_str());
        return false;
      }

      out << entry.str();
      return out.good();
    }

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
      // Icon= replaces the icon the Windows shortcut (.lnk) inherits from
      // the executable. It has to be an absolute path: a bare name is
      // looked up in the XDG icon theme, where the editor is not
      // installed. Only written when the PNG actually exists so the
      // desktop entry never points at a missing file.
      const String iconFile = GetAppIconFile();
      if (std::filesystem::exists(iconFile))
      {
        file << "Icon=" << iconFile << "\n";
      }
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
