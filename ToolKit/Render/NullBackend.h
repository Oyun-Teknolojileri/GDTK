/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "IGraphicsBackend.h"

namespace ToolKit
{

  /**
   * A no-op graphics backend for headless tools (Packer, Import) that do not
   * need actual GPU rendering. All GPU operations are stubbed out so the
   * engine's resource managers can load and serialize assets without a
   * physical GPU, window, or OpenGL/Vulkan context.
   *
   * Usage:
   *   RenderSystem::UseNullBackend() before Main::Init().
   *   Skip SDL_INIT_VIDEO, window creation, GL context, and InitGraphics.
   */
  class TK_API NullBackend : public IGraphicsBackend
  {
   public:
    NullBackend()  = default;
    ~NullBackend() override = default;

    // ---- lifecycle ----
    void InitBackend(const BackendInitParams& params) override;

    void BeginFrame() override;
    void EndFrame() override;
    void Present() override;

    // ---- passes ----
    void StartPass(const PassDesc& desc) override;
    void FinishPass() override;

    // ---- viewport / scissor ----
    void SetViewport(uint x, uint y, uint w, uint h) override;
    void SetScissor(uint x, uint y, uint w, uint h) override;

    // ---- clear ----
    void ClearBuffer(GraphicBitFields fields, const Vec4& color) override;
    void ClearColorBuffer(const Vec4& color) override;

    // ---- pipeline ----
    void BindPipeline(const GpuProgramPtr& program, const RenderState* state) override;

    // ---- draw ----
    void SubmitPerDrawData(const void* data, size_t size) override;
    void BindTexture(ubyte slot, TexturePtr tex) override;
    void BindUniformBuffer(const String& name, UniformBuffer* ub) override;
    void BindUniformBuffer(UniformBuffer* ub, int slot) override;
    void Draw(const DrawDesc& desc) override;

    // ---- framebuffer ops ----
    void ResolveFramebuffer(FramebufferPtr src, FramebufferPtr dst, const IntArray& attachments) override;
    void CopyFramebuffer(FramebufferPtr src, FramebufferPtr dst, GraphicBitFields fields) override;
    void BlitToScreen(FramebufferPtr src) override;

    // ---- timer queries ----
    void StartTimerQuery() override;
    void EndTimerQuery() override;
    void GetElapsedTime(float& cpu, float& gpu) override;

    // ---- textures ----
    void CreateTexture(Texture* tex) override;
    void DestroyTexture(Texture* tex) override;
    void ApplyTextureSettings(Texture* tex) override;
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

    // ---- meshes ----
    void CreateMesh(Mesh* mesh) override;
    void DestroyMesh(Mesh* mesh) override;

    // ---- uniform buffers ----
    void CreateUniformBuffer(UniformBuffer* ub, uint64 size) override;
    void DestroyUniformBuffer(UniformBuffer* ub) override;
    void UpdateUniformBuffer(UniformBuffer* ub, const void* data, uint64 size) override;

    // ---- shaders ----
    GpuResourceDataPtr CreateShader(Shader* shader, const String& source) override;
    void DestroyShader(GpuResourceData* shaderData) override;

    // ---- gpu programs ----
    void CreateGpuProgram(GpuProgram* program, const ShaderResourceBinding* bindings, int bindingCount) override;
    void DestroyGpuProgram(GpuProgram* program) override;
    int GetUniformLocation(GpuProgram* program, const char* name) override;

    // ---- framebuffers ----
    void CreateFramebuffer(Framebuffer* fb) override;
    void DestroyFramebuffer(Framebuffer* fb) override;
    void AttachColorTarget(Framebuffer* fb, RenderTargetPtr rt, int attachment, int mip, int layer, int face) override;
    void DetachColorTarget(Framebuffer* fb, int attachment) override;
    void AttachDepthTarget(Framebuffer* fb, DepthTexturePtr dt) override;
    void DetachDepthTarget(Framebuffer* fb) override;

    // ---- uniform setters ----
    void SetUniform4f(int location, const Vec4& value) override;

    // ---- queries ----
    String GetBackendRendererString() override;
    int GetMaxArrayTextureLayers() override;

    // ---- srgb ----
    void SetSrgbAutoEncoding(bool enable) override;

    // ---- misc ----
    void Finish() override;
    void SetDefaultClearColor(const Vec4& color) override;
    bool ValidateBackbufferSrgbEncoding() override;
    void EnableScissorTest(bool enable) override;
    void ReadPixels(int x, int y, int w, int h, GraphicTypes format, GraphicTypes type, void* data) override;
    void UpdateTextureSubRegion(Texture* tex, int x, int y, int w, int h, const void* data) override;

    void PushDebugGroup(StringView name) override;
    void PopDebugGroup() override;

    void SetDebugLabel(Texture* tex) override;
    void SetDebugLabel(Framebuffer* fb) override;

    bool SupportsFloatTextureLinearFilter() override;
    bool IsDepthClampSupported() override;

    void* GetNativeTextureHandle(Texture* tex) override;
  };

} // namespace ToolKit
