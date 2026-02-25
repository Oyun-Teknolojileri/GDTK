/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Types.h"

namespace ToolKit
{

  // Hierarchical Profiler System
  //////////////////////////////////////////

  typedef std::vector<struct ProfilerNode*> ProfilerNodeArray;

  /** A single node in the profiler tree hierarchy. */
  struct TK_API ProfilerNode
  {
    String name;                         //!< Name of this scope.
    float beginTime           = 0.0f;    //!< Start time of current measurement.
    float inclusiveTime       = 0.0f;    //!< Total time including children (ms) - current frame.
    float exclusiveTime       = 0.0f;    //!< Time excluding children (ms) - current frame.
    float inclusiveTimePrev   = 0.0f;    //!< Previous frame inclusive time for display.
    float exclusiveTimePrev   = 0.0f;    //!< Previous frame exclusive time for display.
    float accumulatedIncl     = 0.0f;    //!< Accumulated inclusive time over frames.
    float accumulatedExcl     = 0.0f;    //!< Accumulated exclusive time over frames.
    float childrenTimeAtBegin = 0.0f;    //!< Snapshot of children's inclusiveTime sum at BeginScope.
    uint hitCount             = 0;       //!< Number of times this scope was hit.
    uint hitCountPrev         = 0;       //!< Previous frame hit count for display.
    uint depth                = 0;       //!< Depth in the tree (0 = root).
    ProfilerNode* parent      = nullptr; //!< Parent node.
    ProfilerNodeArray children;          //!< Child nodes.
    bool expanded = false;               //!< UI expansion state.
    bool enabled  = true;                //!< Whether to display this node.

    /** Get average inclusive time. */
    float GetAverageInclusive() const { return hitCount > 0 ? accumulatedIncl / hitCount : 0.0f; }

    /** Get average exclusive time. */
    float GetAverageExclusive() const { return hitCount > 0 ? accumulatedExcl / hitCount : 0.0f; }

    /** Reset frame-specific data - called at end of frame to swap values. */
    void SwapFrameData()
    {
      inclusiveTimePrev   = inclusiveTime;
      exclusiveTimePrev   = exclusiveTime;
      hitCountPrev        = hitCount;
      inclusiveTime       = 0.0f;
      exclusiveTime       = 0.0f;
      childrenTimeAtBegin = 0.0f;
      hitCount            = 0;
    }

    /** Reset all accumulated data. */
    void ResetAll()
    {
      inclusiveTime       = 0.0f;
      exclusiveTime       = 0.0f;
      inclusiveTimePrev   = 0.0f;
      exclusiveTimePrev   = 0.0f;
      accumulatedIncl     = 0.0f;
      accumulatedExcl     = 0.0f;
      childrenTimeAtBegin = 0.0f;
      hitCount            = 0;
      hitCountPrev        = 0;
    }
  };

  /** Main profiler class that manages hierarchical timing data. */
  class TK_API TKProfiler
  {
   public:
    TKProfiler();
    ~TKProfiler();

    /** Begin a profiling scope. Call at the start of a code block. */
    void BeginScope(StringView name);

    /** End the current profiling scope. Call at the end of a code block. */
    void EndScope();

    /** Called at the beginning of each frame to prepare for new measurements. */
    void BeginFrame();

    /** Called at the end of each frame to finalize measurements. */
    void EndFrame();

    /** Reset all profiling data. */
    void Reset();

    /** Get the root nodes of the profile tree. */
    const ProfilerNodeArray& GetRootNodes() const { return m_rootNodes; }

    /** Get total frame time in milliseconds. */
    float GetFrameTime() const { return m_frameTime; }

    /** Get average frame time over accumulated frames. */
    float GetAverageFrameTime() const { return m_frameCount > 0 ? m_accumulatedFrameTime / m_frameCount : 0.0f; }

    /** Get the number of frames recorded. */
    uint GetFrameCount() const { return m_frameCount; }

    /** Generate a formatted string representation of the profile tree. */
    String GetProfileTreeString() const;

    /** Enable or disable profiling. */
    void SetEnabled(bool enabled) { m_enabled = enabled; }

    /** Check if profiling is enabled. */
    bool IsEnabled() const { return m_enabled; }

   private:
    void BuildTreeString(const ProfilerNode* node, String& output, const String& prefix, bool isLast) const;
    void DeleteNodeRecursive(ProfilerNode* node);
    void SwapNodeFrameData(ProfilerNode* node);

   private:
    ProfilerNodeArray m_rootNodes;          //!< Root level nodes.
    ProfilerNode* m_currentNode  = nullptr; //!< Currently active scope.
    float m_frameBeginTime       = 0.0f;    //!< Time when frame began.
    float m_frameTime            = 0.0f;    //!< Last frame's total time.
    float m_accumulatedFrameTime = 0.0f;    //!< Accumulated frame time.
    uint m_frameCount            = 0;       //!< Number of frames recorded.
    bool m_enabled               = true;    //!< Whether profiling is active.
  };

  /** RAII helper for automatic scope management. */
  class TK_API ProfileScope
  {
   public:
    ProfileScope(StringView name);
    ~ProfileScope();

   private:
    bool m_active = false;
  };

// Convenience macros for profiling.
#define TK_PROFILE_SCOPE(name) ToolKit::ProfileScope _tkProfileScope##__LINE__(name)
#define TK_PROFILE_FUNCTION()  TK_PROFILE_SCOPE(__FUNCTION__)

  // Per-Frame Counter
  //////////////////////////////////////////

  /** Indices for per-frame stat counters. */
  enum class FrameStatType
  {
    LightCacheInvalidation,
    MaterialCacheInvalidation,
    UboUpdates,
    CameraUpdate,
    DirectionalLightUpdate,
    DrawCall,
    RenderPass,
    Count
  };

  enum class MemoryUnit
  {
    Byte,
    KB,
    MB
  };

  // TKStats - opaque, defined in Stats.cpp.
  class TKStats;

  /** Creates a TKStats instance. Defined in Stats.cpp. */
  TK_API TKStats* CreateTKStats();

  /** Destroys a TKStats instance. Defined in Stats.cpp. */
  TK_API void DestroyTKStats(TKStats* stats);

  namespace Stats
  {
    // GPU Debug
    TK_API void SetGpuResourceLabel(StringView label, GpuResourceType resourceType, uint resourceId);
    TK_API void BeginGpuScope(StringView name);
    TK_API void EndGpuScope();

    // Per-Frame Counter API
    TK_API void IncrementStat(FrameStatType type);
    TK_API void AddStat(FrameStatType type, uint64 amount);
    TK_API void SwapFrameStats();
    TK_API uint64 GetStatPrev(FrameStatType type);

    // VRAM
    TK_API uint64 GetVRAMUsage(MemoryUnit unit);
    TK_API void AddVRAMUsageInBytes(uint64 bytes);
    TK_API void RemoveVRAMUsageInBytes(uint64 bytes);
    TK_API void ResetVRAMUsage();

    // Render Time
    TK_API void GetRenderTime(float& cpu, float& gpu);
    TK_API void GetRenderTimeAvg(float& cpu, float& gpu);
    TK_API void SetRenderTime(float cpu, float gpu);
    TK_API void SetRenderTimeAvg(float cpu, float gpu);

    // Formatted Stats
    TK_API String GetPerFrameStats();
  } // namespace Stats

  // Hierarchical Profiler API
  namespace Profiler
  {
    TK_API void BeginProfileScope(StringView name);
    TK_API void EndProfileScope();
    TK_API void BeginProfileFrame();
    TK_API void EndProfileFrame();
    TK_API void ResetProfiler();
    TK_API TKProfiler* GetProfiler();
    TK_API String GetProfileTreeString();
    TK_API void SetProfilerEnabled(bool enabled);
    TK_API bool IsProfilerEnabled();
  } // namespace Profiler

} // namespace ToolKit
