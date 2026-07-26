/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Hash.h"
#include "Types.h"

namespace ToolKit
{

  // Hierarchical Profiler System
  //////////////////////////////////////////

  typedef std::vector<struct ProfilerNode*> ProfilerNodeArray;

  /** A single node in the profiler tree hierarchy. */
  struct TK_API ProfilerNode
  {
    String name;                          //!< Name of this scope.
    uint64 nameHash            = 0;       //!< Hash of the name for fast lookups.
    uint64 beginCycle          = 0;       //!< Start cycle of current measurement (rdtsc).
    uint64 inclusiveCycles     = 0;       //!< Total cycles including children - current frame.
    uint64 exclusiveCycles     = 0;       //!< Cycles excluding children - current frame.
    uint64 childrenCyclesAtBegin = 0;     //!< Snapshot of children's inclusiveCycles sum at BeginScope.
    uint64 childrenInclusiveSum = 0;      //!< Running sum of children's inclusiveCycles (avoids O(n) loop).
    // Sliding-window display values (~1s = WINDOW_SIZE frames at 60fps).
    static constexpr uint8 WINDOW_SIZE = 15;
    float inclHistory[WINDOW_SIZE]      = {};  //!< Ring buffer of inclusive ms per frame.
    float exclHistory[WINDOW_SIZE]      = {};  //!< Ring buffer of exclusive ms per frame.
    float inclWindowSum                 = 0.0f; //!< Running sum of inclHistory (O(1) average).
    float exclWindowSum                 = 0.0f; //!< Running sum of exclHistory (O(1) average).
    uint8 historyPos                    = 0;    //!< Next write position in the ring buffer.
    uint8 historyCount                  = 0;    //!< Number of entries in the buffer (0..WINDOW_SIZE).
    float inclusiveTimePrev             = 0.0f; //!< Window average of inclusive time for display (ms).
    float exclusiveTimePrev             = 0.0f; //!< Window average of exclusive time for display (ms).
    float accumulatedIncl               = 0.0f; //!< Accumulated inclusive time over frames (ms).
    float accumulatedExcl               = 0.0f; //!< Accumulated exclusive time over frames (ms).
    uint hitCount                       = 0;    //!< Number of times this scope was hit.
    uint hitCountPrev                   = 0;    //!< Previous frame hit count for display.
    uint depth                          = 0;    //!< Depth in the tree (0 = root).
    ProfilerNode* parent                = nullptr; //!< Parent node.
    ProfilerNodeArray children;                 //!< Child nodes.
    bool expanded                       = false; //!< UI expansion state.
    bool enabled                        = true;  //!< Whether to display this node.

    /** Get average inclusive time. */
    float GetAverageInclusive() const { return hitCount > 0 ? accumulatedIncl / hitCount : 0.0f; }

    /** Get average exclusive time. */
    float GetAverageExclusive() const { return hitCount > 0 ? accumulatedExcl / hitCount : 0.0f; }

    /** Reset all accumulated data. */
    void ResetAll()
    {
      inclusiveCycles       = 0;
      exclusiveCycles       = 0;
      childrenCyclesAtBegin = 0;
      childrenInclusiveSum  = 0;
      // Reset sliding window.
      for (uint8 i = 0; i < WINDOW_SIZE; ++i)
      {
        inclHistory[i] = 0.0f;
        exclHistory[i] = 0.0f;
      }
      inclWindowSum     = 0.0f;
      exclWindowSum     = 0.0f;
      historyPos        = 0;
      historyCount      = 0;
      inclusiveTimePrev = 0.0f;
      exclusiveTimePrev = 0.0f;
      accumulatedIncl   = 0.0f;
      accumulatedExcl   = 0.0f;
      hitCount          = 0;
      hitCountPrev      = 0;
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
    void BeginScope(uint64_t nameHash, StringView name);

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

    /** Get sliding-window average frame time for stable display. */
    float GetSmoothedFrameTime() const
    {
      return m_frameTimeCount > 0 ? m_frameTimeSum / m_frameTimeCount : 0.0f;
    }

    /** Get the number of frames recorded. */
    uint GetFrameCount() const { return m_frameCount; }

    /** Enable or disable profiling. */
    void SetEnabled(bool enabled) { m_enabled = enabled; }

    /** Check if profiling is enabled. */
    bool IsEnabled() const { return m_enabled; }

   private:
    void DeleteNodeRecursive(ProfilerNode* node);
    void SwapNodeFrameData(ProfilerNode* node);
    void EnsureCalibrated();

    // Outlier detection: returns true and replaces value with avg of previous 2
    // if value > 3x the average.
    static float FilterOutlier(float value, const float* history, uint8 pos, uint8 count);

   private:
    ProfilerNodeArray m_rootNodes;          //!< Root level nodes.
    ProfilerNode* m_currentNode  = nullptr; //!< Currently active scope.
    float m_frameBeginTime       = 0.0f;    //!< Time when frame began.
    float m_frameTime            = 0.0f;    //!< Last frame's total time.
    float m_accumulatedFrameTime = 0.0f;    //!< Accumulated frame time.
    // Sliding-window frame time history (~1s).
    float m_frameTimeHistory[ProfilerNode::WINDOW_SIZE] = {};
    float m_frameTimeSum                                = 0.0f;
    uint8 m_frameTimePos                                = 0;
    uint8 m_frameTimeCount                              = 0;
    uint m_frameCount            = 0;       //!< Number of frames recorded.
    bool m_enabled               = true;    //!< Whether profiling is active.
    double m_cyclesToMs          = 0.0;     //!< Calibration: cycles to milliseconds.
  };

  /** RAII helper for automatic scope management. */
  class TK_API ProfileScope
  {
   public:
    ProfileScope(StringView name);
    ProfileScope(uint64_t nameHash, StringView name);
    ~ProfileScope();

   private:
    bool m_active = false;
  };

// Convenience macros for profiling.
#define TK_PROFILE_SCOPE(name) ToolKit::ProfileScope _tkProfileScope##__LINE__(name)
#define TK_PROFILE_FUNCTION()                                                                                          \
  ToolKit::ProfileScope _tkProfileScope##__LINE__(ToolKit::TKConstexprHash(__FUNCTION__, sizeof(__FUNCTION__) - 1),    \
                                                  __FUNCTION__)

  // Per-Frame Counter
  //////////////////////////////////////////

  /** Indices for per-frame stat counters. */
  enum class FrameStatType
  {
    // Pre-Phase-1
    LightCacheInvalidation,
    MaterialCacheInvalidation,
    UboUpdates,
    CameraUpdate,
    DirectionalLightUpdate,
    DrawCall,
    RenderPass,
    // Phase 1 - live overlay
    InstancedDrawCall,
    BatchCount,
    RenderItemCount,
    ShadowRedrawCount,
    CulledObjectCount,
    VisibleObjectCount,
    UploadedBytes,
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

    // GPU Elapsed Time (for A/B path comparison — instanced vs legacy, VTF vs vertex-attribute).
    TK_API void SetGpuElapsedTime(float cpu, float gpu);
    TK_API void GetGpuElapsedTime(float& cpu, float& gpu);

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
    TK_API void SetProfilerEnabled(bool enabled);
    TK_API bool IsProfilerEnabled();
  } // namespace Profiler

} // namespace ToolKit
