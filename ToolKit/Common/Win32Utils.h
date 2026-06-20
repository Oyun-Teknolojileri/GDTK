/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#ifdef _WIN32
  #define NOMINMAX
  #define WIN32_LEAN_AND_MEAN
  #include <Windows.h>
  #include <shellapi.h>
  #include <shlobj.h>
  #include <strsafe.h>

  #include <chrono>
  #include <filesystem>
  #include <fstream>
  #include <thread>
  #include <vector>

  #include "Types.h"

namespace ToolKit
{
  namespace PlatformHelpers
  {
    namespace UTF8Util
    {
      // Function to convert UTF-8 to UTF-16
      inline std::wstring ConvertUTF8ToUTF16(const std::string& utf8String)
      {
        // Calculate the length of the UTF-16 string
        int utf16Length = MultiByteToWideChar(CP_UTF8, 0, utf8String.c_str(), -1, NULL, 0);

        if (utf16Length == 0)
        {
          throw std::runtime_error("Error calculating UTF-16 string length");
        }

        // Allocate memory for the UTF-16 string
        wchar_t* utf16String = new wchar_t[utf16Length];

        // Convert UTF-8 to UTF-16
        if (MultiByteToWideChar(CP_UTF8, 0, utf8String.c_str(), -1, utf16String, utf16Length) == 0)
        {
          delete[] utf16String;
          throw std::runtime_error("Error converting UTF-8 to UTF-16");
        }

        // Create a wstring from the UTF-16 string
        std::wstring result(utf16String);

        // Clean up
        delete[] utf16String;

        return result;
      }
    } // namespace UTF8Util

    // Returns the absolute path of the running executable via
    // GetModuleFileNameW, converted to UTF-8 and normalized.
    // Implementation detail for GetExecutableDirectory() /
    // GetSiblingExecutablePath().
    inline String GetExecutablePath()
    {
      wchar_t buf[MAX_PATH] = {0};
      DWORD len             = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
      if (len == 0 || len >= MAX_PATH)
      {
        return String();
      }
      return std::filesystem::path(buf).lexically_normal().u8string();
    }

    // Returns the directory containing the running executable
    // (with trailing separator). Use to resolve sibling binaries
    // that ship next to the current process.
    inline String GetExecutableDirectory()
    {
      String exePath = GetExecutablePath();
      if (exePath.empty())
      {
        return String();
      }
      std::filesystem::path p(exePath);
      String dir = p.parent_path().u8string();
      if (!dir.empty() && dir.back() != '\\' && dir.back() != '/')
      {
        dir += '\\';
      }
      return dir;
    }

    // Returns the absolute path of `name` interpreted as a binary
    // that lives next to the current process (e.g. Editor.exe
    // next to Launcher.exe, both in Bin/).
    inline String GetSiblingExecutablePath(const String& name)
    {
      return GetExecutableDirectory() + name;
    }

    // Returns the absolute path of the editor binary. The editor
    // is expected to ship next to the current process (both live
    // in Bin/), so this is just a sibling lookup.
    inline String GetEditorExecutablePath()
    {
      return GetSiblingExecutablePath(GetEditorExecutableName());
    }

    // Returns the absolute path of the packer binary. Same sibling
    // semantics as GetEditorExecutablePath().
    inline String GetPackerExecutablePath()
    {
      return GetSiblingExecutablePath(GetPackerExecutableName());
    }

    // Quote a single argv entry per the Microsoft "Parsing C++
    // Command-Line Arguments" rules so it survives a round trip
    // through CreateProcessW's lpCommandLine parser. Empty strings
    // become ""; strings with no whitespace / quotes pass through
    // untouched; everything else is wrapped in double quotes with
    // backslashes doubled appropriately.
    inline std::wstring Win32QuoteArg(const std::wstring& arg)
    {
      if (arg.empty())
      {
        return L"\"\"";
      }

      bool needsQuoting = false;
      for (wchar_t c : arg)
      {
        if (c == L' ' || c == L'\t' || c == L'"')
        {
          needsQuoting = true;
          break;
        }
      }
      if (!needsQuoting)
      {
        return arg;
      }

      std::wstring result;
      result.push_back(L'"');
      size_t backslashes = 0;
      for (wchar_t c : arg)
      {
        if (c == L'\\')
        {
          backslashes++;
        }
        else if (c == L'"')
        {
          // N backslashes + literal quote -> 2N+1 backslashes + quote.
          result.append(backslashes * 2 + 1, L'\\');
          result.push_back(L'"');
          backslashes = 0;
        }
        else
        {
          if (backslashes > 0)
          {
            result.append(backslashes, L'\\');
            backslashes = 0;
          }
          result.push_back(c);
        }
      }
      // Trailing backslashes: double them so they don't escape the
      // closing quote we are about to append.
      result.append(backslashes * 2, L'\\');
      result.push_back(L'"');
      return result;
    }

    // Win32 console command execution callback.
    //
    // Takes a tokenized argv (argv[0] is the executable, typically
    // an absolute path returned by GetEditorExecutablePath). No
    // cmd.exe shell is involved -- CreateProcessW receives a
    // properly quoted command line so each argv entry reaches the
    // child verbatim, with no PATH-lookup side effects and no
    // shell-escaping pitfalls (paths with spaces, quotes, $vars).
    //
    // async=true  -> spawn child, fire `callback` from a detached thread
    //                once the child exits; the call returns 0 immediately.
    // async=false -> wait for the child synchronously and return its
    //                exit status.
    inline int SysComExec(const std::vector<String>& argv,
                          bool async,
                          bool showConsole,
                          std::function<void(int)> callback)
    {
      if (argv.empty())
      {
        TK_ERR("SysComExec: empty argv.");
        return -1;
      }

      // Build a Win32 command line by quoting each argv entry. The
      // first token is conventionally the program name even when
      // lpApplicationName is set; this is what CommandLineToArgvW
      // and most child programs expect.
      std::wstring cmdLine;
      for (size_t i = 0; i < argv.size(); ++i)
      {
        if (i > 0)
        {
          cmdLine.push_back(L' ');
        }
        std::wstring wArg = UTF8Util::ConvertUTF8ToUTF16(argv[i]);
        cmdLine          += Win32QuoteArg(wArg);
      }

      // Pass argv[0] as lpApplicationName so CreateProcess doesn't
      // need to re-parse the program name out of the command line
      // (which would defeat the quoting for spaces-in-paths).
      std::wstring wExePath = UTF8Util::ConvertUTF8ToUTF16(argv[0]);

      // https://learn.microsoft.com/en-us/windows/win32/procthread/creating-processes
      STARTUPINFOW si;
      PROCESS_INFORMATION pi;

      ZeroMemory(&si, sizeof(si));
      si.cb          = sizeof(si);
      si.dwFlags     = STARTF_USESHOWWINDOW;
      si.wShowWindow = showConsole ? SW_SHOWNORMAL : SW_HIDE;

      ZeroMemory(&pi, sizeof(pi));

      // Start the child process. CreateProcessW may modify the
      // command line buffer in place (it rewrites argv[0] to the
      // full path), so we pass a mutable wstring's data().
      if (!CreateProcessW(wExePath.data(),               // module name (no PATH lookup)
                          cmdLine.empty() ? nullptr : cmdLine.data(),
                          NULL,                          // Process handle not inheritable
                          NULL,                          // Thread handle not inheritable
                          FALSE,                         // Set handle inheritance to FALSE
                          0,                             // No creation flags
                          NULL,                          // Use parent's environment block
                          NULL,                          // Use parent's starting directory
                          &si,                           // Pointer to STARTUPINFO structure
                          &pi)                           // Pointer to PROCESS_INFORMATION structure
      )
      {
        DWORD errCode = GetLastError();
        TK_ERR("CreateProcess failed (%d).\n", errCode);
        return (int) errCode;
      }

      SetWindowPos((HWND) pi.hProcess, HWND_TOPMOST, 0, 0, 0, 0, 0);

      auto finalizeFn = [pi, callback](DWORD stat) -> int
      {
        // Close process and thread handles.
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (callback != nullptr)
        {
          callback((int) stat);
        }

        return stat;
      };

      if (!async)
      {
        // Wait until child process exits.
        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD stat = 0;
        GetExitCodeProcess(pi.hProcess, &stat);
        return finalizeFn(stat);
      }
      else
      {
        // We suppose to wait to call callback.
        if (callback != nullptr)
        {
          std::thread t(
              [pi, callback, finalizeFn]() -> void
              {
                DWORD stat = 0;
                bool exit  = false;
                while (!exit)
                {
                  GetExitCodeProcess(pi.hProcess, &stat);
                  if (stat != STILL_ACTIVE)
                  {
                    exit = true;
                  }

                  std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                finalizeFn(stat);
              });

          t.detach();
        }
        else
        {
          return finalizeFn(0);
        }
      }

      return 0;
    };

    inline void OutputLog(int logType, const char* szFormat, ...)
    {
      static const char* logNames[] = {"[Memo]", "[Error]", "[Warning]", "[Command]"};

      static char szBuff[1024]      = {0};
      va_list arg;
      va_start(arg, szFormat);
      _vsnprintf(szBuff, sizeof(szBuff), szFormat, arg);
      va_end(arg);

      static char szOutputBuff[1024] = {0};

      // concat log type name and log string
      _snprintf(szOutputBuff, sizeof(szOutputBuff), "%s %s\n", logNames[logType], szBuff);
      std::wstring wOutput = UTF8Util::ConvertUTF8ToUTF16(szOutputBuff);

      OutputDebugStringW(wOutput.data());
    }

    inline void OpenExplorer(const StringView utf8Path)
    {
      std::filesystem::path systemPath = utf8Path;
      String systemPathStr             = systemPath.lexically_normal().u8string(); // Windows style path normalization.
      std::wstring utf16Path           = UTF8Util::ConvertUTF8ToUTF16(systemPathStr);
      HINSTANCE result =
          ShellExecuteW(GetActiveWindow(), L"open", L"explorer.exe", utf16Path.data(), NULL, SW_SHOWNORMAL);

      // Check the result of ShellExecute
      if ((intptr_t) result <= 32)
      {
        // ShellExecute failed
        TK_ERR("Failed to open the folder: %s", utf8Path);
      }
    }

    inline void HideConsoleWindow()
    {
      HWND handle = GetConsoleWindow();
      ShowWindow(handle, SW_HIDE);
    }

    // Fix working directory when launched from shortcut (shortcut's working dir is Desktop).
    // Set it to exe directory (Bin/) so Resources/Config relative paths work.
    inline void SetWorkingDirectoryToBinFolder()
    {
      wchar_t exePathW[MAX_PATH] = {0};
      DWORD len                  = ::GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
      if (len > 0 && len < MAX_PATH)
      {
        std::filesystem::path exePath(exePathW);
        std::filesystem::path exeDir = exePath.parent_path(); // .../Bin
        std::wstring exeDirW         = exeDir.wstring();
        if (!exeDirW.empty())
        {
          ::SetCurrentDirectoryW(exeDirW.c_str());
        }
      }
    }

    // Returns the per-user config directory to use for engine config
    // files (Workspace.settings, Editor.settings, etc.). On Windows
    // that's %APPDATA% (e.g. C:/Users/<u>/AppData/Roaming), which is
    // always set for an interactive session. Returns an empty String
    // when APPDATA is missing or empty; the caller should treat that
    // as a soft failure and skip the config bootstrap rather than
    // abort.
    inline String GetUserConfigDir()
    {
      const char* raw = getenv("APPDATA");
      if (raw == nullptr || raw[0] == '\0')
      {
        return String();
      }
      return raw;
    }

    inline String GetCreationTime(const String& fullPath)
    {
      std::wstring wFile = UTF8Util::ConvertUTF8ToUTF16(fullPath.c_str());

      WIN32_FILE_ATTRIBUTE_DATA attrData;
      GetFileAttributesExW(wFile.data(), GetFileExInfoStandard, &attrData);

      String time = std::to_string(attrData.ftLastWriteTime.dwHighDateTime) +
                    std::to_string(attrData.ftLastWriteTime.dwLowDateTime);

      return time;
    }

    inline void* TKLoadModule(StringView fullPath)
    {
      std::wstring wFile = UTF8Util::ConvertUTF8ToUTF16(fullPath.data());
      HMODULE module     = LoadLibraryW(wFile.data());

      return (void*) module;
    }

    inline void TKFreeModule(void* module) { FreeLibrary((HMODULE) module); }

    inline void* TKGetFunction(void* module, StringView func) { return (void*) GetProcAddress((HMODULE) module, func.data()); }

    inline void UpdateAppIcon()
    {
      HINSTANCE handle = ::GetModuleHandle(nullptr);

      // MAIN_ICON is defined as 102 in Editor.rc
      HICON icon       = ::LoadIcon(handle, MAKEINTRESOURCE(102));
      if (icon != nullptr)
      {
        HWND hwnd = GetActiveWindow();
        if (hwnd != nullptr)
        {
          SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM) icon);
          SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM) icon);
        }
      }
    }

    // Create a desktop shortcut (.lnk) that launches the current editor executable
    // with optional command-line arguments.
    //
    // - shortcutName: File name without extension (".bat" will be appended).
    // - arguments   : Optional argument string passed to the executable.
    //
    // Returns true on success, false otherwise.
    inline bool CreateProjectShortcutOnDesktop(const String& shortcutName,
                                               const String& arguments,
                                               const String& exePathOverride = "")
    {
      std::wstring exePathW;
      if (exePathOverride.empty())
      {
        wchar_t buf[MAX_PATH] = {0};
        DWORD len             = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
        {
          return false;
        }
        exePathW = buf;
      }
      else
      {
        exePathW = UTF8Util::ConvertUTF8ToUTF16(exePathOverride);
      }

      // Get desktop folder path using Windows API (handles Public/User desktop, etc.)
      wchar_t desktopPathW[MAX_PATH] = {0};
      HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, SHGFP_TYPE_CURRENT, desktopPathW);
      if (FAILED(hr) || desktopPathW[0] == L'\0')
      {
        return false;
      }

      // Convert to UTF-8 (for CheckSystemFile)
      std::filesystem::path desktopPathFs(desktopPathW);
      String desktopPath = desktopPathFs.u8string();

      // Check if desktop directory exists, if not, fail.
      if (!CheckSystemFile(desktopPath))
      {
        return false;
      }

      // Initialize COM for shell link creation.
      hr                     = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
      bool needUninit        = SUCCEEDED(hr);

      IShellLinkW* shellLink = nullptr;
      hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*) &shellLink);
      if (FAILED(hr) || shellLink == nullptr)
      {
        if (needUninit)
        {
          CoUninitialize();
        }
        return false;
      }

      shellLink->SetPath(exePathW.c_str());

      // Optional arguments.
      if (!arguments.empty())
      {
        std::wstring wArgs = UTF8Util::ConvertUTF8ToUTF16(arguments);
        shellLink->SetArguments(wArgs.c_str());
      }

      // Resolve .lnk path on desktop.
      String shortcutFileNameUtf8    = shortcutName + ".lnk";
      std::wstring shortcutFileNameW = UTF8Util::ConvertUTF8ToUTF16(shortcutFileNameUtf8);
      std::filesystem::path shortcutPathFs(desktopPathW);
      shortcutPathFs            /= shortcutFileNameW;

      IPersistFile* persistFile  = nullptr;
      hr                         = shellLink->QueryInterface(IID_IPersistFile, (void**) &persistFile);
      if (FAILED(hr) || persistFile == nullptr)
      {
        shellLink->Release();
        if (needUninit)
        {
          CoUninitialize();
        }
        return false;
      }

      hr = persistFile->Save(shortcutPathFs.c_str(), TRUE);
      persistFile->Release();
      shellLink->Release();

      if (needUninit)
      {
        CoUninitialize();
      }

      return SUCCEEDED(hr);
    }

  } // namespace PlatformHelpers
} // namespace ToolKit

#endif
