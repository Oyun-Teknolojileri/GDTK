/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file RenderDocCapture.h Passive RenderDoc frame capture for any host that
 * links the engine.
 *
 * This is the capture machinery, not a UI: bind the library RenderDoc injected
 * into the process (Dependency/utilities/renderdoc_app.h,
 * https://renderdoc.org/docs/in_application_api.html), open a capture before a
 * frame and close it after the present, write the .rdc and a JSON sidecar, and
 * accept requests from game code, a console, a menu or a request file.
 *
 * It lives in the engine rather than in a module because both hosts need it and
 * neither can do without the other's half: a published game links only
 * ToolKit, and the editor is just one host. What stays host specific is where
 * captures go - Init() takes a callback for that - and everything the editor
 * contributes on top (console commands, the Modules menu) lives in
 * Modules/RenderDoc.
 *
 * The library is only ever looked up by name (renderdoc.dll / librenderdoc.so /
 * libVkLayer_GLES_RenderDoc.so); the engine never loads it. That is a hard
 * requirement: the capture hooks have to be in place before the graphics device
 * is created, so the host has to be started through RenderDoc:
 *
 *   renderdoccmd capture -w BinDebug/Editor.exe ...
 *   renderdoccmd capture -w BinDebug/MyGame.exe
 *
 * With RenderDoc absent - the normal start, and every shipped game - Init()
 * reports false and every other entry point does nothing. The code is in the
 * binary either way; it is the *library* that is never there.
 *
 * A capture is always queued, never opened inside the call: BeginFrame() opens
 * it, EndFrame() closes it, so a request from inside a frame gets the next
 * whole frame instead of half of one.
 */

#include "Logger.h"
#include "Types.h"

#include <map>

namespace ToolKit
{
  namespace RenderDocCapture
  {

    /**
     * Where captures and the request file live. Called once per capture, so a
     * host whose project can change (the editor) can answer differently later.
     * An empty result falls back to <config dir>/Captures.
     */
    typedef std::function<String()> CaptureRootFn;

    /**
     * Extra key/value pairs for the capture's JSON sidecar, e.g. project and
     * scene. Called once per capture; a plain string map, so the sidecar stays
     * free of host types. Optional.
     */
    typedef std::function<std::map<String, String>()> CaptureContextFn;

    /** Everything the engine knows about its RenderDoc connection. */
    struct TK_API Status
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
     * Binds the RenderDoc API, installs the RequestFrameCapture() handler and
     * remembers where captures go. Called once from the host startup, before any
     * graphics work.
     * @param rootFn Answers the capture directory. May be empty, in which case
     * captures go to the user config directory.
     * @param contextFn Adds fields to the sidecar. May be empty.
     * @return True when RenderDoc is present and captures can be requested.
     */
    TK_API bool Init(const CaptureRootFn& rootFn, const CaptureContextFn& contextFn = nullptr);

    /**
     * Drops the API pointer and the RequestFrameCapture() handler. Called from
     * the host teardown. The injected library stays mapped: it hooked the
     * graphics driver at process start and unloading it would pull the hooks out
     * from under a live driver.
     */
    TK_API void Shutdown();

    /**
     * Consumes a pending request file, then opens a queued capture. Call it at
     * the top of the frame, before anything is drawn: start and end have to
     * bracket the whole frame including its present, otherwise RenderDoc refuses
     * the capture.
     */
    TK_API void BeginFrame();

    /** Closes the capture once its last frame was presented and writes the file. */
    TK_API void EndFrame();

    /** True when RenderDoc is in the process and a capture can be requested. */
    TK_API bool IsAvailable();

    /** Returns a copy of the capture state. Safe to call from any thread. */
    TK_API Status GetStatus();

    /**
     * Queues a frame capture. This is the handler RequestFrameCapture() forwards
     * to, so it may run on a game thread; it only queues.
     * @param label Human readable name, sanitized into the file name.
     * @param frameCount Frames to span, clamped to [1, 60].
     * @return True when the request was queued. False when RenderDoc is not
     * available or a capture is already queued or running.
     */
    TK_API bool RequestCapture(const String& label, int frameCount = 1);

    /** Opens the RenderDoc UI on the last capture, for a human to look at it. */
    TK_API bool OpenLastCapture();

    /** The directory captures and the request file are resolved to right now. */
    TK_API String GetCaptureDir();

  } // namespace RenderDocCapture
} // namespace ToolKit
