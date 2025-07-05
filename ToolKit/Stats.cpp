/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Stats.h"

#include "RenderSystem.h"
#include "TKAssert.h"
#include "TKOpenGL.h"
#include "ToolKit.h"
#include "Util.h"

#include "DebugNew.h"

namespace ToolKit
{

  void TKStats::BeginTimer(StringView name)
  {
    TimeArgs& args = m_profileTimerMap[name.data()];
    args.beginTime = GetElapsedMilliSeconds();
  }

  // Finalize a timer updates statistics.
  void TKStats::EndTimer(StringView name)
  {
    TimeArgs& args        = m_profileTimerMap[name.data()];
    args.elapsedTime      = GetElapsedMilliSeconds() - args.beginTime;
    args.accumulatedTime += args.elapsedTime;
    args.hitCount++;
  }

  void TKStats::RemoveVRAMUsageInBytes(uint64 bytes)
  {
    uint64 old = m_totalVRAMUsageInBytes;

    TK_ASSERT_ONCE(m_totalVRAMUsageInBytes >= bytes);

    m_totalVRAMUsageInBytes -= bytes;
  }

  String TKStats::GetPerFrameStats()
  {
    String stats;

    float cpuTime = 0.0f, gpuTime = 0.0f;
    float cpuTimeAvg = 0.0f, gpuTimeAvg = 0.0f;
    Stats::GetRenderTime(cpuTime, gpuTime);
    Stats::GetRenderTimeAvg(cpuTimeAvg, gpuTimeAvg);

    UVec2 appWndSize = GetRenderSystem()->GetAppWindowSize();

    char buffer[128];

    snprintf(buffer, sizeof(buffer), "Window Resolution: %ux%u\n", appWndSize.x, appWndSize.y);
    stats += buffer;

    snprintf(buffer, sizeof(buffer), "Render Time (gpu-ms): %.2f, FPS: %.2f\n", gpuTime, 1000.0f / gpuTime);
    stats += buffer;

    snprintf(buffer, sizeof(buffer), "Render Time (gpuAvg-ms): %.2f, FPS: %.2f\n", gpuTimeAvg, 1000.0f / gpuTimeAvg);
    stats += buffer;

    snprintf(buffer, sizeof(buffer), "Render Time (cpu-ms): %.2f, FPS: %.2f\n", cpuTime, 1000.0f / cpuTime);
    stats += buffer;

    snprintf(buffer, sizeof(buffer), "Render Time (cpuAvg-ms): %.2f, FPS: %.2f\n", cpuTimeAvg, 1000.0f / cpuTimeAvg);
    stats += buffer;

    stats += "----------\n";

    snprintf(buffer, sizeof(buffer), "Total Draw Call: %llu\n", Stats::GetDrawCallCount());
    stats += buffer;

    snprintf(buffer, sizeof(buffer), "Total Hardware Render Pass: %llu\n", Stats::GetRenderPassCount());
    stats += buffer;

    snprintf(buffer, sizeof(buffer), "Approximate Total VRAM Usage: %llu MB\n", Stats::GetTotalVRAMUsageInMB());
    stats += buffer;

    snprintf(buffer,
             sizeof(buffer),
             "Light Cache Invalidation Per Frame: %llu\n",
             Stats::GetLightCacheInvalidationPerFrame());
    stats += buffer;

    snprintf(buffer, sizeof(buffer), "Camera updates Per Frame: %llu\n", Stats::GetCameraUpdatesPerFrame());
    stats += buffer;

    snprintf(buffer,
             sizeof(buffer),
             "Directional Light & PVM updates Per Frame: %llu\n",
             Stats::GetDirectionalLightUpdatesPerFrame());
    stats += buffer;

    snprintf(buffer, sizeof(buffer), "UBO updates Per Frame: %llu\n", Stats::GetUboUpdatesPerFrame());
    stats += buffer;

    return stats;
  }

  namespace Stats
  {
    TK_API void SetGpuResourceLabel(StringView label, GpuResourceType resourceType, uint resourceId)
    {
      if (glLabelObjectEXT != nullptr && label.size() > 0)
      {
        String labelId = String(label) + "_" + std::to_string(resourceId);
        glLabelObjectEXT((GLenum) resourceType, (GLuint) resourceId, 0, labelId.c_str());
      }
    }

    void BeginGpuScope(StringView name)
    {
      if (glPushGroupMarkerEXT != nullptr)
      {
        glPushGroupMarkerEXT(-1, name.data());
      }
    }

    void EndGpuScope()
    {
      if (glPopGroupMarkerEXT != nullptr)
      {
        glPopGroupMarkerEXT();
      }
    }

    void BeginTimeScope(StringView name)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->BeginTimer(name);
      }
    }

    void EndTimeScope(StringView name)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->EndTimer(name);
      }
    }

    uint64 GetLightCacheInvalidationPerFrame()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->m_lightCacheInvalidationPerFramePrev;
      }

      return 0;
    }

    uint64 GetUboUpdatesPerFrame()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->m_uboUpdatesPerFramePrev;
      }

      return 0;
    }

    uint64 GetCameraUpdatesPerFrame()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->m_cameraUpdatePerFramePrev;
      }
      return 0;
    }

    uint64 GetDirectionalLightUpdatesPerFrame()
    {

      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->m_directionalLightUpdatePerFramePrev;
      }
      return 0;
    }

    uint64 GetTotalVRAMUsageInBytes()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->GetTotalVRAMUsageInBytes();
      }

      return 0;
    }

    uint64 GetTotalVRAMUsageInKB()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->GetTotalVRAMUsageInKB();
      }

      return 0;
    }

    uint64 GetTotalVRAMUsageInMB()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->GetTotalVRAMUsageInMB();
      }

      return 0;
    }

    void AddVRAMUsageInBytes(uint64 bytes)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->AddVRAMUsageInBytes(bytes);
      }
    }

    void RemoveVRAMUsageInBytes(uint64 bytes)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->RemoveVRAMUsageInBytes(bytes);
      }
    }

    void ResetVRAMUsage()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->ResetVRAMUsage();
      }
    }

    void AddDrawCall()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->AddDrawCall();
      }
    }

    uint64 GetDrawCallCount()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->GetDrawCallCount();
      }
      else
      {
        return 0;
      }
    }

    uint64 GetRenderPassCount()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->GetRenderPassCount();
      }
      else
      {
        return 0;
      }
    }

    void GetRenderTime(float& cpu, float& gpu)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        cpu = tkStats->m_elapsedCpuRenderTime;
        gpu = tkStats->m_elapsedGpuRenderTime;
      }
      else
      {
        cpu = 1.0f;
        gpu = 1.0f;
      }
    }

    void GetRenderTimeAvg(float& cpu, float& gpu)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        cpu = tkStats->m_elapsedCpuRenderTimeAvg;
        gpu = tkStats->m_elapsedGpuRenderTimeAvg;
      }
      else
      {
        cpu = 1.0f;
        gpu = 1.0f;
      }
    }

  } // namespace Stats

} // namespace ToolKit
