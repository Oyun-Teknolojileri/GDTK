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
#include <unordered_map>
#include <thread>

#include "DebugNew.h"

namespace ToolKit
{

// Portable cycle counter: rdtsc on x86/64, CNTVCT on ARM64, chrono fallback.
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
    uint64 exclusive      = elapsed - childrenDelta;

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
    m_frameCount           = 0;
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

  void TKProfiler::SwapNodeFrameData(ProfilerNode* node)
  {
    if (node == nullptr)
    {
      return;
    }

    EnsureCalibrated();

    // Convert cycles to milliseconds for display.
    float inclMs = static_cast<float>(node->inclusiveCycles * m_cyclesToMs);
    float exclMs = static_cast<float>(node->exclusiveCycles * m_cyclesToMs);

    // Accumulate over frames (ms).
    node->accumulatedIncl += inclMs;
    node->accumulatedExcl += exclMs;

    // Swap current to prev for display.
    node->inclusiveTimePrev = inclMs;
    node->exclusiveTimePrev = exclMs;
    node->hitCountPrev      = node->hitCount;

    // Reset current-frame cycle counters.
    node->inclusiveCycles     = 0;
    node->exclusiveCycles     = 0;
    node->childrenCyclesAtBegin = 0;
    node->childrenInclusiveSum  = 0;
    node->hitCount            = 0;

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

    // Batched draws and the objects they covered: without the second number the first one only
    // says how many calls were instanced, not whether batching is worth anything.
    snprintf(buffer,
             sizeof(buffer),
             "Instanced Batches: %llu (%llu objects)\n",
             Stats::GetStatPrev(FrameStatType::InstancedBatch),
             Stats::GetStatPrev(FrameStatType::InstancedObject));
    stats += buffer;

    // Local lights and meshes the depth grid proved hidden and dropped, with the shadow maps the
    // lights would have cost and the draws the meshes would have issued.
    snprintf(buffer,
             sizeof(buffer),
             "Depth Grid Culled Lights: %llu (Meshes: %llu)\n",
             Stats::GetStatPrev(FrameStatType::DepthGridCulledLight),
             Stats::GetStatPrev(FrameStatType::DepthGridCulledEntity));
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

  namespace
  {
    /**
     * Samples a reported value averages over.
     *
     * A single timestamp pair is noisy - the gpu answers a frame or more late, and the frame it lands
     * on can be longer or shorter than its neighbours - so a pass read once per frame jitters by a few
     * tenths of a millisecond. Thirty frames is half a second at sixty: long enough to read a number
     * off it, short enough to still follow a real change.
     */
    constexpr uint PassGpuWindow = 30;

    /**
     * One scope's gpu time and when it was last reported.
     *
     * The age is what lets a scope leave the table: a pass that ran once at load - the env map
     * generation, a cube map bake - would otherwise keep its startup cost in a steady state table
     * and skew every percentage in it, and nothing would ever remove the name.
     */
    struct PassGpuEntry
    {
      /** The last PassGpuWindow samples, oldest overwritten first. */
      float samples[PassGpuWindow] = {};
      uint count                   = 0;
      uint next                    = 0;

      /** Average of the samples above, which is what a reader gets. */
      float ms                     = 0.0f;
      double reportedAt            = 0.0;
    };

    std::unordered_map<String, PassGpuEntry>& PassGpuTimes()
    {
      static std::unordered_map<String, PassGpuEntry> times;
      return times;
    }

    /** A scope that has not answered for this long is not part of the frame any more. */
    constexpr double PassGpuMaxAgeMs = 2000.0;
  } // namespace

  bool EnablePassGpuTiming(bool enable)
  {
    RenderSystem* renderSystem = GetRenderSystem_noexcep();
    if (IGraphicsBackend* backend = renderSystem != nullptr ? renderSystem->GetBackend() : nullptr)
    {
      return backend->SetGpuScopeTiming(enable);
    }

    return false;
  }

  std::unordered_map<String, float> GetPassGpuTimes()
  {
    const double now = GetElapsedMilliSeconds();
    std::unordered_map<String, float> fresh;

    auto& times = PassGpuTimes();
    for (auto it = times.begin(); it != times.end();)
    {
      if (now - it->second.reportedAt > PassGpuMaxAgeMs)
      {
        it = times.erase(it);
        continue;
      }

      fresh[it->first] = it->second.ms;
      ++it;
    }

    return fresh;
  }

  void SetPassGpuMs(StringView name, float ms)
  {
    PassGpuEntry& entry = PassGpuTimes()[String(name)];

    entry.samples[entry.next] = ms;
    entry.next                = (entry.next + 1) % PassGpuWindow;
    entry.count               = glm::min(entry.count + 1, PassGpuWindow);

    float sum                 = 0.0f;
    for (uint i = 0; i < entry.count; ++i)
    {
      sum += entry.samples[i];
    }

    entry.ms         = sum / (float) entry.count;
    entry.reportedAt = GetElapsedMilliSeconds();
  }

  float GetPassGpuMs(StringView name)
  {
    const auto& times = PassGpuTimes();
    auto it           = times.find(String(name));
    if (it == times.end())
    {
      return 0.0f;
    }

    // Same window as the table: a scope that stopped running reads as zero, not as its last value.
    if (GetElapsedMilliSeconds() - it->second.reportedAt > PassGpuMaxAgeMs)
    {
      return 0.0f;
    }

    return it->second.ms;
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
