/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Types.h"

namespace ToolKit
{

  /**
   * What a start up cost: the phases it goes through and the shader work behind them.
   *
   * The timeline is one monotonic clock for the whole engine, started by Begin(), so a phase mark
   * and a shader line are comparable and the same numbers come out of the desktop and a device.
   * Every line goes through TK_LOG, which is what makes an Android run readable from logcat.
   */
  namespace BootStats
  {
    /** Starts the timeline. The first call wins, a later one does not move the origin. */
    void Begin();

    /** Milliseconds since Begin(). */
    double ElapsedMs();

    /** Logs a phase with its place on the timeline. */
    void Mark(const String& phase);

    /** Records one shader stage the backend compiled, and logs it with its variant and cost. */
    void StageCompiled(const String& relativeShaderFile, const String& variantKey, float milliseconds);

    /** Records one program the program manager produced, restored from the cache or linked. */
    void ProgramReady(bool fromCache, float milliseconds);

    /** Records one program binary written back to the cache. */
    void ProgramStored(float milliseconds);

    /** Records one shader source the resource manager parsed, merged and pruned. */
    void ShaderSourceParsed(float milliseconds);

    /** Logs the shader totals and the variants behind them. Reported once. */
    void Report();
  }

} // namespace ToolKit
