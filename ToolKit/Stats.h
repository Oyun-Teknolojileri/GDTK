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

#define TKStatTimerMap GetTKStats()->m_profileTimerMap

  class TK_API TKStats
  {
   public:
    // Timers
    //////////////////////////////////////////

    /** Timer arguments for providing statistics. */
    struct TimeArgs
    {
      bool enabled          = true; //!< Whether show the timer in the console or not.
      uint hitCount         = 1;    //!< Number of times the measurement performed.
      float beginTime       = 0.0f; //!< Current start time.
      float elapsedTime     = 0.0f; //!< Elapsed time between consecutive begin - end calls..
      float accumulatedTime = 0.0f; //! Accumulated elapsed time.
    };

    // Creates a timer or register its beginning.
    void BeginTimer(StringView name);

    // Finalize a timer updates statistics.
    void EndTimer(StringView name);

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

    /** Timers added to the source. */
    std::unordered_map<String, TimeArgs> m_profileTimerMap;

    uint64 m_totalVRAMUsageInBytes = 0;
  };

  namespace Stats
  {
    TK_API void SetGpuResourceLabel(StringView label, GpuResourceType resourceType, uint resourceId);
    TK_API void BeginGpuScope(StringView name);
    TK_API void EndGpuScope();
    TK_API void BeginTimeScope(StringView name);
    TK_API void EndTimeScope(StringView name);
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

  }; // namespace Stats

} // namespace ToolKit
