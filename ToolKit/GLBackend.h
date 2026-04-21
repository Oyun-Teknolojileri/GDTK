/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "IGraphicsBackend.h"
#include "UniformBuffer.h"

namespace ToolKit
{

  class TK_API GLBackend : public IGraphicsBackend
  {
   public:
    GLBackend();
    ~GLBackend() override;

    void InitBackend(const BackendInitParams& params) override;
    void BeginFrame() override;
    void EndFrame() override;
    void Present() override;

    void BeginPass(const PassDesc& desc) override;
    void EndPass() override;

    void SetViewport(uint x, uint y, uint w, uint h) override;
    void SetScissor(uint x, uint y, uint w, uint h) override;

    void ClearBuffer(GraphicBitFields fields, const Vec4& color) override;
    void ClearColorBuffer(const Vec4& color) override;

    void BindPipeline(const GpuProgramPtr& program, const RenderState* state) override;

    void SubmitPerDrawData(const void* data, size_t size) override;
    void BindTexture(ubyte slot, TexturePtr tex) override;

    void Draw(const DrawDesc& desc) override;

    void ResolveFramebuffer(FramebufferPtr src, FramebufferPtr dst, const IntArray& attachments) override;
    void CopyFramebuffer(FramebufferPtr src, FramebufferPtr dst, GraphicBitFields fields) override;
    void BlitToScreen(FramebufferPtr src) override;

    void StartTimerQuery() override;
    void EndTimerQuery() override;
    void GetElapsedTime(float& cpu, float& gpu) override;

    // Mesh resource management
    void CreateMesh(Mesh* mesh) override;
    void DestroyMesh(Mesh* mesh) override;

    // UniformBuffer resource management
    void CreateUniformBuffer(UniformBuffer* ub, uint64 size) override;
    void DestroyUniformBuffer(UniformBuffer* ub) override;
    void UpdateUniformBuffer(UniformBuffer* ub, const void* data, uint64 size) override;

    // Shader resource management
    GpuResourceDataPtr CreateShader(Shader* shader, const String& source) override;
    void DestroyShader(GpuResourceData* shaderData) override;

    // GpuProgram resource management
    void CreateGpuProgram(GpuProgram* program, struct GlobalGpuBuffers* buffers) override;
    void DestroyGpuProgram(GpuProgram* program) override;
    int GetUniformLocation(GpuProgram* program, const char* name) override;

    // Texture resource management
    void CreateTexture(Texture* tex) override;
    void DestroyTexture(Texture* tex) override;
    void ApplyTextureSettings(Texture* tex) override;
    void SetTextureSwizzleAlpha(Texture* tex, bool swizzleToOne, bool setLastBindBack = false) override;
    void GenerateMipmaps(Texture* tex) override;
    void UpdateTextureRegion(Texture* tex, const void* data) override;
    void SetTextureMaxMipLevel(Texture* tex, int maxLevel) override;
    void AllocateCubemapMipStorage(Texture* tex) override;
    void CopyCubemapFaceFromFramebuffer(Texture* cubemap,
                                        int face,
                                        int mip,
                                        int width,
                                        int height,
                                        Framebuffer* readFb,
                                        Framebuffer* writeFb) override;

    // Framebuffer resource management
    void CreateFramebuffer(Framebuffer* fb) override;
    void DestroyFramebuffer(Framebuffer* fb) override;
    void AttachColorTarget(Framebuffer* fb, RenderTargetPtr rt, int attachment, int mip, int layer, int face) override;
    void DetachColorTarget(Framebuffer* fb, int attachment) override;
    void AttachDepthTarget(Framebuffer* fb, DepthTexturePtr dt) override;
    void DetachDepthTarget(Framebuffer* fb) override;

    // Cache management (Internal use by RHI)
    void BindFramebuffer(uint target, uint id);
    void BindTextureDirect(uint target, uint id, uint slot);
    void BindVAO(uint vao);

    void InvalidateFboCache(uint id);
    void InvalidateTextureCache(uint id);

    // Phase 7a: Custom uniforms and renderer utility
    void SubmitCustomUniforms(const GpuProgramPtr& program,
                              std::unordered_map<String, ShaderUniform>& uniforms) override;
    void SetUniform4f(int location, const Vec4& value) override;
    String GetBackendRendererString() override;
    int GetMaxArrayTextureLayers() override;
    void SetSrgbAutoEncoding(bool enable) override;
    void Finish() override;
    void SetDefaultClearColor(const Vec4& color) override;
    bool ValidateBackbufferSrgbEncoding() override;
    void EnableScissorTest(bool enable) override;
    void ReadPixels(int x, int y, int w, int h, GraphicTypes format, GraphicTypes type, void* data) override;
    void UpdateTextureSubRegion(Texture* tex, int x, int y, int w, int h, const void* data) override;
    void PushDebugGroup(StringView name) override;
    void PopDebugGroup() override;
    bool SupportsFloatTextureLinearFilter() override;
    void* GetNativeTextureHandle(Texture* tex) override;

   private:
    void StoreFboBindings();
    void RestoreFboBindings();
    void SetDrawBuffers(Framebuffer* fb);
    void CheckFramebufferComplete(Framebuffer* fb);

    RenderState m_lastAppliedState;
    bool m_firstBind = true;
    PassDesc m_activePassDesc;

    // Program cache — avoids glUseProgram no-ops and glGetIntegerv(GL_CURRENT_PROGRAM) roundtrips.
    uint        m_currentProgramId = 0;
    GpuProgram* m_currentProgram   = nullptr;

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
    uint m_currentVAO        = (uint) -1;

    // Timer query
    uint m_gpuTimerQuery     = 0;
    float m_cpuTime          = 1.0f;
    float m_gpuTime          = 1.0f;
    bool m_timerQueryActive  = false;
    bool m_timerQueryWaiting = false;
  };

} // namespace ToolKit
