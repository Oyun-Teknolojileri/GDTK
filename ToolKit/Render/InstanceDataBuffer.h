/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Renderer.h"      // InstanceRecord, InstanceRecordStride, InstanceFlags
#include "TextureBuffer.h" // TextureBuffer<> (RGBA32F-backed struct array)

namespace ToolKit
{

  /**
   * Instance data transport — Phase 2b (see `rendering-roadmap.md` §Phase 2b).
   *
   * Renderer-side wrapper around a `TextureBuffer<InstanceRecord, FormatRGBA32F>`: the CPU-side
   * array of lean instance records (5 RGBA32F texels = 80 B each) plus the GPU texture that the
   * GL/WebGL `LoadInstance(id)` path (see `Resources/Engine/Shaders/instanceDataInc.shader`) reads
   * via `texelFetch`. Adds no new `IGraphicsBackend` virtual — the transport reuses the existing
   * `DataTexture` create/map / `UpdateTextureRegion` + `BindTexture` path.
   *
   * 2a scope was full-record, full-buffer upload. 2b switches to the lean `InstanceRecord`
   * (~80 B, 5 texel) + `RenderObject` (on perDraw UBO through step 6, then mini-UBO in step 7).
   * Region-scoped uploads (step 2) reduce per-flush bytes; global tables (steps 3-6) move shared
   * data out of the per-instance record.
   */
  class TK_API InstanceDataBuffer
  {
   public:
    /** Allocate the CPU array and the backing RGBA32F texture for up to `maxInstances` records.
     *  The 2b budget is 1024; per-profile sizing arrives in Phase 3. */
    void Init(int maxInstances) { m_texBuffer.Resize(maxInstances); }

    /** Write one record at `index` (CPU-side; upload it to the GPU with `Flush()`). */
    void Write(int index, const InstanceRecord& record) { m_texBuffer[index] = record; }

    /** CPU-side record at `index` (write target for `FeedUniforms`, and the `memcmp` source the
     *  2b verification compares against the per-draw UBO model field). */
    InstanceRecord& Record(int index) { return m_texBuffer[index]; }
    const InstanceRecord& Record(int index) const { return m_texBuffer[index]; }

    /** Full re-upload of every CPU row to the GPU texture. */
    void Flush() { m_texBuffer.Map(); }

    /** Upload only the first `usedRows` rows to the GPU (region-scoped, Phase 2b step 2).
     *  Reduces per-draw upload from ~1.1 MB (2a full buffer) to Stride*16*usedRows bytes. */
    void Flush(int usedRows) { m_texBuffer.Map(0, usedRows); }

    /** The GPU texture the renderer binds to the `s_instanceData` slot (`TK_SAMPLER_BINDING(14)`),
     *  via `Renderer::SetTexture`. */
    DataTexturePtr GetTexture() const { return m_texBuffer.m_buffer; }

   private:
    TextureBuffer<InstanceRecord, GraphicTypes::FormatRGBA32F> m_texBuffer;
  };

} // namespace ToolKit
