/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "RenderState.h"
#include "Types.h"

// Forward declarations for resource types not pulled in via Types.h
namespace ToolKit { class UniformBuffer; }

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
    virtual void StoreFboBindings()                                 = 0;
    virtual void RestoreFboBindings()                               = 0;

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
    virtual void InvalidateFramebuffer(FramebufferPtr fb,
                                       GraphicBitFields bits)       = 0;

    // Timer queries
    virtual void StartTimerQuery()                                  = 0;
    virtual void EndTimerQuery()                                    = 0;
    virtual void GetElapsedTime(float& cpu, float& gpu)             = 0;

    // Texture resource management
    // CreateTexture: allocates GPU storage (gen + texImage/renderbufferStorage). Reads all
    // required info from tex->Settings(), tex->m_width/height, and pixel data pointers.
    virtual void CreateTexture(Texture* tex)                        = 0;
    virtual void DestroyTexture(Texture* tex)                       = 0;
    // ApplyTextureSettings: sets sampler parameters (filter, wrap) for the currently bound texture.
    // Must be called after CreateTexture while the texture is still bound.
    virtual void ApplyTextureSettings(Texture* tex)                 = 0;
    virtual void GenerateMipmaps(Texture* tex)                      = 0;
    // UpdateTextureRegion: full-surface upload via glTexSubImage2D. Used by DataTexture::Map.
    virtual void UpdateTextureRegion(Texture* tex, const void* data) = 0;
    // SetTextureMaxMipLevel: sets GL_TEXTURE_MAX_LEVEL (or VK equivalent). Used by specular IBL maps.
    virtual void SetTextureMaxMipLevel(Texture* tex, int maxLevel)  = 0;
    // AllocateCubemapMipStorage: pre-allocates storage for all non-base mip levels of a cube map.
    virtual void AllocateCubemapMipStorage(Texture* tex)            = 0;
    // CopyCubemapFaceFromFramebuffer: copies current read framebuffer to a cubemap face at a given mip.
    // Equivalent to glCopyTexSubImage2D on GL_TEXTURE_CUBE_MAP_POSITIVE_X + face.
    virtual void CopyCubemapFaceFromFramebuffer(Texture* cubemap, int face, int mip, int width, int height) = 0;

    // UniformBuffer resource management
    virtual void CreateUniformBuffer(UniformBuffer* ub, uint64 size) = 0;
    // glGenBuffers + glBindBuffer + glBufferData(DYNAMIC_DRAW)
    virtual void DestroyUniformBuffer(UniformBuffer* ub)             = 0;
    // glDeleteBuffers
    virtual void UpdateUniformBuffer(UniformBuffer* ub,
                                     const void* data, uint64 size)  = 0;
    // glBufferSubData — called only when dirty (GpuBufferBase cache preserved)

    // GpuProgram resource management
    virtual void CreateGpuProgram(GpuProgram* program,
                                  struct GlobalGpuBuffers* buffers)      = 0;
    // glCreateProgram + glAttachShader + glLinkProgram
    // sampler slot binding, UBO block index + glBindBufferBase
    // default + array uniform location caching
    virtual void DestroyGpuProgram(GpuProgram* program)                  = 0;
    // glDeleteProgram
    virtual int  GetUniformLocation(uint programHandle, const char* name) = 0;
    // glGetUniformLocation — used for lazy custom uniform lookup at draw time.
    // Vulkan: returns -1 (push constants / descriptors handle this differently)

    // Framebuffer resource management
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
    virtual void SetDrawBuffers(Framebuffer* fb)                    = 0;
    virtual void CheckFramebufferComplete(Framebuffer* fb)          = 0;
  };

} // namespace ToolKit
