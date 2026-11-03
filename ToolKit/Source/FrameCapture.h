/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file FrameCapture.h Engine side entry point for a host provided frame
 * capture tool.
 *
 * The engine has no capture backend of its own. A host that has one - the
 * editor drives RenderDoc - installs a handler with SetFrameCaptureRequestFn(),
 * and game code, editor plugins and console commands ask for a capture with
 * RequestFrameCapture() without knowing which tool, or whether any tool at
 * all, sits behind it. A published game has no handler installed, so every
 * request returns false and nothing else happens.
 *
 * Why the indirection exists: the capture tool lives in the editor, but a game
 * build only links the engine. Routing the request through the engine is what
 * lets the same game code ask for a capture both in the editor (where the
 * editor handler answers) and in a shipped build (where nobody does), without
 * the game linking editor symbols or knowing anything about RenderDoc.
 */

#include "Types.h"

namespace ToolKit
{

  /**
   * Signature of the handler a host installs to record frames.
   * @param label Human readable name for the capture. The host puts it in the
   * file name and in the metadata written next to the capture.
   * @param frameCount Number of consecutive frames the capture should span. A
   * value below one is treated as one.
   * @return True when the request was accepted, false when the host is not
   * ready to capture (no capture tool injected, a capture already running).
   */
  typedef std::function<bool(const String& label, int frameCount)> FrameCaptureRequestFn;

  /**
   * Installs the handler that RequestFrameCapture() forwards to.
   * Meant for hosts, not for game code: the editor installs its RenderDoc
   * handler at startup and removes it again on shutdown. The handler runs on
   * the thread that asks for the capture, so it must only queue the request,
   * never touch a graphics device.
   * @param fn The handler. An empty function removes the current one.
   */
  TK_API void SetFrameCaptureRequestFn(const FrameCaptureRequestFn& fn);

  /**
   * Asks the host to capture the next frame(s).
   * Game code calls this from its update, a plugin from its Frame() callback,
   * the editor console from a command. The capture does not start inside the
   * call: the host opens it at the next frame boundary, so the whole frame,
   * its present included, ends up in the capture.
   * @param label Human readable name for the capture.
   * @param frameCount Number of consecutive frames the capture should span.
   * @return True when a host accepted the request, false when no capture tool
   * is installed or the host refused the request.
   */
  TK_API bool RequestFrameCapture(const String& label, int frameCount = 1);

} // namespace ToolKit
