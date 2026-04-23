/*
 /*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../IGraphicsBackend.h"

namespace ToolKit
{

  class TK_API VulkanBackend : public IGraphicsBackend
  {
   public:
    VulkanBackend();
    ~VulkanBackend() override;

    // Backend initialization
    void InitBackend(const BackendInitParams& params) override;

    // Frame lifecycle
    void BeginFrame() override;
    void EndFrame() override;
    void Present() override;

    // Pass boundary
    void BeginPass(const PassDesc& desc) override;
    void EndPass() override;

    // Viewport / scissor
    void SetViewport(uint x, uint y, uint w, uint h) override;
    void SetScissor(uint x, uint y, uint w, uint h) override;

    // Clear
    void ClearBuffer(GraphicBitFields fields, const Vec4& color) override;
    void ClearColorBuffer(const Vec4& color) override;

    // Pipeline / program binding
    void BindPipeline(const GpuProgramPtr& program, const RenderState* state) override;

    // Per-draw resource binding
    void SubmitPerDrawData(const void* data, size_t size) override;
    void BindTexture(ubyte slot, TexturePtr tex) override;

    // Geometry draw
    void Draw(const DrawDesc& desc) override;

    // Utility / blit
    void ResolveFramebuffer(FramebufferPtr src, FramebufferPtr dst, const IntArray& attachments) override;
    void CopyFramebuffer(FramebufferPtr src, FramebufferPtr dst, GraphicBitFields fields) override;
    void BlitToScreen(FramebufferPtr src) override;

    // Timer queries
    void StartTimerQuery() override;
    void EndTimerQuery() override;
    void GetElapsedTime(float& cpu, float& gpu) override;

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

    // Framebuffer resource management
    void CreateFramebuffer(Framebuffer* fb) override;
    void DestroyFramebuffer(Framebuffer* fb) override;
    void AttachColorTarget(Framebuffer* fb, RenderTargetPtr rt, int attachment, int mip, int layer, int face) override;
    void DetachColorTarget(Framebuffer* fb, int attachment) override;
    void AttachDepthTarget(Framebuffer* fb, DepthTexturePtr dt) override;
    void DetachDepthTarget(Framebuffer* fb) override;

    // Custom uniforms and renderer utility
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
    void SetDebugLabel(Texture* tex) override;
    void SetDebugLabel(Framebuffer* fb) override;
  };

  /** Factory function called by RenderSystem::CreateBackend(). */
  IGraphicsBackend* CreateGraphicsBackend();

} // namespace ToolKit