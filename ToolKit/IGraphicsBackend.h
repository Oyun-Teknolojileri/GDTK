/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "RenderState.h"
#include "Types.h"

namespace ToolKit
{

  struct PassDesc
  {
    FramebufferPtr target;
    GraphicBitFields clearBits = GraphicBitFields::None;
    Vec4 clearColor            = Vec4(0.0f);
    bool loadColor             = false;
    bool loadDepth             = false;
  };

  struct DrawDesc
  {
    const Mesh* mesh    = nullptr;
    bool indexed        = true;
    uint elementCount   = 0;
    uint instanceCount  = 1;
    DrawType type       = DrawType::Triangle;
  };

  class TK_API IGraphicsBackend
  {
   public:
    virtual ~IGraphicsBackend() = default;

    // Frame lifecycle
    virtual void BeginFrame()  = 0;
    virtual void EndFrame()    = 0;
    virtual void Present()     = 0;

    // Pass boundary
    virtual void BeginPass(const PassDesc& desc)                    = 0;
    virtual void EndPass()                                          = 0;

    // Viewport / scissor
    virtual void SetViewport(uint x, uint y, uint w, uint h)       = 0;
    virtual void SetScissor(uint x, uint y, uint w, uint h)        = 0;

    // Clear
    virtual void ClearBuffer(GraphicBitFields fields, const Vec4& color) = 0;
    virtual void ClearColorBuffer(const Vec4& color)                     = 0;

    // Pipeline / program binding
    virtual void BindPipeline(const GpuProgramPtr& program,
                              const RenderState* state)             = 0;

    // Per-draw resource binding
    virtual void SubmitPerDrawData(const void* data, size_t size)   = 0;
    virtual void BindTexture(ubyte slot, TexturePtr tex)            = 0;

    // Geometry draw
    virtual void Draw(const DrawDesc& desc)                         = 0;

    // Utility / blit
    virtual void ResolveFramebuffer(FramebufferPtr src,
                                    FramebufferPtr dst,
                                    const IntArray& attachments)    = 0;
    virtual void CopyFramebuffer(FramebufferPtr src,
                                 FramebufferPtr dst,
                                 GraphicBitFields fields)           = 0;
    virtual void BlitToScreen(FramebufferPtr src)                   = 0;

    // Timer queries
    virtual void StartTimerQuery()                                  = 0;
    virtual void EndTimerQuery()                                    = 0;
    virtual void GetElapsedTime(float& cpu, float& gpu)             = 0;
  };

} // namespace ToolKit
