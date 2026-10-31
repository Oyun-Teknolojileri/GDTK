/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "BootStats.h"

#include "Logger.h"

#include <chrono>
#include <cstdio>
#include <mutex>

namespace ToolKit
{

  namespace BootStats
  {
    namespace
    {
      using Clock = std::chrono::steady_clock;

      /**
       * The whole accounting in one struct behind one mutex. Materials and resources compile shaders
       * on the worker threads while the frame that reports reads the numbers, so the counters are
       * not touched without it. It holds plain numbers and strings, never an engine object, so it is
       * safe for it to outlive the engine as a function local static.
       */
      struct BootRecord
      {
        std::mutex lock;
        Clock::time_point origin;
        bool started = false;
        bool reported = false;

        uint stageCount = 0;
        uint programCount = 0;
        uint programFromCache = 0;
        uint programStored = 0;
        float stageMilliseconds = 0.0f;
        float programMilliseconds = 0.0f;
        float storeMilliseconds = 0.0f;
        uint sourceCount = 0;
        float sourceMilliseconds = 0.0f;

        /** Each compiled stage as "file [variant] cost ms", in the order they were compiled. */
        StringArray stages;

        /** Position of the first and last shader event on the timeline, -1 until one happened. */
        double firstEventMs = -1.0;
        double lastEventMs = 0.0;
      };

      BootRecord& GetRecord()
      {
        static BootRecord record;
        return record;
      }

      /** Seconds to milliseconds between two clock readings. */
      double ToMilliseconds(Clock::time_point from, Clock::time_point to)
      {
        return std::chrono::duration<double, std::milli>(to - from).count();
      }

      /** Reads the timeline without taking the lock again, the caller already holds it. */
      double ElapsedMsLocked(const BootRecord& record)
      {
        return record.started ? ToMilliseconds(record.origin, Clock::now()) : 0.0;
      }
    } // namespace

    void Begin()
    {
      BootRecord& record = GetRecord();
      std::lock_guard<std::mutex> guard(record.lock);

      if (!record.started)
      {
        record.origin = Clock::now();
        record.started = true;
      }
    }

    double ElapsedMs()
    {
      BootRecord& record = GetRecord();
      std::lock_guard<std::mutex> guard(record.lock);
      return ElapsedMsLocked(record);
    }

    void Mark(const String& phase)
    {
      Begin();
      TK_LOG("BOOT %s at %.1f ms", phase.c_str(), ElapsedMs());
    }

    void StageCompiled(const String& relativeShaderFile, const String& variantKey, float milliseconds)
    {
      Begin();

      String entry = relativeShaderFile;
      if (variantKey.empty())
      {
        entry += " [no defines]";
      }
      else
      {
        entry += " [" + variantKey + "]";
      }

      // Logged outside the lock: the logger has a lock of its own and a stage is not a hot path.
      char cost[48];
      snprintf(cost, sizeof(cost), "%.1f ms", (double) milliseconds);

      {
        BootRecord& record = GetRecord();
        std::lock_guard<std::mutex> guard(record.lock);

        const double at = ElapsedMsLocked(record);
        record.stageCount++;
        record.stageMilliseconds += milliseconds;
        record.stages.push_back(entry + " " + cost);

        if (record.firstEventMs < 0.0)
        {
          record.firstEventMs = at;
        }
        record.lastEventMs = at;
      }

      TK_LOG("Shader compile %s %.1f ms", entry.c_str(), (double) milliseconds);
    }

    void ProgramReady(bool fromCache, float milliseconds)
    {
      Begin();

      BootRecord& record = GetRecord();
      std::lock_guard<std::mutex> guard(record.lock);

      const double at = ElapsedMsLocked(record);
      record.programCount++;
      record.programFromCache += fromCache ? 1 : 0;
      record.programMilliseconds += milliseconds;

      if (record.firstEventMs < 0.0)
      {
        record.firstEventMs = at;
      }
      record.lastEventMs = at;
    }

    void ProgramStored(float milliseconds)
    {
      Begin();

      BootRecord& record = GetRecord();
      std::lock_guard<std::mutex> guard(record.lock);

      const double at = ElapsedMsLocked(record);
      record.programStored++;
      record.storeMilliseconds += milliseconds;

      record.lastEventMs = at;
    }

    void ShaderSourceParsed(float milliseconds)
    {
      Begin();

      BootRecord& record = GetRecord();
      std::lock_guard<std::mutex> guard(record.lock);

      record.sourceCount++;
      record.sourceMilliseconds += milliseconds;
    }

    void Report()
    {
      uint stageCount = 0;
      uint programCount = 0;
      uint programFromCache = 0;
      uint programStored = 0;
      float stageMilliseconds = 0.0f;
      float programMilliseconds = 0.0f;
      float storeMilliseconds = 0.0f;
      uint sourceCount = 0;
      float sourceMilliseconds = 0.0f;
      double firstEventMs = -1.0;
      double lastEventMs = 0.0;
      StringArray stages;

      {
        BootRecord& record = GetRecord();
        std::lock_guard<std::mutex> guard(record.lock);

        if (record.reported)
        {
          return;
        }
        record.reported = true;

        stageCount = record.stageCount;
        programCount = record.programCount;
        programFromCache = record.programFromCache;
        programStored = record.programStored;
        stageMilliseconds = record.stageMilliseconds;
        programMilliseconds = record.programMilliseconds;
        storeMilliseconds = record.storeMilliseconds;
        sourceCount = record.sourceCount;
        sourceMilliseconds = record.sourceMilliseconds;
        firstEventMs = record.firstEventMs;
        lastEventMs = record.lastEventMs;
        stages = record.stages;
      }

      TK_LOG("BOOT shaders: %u stage variants compiled in %.1f ms, %u programs (%u linked, %u restored) in %.1f ms",
             stageCount,
             (double) stageMilliseconds,
             programCount,
             programCount - programFromCache,
             programFromCache,
             (double) programMilliseconds);

      TK_LOG("BOOT shaders: %u sources parsed in %.1f ms", sourceCount, (double) sourceMilliseconds);

      if (programStored > 0)
      {
        TK_LOG("BOOT shaders: %u program binaries written back in %.1f ms", programStored, (double) storeMilliseconds);
      }

      if (firstEventMs >= 0.0)
      {
        TK_LOG("BOOT shaders: work spans %.1f ms .. %.1f ms of the boot (%.1f ms wall)",
               firstEventMs,
               lastEventMs,
               lastEventMs - firstEventMs);
      }

      for (const String& stage : stages)
      {
        TK_LOG("BOOT variant %s", stage.c_str());
      }

      TK_LOG("BOOT done at %.1f ms", ElapsedMs());
    }

  } // namespace BootStats

} // namespace ToolKit
