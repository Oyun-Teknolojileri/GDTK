/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Logger.h"

#include "ToolKit.h"

#include "DebugNew.h"

#ifdef TK_WEB
  #include <emscripten.h>
#endif

namespace ToolKit
{

  constexpr uint TKMessageBufferLength = 4096;

  constexpr int TKLogLevelAll      = 0;
  constexpr int TKLogLevelWarnings = 1;
  constexpr int TKLogLevelErrors   = 2;

  namespace
  {
    /**
     * Read on every log line and written from the editor console or the web export, so it is atomic:
     * logging runs on the worker threads as well.
     */
    std::atomic<int> g_logLevel {TKLogLevelAll};

    /** States whether a line of the given type survives the active log level. */
    bool IsLoggable(LogType logType)
    {
      const int level = g_logLevel.load(std::memory_order_relaxed);

      switch (logType)
      {
        case LogType::Error:
          return true; // every level keeps errors

        case LogType::Command:
          return true; // the answer to a typed command, never swallowed

        case LogType::Warning:
          return level < TKLogLevelErrors;

        case LogType::Memo:
        case LogType::Success:
        default:
          return level == TKLogLevelAll;
      }
    }
  } // namespace

  void SetLogLevel(int level)
  {
    g_logLevel.store(glm::clamp(level, TKLogLevelAll, TKLogLevelErrors), std::memory_order_relaxed);
  }

  int GetLogLevel() { return g_logLevel.load(std::memory_order_relaxed); }

  void OutputUtil(ConsoleOutputFn logFn, LogType logType, const char* msg, va_list args)
  {
    char messageBuffer[TKMessageBufferLength];

    if (logFn == nullptr)
    {
      return;
    }

    // vsnprintf so an over-long formatted message doesn't blow the
    // fixed-size buffer; vsprintf would happily write past it.
    vsnprintf(messageBuffer, sizeof(messageBuffer), msg, args);
    strcat(messageBuffer, "\n");

    logFn(logType, String(messageBuffer));
  }

  Logger::Logger() { m_logFile.open("Log.txt", std::ios::out); }

  Logger::~Logger() { m_logFile.close(); }

  void Logger::Log(const String& message)
  {
    // The string overload carries no type, so it counts as a memo line.
    if (!IsLoggable(LogType::Memo))
    {
      return;
    }

    SpinlockGuard lock(m_writeLock);
    if constexpr (TK_PLATFORM == PLATFORM::TKWeb)
    {
      String emLog = message + "\n";
      printf("%s", emLog.c_str());
    }
    else
    {
      m_logFile << message << std::endl;
    }
  }

  void Logger::Log(LogType logType, const char* msg, ...)
  {
    if (!IsLoggable(logType))
    {
      return;
    }

    SpinlockGuard lock(m_writeLock);
    va_list args;
    va_start(args, msg);

    static const char* logTypes[] = {"[Memo]", "[Error]", "[Warning]", "[Command]", "[Success]"};

    char messageBuffer[TKMessageBufferLength];
    vsnprintf(messageBuffer, sizeof(messageBuffer), msg, args);

    m_logFile << logTypes[(int) logType] << messageBuffer << std::endl;

    if (m_writeConsoleFn != nullptr)
    {
      m_writeConsoleFn(logType, messageBuffer);
    }

    if (m_platfromConsoleFn != nullptr)
    {
      m_platfromConsoleFn(logType, messageBuffer);
    }

    va_end(args);
  }

  void Logger::WriteTKConsole(LogType logType, const char* msg, ...)
  {
    // The level is checked before anything is formatted: the formatting is most of what a silenced
    // line would still cost.
    if (!IsLoggable(logType))
    {
      return;
    }

    SpinlockGuard lock(m_writeLock);
    if (strlen(msg) >= TKMessageBufferLength)
    {
      // Overflow path: hand the raw (still-unformatted) format string
      // to the platform console verbatim. It does NOT take varargs,
      // so we must not pass an uninitialised va_list into the
      // %s-expanding callback. The previous code passed `msg` and
      // expected it to render as a literal; that only worked by
      // accident on console sinks that treat the second arg as
      // already-rendered text. Print the format string as a literal
      // (it carries no embedded %s we could expand anyway, since
      // we bailed before formatting).
      if (m_platfromConsoleFn)
      {
        m_platfromConsoleFn(LogType::Warning, "Maximum size for WriteConsole exceeded, cannot format.");
        m_platfromConsoleFn(logType, String(msg));
      }
      return;
    }

    // va_list is consumed by the first call to a vararg consumer
    // (vsnprintf inside OutputUtil). Re-initialise it for the second
    // call -- the old code passed the same args twice, which on
    // x86-64 read past the stack and segfaulted inside __strlen_avx2.
    va_list args;
    va_start(args, msg);
    OutputUtil(m_writeConsoleFn, logType, msg, args);
    va_end(args);

    // Echo to platform console.
    if (m_platfromConsoleFn)
    {
      va_start(args, msg);
      OutputUtil(m_platfromConsoleFn, logType, msg, args);
      va_end(args);
    }
  }

  void Logger::WritePlatformConsole(LogType logType, const char* msg, ...)
  {
    if (!IsLoggable(logType))
    {
      return;
    }

    SpinlockGuard lock(m_writeLock);
    if (strlen(msg) >= TKMessageBufferLength)
    {
      if (m_platfromConsoleFn)
      {
        m_platfromConsoleFn(logType, "Maximum size for WriteConsole exceeded, cannot format.");
        m_platfromConsoleFn(logType, msg);
      }
      return;
    }
    va_list args;
    va_start(args, msg);

    OutputUtil(m_platfromConsoleFn, logType, msg, args);

    va_end(args);
  }

  void Logger::SetWriteConsoleFn(ConsoleOutputFn fn) { m_writeConsoleFn = fn; }

  void Logger::SetClearConsoleFn(ClearConsoleFn fn) { m_clearConsoleFn = fn; }

  void Logger::SetPlatformConsoleFn(ConsoleOutputFn fn) { m_platfromConsoleFn = fn; }

  void Logger::ClearConsole() { m_clearConsoleFn(); }

} // namespace ToolKit

#ifdef TK_WEB
  /**
   * The log level, reachable from JavaScript. Call it as `LogLevel(1)` from the shell's helper,
   * `Module._LogLevel(1)`, or `Module.ccall('LogLevel', null, ['number'], [1])`.
   *
   * It only touches the level's atomic, so the page can set it before the engine boots -- which is
   * when the boot's own memo lines are the noisiest.
   */
  extern "C" EMSCRIPTEN_KEEPALIVE void LogLevel(int level) { ToolKit::SetLogLevel(level); }
#endif
