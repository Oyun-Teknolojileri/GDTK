/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "GpuProgram.h"
#include "IGraphicsBackend.h"
#include "Pass.h"

namespace ToolKit
{

  /**
   * Base class responsible of creating render results using passes.
   */
  class TK_API RenderPath
  {
   public:
    RenderPath();
    virtual ~RenderPath();
    virtual void Render(Renderer* renderer);
    virtual void PreRender(Renderer* renderer);
    virtual void PostRender(Renderer* renderer);

   public:
    PassPtrArray m_passArray;
  };

  typedef std::function<void(Renderer*)> RenderTaskFn;
  typedef std::function<void()> RenderTaskOnComplatedFn;

  enum class RenderTaskPriority
  {
    High,
    Low
  };

  struct RenderTask
  {
    RenderTaskFn Task                = nullptr;
    RenderTaskOnComplatedFn Callback = nullptr;
    RenderTaskPriority Priority      = RenderTaskPriority::High;
  };

  typedef std::vector<RenderTask> RenderTaskArray;

  /**
   * System class that facilitates renderer to the techniques.
   */
  class TK_API RenderSystem
  {
   public:
    RenderSystem();
    ~RenderSystem();

    void Init();
    void AddRenderTask(RenderTask task);
    void ExecuteRenderTasks();
    void FlushRenderTasks();
    void FlushGpuPrograms();

    /**
     * Sets application window size. Doesn't necessarily update any frame buffer
     * or render target. Systems that rely on this data updates them selfs.
     * Programmer is responsible to keep this value up to date when application
     * size has changed.
     */
    void SetAppWindowSize(uint width, uint height);

    UVec2 GetAppWindowSize();

    void SetClearColor(const Vec4& clearColor);

    uint GetFrameCount();

    void ResetFrameCount();

    void DecrementSkipFrame();

    bool IsSkipFrame() const;

    void SkipSceneFrames(int numFrames);

    /**
     * Initializes the active graphics backend.
     * Host application fills BackendInitParams with the data the backend needs.
     * @param params backend initialization parameters.
     */
    void InitGraphics(const IGraphicsBackend::BackendInitParams& params);

    void StartFrame();

    void EndFrame();

    bool IsGammaCorrectionNeeded();

    void SrgbAutoEncoding(bool enable);

    GpuProgramManager* GetGpuProgramManager();
    IGraphicsBackend* GetBackend();

    void SetPresentCallback(std::function<void()> callback) { m_presentCallback = std::move(callback); }

    void Present();

   private:
    IGraphicsBackend* CreateBackend();

    void ExecuteTaskImp(RenderTask& task);

   public:
    bool m_backbufferFormatIsSRGB = false;

   private:
    RenderTaskArray m_highQueue;
    RenderTaskArray m_lowQueue;

    Renderer* m_renderer = nullptr;
    int m_skipFrames     = 0;
    uint m_frameCount    = 0;
    std::function<void()> m_presentCallback;
  };

} // namespace ToolKit