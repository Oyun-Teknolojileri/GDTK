/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Threads.h"
#include "Types.h"

namespace ToolKit
{

#define TK_LOG(format, ...)    ToolKit::GetLogger()->WriteTKConsole(ToolKit::LogType::Memo, format, ##__VA_ARGS__)
#define TK_SYSLOG(format, ...) ToolKit::GetLogger()->WritePlatformConsole(ToolKit::LogType::Memo, format, ##__VA_ARGS__)
#define TK_WRN(format, ...)    ToolKit::GetLogger()->WriteTKConsole(ToolKit::LogType::Warning, format, ##__VA_ARGS__)
#define TK_ERR(format, ...)    ToolKit::GetLogger()->WriteTKConsole(ToolKit::LogType::Error, format, ##__VA_ARGS__)
#define TK_SUCCESS(format, ...) ToolKit::GetLogger()->WriteTKConsole(ToolKit::LogType::Success, format, ##__VA_ARGS__)

  enum class LogType
  {
    Memo,
    Error,
    Warning,
    Command,
    Success
  };

  /**
   * Sets how much of the log reaches the sinks. Every sink is filtered, the log file included: what a
   * log line costs is the formatting and the write, not only where it lands.
   *
   *   0 -- everything: memo, success, command output, warning, error
   *   1 -- warnings and errors
   *   2 -- errors only
   *
   * Command output always passes, it is the answer to something the user typed. The level lives in a
   * free function rather than on the Logger because it has to be settable before the engine boots --
   * the web export is called from the page -- and without a Main instance.
   * @param level is clamped to 0..2.
   */
  TK_API void SetLogLevel(int level);

  /** Returns the active log level. 0 (everything) by default. */
  TK_API int GetLogLevel();

  typedef std::function<void(LogType, const String&)> ConsoleOutputFn;
  typedef std::function<void()> ClearConsoleFn;

  class TK_API Logger
  {
   public:
    Logger();
    ~Logger();
    void Log(const String& message);
    void Log(LogType logType, const char* msg, ...);
    void SetWriteConsoleFn(ConsoleOutputFn fn);
    void SetClearConsoleFn(ClearConsoleFn fn);
    void SetPlatformConsoleFn(ConsoleOutputFn fn);
    void ClearConsole();
    void WriteTKConsole(LogType logType, const char* msg, ...);
    void WritePlatformConsole(LogType logType, const char* msg, ...);

   private:
    std::ofstream m_logFile;
    ClearConsoleFn m_clearConsoleFn;
    ConsoleOutputFn m_writeConsoleFn    = nullptr;
    ConsoleOutputFn m_platfromConsoleFn = nullptr;
    Spinlock m_writeLock;
  };
} // namespace ToolKit
