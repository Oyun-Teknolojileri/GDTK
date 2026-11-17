/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Threads.h"

#include "Platform.h"
#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{

  namespace
  {
    /** Upper bound for the bulk pool; see the comment in WorkerManager::Init. */
    constexpr unsigned int MaxBulkThreads = 16;
  } // namespace

  WorkerManager::WorkerManager() {}

  WorkerManager::~WorkerManager() { UnInit(); }

  void WorkerManager::Init()
  {
    uint coreCount = std::thread::hardware_concurrency();
    if constexpr (TK_PLATFORM == PLATFORM::TKWeb)
    {
      m_frameWorkers      = new ThreadPool(glm::min(coreCount, 2u));
      m_backgroundWorkers = new ThreadPool(glm::min(coreCount, 2u));
      // No bulk pool on the web: the pthread pool is fixed size and an extra thread it was not
      // sized for can deadlock the runtime. GetPool(BulkPool) falls back to the background pool.
    }
    else
    {
      m_frameWorkers      = new ThreadPool(glm::min(coreCount, 4u));
      m_backgroundWorkers = new ThreadPool(glm::min(coreCount, 2u));

      // Batch work gets the machine minus two cores for the caller and the OS, capped at
      // MaxBulkThreads. The cap is a memory decision, not a speed one: encoding a 2K texture peaks
      // around 250 MB per worker, and on a 32 core box 16 threads measured the same wall clock as
      // 30 (the encode is memory bandwidth bound there) at less than half the peak RSS. Tools can
      // still ask for another width with set_num_threads.
      const unsigned int usable = coreCount > 2u ? coreCount - 2u : 1u;
      m_bulkWorkers             = new ThreadPool(std::min(MaxBulkThreads, std::max(1u, usable)));
    }

    Main::GetInstance()->RegisterPostUpdateFunction([this](float deltaTime) -> void
                                                    { ExecuteTasks(m_mainThreadTasks, m_mainTaskMutex); });
  }

  void WorkerManager::UnInit()
  {
    SafeDel(m_frameWorkers);
    SafeDel(m_backgroundWorkers);
    SafeDel(m_bulkWorkers);
  }

  ThreadPool& WorkerManager::GetPool(Executor executor)
  {
    switch (executor)
    {
      case WorkerManager::Executor::BackgroundPool:
        return *m_backgroundWorkers;
      case WorkerManager::Executor::BulkPool:
        // The web build has no bulk pool; batch work runs on the background pool there, which is
        // the same width the engine already uses for off-frame work.
        return m_bulkWorkers ? *m_bulkWorkers : *m_backgroundWorkers;
      case WorkerManager::Executor::FramePool:
      default:
        return *m_frameWorkers;
        break;
    }
  }

  int WorkerManager::GetThreadCount(Executor executor)
  {
    return Main::GetInstance()->m_threaded ? GetPool(executor).get_num_threads() : 0;
  }

  void WorkerManager::Flush()
  {
    auto flushPoolFn = [](ThreadPool* pool) -> void
    {
      pool->pause();
      pool->wait_for_tasks();
      pool->unpause();
    };

    flushPoolFn(m_frameWorkers);
    flushPoolFn(m_backgroundWorkers);

    ExecuteTasks(m_mainThreadTasks, m_mainTaskMutex);
  }

  void WorkerManager::ExecuteTasks(TaskQueue& queue, std::mutex& mex)
  {
    for (int i = 0; i < (int) queue.size(); i++)
    {
      mex.lock();
      std::packaged_task<void()> task {std::move(queue.front())};
      m_mainThreadTasks.pop();
      mex.unlock();

      task();
    }
  }

} // namespace ToolKit
