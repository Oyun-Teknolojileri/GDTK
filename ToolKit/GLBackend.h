/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "IGraphicsBackend.h"

namespace ToolKit
{

  class TK_API GLBackend : public IGraphicsBackend
  {
   public:
    GLBackend();
    ~GLBackend() override;

    void BeginFrame() override;
    void EndFrame() override;
    void Present() override;

    void BeginPass(const PassDesc& desc) override;
    void EndPass() override;

    void SetViewport(uint x, uint y, uint w, uint h) override;
    void SetScissor(uint x, uint y, uint w, uint h) override;

    void ClearBuffer(GraphicBitFields fields, const Vec4& color) override;
    void ClearColorBuffer(const Vec4& color) override;

    void BindPipeline(const GpuProgramPtr& program,
                      const RenderState* state) override;

    void SubmitPerDrawData(const void* data, size_t size) override;
    void BindTexture(ubyte slot, TexturePtr tex) override;

    void Draw(const DrawDesc& desc) override;

    void ResolveFramebuffer(FramebufferPtr src,
                            FramebufferPtr dst,
                            const IntArray& attachments) override;
    void CopyFramebuffer(FramebufferPtr src,
                         FramebufferPtr dst,
                         GraphicBitFields fields) override;
    void BlitToScreen(FramebufferPtr src) override;

    void StartTimerQuery() override;
    void EndTimerQuery() override;
    void GetElapsedTime(float& cpu, float& gpu) override;

   private:
    RenderState m_lastAppliedState;
    bool m_firstBind = true;
  };

} // namespace ToolKit
