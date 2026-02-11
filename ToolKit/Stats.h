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

  /** A single node in the profiler tree hierarchy. */
  struct TK_API ProfilerNode
  {
    String name;                         //!< Name of this scope.
    float beginTime         = 0.0f;      //!< Start time of current measurement.
    float inclusiveTime     = 0.0f;      //!< Total time including children (ms) - current frame.
    float exclusiveTime     = 0.0f;      //!< Time excluding children (ms) - current frame.
    float inclusiveTimePrev = 0.0f;      //!< Previous frame inclusive time for display.
    float exclusiveTimePrev = 0.0f;      //!< Previous frame exclusive time for display.
    float accumulatedIncl   = 0.0f;      //!< Accumulated inclusive time over frames.
    float accumulatedExcl   = 0.0f;      //!< Accumulated exclusive time over frames.
    uint hitCount           = 0;         //!< Number of times this scope was hit.
    uint hitCountPrev       = 0;         //!< Previous frame hit count for display.
    uint depth              = 0;         //!< Depth in the tree (0 = root).
    ProfilerNode* parent    = nullptr;   //!< Parent node.
    std::vector<ProfilerNode*> children; //!< Child nodes.
    bool expanded = false;               //!< UI expansion state.
    bool enabled  = true;                //!< Whether to display this node.

    /** Get average inclusive time. */
    float GetAverageInclusive() const { return hitCount > 0 ? accumulatedIncl / hitCount : 0.0f; }

    /** Get average exclusive time. */
    float GetAverageExclusive() const { return hitCount > 0 ? accumulatedExcl / hitCount : 0.0f; }

    /** Reset frame-specific data - called at end of frame to swap values. */
    void SwapFrameData()
    {
      inclusiveTimePrev = inclusiveTime;
      exclusiveTimePrev = exclusiveTime;
      hitCountPrev      = hitCount;
      inclusiveTime     = 0.0f;
      exclusiveTime     = 0.0f;
      hitCount          = 0;
    }

    /** Reset all accumulated data. */
    void ResetAll()
    {
      inclusiveTime     = 0.0f;
      exclusiveTime     = 0.0f;
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

    /** End the current profiling scope. Call at the end of a code block. */
    void EndScope();

    /** Called at the beginning of each frame to prepare for new measurements. */
    void BeginFrame();

    /** Called at the end of each frame to finalize measurements. */
    void EndFrame();

    /** Reset all profiling data. */
    void Reset();

    /** Get the root nodes of the profile tree. */
    const std::vector<ProfilerNode*>& GetRootNodes() const { return m_rootNodes; }

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

    /** Set whether to expand all nodes by default. */
    void SetExpandAll(bool expand);

   private:
    ProfilerNode* FindOrCreateChild(ProfilerNode* parent, StringView name);
    void BuildTreeString(const ProfilerNode* node, String& output, const String& prefix, bool isLast) const;
    void ResetNodeRecursive(ProfilerNode* node);
    void DeleteNodeRecursive(ProfilerNode* node);
    void SwapNodeFrameData(ProfilerNode* node);

   private:
    std::unordered_map<String, ProfilerNode*> m_nodeMap; //!< Fast lookup by full path.
    std::vector<ProfilerNode*> m_rootNodes;              //!< Root level nodes.
    ProfilerNode* m_currentNode = nullptr;               //!< Currently active scope.
    std::vector<String> m_scopeStack;                    //!< Stack of active scope names for path building.
    float m_frameBeginTime       = 0.0f;                 //!< Time when frame began.
    float m_frameTime            = 0.0f;                 //!< Last frame's total time.
    float m_accumulatedFrameTime = 0.0f;                 //!< Accumulated frame time.
    uint m_frameCount            = 0;                    //!< Number of frames recorded.
    bool m_enabled               = true;                 //!< Whether profiling is active.
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

  // TKStats Class (Original + Profiler Integration)
  //////////////////////////////////////////

  class TK_API TKStats
  {
   public:
    // Vram Usage
    //////////////////////////////////////////

    inline uint64 GetTotalVRAMUsageInBytes() { return m_totalVRAMUsageInBytes; }

    inline uint64 GetTotalVRAMUsageInKB() { return m_totalVRAMUsageInBytes / 1024; }

    inline uint64 GetTotalVRAMUsageInMB() { return m_totalVRAMUsageInBytes / (1024 * 1024); }

    inline void AddVRAMUsageInBytes(uint64 bytes) { m_totalVRAMUsageInBytes += bytes; }

    void RemoveVRAMUsageInBytes(uint64 bytes);

    inline void ResetVRAMUsage() { m_totalVRAMUsageInBytes = 0; }

    // Draw Call
    //////////////////////////////////////////

    inline void AddDrawCall() { m_drawCallCount++; }

    inline uint64 GetDrawCallCount() { return m_drawCallCountPrev; }

    // Hardware Render Pass Counter
    //////////////////////////////////////////

    inline uint64 GetRenderPassCount() { return m_renderPassCountPrev; }

    /** Returns all measured per frame statistics as string. */
    String GetPerFrameStats();

    // Hierarchical Profiler
    //////////////////////////////////////////

    /** Get the hierarchical profiler instance. */
    TKProfiler& GetProfiler() { return m_profiler; }

   public:
    /** Gpu Frame time for current frame. */
    float m_elapsedGpuRenderTime                 = 0.0f;
    /** Gpu Frame time for average over 100 frames. */
    float m_elapsedGpuRenderTimeAvg              = 0.0f;
    /** Cpu Frame time for current frame. */
    float m_elapsedCpuRenderTime                 = 0.0f;
    /** Cpu Frame time for average over 100 frames. */
    float m_elapsedCpuRenderTimeAvg              = 0.0f;
    /** Number of times the light cache invalidated for a frame */
    uint m_lightCacheInvalidationPerFrame        = 0;
    uint m_lightCacheInvalidationPerFramePrev    = 0;
    /** Number of times the material cache invalidated for a frame */
    uint m_materialCacheInvalidationPerFrame     = 0;
    uint m_materialCacheInvalidationPerFramePrev = 0;
    /** Number of times any ubo mapped for a frame. */
    uint m_uboUpdatesPerFrame                    = 0;
    uint m_uboUpdatesPerFramePrev                = 0;
    /** Number of times camera ubo update for a frame. */
    uint m_cameraUpdatePerFrame                  = 0;
    uint m_cameraUpdatePerFramePrev              = 0;
    /** Number of times directional light updated in a frame. */
    uint m_directionalLightUpdatePerFrame        = 0;
    uint m_directionalLightUpdatePerFramePrev    = 0;

    /** Number of draw calls in a frame. */
    uint64 m_drawCallCount                       = 0;
    uint64 m_drawCallCountPrev                   = 0;

    /** Number of hardware render passes in a frame. */
    uint64 m_renderPassCount                     = 0;
    uint64 m_renderPassCountPrev                 = 0;

    uint64 m_totalVRAMUsageInBytes               = 0;

   private:
    TKProfiler m_profiler;
  };

  namespace Stats
  {
    TK_API void SetGpuResourceLabel(StringView label, GpuResourceType resourceType, uint resourceId);
    TK_API void BeginGpuScope(StringView name);
    TK_API void EndGpuScope();
    TK_API uint64 GetLightCacheInvalidationPerFrame();
    TK_API uint64 GetUboUpdatesPerFrame();
    TK_API uint64 GetCameraUpdatesPerFrame();
    TK_API uint64 GetDirectionalLightUpdatesPerFrame();
    TK_API uint64 GetTotalVRAMUsageInBytes();
    TK_API uint64 GetTotalVRAMUsageInKB();
    TK_API uint64 GetTotalVRAMUsageInMB();
    TK_API void AddVRAMUsageInBytes(uint64 bytes);
    TK_API void RemoveVRAMUsageInBytes(uint64 bytes);
    TK_API void ResetVRAMUsage();
    TK_API void AddDrawCall();
    TK_API uint64 GetDrawCallCount();
    TK_API uint64 GetRenderPassCount();
    TK_API void GetRenderTime(float& cpu, float& gpu);
    TK_API void GetRenderTimeAvg(float& cpu, float& gpu);

    // Hierarchical Profiler API
    TK_API void BeginProfileScope(StringView name);
    TK_API void EndProfileScope();
    TK_API void BeginProfileFrame();
    TK_API void EndProfileFrame();
    TK_API void ResetProfiler();
    TK_API TKProfiler* GetProfiler();
    TK_API String GetProfileTreeString();
    TK_API void SetProfilerEnabled(bool enabled);
    TK_API bool IsProfilerEnabled();

  }; // namespace Stats

} // namespace ToolKit
