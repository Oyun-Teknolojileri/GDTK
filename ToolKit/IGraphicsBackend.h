/*
 /*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "RenderState.h"
#include "ShaderUniform.h"
#include "Types.h"

#include <memory>

namespace ToolKit { class UniformBuffer; class Mesh; }

namespace ToolKit
{

  /** Base class for backend-specific GPU resource data. */
  struct GpuResourceData
  {
    virtual ~GpuResourceData() = default;
  };

  using GpuResourceDataPtr = std::shared_ptr<GpuResourceData>;

  struct PassDesc
  {
    FramebufferPtr target;
    GraphicBitFields clearBits   = GraphicBitFields::None;
    GraphicBitFields discardBits = GraphicBitFields::None;
    Vec4 clearColor              = Vec4(0.0f);
    bool loadColor               = false;
    bool loadDepth               = false;
  };

  struct DrawDesc
  {
    const Mesh* mesh          = nullptr;
    VertexLayout vertexLayout = VertexLayout::Mesh;
    bool indexed              = true;
    uint elementCount   = 0;
    uint instanceCount  = 1;
    DrawType type       = DrawType::Triangle;
  };

  class TK_API IGraphicsBackend
  {
   public:
    virtual ~IGraphicsBackend() = default;

    struct BackendInitParams
    {
      void* getProcAddress = nullptr;
      GpuErrorCallback errorCallback;
    };

    virtual void InitBackend(const BackendInitParams& params) = 0;

    virtual void BeginFrame()  = 0;
    virtual void EndFrame()    = 0;
    virtual void Present()     = 0;

    virtual void BeginPass(const PassDesc& desc)                                    = 0;
    virtual void EndPass() = 0;

    virtual void SetViewport(uint x, uint y, uint w, uint h)       = 0;
    virtual void SetScissor(uint x, uint y, uint w, uint h)        = 0;

    virtual void ClearBuffer(GraphicBitFields fields, const Vec4& color) = 0;
    virtual void ClearColorBuffer(const Vec4& color)                     = 0;

    virtual void BindPipeline(const GpuProgramPtr& program,
                              const RenderState* state)             = 0;

    virtual void SubmitPerDrawData(const void* data, size_t size)   = 0;
    virtual void BindTexture(ubyte slot, TexturePtr tex)            = 0;

    virtual void Draw(const DrawDesc& desc)                         = 0;

    virtual void ResolveFramebuffer(FramebufferPtr src,
                                    FramebufferPtr dst,
                                    const IntArray& attachments)    = 0;
    virtual void CopyFramebuffer(FramebufferPtr src,
                                 FramebufferPtr dst,
                                 GraphicBitFields fields)           = 0;
    virtual void BlitToScreen(FramebufferPtr src)                   = 0;
    virtual void StartTimerQuery()                                  = 0;
    virtual void EndTimerQuery()                                    = 0;
    virtual void GetElapsedTime(float& cpu, float& gpu)             = 0;

    virtual void CreateTexture(Texture* tex)                        = 0;
    virtual void DestroyTexture(Texture* tex)                       = 0;
    virtual void ApplyTextureSettings(Texture* tex)                 = 0;
    virtual void SetTextureSwizzleAlpha(Texture* tex, bool swizzleToOne, bool setLastBindBack = false) = 0;
    virtual void GenerateMipmaps(Texture* tex)                      = 0;
    virtual void UpdateTextureRegion(Texture* tex, const void* data) = 0;
    virtual void SetTextureMaxMipLevel(Texture* tex, int maxLevel)  = 0;
    virtual void AllocateCubemapMipStorage(Texture* tex)            = 0;
    virtual void CopyCubemapFaceFromFramebuffer(Texture* cubemap, int face, int mip, int width, int height,
                                                 Framebuffer* readFb, Framebuffer* writeFb) = 0;

    virtual void CreateMesh(Mesh* mesh)          = 0;
    virtual void DestroyMesh(Mesh* mesh)         = 0;

    virtual void CreateUniformBuffer(UniformBuffer* ub, uint64 size) = 0;
    virtual void DestroyUniformBuffer(UniformBuffer* ub)             = 0;
    virtual void UpdateUniformBuffer(UniformBuffer* ub,
                                     const void* data, uint64 size)  = 0;

    virtual GpuResourceDataPtr CreateShader(Shader* shader, const String& source) = 0;
    virtual void DestroyShader(GpuResourceData* shaderData)                       = 0;

    virtual void CreateGpuProgram(GpuProgram* program,
                                  struct GlobalGpuBuffers* buffers)      = 0;
    virtual void DestroyGpuProgram(GpuProgram* program)                  = 0;
    virtual int  GetUniformLocation(GpuProgram* program, const char* name) = 0;

    virtual void CreateFramebuffer(Framebuffer* fb)                 = 0;
    virtual void DestroyFramebuffer(Framebuffer* fb)                = 0;
    virtual void AttachColorTarget(Framebuffer* fb,
                                   RenderTargetPtr rt,
                                   int attachment,
                                   int mip,
                                   int layer,
                                   int face)                        = 0;
    virtual void DetachColorTarget(Framebuffer* fb, int attachment) = 0;
    virtual void AttachDepthTarget(Framebuffer* fb,
                                   DepthTexturePtr dt)              = 0;
    virtual void DetachDepthTarget(Framebuffer* fb)                 = 0;

    virtual void SubmitCustomUniforms(const GpuProgramPtr& program,
                                      std::unordered_map<String, ShaderUniform>& uniforms) = 0;

    virtual void SetUniform4f(int location, const Vec4& value) = 0;

    virtual String GetBackendRendererString() = 0;

    virtual int GetMaxArrayTextureLayers() = 0;

    virtual void SetSrgbAutoEncoding(bool enable) = 0;

    virtual void Finish() = 0;

    virtual void SetDefaultClearColor(const Vec4& color) = 0;

    virtual bool ValidateBackbufferSrgbEncoding() = 0;

    virtual void EnableScissorTest(bool enable) = 0;

    virtual void ReadPixels(int x, int y, int w, int h,
                            GraphicTypes format, GraphicTypes type, void* data) = 0;

    virtual void UpdateTextureSubRegion(Texture* tex, int x, int y, int w, int h, const void* data) = 0;

    virtual void PushDebugGroup(StringView name) = 0;
    virtual void PopDebugGroup() = 0;

    virtual bool SupportsFloatTextureLinearFilter() = 0;

    virtual void* GetNativeTextureHandle(Texture* tex) = 0;
  };

} // namespace ToolKit
