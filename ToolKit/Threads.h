/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Types.h"

#include <poolSTL/include/poolstl/poolstl.hpp>

#if defined(__EMSCRIPTEN__)
  // WebAssembly: no-op
  #define HyperThreadPause() ((void) 0)

#elif defined(__aarch64__) || defined(__arm__)
  // ARM: use yield hint
  #define HyperThreadPause() asm volatile("yield")

#elif defined(_MSC_VER)
  // MSVC x86/x64: use _mm_pause
  #include <immintrin.h>
  #define HyperThreadPause() _mm_pause()

#elif defined(__i386__) || defined(__x86_64__)
  // GCC/Clang x86/x64: builtin pause
  #define HyperThreadPause() __builtin_ia32_pause()

#else
  // Fallback: no-op
  #define HyperThreadPause() ((void) 0)
#endif

namespace ToolKit
{

  /** Spinlock suitable for low contention quick locking. If threads will wait more than nano seconds, use a mutex. */
  struct Spinlock
  {
    std::atomic<bool> lock {false};

    void Lock() noexcept
    {
      for (;;)
      {
        // Try to acquire lock optimistically
        if (!lock.exchange(true, std::memory_order_acquire))
          return;

        // Spin until it's released
        while (lock.load(std::memory_order_relaxed))
        {
          HyperThreadPause(); // CPU hint
        }
      }
    }

    bool TryLock() noexcept
    {
      // Quick relaxed check to avoid cache miss
      return !lock.load(std::memory_order_relaxed) && !lock.exchange(true, std::memory_order_acquire);
    }

    void Unlock() noexcept { lock.store(false, std::memory_order_release); }
  };

  /** Spin waits until the cond become false. Condition must be a lambda that returns boolean. */
  template <typename Condition>
  void SpinWaitBarrier(Condition cond)
  {
    while (cond())
    {
      HyperThreadPause();
    }
  }

  typedef task_thread_pool::task_thread_pool ThreadPool;
  typedef std::queue<std::packaged_task<void()>> TaskQueue;
  typedef std::function<void()> Task;

  /** This is the class that keeps the thread pools and manages async tasks. */
  class TK_API WorkerManager
  {
   public:
    /** Predefined thread pools for specific jobs. */
    enum Executor
    {
      MainThread,     //!< Tasks in this executor runs in sync with main thread at the end of the current frame.
      FramePool,      //!< Tasks that need to be completed within the frame should use this pool.
      BackgroundPool, //!< Tasks that needs to be completed in the background should be performed using this pool.
    };

   public:
    /** Default constructor, initializes thread pools. */
    WorkerManager();

    /** Default destructor, destroy thread pools. */
    ~WorkerManager();

    /** Initialize threads,  pools and task queues. */
    void Init();

    /** Flushes all the tasks in the pools, queues than terminates threads. */
    void UnInit();

    /** Returns the thread pool corresponding to the executor. */
    ThreadPool& GetPool(Executor executor);

    /** Returns available threads for given executor. */
    int GetThreadCount(Executor executor);

    /** Stops waiting tasks and completes ongoing tasks on all pools and threads. */
    void Flush();

    template <typename F, typename... A, typename R = std::invoke_result_t<std::decay_t<F>, std::decay_t<A>...>>
    std::future<R> AsyncTask(Executor exec, F&& func, A&&... args)
    {
      if (exec == FramePool)
      {
        return m_frameWorkers->submit(func, std::forward<A>(args)...);
      }
      else if (exec == BackgroundPool)
      {
        return m_backgroundWorkers->submit(func, std::forward<A>(args)...);
      }
      else if (exec == MainThread)
      {
        std::shared_ptr<std::packaged_task<R()>> ptask =
            std::make_shared<std::packaged_task<R()>>(std::bind(std::forward<F>(func), std::forward<A>(args)...));

        const std::lock_guard<std::mutex> tasks_lock(m_mainTaskMutex);
        m_mainThreadTasks.emplace(std::bind(std::forward<F>(func), std::forward<A>(args)...));

        return ptask->get_future();
      }

      return std::future<void>();
    };

   private:
    void ExecuteTasks(TaskQueue& queue, std::mutex& mex);

   public:
    /** Task that suppose to complete in a frame should be using this pool. */
    ThreadPool* m_frameWorkers      = nullptr;

    /** Tasks that needs to be run in the background should be performed using this pool. */
    ThreadPool* m_backgroundWorkers = nullptr;

    /** Tasks that will be executed at the main thread frame end is stored here. */
    TaskQueue m_mainThreadTasks;

   private:
    /** Lock for main thread tasks. */
    std::mutex m_mainTaskMutex;
  };

/**
 * Parallel loop execution target which lets the programmer to choose the thread pool to execute for loop on.
 * Allows to decide to run for loop sequential or parallel based on the given condition.
 */
#define TKExecByConditional(Condition, Target)                                                                         \
  poolstl::par_if((Condition) && Main::GetInstance()->m_threaded, GetWorkerManager()->GetPool(Target))

/** Parallel loop execution target which lets the programmer to choose the thread pool to execute for loop on. */
#define TKExecBy(Target) poolstl::par_if(Main::GetInstance()->m_threaded, GetWorkerManager()->GetPool(Target))

/** Insert an async task to given target. */
#define TKAsyncTask(Target, ...)                                                                                       \
  GetWorkerManager()->AsyncTask(Main::GetInstance()->m_threaded ? Target : WorkerManager::MainThread, __VA_ARGS__);

} // namespace ToolKit