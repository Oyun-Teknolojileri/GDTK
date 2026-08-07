/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Logger.h"

#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{

  constexpr uint TKMessageBufferLength = 4096;

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
    SpinlockGuard lock(m_writeLock);
    va_list args;
    va_start(args, msg);

    static const char* logTypes[] = {"[Memo]", "[Error]", "[Warning]", "[Command]"};

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
