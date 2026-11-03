/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file RenderDocModule.h Frame capture for the editor, driven by RenderDoc's
 * in-application API.
 *
 * The module is the only place that talks to RenderDoc. It finds the library
 * RenderDoc injected into the process, binds the API
 * (ThirdParty/renderdoc_app.h, https://renderdoc.org/docs/in_application_api.html)
 * and records frames on request. The editor knows three things about it: call
 * Init() once at startup, BeginFrame()/EndFrame() around the frame loop, and
 * Shutdown() on exit. Everything else - the console commands, the capture
 * directory, the file format - belongs to the module.
 *
 * Nothing of RenderDoc reaches the engine: game code and editor plugins ask
 * through ToolKit::RequestFrameCapture() (ToolKit/Source/FrameCapture.h), and
 * the engine hook this module installs in Init() is what answers.
 *
 * The library is only ever looked up by name (renderdoc.dll / librenderdoc.so);
 * this module never loads it. That is a hard requirement, not a preference: the
 * capture hooks have to be in place before the graphics device is created, so
 * the editor has to be started through RenderDoc:
 *
 *   renderdoccmd capture -w BinDebug/Editor.exe --workspace <ws>
 *   renderdoccmd inject <pid>            (attach to a running editor)
 *
 * With RenderDoc absent - the normal start - Init() reports false and every
 * other entry point does nothing.
 *
 * Four ways to ask for a capture, all through one queue:
 *   - game code / plugin:  ToolKit::RequestFrameCapture(label, frames)
 *   - editor code:         ToolKit::RenderDoc::RequestCapture(label, frames)
 *   - console / menu:      CaptureFrame <label> [frames], CaptureStatus, CaptureOpen
 *   - anything out of process: write "<capture dir>/request" with "label [frames]"
 *
 * Captures land in <workspace>/<project>/Captures as
 * <label>_<stamp>_capture.rdc next to a .json sidecar, and last_capture.json
 * names the one that finished last. Utils/RenderDoc turns a capture into a
 * readable report.
 */

#include <Logger.h>
#include <Types.h>

#ifdef TK_WIN
  #if defined(TK_RENDERDOC_DLL_EXPORT)
    #define TK_RENDERDOC_API __declspec(dllexport)
  #else
    #define TK_RENDERDOC_API __declspec(dllimport)
  #endif
#else
  #define TK_RENDERDOC_API __attribute__((visibility("default")))
#endif

namespace ToolKit
{
  namespace RenderDoc
  {

    /** Everything the module knows about its RenderDoc connection. */
    struct TK_RENDERDOC_API Status
    {
      /** True when RenderDoc is mapped into the process and handed us its API. */
      bool m_available      = false;
      /** True while a frame capture is open, from the first frame to the last. */
      bool m_capturing      = false;
      /** True when a request is queued and its capture has not started yet. */
      bool m_requestPending = false;
      /** Frames the queued or running capture spans. */
      int m_pendingFrames   = 0;
      /** Captures this session finished and wrote to disk. */
      int m_captureCount    = 0;
      /** Version of the API struct RenderDoc returned, not the version asked for. */
      int m_apiMajor        = 0;
      int m_apiMinor        = 0;
      int m_apiPatch        = 0;
      /** Full path of the injected RenderDoc library, empty when there is none. */
      String m_modulePath;
      /** Directory captures and the request file live in. */
      String m_captureDir;
      /** Capture file written last, empty until the first capture finishes. */
      String m_lastCapturePath;
      /** Why the last operation failed, empty on success. */
      String m_lastError;
    };

    /**
     * Binds the RenderDoc API, installs the engine capture hook and registers
     * the console commands. Called once from the host startup, before any
     * graphics work.
     * @return True when RenderDoc is present and captures can be requested.
     */
    TK_RENDERDOC_API bool Init();

    /**
     * Drops the API pointer, the engine hook and the console commands. Called
     * from the host teardown. The injected library stays mapped: it hooked the
     * graphics driver at process start and unloading it would pull the hooks out
     * from under a live driver.
     */
    TK_RENDERDOC_API void Shutdown();

    /**
     * Consumes a pending request file, then opens a queued capture. Call it at
     * the top of the frame, before anything is drawn: start and end have to
     * bracket the whole frame including its present, otherwise RenderDoc refuses
     * the capture.
     */
    TK_RENDERDOC_API void BeginFrame();

    /** Closes the capture once its last frame was presented and writes the file. */
    TK_RENDERDOC_API void EndFrame();

    /** True when RenderDoc is in the process and a capture can be requested. */
    TK_RENDERDOC_API bool IsAvailable();

    /** Returns a copy of the module state. Safe to call from any thread. */
    TK_RENDERDOC_API Status GetStatus();

    /**
     * Queues a frame capture. The capture does not start inside the call: the
     * request is stored and BeginFrame() opens it, so a caller in the middle of
     * a frame gets the next whole frame instead of half of one. This is also the
     * handler the engine hook forwards to, so it may run on a game thread.
     * @param label Human readable name, sanitized into the file name.
     * @param frameCount Frames to span, clamped to [1, 60].
     * @return True when the request was queued. False when RenderDoc is not
     * available or a capture is already queued or running.
     */
    TK_RENDERDOC_API bool RequestCapture(const String& label, int frameCount = 1);

    /** Opens the RenderDoc UI on the last capture, for a human to look at it. */
    TK_RENDERDOC_API bool OpenLastCapture();

    /**
     * Directory captures are written to: <workspace>/<project>/Captures when a
     * project is open, the user config directory otherwise. It is also where the
     * request file is read from and where last_capture.json lands.
     */
    TK_RENDERDOC_API String GetCaptureDir();

  } // namespace RenderDoc
} // namespace ToolKit
