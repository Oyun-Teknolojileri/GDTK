/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file RenderDocCapture.h RenderDoc frame capture control for the editor.
 *
 * The editor is the only place that talks to RenderDoc. It finds the library
 * RenderDoc injected into the process, binds its in-application API
 * (Dependency/utilities/renderdoc_app.h,
 * https://renderdoc.org/docs/in_application_api.html) and records frames on
 * request. Nothing of this leaks into the engine: game code and plugins ask
 * through ToolKit::RequestFrameCapture(), the editor answers.
 *
 * The editor is a development tool, so capture control is built into every
 * editor config (Debug, RelWithDebInfo, Release, MinSizeRel). What keeps
 * RenderDoc out of a shipped game is that the editor is not part of one, not a
 * preprocessor switch: no game target links this translation unit, and the
 * library is only ever looked up by name, never loaded.
 *
 * Usage:
 * @code
 *   // Human: console command, or request a capture from any editor code.
 *   ToolKit::Editor::RequestRenderDocCapture("boss_intro");
 *
 *   // Game code (update loop, plugin Frame(), a console command):
 *   ToolKit::RequestFrameCapture("boss_intro");   // no-op in a shipped build
 *
 *   // A tool that cannot call into the process (an AI agent, a test
 *   // harness): write "<capture dir>/request" with "label [frameCount]" and
 *   // the editor picks it up on the next poll (RenderDocPollRequestFile).
 * @endcode
 */

#include "EditorTypes.h"

namespace ToolKit
{
  namespace Editor
  {

    /** Everything the editor knows about its RenderDoc connection. */
    struct TK_EDITOR_API RenderDocStatus
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
     * Looks for RenderDoc in this process and binds its in-application API.
     *
     * The library is looked up by name - renderdoc.dll on Windows,
     * librenderdoc.so on Linux - which is how RenderDoc presents itself once it
     * has been injected. The editor never loads RenderDoc itself: the capture
     * hooks have to be in place before the graphics device is created, so the
     * process has to be started through RenderDoc:
     *
     *   renderdoccmd capture -w BinDebug/Editor.exe --workspace <path>
     *   renderdoccmd inject <pid>            (attach to a running editor)
     *
     * Called once from PreInit(), before any graphics work. Does nothing and
     * returns false when RenderDoc is not there, which is the normal case for
     * a plain editor start.
     * @return True when the API is ready and captures can be requested.
     */
    TK_EDITOR_API bool RenderDocInit();

    /**
     * Drops the API pointers and the capture handler. Called from the host
     * teardown. The injected library is left mapped: it hooked the graphics
     * driver at process start and unloading it would pull the hooks out from
     * under a live driver.
     */
    TK_EDITOR_API void RenderDocUninit();

    /** Returns a copy of the capture state. Safe to call from any thread. */
    TK_EDITOR_API RenderDocStatus GetRenderDocStatus();

    /** True when RenderDoc is injected and a capture can be requested. */
    TK_EDITOR_API bool IsRenderDocAvailable();

    /**
     * Queues a frame capture.
     *
     * The capture is not opened inside this call. The request is stored and
     * RenderDocBeginFrame() opens it before the next frame's first draw call,
     * so a caller in the middle of a frame gets the next whole frame instead of
     * a capture that starts halfway through. This is also the handler the
     * engine installs, so it may run on a game thread: it only queues.
     * @param label Human readable name, sanitized into the file name.
     * @param frameCount Frames to span, clamped to [1, 60].
     * @return True when the request was queued. False when RenderDoc is not
     * available or a capture is already queued or running.
     */
    TK_EDITOR_API bool RequestRenderDocCapture(const String& label, int frameCount = 1);

    /**
     * Opens the capture a pending request asked for. Call it at the top of the
     * frame, before anything is drawn: start and end have to bracket the whole
     * frame including its present, otherwise RenderDoc refuses the capture.
     */
    TK_EDITOR_API void RenderDocBeginFrame();

    /**
     * Closes the capture once its last frame was presented, writes the file and
     * the sidecar metadata. Call it right after the frame's present.
     */
    TK_EDITOR_API void RenderDocEndFrame();

    /**
     * Consumes a capture request dropped into the capture directory.
     *
     * The request file is the way in for anything that cannot call into the
     * editor: an AI agent, a shell script, a CI job. It holds one line,
     * "label [frameCount]", and the editor deletes it after reading so a stale
     * file cannot trigger a second capture. Polled from the frame loop, so the
     * cost is one file check every few frames.
     */
    TK_EDITOR_API void RenderDocPollRequestFile();

    /** Opens the RenderDoc UI on the last capture, for a human to look at it. */
    TK_EDITOR_API bool RenderDocOpenUI();

    /**
     * Directory captures are written to: <workspace>/<project>/Captures when a
     * project is open, the user config directory otherwise. It is also where
     * the request file is read from and where last_capture.json lands, so a
     * tool can find out what was captured without reading the editor console.
     */
    TK_EDITOR_API String GetRenderDocCaptureDir();

  } // namespace Editor
} // namespace ToolKit
