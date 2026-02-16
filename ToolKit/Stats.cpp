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

  TKProfiler::~TKProfiler()
  {
    for (ProfilerNode* root : m_rootNodes)
    {
      DeleteNodeRecursive(root);
    }
    m_rootNodes.clear();
    m_nodeMap.clear();
  }

  void TKProfiler::BeginScope(StringView name)
  {
    if (!m_enabled)
    {
      return;
    }

    // Build the full path for this scope.
    m_scopeStack.push_back(String(name));

    // Build path string for node lookup.
    String fullPath;
    for (size_t i = 0; i < m_scopeStack.size(); ++i)
    {
      if (i > 0)
      {
        fullPath += "/";
      }
      fullPath += m_scopeStack[i];
    }

    // Find or create the node.
    ProfilerNode* node = nullptr;
    auto it            = m_nodeMap.find(fullPath);
    if (it != m_nodeMap.end())
    {
      node = it->second;
    }
    else
    {
      node        = new ProfilerNode();
      node->name  = String(name);
      node->depth = (uint) m_scopeStack.size() - 1;

      if (m_currentNode == nullptr)
      {
        // This is a root node.
        m_rootNodes.push_back(node);
      }
      else
      {
        // Add as child of current node.
        node->parent = m_currentNode;
        m_currentNode->children.push_back(node);
      }

      m_nodeMap[fullPath] = node;
    }

    // Snapshot children's current inclusive time sum before this scope runs.
    float childrenSum = 0.0f;
    for (ProfilerNode* child : node->children)
    {
      childrenSum += child->inclusiveTime;
    }
    node->childrenTimeAtBegin = childrenSum;

    node->beginTime           = GetElapsedMilliSeconds();
    m_currentNode             = node;
  }

  void TKProfiler::EndScope()
  {
    if (!m_enabled || m_currentNode == nullptr || m_scopeStack.empty())
    {
      return;
    }

    float endTime                 = GetElapsedMilliSeconds();
    float elapsed                 = endTime - m_currentNode->beginTime;

    m_currentNode->inclusiveTime += elapsed;
    m_currentNode->hitCount++;
    m_currentNode->accumulatedIncl += elapsed;

    // Calculate exclusive time: elapsed minus only the children time accumulated during this scope call.
    float childrenTimeNow           = 0.0f;
    for (ProfilerNode* child : m_currentNode->children)
    {
      childrenTimeNow += child->inclusiveTime;
    }
    float childrenDelta             = childrenTimeNow - m_currentNode->childrenTimeAtBegin;

    float exclusive                 = elapsed - childrenDelta;
    m_currentNode->exclusiveTime   += exclusive;
    m_currentNode->accumulatedExcl += exclusive;

    // Pop the scope stack.
    m_scopeStack.pop_back();

    // Move to parent.
    m_currentNode = m_currentNode->parent;
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
    m_nodeMap.clear();
    m_currentNode = nullptr;
    m_scopeStack.clear();
    m_frameTime            = 0.0f;
    m_accumulatedFrameTime = 0.0f;
    m_frameCount           = 0;
  }

  void TKProfiler::SetExpandAll(bool expand)
  {
    for (auto& pair : m_nodeMap)
    {
      pair.second->expanded = expand;
    }
  }

  ProfilerNode* TKProfiler::FindOrCreateChild(ProfilerNode* parent, StringView name)
  {
    if (parent)
    {
      for (ProfilerNode* child : parent->children)
      {
        if (child->name == name)
        {
          return child;
        }
      }
    }
    return nullptr;
  }

  void TKProfiler::ResetNodeRecursive(ProfilerNode* node)
  {
    if (node == nullptr)
    {
      return;
    }

    node->ResetAll();

    for (ProfilerNode* child : node->children)
    {
      ResetNodeRecursive(child);
    }
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

    node->SwapFrameData();

    for (ProfilerNode* child : node->children)
    {
      SwapNodeFrameData(child);
    }
  }

  String TKProfiler::GetProfileTreeString() const
  {
    if (m_rootNodes.empty())
    {
      return "No profiling data available.\n";
    }

    String output;

    // Header.
    output += "====================================================================================================\n";

    // Column headers with fixed widths.
    char header[256];
    snprintf(header,
             sizeof(header),
             "%-40s | %10s | %10s | %8s | %8s | %6s\n",
             "Scope Name",
             "Incl (ms)",
             "Excl (ms)",
             "Incl %",
             "Excl %",
             "Hits");
    output += header;

    output += "----------------------------------------------------------------------------------------------------\n";

    // Build tree for each root.
    for (size_t i = 0; i < m_rootNodes.size(); ++i)
    {
      bool isLast = (i == m_rootNodes.size() - 1);
      BuildTreeString(m_rootNodes[i], output, "", isLast);
    }

    output += "====================================================================================================\n";

    // Summary.
    char summary[256];
    snprintf(summary,
             sizeof(summary),
             "Frame Time: %.3f ms | Avg Frame: %.3f ms | Frames: %u\n",
             m_frameTime,
             GetAverageFrameTime(),
             m_frameCount);
    output += summary;

    return output;
  }

  void TKProfiler::BuildTreeString(const ProfilerNode* node, String& output, const String& prefix, bool isLast) const
  {
    if (node == nullptr)
    {
      return;
    }

    // Build the tree branch visualization.
    String branch = prefix;
    if (node->depth > 0)
    {
      branch += isLast ? "+-- " : "+-- ";
    }

    // Use previous frame values for display.
    float inclTime     = node->inclusiveTimePrev;
    float exclTime     = node->exclusiveTimePrev;
    uint hitCount      = node->hitCountPrev;

    // Calculate percentages relative to frame time.
    float inclPercent  = m_frameTime > 0.0f ? (inclTime / m_frameTime) * 100.0f : 0.0f;
    float exclPercent  = m_frameTime > 0.0f ? (exclTime / m_frameTime) * 100.0f : 0.0f;

    // Truncate name if too long.
    String displayName = branch + node->name;
    if (displayName.length() > 38)
    {
      displayName = displayName.substr(0, 35) + "...";
    }

    char line[256];
    snprintf(line,
             sizeof(line),
             "%-40s | %10.3f | %10.3f | %7.2f%% | %7.2f%% | %6u\n",
             displayName.c_str(),
             inclTime,
             exclTime,
             inclPercent,
             exclPercent,
             hitCount);
    output             += line;

    // Build prefix for children.
    String childPrefix  = prefix;
    if (node->depth > 0)
    {
      childPrefix += isLast ? "    " : "|   ";
    }

    // Recursively add children.
    for (size_t i = 0; i < node->children.size(); ++i)
    {
      bool childIsLast = (i == node->children.size() - 1);
      BuildTreeString(node->children[i], output, childPrefix, childIsLast);
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
             "Total Hardware Render Pass: %llu\n",
             Stats::GetStatPrev(FrameStatType::RenderPass));
    stats += buffer;

    snprintf(buffer, sizeof(buffer), "Approximate Total VRAM Usage: %llu MB\n", Stats::GetVRAMUsage(MemoryUnit::MB));
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
    void SetGpuResourceLabel(StringView label, GpuResourceType resourceType, uint resourceId)
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

    String GetProfileTreeString()
    {
      if (TKStats* tkStats = GetTKStats())
      {
        return tkStats->GetProfiler().GetProfileTreeString();
      }
      return "Profiler not available.\n";
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
