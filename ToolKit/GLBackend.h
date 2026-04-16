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
    void StoreFboBindings() override;
    void RestoreFboBindings() override;

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
    void InvalidateFramebuffer(FramebufferPtr fb, GraphicBitFields bits) override;

    void StartTimerQuery() override;
    void EndTimerQuery() override;
    void GetElapsedTime(float& cpu, float& gpu) override;

    // GpuProgram resource management
    void CreateGpuProgram(GpuProgram* program, struct GlobalGpuBuffers* buffers) override;
    void DestroyGpuProgram(GpuProgram* program) override;
    int  GetUniformLocation(uint programHandle, const char* name) override;

    // Texture resource management
    void CreateTexture(Texture* tex) override;
    void DestroyTexture(Texture* tex) override;
    void ApplyTextureSettings(Texture* tex) override;
    void GenerateMipmaps(Texture* tex) override;
    void UpdateTextureRegion(Texture* tex, const void* data) override;
    void SetTextureMaxMipLevel(Texture* tex, int maxLevel) override;
    void AllocateCubemapMipStorage(Texture* tex) override;
    void CopyCubemapFaceFromFramebuffer(Texture* cubemap, int face, int mip, int width, int height) override;

    // Framebuffer resource management
    void CreateFramebuffer(Framebuffer* fb) override;
    void DestroyFramebuffer(Framebuffer* fb) override;
    void AttachColorTarget(Framebuffer* fb, RenderTargetPtr rt, int attachment, int mip, int layer, int face) override;
    void DetachColorTarget(Framebuffer* fb, int attachment) override;
    void AttachDepthTarget(Framebuffer* fb, DepthTexturePtr dt) override;
    void DetachDepthTarget(Framebuffer* fb) override;
    void SetDrawBuffers(Framebuffer* fb) override;
    void CheckFramebufferComplete(Framebuffer* fb) override;

    // Cache management (Internal use by RHI)
    void BindFramebuffer(uint target, uint id);
    void BindTextureDirect(uint target, uint id, uint slot);
    void BindVAO(uint vao);

    void InvalidateFboCache(uint id);
    void InvalidateTextureCache(uint id);

   private:
    RenderState m_lastAppliedState;
    bool m_firstBind = true;

    // Framebuffer cache
    uint m_currentReadFboId = (uint) -1;
    uint m_currentDrawFboId = (uint) -1;
    IntArray m_storedReadFboStack;
    IntArray m_storedDrawFboStack;

    // Texture slot cache
    struct TextureSlotState
    {
      uint textureID = 0;
      uint target    = 0;
    };
    TextureSlotState m_textureSlotCache[32];

    // VAO cache
    uint m_currentVAO = (uint) -1;

    // Timer query
    uint m_gpuTimerQuery     = 0;
    float m_cpuTime          = 1.0f;
    float m_gpuTime          = 1.0f;
    bool m_timerQueryActive  = false;
    bool m_timerQueryWaiting = false;
  };

} // namespace ToolKit
