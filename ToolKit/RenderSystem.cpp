/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "RenderSystem.h"

#include "GLBackend.h"
#include "Image.h"
#include "Logger.h"
#include "RHI.h"
#include "Stats.h"
#include "ToolKit.h"
#ifdef TK_VULKAN
  #include "Vulkan/VulkanBackend.h"
#endif

#include "DebugNew.h"

namespace ToolKit
{
  RenderPath::RenderPath() {}

  RenderPath::~RenderPath() { m_passArray.clear(); }

  void RenderPath::Render(Renderer* renderer)
  {
    for (PassPtr& pass : m_passArray)
    {
      pass->SetRenderer(renderer);
      pass->PreRender();
      pass->Render();
      pass->PostRender();
    }
  }

  void RenderPath::PreRender(Renderer* renderer) { renderer->StartTimerQuery(); }

  void RenderPath::PostRender(Renderer* renderer) { renderer->EndTimerQuery(); }

  RenderSystem::RenderSystem()
  {
    m_renderer = new Renderer();
    m_renderer->SetBackend(CreateBackend());
  }

  RenderSystem::~RenderSystem() { SafeDel(m_renderer); }

  void RenderSystem::Init()
  {
    m_renderer->Init();
    AddRenderTask({[](Renderer* renderer) -> void { /*TODO renderer->GenerateBRDFLutTexture();*/ }});
  }

  IGraphicsBackend* RenderSystem::CreateBackend()
  {
#ifdef TK_VULKAN
    return new VulkanBackend();
#else
    return new GLBackend();
#endif
  }

  void RenderSystem::AddRenderTask(RenderTask task)
  {
    switch (task.Priority)
    {
      case RenderTaskPriority::High:
        m_highQueue.push_back(task);
        break;
      case RenderTaskPriority::Low:
        m_lowQueue.push_back(task);
        break;
    }
  }

  void RenderSystem::ExecuteRenderTasks()
  {
    // Immediate execution.
    RenderTaskArray tasks = std::move(m_highQueue);
    for (RenderTask& rt : tasks)
    {
      ExecuteTaskImp(rt);
    }

    // Time limited execution.
    if (!m_lowQueue.empty())
    {
      const float timeLimit = 10.0f; // Adjust this to give more chance.
      float time0           = GetElapsedMilliSeconds();
      float time1           = time0;

      tasks                 = std::move(m_lowQueue);
      while (time1 - time0 < timeLimit && !tasks.empty())
      {
        RenderTask rt = tasks.front();
        pop_front<RenderTask>(tasks);

        ExecuteTaskImp(rt);

        time1 = GetElapsedMilliSeconds();
      }

      // Merge remaining.
      m_lowQueue.insert(m_lowQueue.begin(), tasks.begin(), tasks.end());
    }
  }

  void RenderSystem::FlushRenderTasks()
  {
    auto flushTasksFn = [this](RenderTaskArray& rts) -> void
    {
      while (!rts.empty())
      {
        // Complete all existing and potential fallow-up render tasks.
        // For this reason while loop must stay.
        RenderTaskArray tasks = std::move(rts);
        for (RenderTask& rt : tasks)
        {
          ExecuteTaskImp(rt);
        }
      }
    };

    flushTasksFn(m_highQueue);
    flushTasksFn(m_lowQueue);
  }

  void RenderSystem::FlushGpuPrograms() { m_renderer->GetGpuProgramManager()->FlushPrograms(); }

  GpuProgramManager* RenderSystem::GetGpuProgramManager() { return m_renderer->GetGpuProgramManager(); }

  IGraphicsBackend* RenderSystem::GetBackend() { return m_renderer->GetBackend(); }

  void RenderSystem::SetAppWindowSize(uint width, uint height) { m_renderer->m_windowSize = UVec2(width, height); }

  UVec2 RenderSystem::GetAppWindowSize() { return m_renderer->m_windowSize; }

  void RenderSystem::SetClearColor(const Vec4& clearColor) { m_renderer->m_clearColor = clearColor; }

  void RenderSystem::DecrementSkipFrame()
  {
    if (m_skipFrames == 0)
    {
      return;
    }
    m_skipFrames--;
  }

  bool RenderSystem::IsSkipFrame() const { return m_skipFrames != 0; }

  uint RenderSystem::GetFrameCount() { return m_frameCount; }

  void RenderSystem::ResetFrameCount() { m_frameCount = 0; }

  void RenderSystem::SkipSceneFrames(int numFrames) { m_skipFrames = numFrames; }

  void RenderSystem::InitGraphics(const IGraphicsBackend::BackendInitParams& params)
  {
    // Texture origin is bottom-left for OpenGL-style APIs.
    ImageSetVerticalOnLoad(true);

    // Delegate to the active backend.
    m_renderer->GetBackend()->InitBackend(params);
  }

  void RenderSystem::Present()
  {
    if (m_presentCallback)
    {
      m_presentCallback();
    }
    else
    {
      m_renderer->GetBackend()->Present();
    }
  }

  void RenderSystem::ExecuteTaskImp(RenderTask& task)
  {
    if (task.Task != nullptr)
    {
      task.Task(m_renderer);

      if (task.Callback != nullptr)
      {
        task.Callback();
      }
    }
  }

  void RenderSystem::StartFrame() { m_renderer->BeginRenderFrame(); }

  void RenderSystem::EndFrame()
  {
    m_renderer->EndRenderFrame();

    m_frameCount++;
    m_renderer->m_frameCount  = m_frameCount;

    static uint avgFrameStart = m_frameCount;
    static float avgCpuTime   = 0.0f;
    static float avgGpuTime   = 0.0f;

    float cpuTime, gpuTime;
    m_renderer->GetElapsedTime(cpuTime, gpuTime);
    Stats::SetRenderTime(cpuTime, gpuTime);

    avgCpuTime += cpuTime;
    avgGpuTime += gpuTime;

    // Average over 100 frames.
    if (m_frameCount - avgFrameStart >= 100)
    {
      Stats::SetRenderTimeAvg(avgCpuTime / 100.0f, avgGpuTime / 100.0f);

      avgFrameStart = m_frameCount;
      avgCpuTime    = 0.0f;
      avgGpuTime    = 0.0f;
    }
  }

  bool RenderSystem::IsGammaCorrectionNeeded() { return !m_backbufferFormatIsSRGB; }

  void RenderSystem::SrgbAutoEncoding(bool enable)
  {
    if (m_renderer)
    {
      m_renderer->SrgbAutoEncoding(enable);
    }
  }

} // namespace ToolKit
