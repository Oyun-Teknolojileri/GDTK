/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Pass.h"
#include "Renderer.h"
#include "StencilPass.h"

namespace ToolKit
{

  /** Single vec4 outline color, consumed by `dilateFrag.shader`. */
  struct DilatePassDataLayout
  {
    Vec4 color;
  };

  typedef GpuBufferBase<DilatePassDataLayout> DilatePassDataBuffer;

  struct OutlinePassParams
  {
    RenderJobArray* RenderJobs = nullptr;
    FramebufferPtr FrameBuffer = nullptr;
    CameraPtr Camera           = nullptr;
    Vec4 OutlineColor          = Vec4(1.0f);
  };

  /**
   * Draws given entities' outlines to the FrameBuffer.
   * TODO: It should be RenderPath instead of Pass
   */
  class TK_API OutlinePass : public Pass
  {
   public:
    OutlinePass();

    void Render() override;
    void PreRender() override;
    void PostRender() override;

   public:
    OutlinePassParams m_params;

   private:
    StencilRenderPassPtr m_stencilPass = nullptr;
    FullQuadPassPtr m_outlinePass      = nullptr;
    ShaderPtr m_dilateShader           = nullptr;
    RenderTargetPtr m_stencilAsRt      = nullptr;

    /** Pass-specific UBO holding the outline color (slot 7). Lazily initialized on first
        render so the GL backend has a live context when CreateUniformBuffer runs. */
    DilatePassDataBuffer m_dilateBuffer;
    bool m_dilateBufferInitialized = false;
  };

  typedef std::shared_ptr<OutlinePass> OutlinePassPtr;

} // namespace ToolKit