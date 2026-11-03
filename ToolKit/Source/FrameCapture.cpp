/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "FrameCapture.h"

#include <mutex>

namespace ToolKit
{

  namespace
  {

    /**
     * The handler belongs to the host and is read from whatever thread asks
     * for a capture, so a plain mutex guards it. The lock is held only while
     * the std::function is copied out; the handler itself runs unlocked, which
     * keeps a slow host from stalling the other threads and keeps the host free
     * to call back into the engine from inside the handler.
     *
     * Process lifetime state, but it holds no engine object: the handler the
     * editor installs captures nothing, and the editor removes it in
     * RenderDocUninit() before the engine is torn down. A std::function that
     * outlived the engine would still be safe to destroy here.
     */
    std::mutex& CaptureFnMutex()
    {
      static std::mutex mutex;
      return mutex;
    }

    FrameCaptureRequestFn& CaptureFnHolder()
    {
      static FrameCaptureRequestFn fn = nullptr;
      return fn;
    }

  } // namespace

  void SetFrameCaptureRequestFn(const FrameCaptureRequestFn& fn)
  {
    std::lock_guard<std::mutex> lock(CaptureFnMutex());
    CaptureFnHolder() = fn;
  }

  bool RequestFrameCapture(const String& label, int frameCount)
  {
    FrameCaptureRequestFn fn;
    {
      std::lock_guard<std::mutex> lock(CaptureFnMutex());
      fn = CaptureFnHolder();
    }

    if (fn == nullptr)
    {
      return false;
    }

    return fn(label, frameCount < 1 ? 1 : frameCount);
  }

} // namespace ToolKit
