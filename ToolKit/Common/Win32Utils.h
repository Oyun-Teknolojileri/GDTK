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

namespace ToolKit
{
  namespace PlatformHelpers
  {
    namespace UTF8Util
    {
      // Function to convert UTF-8 to UTF-16
      std::wstring ConvertUTF8ToUTF16(const std::string& utf8String)
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

    // Win32 console command execution callback.
    int SysComExec(StringView cmd, bool async, bool showConsole, std::function<void(int)> callback)
    {
      // https://learn.microsoft.com/en-us/windows/win32/procthread/creating-processes
      STARTUPINFOW si;
      PROCESS_INFORMATION pi;

      ZeroMemory(&si, sizeof(si));
      si.cb          = sizeof(si);
      si.dwFlags     = STARTF_USESHOWWINDOW;
      si.wShowWindow = showConsole ? SW_SHOWNORMAL : SW_HIDE;

      ZeroMemory(&pi, sizeof(pi));

      std::wstring wCmd = UTF8Util::ConvertUTF8ToUTF16("cmd /C ") + UTF8Util::ConvertUTF8ToUTF16(cmd.data());

      // Start the child process.
      if (!CreateProcessW(NULL,        // No module name (use command line)
                          wCmd.data(), // Command line
                          NULL,        // Process handle not inheritable
                          NULL,        // Thread handle not inheritable
                          FALSE,       // Set handle inheritance to FALSE
                          0,           // No creation flags
                          NULL,        // Use parent's environment block
                          NULL,        // Use parent's starting directory
                          &si,         // Pointer to STARTUPINFO structure
                          &pi)         // Pointer to PROCESS_INFORMATION structure
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

    void OutputLog(int logType, const char* szFormat, ...)
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

    void OpenExplorer(const StringView utf8Path)
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

    void HideConsoleWindow()
    {
      HWND handle = GetConsoleWindow();
      ShowWindow(handle, SW_HIDE);
    }

    // Fix working directory when launched from shortcut (shortcut's working dir is Desktop).
    // Set it to exe directory (Bin/) so Resources/Config relative paths work.
    void SetWorkingDirectoryToBinFolder()
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

    String GetCreationTime(const String& fullPath)
    {
      std::wstring wFile = UTF8Util::ConvertUTF8ToUTF16(fullPath.c_str());

      WIN32_FILE_ATTRIBUTE_DATA attrData;
      GetFileAttributesExW(wFile.data(), GetFileExInfoStandard, &attrData);

      String time = std::to_string(attrData.ftLastWriteTime.dwHighDateTime) +
                    std::to_string(attrData.ftLastWriteTime.dwLowDateTime);

      return time;
    }

    void* TKLoadModule(StringView fullPath)
    {
      std::wstring wFile = UTF8Util::ConvertUTF8ToUTF16(fullPath.data());
      HMODULE module     = LoadLibraryW(wFile.data());

      return (void*) module;
    }

    void TKFreeModule(void* module) { FreeLibrary((HMODULE) module); }

    void* TKGetFunction(void* module, StringView func) { return (void*) GetProcAddress((HMODULE) module, func.data()); }

    void UpdateAppIcon()
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
    bool CreateProjectShortcutOnDesktop(const String& shortcutName,
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
