#pragma once

#ifdef TK_MAC

#include <filesystem>
#include <string>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <cstdlib>
#include <iostream>
#include <dlfcn.h>
#include <ctime>

namespace ToolKit
{
  namespace PlatformHelpers
  {
    namespace UTF8Util
    {
      // On macOS, UTF-8 is already the native encoding for std::string.
      inline std::wstring ConvertUTF8ToUTF16(const std::string& utf8String)
      {
        return std::wstring(utf8String.begin(), utf8String.end());
      }
    }

    inline int SysComExec(std::string_view cmd, bool async, bool showConsole, std::function<void(int)> callback, bool captureOutput = false)
    {
      //TODO(erendgrmnc): Add output capture to mac builds
      (void)captureOutput; 
      std::string command = std::string(cmd);

      if (async)
      {
        std::thread([command, callback]() {
          int ret = std::system(command.c_str());
          if (callback)
            callback(ret);
        }).detach();

        return 0;
      }
      else
      {
        int ret = std::system(command.c_str());
        if (callback)
          callback(ret);
        return ret;
      }
    }

    inline void OutputLog(int logType, const char* szFormat, ...)
    {
      static const char* logNames[] = {"[Memo]", "[Error]", "[Warning]", "[Command]"};
      static char szBuff[1024] = {0};

      va_list arg;
      va_start(arg, szFormat);
      vsnprintf(szBuff, sizeof(szBuff), szFormat, arg);
      va_end(arg);

      std::cout << logNames[logType] << " " << szBuff << std::endl;
    }

    inline void OpenExplorer(const std::string_view utf8Path)
    {
      std::string cmd = "open \"" + std::string(utf8Path) + "\"";
      std::system(cmd.c_str());
    }

    inline void HideConsoleWindow() { /* no-op on macOS */ }

    inline std::string GetCreationTime(const std::string& fullPath)
    {
        namespace fs = std::filesystem;

        auto ftime = fs::last_write_time(fullPath);

        // Convert file_clock to system_clock (required on macOS)
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );

        std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);

        return std::to_string(cftime);
    }

    inline void* TKLoadModule(std::string_view fullPath)
    {
      void* handle = dlopen(fullPath.data(), RTLD_LAZY);
      if (!handle)
      {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
      }
      return handle;
    }

    inline void TKFreeModule(void* module)
    {
      if (module) dlclose(module);
    }

    inline void* TKGetFunction(void* module, std::string_view func)
    {
      return module ? dlsym(module, func.data()) : nullptr;
    }

    inline void UpdateAppIcon() { /* no-op on macOS */ }

  } // namespace PlatformHelpers
} // namespace ToolKit

#endif
