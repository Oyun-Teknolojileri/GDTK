/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Renderer.h"      // InstanceRecord2a (= PerDrawUboLayout), InstanceRecord2aStride
#include "TextureBuffer.h" // TextureBuffer<> (RGBA32F-backed struct array)

namespace ToolKit
{

  /**
   * Instance data transport — Phase 2a (see `rendering-roadmap.md` §Phase 2a + §5).
   *
   * Renderer-side wrapper around a `TextureBuffer<InstanceRecord2a, FormatRGBA32F>`: the CPU-side
   * array of instance records plus the RGBA32F GPU texture that the GL/WebGL `LoadInstance(id)`
   * path (see `Resources/Engine/Shaders/instanceDataInc.shader`) reads via `texelFetch`. Adds no
   * new `IGraphicsBackend` virtual — the transport reuses the existing `DataTexture` create/map /
   * `UpdateTextureRegion` + `BindTexture` path (the same one GPU skinning uses). SSBO (native
   * GLES 3.2 + Vulkan) swaps in behind the same `LoadInstance` interface in 2b / Phase 8.
   *
   * 2a scope: full-record, full-buffer upload — `Flush()` re-uploads every row each call. The
   * 2a vertex shader consumes only `_model` / `_inverseTransposeModel`; the fragment shader stays
   * on the per-draw UBO, so the instance texture and the UBO hold identical `PerDrawUboLayout`
   * bytes in one frame (pixel-identical guarantee). Incremental / region-scoped uploads and the
   * lean `InstanceRecord` + `RenderObject` split arrive in 2b / Phase 4. `InstanceRecord2a` is the
   * throwaway 1:1 mirror of `PerDrawUboLayout` (Renderer.h).
   */
  class TK_API InstanceDataBuffer
  {
   public:
    /** Allocate the CPU array and the backing RGBA32F texture for up to `maxInstances` records.
     *  The 2a budget is 1024; per-profile sizing arrives in Phase 3. */
    void Init(int maxInstances) { m_texBuffer.Resize(maxInstances); }

    /** Write one record at `index` (CPU-side; upload it to the GPU with `Flush()`). */
    void Write(int index, const InstanceRecord2a& record) { m_texBuffer[index] = record; }

    /** CPU-side record at `index` (write target for `FeedUniforms`, and the `memcmp` source the
     *  2a verification compares against the per-draw UBO). */
    InstanceRecord2a& Record(int index) { return m_texBuffer[index]; }
    const InstanceRecord2a& Record(int index) const { return m_texBuffer[index]; }

    /** Full re-upload of every CPU row to the GPU texture. Cheap for the 2a single-instance case;
     *  incremental uploads arrive with the Phase 4 generation graph. */
    void Flush() { m_texBuffer.Map(); }

    /** The GPU texture the renderer binds to the `s_instanceData` slot (`TK_SAMPLER_BINDING(14)`),
     *  via `Renderer::SetTexture`. */
    DataTexturePtr GetTexture() const { return m_texBuffer.m_buffer; }

   private:
    TextureBuffer<InstanceRecord2a, GraphicTypes::FormatRGBA32F> m_texBuffer;
  };

} // namespace ToolKit
