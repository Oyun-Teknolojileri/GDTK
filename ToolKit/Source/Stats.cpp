/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Stats.h"

#include "IGraphicsBackend.h"
#include "RenderSystem.h"
#include "Renderer.h"
#include "TKAssert.h"
#include "ToolKit.h"
#include "Util.h"

#include <chrono>
#include <thread>

#include "DebugNew.h"

namespace ToolKit
{

// Portable cycle counter — rdtsc on x86/64, CNTVCT on ARM64, chrono fallback.
inline uint64 ReadCycleCounter()
{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  // x86 / x86-64: rdtsc via intrinsic (GCC, Clang, MSVC).
  #if defined(__GNUC__) || defined(__clang__)
    return __builtin_ia32_rdtsc();
  #elif defined(_MSC_VER)
    #include <intrin.h>
    return __rdtsc();
  #else
    #include <x86intrin.h>
    return __rdtsc();
  #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
  // ARM64: generic timer counter (Linux, Android, Windows ARM).
  uint64 val;
  __asm__ volatile("mrs %0, CNTVCT_EL0" : "=r"(val));
  return val;
#else
  // Fallback: chrono ticks (not used on hot path, calibration only).
  return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}

  // Per-Frame Counter (internal)
  //////////////////////////////////////////

  /** A single per-frame counter with automatic prev/current swap. */
  struct FrameStat
  {
    uint64 current = 0;
    uint64 prev    = 0;

    inline void Increment() { current++; }

    inline void Add(uint64 amount) { current += amount; }

    inline void Swap()
    {
      prev    = current;
      current = 0;
    }
  };

  // TKStats Class (internal, opaque to header consumers)
  //////////////////////////////////////////

  class TKStats
  {
   public:
    // Vram Usage
    inline uint64 GetTotalVRAMUsageInBytes() { return m_totalVRAMUsageInBytes; }

    inline uint64 GetTotalVRAMUsageInKB() { return m_totalVRAMUsageInBytes / 1024; }

    inline uint64 GetTotalVRAMUsageInMB() { return m_totalVRAMUsageInBytes / (1024 * 1024); }

    inline void AddVRAMUsageInBytes(uint64 bytes) { m_totalVRAMUsageInBytes += bytes; }

    void RemoveVRAMUsageInBytes(uint64 bytes);

    inline void ResetVRAMUsage() { m_totalVRAMUsageInBytes = 0; }

    // Per-Frame Counters
    inline void IncrementStat(FrameStatType type) { m_frameStats[(int) type].Increment(); }

    inline void AddStat(FrameStatType type, uint64 amount) { m_frameStats[(int) type].Add(amount); }

    inline uint64 GetStatPrev(FrameStatType type) const { return m_frameStats[(int) type].prev; }

    void SwapFrameStats()
    {
      for (int i = 0; i < (int) FrameStatType::Count; i++)
      {
        m_frameStats[i].Swap();
      }
    }

    String GetPerFrameStats();

    // Hierarchical Profiler
    TKProfiler& GetProfiler() { return m_profiler; }

    // Render Time
    float m_elapsedGpuRenderTime    = 0.0f;
    float m_elapsedGpuRenderTimeAvg = 0.0f;
    float m_elapsedCpuRenderTime    = 0.0f;
    float m_elapsedCpuRenderTimeAvg = 0.0f;

    // GPU elapsed time for A/B path comparison (Phase 2+).
    float m_gpuElapsedCpuTime       = 0.0f;
    float m_gpuElapsedGpuTime       = 0.0f;

   private:
    FrameStat m_frameStats[(int) FrameStatType::Count];
    uint64 m_totalVRAMUsageInBytes = 0;
    TKProfiler m_profiler;
  };

  // TKProfiler Implementation
  //////////////////////////////////////////

  TKProfiler::TKProfiler() {}

  TKProfiler::~TKProfiler() { Reset(); }

  void TKProfiler::BeginScope(StringView name) { BeginScope(std::hash<StringView> {}(name), name); }

  void TKProfiler::BeginScope(uint64_t nameHash, StringView name)
  {
    if (!m_enabled)
    {
      return;
    }

    // 1. Search for the node in the current context's children
    ProfilerNode* node                  = nullptr;
    const ProfilerNodeArray& searchPool = (m_currentNode == nullptr) ? m_rootNodes : m_currentNode->children;

    for (ProfilerNode* child : searchPool)
    {
      if (child->nameHash == nameHash && child->name == name)
      {
        node = child;
        break;
      }
    }

    // 2. If not found, create a new node and link it to the tree
    if (node == nullptr)
    {
      node           = new ProfilerNode();
      node->name     = String(name);
      node->nameHash = nameHash;
      node->depth    = (m_currentNode == nullptr) ? 0 : m_currentNode->depth + 1;
      node->parent   = m_currentNode;

      if (m_currentNode == nullptr)
        m_rootNodes.push_back(node);
      else
        m_currentNode->children.push_back(node);
    }

    // 3. Record start time and snapshot children's running sum (O(1)).
    node->childrenCyclesAtBegin = node->childrenInclusiveSum;
    node->beginCycle            = ReadCycleCounter();

    // Update current context pointer
    m_currentNode               = node;
  }

  void TKProfiler::EndScope()
  {
    if (!m_enabled || m_currentNode == nullptr)
    {
      return;
    }

    uint64 endCycle                 = ReadCycleCounter();
    uint64 elapsed                  = endCycle - m_currentNode->beginCycle;

    m_currentNode->inclusiveCycles += elapsed;
    m_currentNode->hitCount++;

    // Propagate inclusive delta to parent's running children sum (O(1)).
    if (m_currentNode->parent)
    {
      m_currentNode->parent->childrenInclusiveSum += elapsed;
    }

    // Calculate exclusive cycles from the running children sum (O(1)).
    uint64 childrenDelta = m_currentNode->childrenInclusiveSum - m_currentNode->childrenCyclesAtBegin;
    // Guard against underflow: context switches can make childrenDelta > elapsed.
    uint64 exclusive      = (elapsed > childrenDelta) ? (elapsed - childrenDelta) : 0;

    m_currentNode->exclusiveCycles += exclusive;

    // Move back to parent node without stack operations
    m_currentNode                   = m_currentNode->parent;
  }

  void TKProfiler::BeginFrame()
  {
    if (!m_enabled)
    {
      return;
    }

    m_frameBeginTime = GetElapsedMilliSeconds();
    // Don't reset here - we reset at EndFrame after swapping values
  }

  void TKProfiler::EndFrame()
  {
    if (!m_enabled)
    {
      return;
    }

    m_frameTime             = GetElapsedMilliSeconds() - m_frameBeginTime;
    m_accumulatedFrameTime += m_frameTime;
    m_frameCount++;

    // Ensure calibration is done once before we convert cycles to ms.
    EnsureCalibrated();

    // Sliding-window frame time history (~1s).
    float filteredFt = FilterOutlier(m_frameTime, m_frameTimeHistory, m_frameTimePos, m_frameTimeCount);

    // Remove oldest entry if buffer is full.
    if (m_frameTimeCount == ProfilerNode::WINDOW_SIZE)
    {
      m_frameTimeSum -= m_frameTimeHistory[m_frameTimePos];
    }

    m_frameTimeHistory[m_frameTimePos] = filteredFt;
    m_frameTimeSum                    += filteredFt;
    m_frameTimePos                     = (m_frameTimePos + 1) % ProfilerNode::WINDOW_SIZE;
    if (m_frameTimeCount < ProfilerNode::WINDOW_SIZE)
    {
      m_frameTimeCount++;
    }

    // Swap frame data for all nodes - this preserves values for UI display
    // and resets current frame values for next frame
    for (ProfilerNode* root : m_rootNodes)
    {
      SwapNodeFrameData(root);
    }
  }

  void TKProfiler::Reset()
  {
    for (ProfilerNode* root : m_rootNodes)
    {
      DeleteNodeRecursive(root);
    }

    m_rootNodes.clear();
    m_currentNode          = nullptr;
    m_frameTime            = 0.0f;
    m_accumulatedFrameTime = 0.0f;
    // Reset frame time history.
    for (uint8 i = 0; i < ProfilerNode::WINDOW_SIZE; ++i)
    {
      m_frameTimeHistory[i] = 0.0f;
    }
    m_frameTimeSum   = 0.0f;
    m_frameTimePos   = 0;
    m_frameTimeCount = 0;
    m_frameCount     = 0;
  }

  void TKProfiler::DeleteNodeRecursive(ProfilerNode* node)
  {
    if (node == nullptr)
    {
      return;
    }

    for (ProfilerNode* child : node->children)
    {
      DeleteNodeRecursive(child);
    }

    delete node;
  }

  float TKProfiler::FilterOutlier(float value, const float* history, uint8 pos, uint8 count)
  {
    if (count < 2)
    {
      return value; // Not enough history for outlier detection.
    }

    // Average of the previous 2 entries in the ring buffer.
    uint8 prev1 = (pos == 0) ? ProfilerNode::WINDOW_SIZE - 1 : pos - 1;
    uint8 prev2 = (prev1 == 0) ? ProfilerNode::WINDOW_SIZE - 1 : prev1 - 1;
    float avg   = (history[prev1] + history[prev2]) * 0.5f;

    // If > 3x the trailing average, it's a context-switch spike — suppress.
    if (value > avg * 3.0f)
    {
      return avg;
    }
    return value;
  }

  void TKProfiler::SwapNodeFrameData(ProfilerNode* node)
  {
    if (node == nullptr)
    {
      return;
    }

    // Convert cycles to milliseconds for display.
    float rawInclMs = static_cast<float>(node->inclusiveCycles * m_cyclesToMs);
    float rawExclMs = static_cast<float>(node->exclusiveCycles * m_cyclesToMs);

    // Outlier rejection: compare against trailing 2 frames.
    float filtInclMs = FilterOutlier(rawInclMs, node->inclHistory, node->historyPos, node->historyCount);
    float filtExclMs = FilterOutlier(rawExclMs, node->exclHistory, node->historyPos, node->historyCount);

    // Accumulate filtered values over all frames (ms).
    node->accumulatedIncl += filtInclMs;
    node->accumulatedExcl += filtExclMs;

    // Sliding-window ring buffer update (O(1)).
    if (node->historyCount == ProfilerNode::WINDOW_SIZE)
    {
      // Remove oldest entry from the running sum.
      node->inclWindowSum -= node->inclHistory[node->historyPos];
      node->exclWindowSum -= node->exclHistory[node->historyPos];
    }

    node->inclHistory[node->historyPos] = filtInclMs;
    node->exclHistory[node->historyPos] = filtExclMs;
    node->inclWindowSum                += filtInclMs;
    node->exclWindowSum                += filtExclMs;
    node->historyPos                   = (node->historyPos + 1) % ProfilerNode::WINDOW_SIZE;
    if (node->historyCount < ProfilerNode::WINDOW_SIZE)
    {
      node->historyCount++;
    }

    // Display value = window average.
    node->inclusiveTimePrev = node->inclWindowSum / node->historyCount;
    node->exclusiveTimePrev = node->exclWindowSum / node->historyCount;
    node->hitCountPrev      = node->hitCount;

    // Reset current-frame cycle counters.
    node->inclusiveCycles       = 0;
    node->exclusiveCycles       = 0;
    node->childrenCyclesAtBegin = 0;
    node->childrenInclusiveSum  = 0;
    node->hitCount              = 0;

    for (ProfilerNode* child : node->children)
    {
      SwapNodeFrameData(child);
    }
  }

  void TKProfiler::EnsureCalibrated()
  {
    if (m_cyclesToMs > 0.0)
    {
      return; // Already calibrated.
    }

    // One-time calibration: measure cycles over a known time interval.
    auto t1   = std::chrono::high_resolution_clock::now();
    uint64 c1 = ReadCycleCounter();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint64 c2 = ReadCycleCounter();
    auto t2   = std::chrono::high_resolution_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    if (c2 > c1)
    {
      m_cyclesToMs = ms / static_cast<double>(c2 - c1);
    }
  }

  // ProfileScope Implementation
  //////////////////////////////////////////

  ProfileScope::ProfileScope(StringView name)
  {
    if (TKStats* stats = GetTKStats())
    {
      if (stats->GetProfiler().IsEnabled())
      {
        stats->GetProfiler().BeginScope(name);
        m_active = true;
      }
    }
  }

  ProfileScope::ProfileScope(uint64_t nameHash, StringView name)
  {
    if (TKStats* stats = GetTKStats())
    {
      if (stats->GetProfiler().IsEnabled())
      {
        stats->GetProfiler().BeginScope(nameHash, name);
        m_active = true;
      }
    }
  }

  ProfileScope::~ProfileScope()
  {
    if (m_active)
    {
      if (TKStats* stats = GetTKStats())
      {
        stats->GetProfiler().EndScope();
      }
    }
  }

  // TKStats Factory
  //////////////////////////////////////////

  TKStats* CreateTKStats() { return new TKStats(); }

  void DestroyTKStats(TKStats* stats) { delete stats; }

  // TKStats Implementation
  //////////////////////////////////////////

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

    snprintf(buffer, sizeof(buffer), "Total Draw Call: %llu\n", Stats::GetStatPrev(FrameStatType::DrawCall));
    stats += buffer;

    snprintf(buffer,
             sizeof(buffer),
             "Total Hardware Render Pass ~ %llu\n",
             Stats::GetStatPrev(FrameStatType::RenderPass));
    stats += buffer;

    snprintf(buffer, sizeof(buffer), "Total VRAM Usage ~ %llu MB\n", Stats::GetVRAMUsage(MemoryUnit::MB));
    stats += buffer;

    snprintf(buffer,
             sizeof(buffer),
             "Light Cache Invalidation Per Frame: %llu\n",
             Stats::GetStatPrev(FrameStatType::LightCacheInvalidation));
    stats += buffer;

    snprintf(buffer,
             sizeof(buffer),
             "Camera updates Per Frame: %llu\n",
             Stats::GetStatPrev(FrameStatType::CameraUpdate));
    stats += buffer;

    snprintf(buffer,
             sizeof(buffer),
             "Directional Light & PVM updates Per Frame: %llu\n",
             Stats::GetStatPrev(FrameStatType::DirectionalLightUpdate));
    stats += buffer;

    snprintf(buffer, sizeof(buffer), "UBO updates Per Frame: %llu\n", Stats::GetStatPrev(FrameStatType::UboUpdates));
    stats += buffer;

    snprintf(buffer,
             sizeof(buffer),
             "Visible Objects: %llu | Culled: %llu\n",
             Stats::GetStatPrev(FrameStatType::VisibleObjectCount),
             Stats::GetStatPrev(FrameStatType::CulledObjectCount));
    stats += buffer;

    snprintf(buffer,
             sizeof(buffer),
             "Shadow Redraws: %llu\n",
             Stats::GetStatPrev(FrameStatType::ShadowRedrawCount));
    stats += buffer;

    snprintf(buffer,
             sizeof(buffer),
             "Uploaded Bytes Per Frame: %llu\n",
             Stats::GetStatPrev(FrameStatType::UploadedBytes));
    stats += buffer;

    return stats;
  }

  namespace Stats
  {
    void BeginGpuScope(StringView name)
    {
      if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
      {
        backend->PushDebugGroup(name);
      }
    }

    void EndGpuScope()
    {
      if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
      {
        backend->PopDebugGroup();
      }
    }

    void IncrementStat(FrameStatType type)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->IncrementStat(type);
      }
    }

    void AddStat(FrameStatType type, uint64 amount)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->AddStat(type, amount);
      }
    }

    void SwapFrameStats()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->SwapFrameStats();
      }
    }

    uint64 GetStatPrev(FrameStatType type)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->GetStatPrev(type);
      }
      return 0;
    }

    uint64 GetVRAMUsage(MemoryUnit unit)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        switch (unit)
        {
          case MemoryUnit::KB:
            return tkStats->GetTotalVRAMUsageInKB();
          case MemoryUnit::MB:
            return tkStats->GetTotalVRAMUsageInMB();
          default:
            return tkStats->GetTotalVRAMUsageInBytes();
        }
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

    void SetRenderTime(float cpu, float gpu)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->m_elapsedCpuRenderTime = cpu;
        tkStats->m_elapsedGpuRenderTime = gpu;
      }
    }

    void SetRenderTimeAvg(float cpu, float gpu)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->m_elapsedCpuRenderTimeAvg = cpu;
        tkStats->m_elapsedGpuRenderTimeAvg = gpu;
      }
    }

    void SetGpuElapsedTime(float cpu, float gpu)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->m_gpuElapsedCpuTime = cpu;
        tkStats->m_gpuElapsedGpuTime = gpu;
      }
    }

    void GetGpuElapsedTime(float& cpu, float& gpu)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        cpu = tkStats->m_gpuElapsedCpuTime;
        gpu = tkStats->m_gpuElapsedGpuTime;
      }
      else
      {
        cpu = 1.0f;
        gpu = 1.0f;
      }
    }

    String GetPerFrameStats()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->GetPerFrameStats();
      }
      return "";
    }

  } // namespace Stats

  // Hierarchical Profiler API Implementation
  //////////////////////////////////////////

  namespace Profiler
  {
    void BeginProfileScope(StringView name)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->GetProfiler().BeginScope(name);
      }
    }

    void BeginProfileScope(uint64_t nameHash, StringView name)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->GetProfiler().BeginScope(nameHash, name);
      }
    }

    void EndProfileScope()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->GetProfiler().EndScope();
      }
    }

    void BeginProfileFrame()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->GetProfiler().BeginFrame();
      }
    }

    void EndProfileFrame()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->GetProfiler().EndFrame();
      }
    }

    void ResetProfiler()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->GetProfiler().Reset();
      }
    }

    TKProfiler* GetProfiler()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return &tkStats->GetProfiler();
      }
      return nullptr;
    }

    void SetProfilerEnabled(bool enabled)
    {
      if (TKStats* tkStats = GetTKStats())
      {
        tkStats->GetProfiler().SetEnabled(enabled);
      }
    }

    bool IsProfilerEnabled()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->GetProfiler().IsEnabled();
      }
      return false;
    }

  } // namespace Profiler

} // namespace ToolKit
