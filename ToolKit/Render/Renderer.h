/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Camera.h"
#include "GenericBuffers.h"
#include "GpuProgram.h"
#include "IGraphicsBackend.h"
#include "Material.h"
#include "Primative.h"
#include "RHI.h"
#include "RenderState.h"
#include "Sky.h"
#include "Types.h"
#include "Viewport.h"

#include <cstddef> // offsetof (InstanceRecord2a static_asserts)
#include <unordered_map>

namespace ToolKit
{

  class ForwardSceneRenderPath;

  // DrawCommand
  //////////////////////////////////////////

  struct DrawCommand
  {
    // --- Global data (2 Vec4) ---

    /** x: iblInUse, y: ambientOcclusionInUse, z: unused, w: unused */
    Vec4 global0;

    /** x: activePointLightCount, y: activeSpotLightCount, z: activeDirectionalLightCount, w: unused */
    Vec4 global1;

    // --- Volume 0 / Primary (11 Vec4) ---

    /** x: intensity, y: fadeDistance, z: interior, w: pccEnabled */
    Vec4 vol0Params;
    Vec4 vol0Min; /**< xyz: volume min (local space) */
    Vec4 vol0Max; /**< xyz: volume max (local space) */
    Vec4 vol0InvTransform0, vol0InvTransform1, vol0InvTransform2, vol0InvTransform3;
    Vec4 vol0WorldTransform0, vol0WorldTransform1, vol0WorldTransform2, vol0WorldTransform3;

    // --- Volume 1 / Secondary (11 Vec4) ---

    /** x: intensity, y: fadeDistance, z: interior, w: pccEnabled */
    Vec4 vol1Params;
    Vec4 vol1Min; /**< xyz: volume min (local space) */
    Vec4 vol1Max; /**< xyz: volume max (local space) */
    Vec4 vol1InvTransform0, vol1InvTransform1, vol1InvTransform2, vol1InvTransform3;
    Vec4 vol1WorldTransform0, vol1WorldTransform1, vol1WorldTransform2, vol1WorldTransform3;

    // --- Global setters ---

    void SetIblInUse(bool inUse) { global0.x = inUse ? 1.0f : 0.0f; }

    void SetAmbientOcclusionInUse(bool inUse) { global0.y = inUse ? 1.0f : 0.0f; }

    /** Sky intensity (0 = no sky). */
    void SetSkyIntensity(float intensity) { global0.z = intensity; }

    void SetActivePointLightCount(int count) { global1.x = (float) count; }

    void SetActiveSpotLightCount(int count) { global1.y = (float) count; }

    void SetActiveDirectionalLightCount(int count) { global1.z = (float) count; }

    // --- Per-volume setters ---

    void SetVolumeIntensity(int vol, float intensity) { Params(vol).x = intensity; }

    void SetVolumeFadeDistance(int vol, float fade) { Params(vol).y = fade; }

    void SetVolumeInterior(int vol, bool interior) { Params(vol).z = interior ? 1.0f : 0.0f; }

    void SetVolumePccEnabled(int vol, bool enabled) { Params(vol).w = enabled ? 1.0f : 0.0f; }

    void SetVolumeMin(int vol, const Vec3& minVal) { Min(vol) = Vec4(minVal, 0.0f); }

    void SetVolumeMax(int vol, const Vec3& maxVal) { Max(vol) = Vec4(maxVal, 0.0f); }

    void SetVolumeInverseTransform(int vol, const Mat4& m)
    {
      InvT(vol, 0) = Vec4(m[0]);
      InvT(vol, 1) = Vec4(m[1]);
      InvT(vol, 2) = Vec4(m[2]);
      InvT(vol, 3) = Vec4(m[3]);
    }

    void SetVolumeWorldTransform(int vol, const Mat4& m)
    {
      WldT(vol, 0) = Vec4(m[0]);
      WldT(vol, 1) = Vec4(m[1]);
      WldT(vol, 2) = Vec4(m[2]);
      WldT(vol, 3) = Vec4(m[3]);
    }

   private:
    Vec4& Params(int vol) { return vol == 0 ? vol0Params : vol1Params; }

    Vec4& Min(int vol) { return vol == 0 ? vol0Min : vol1Min; }

    Vec4& Max(int vol) { return vol == 0 ? vol0Max : vol1Max; }

    Vec4& InvT(int vol, int row)
    {
      Vec4* base = vol == 0 ? &vol0InvTransform0 : &vol1InvTransform0;
      return base[row];
    }

    Vec4& WldT(int vol, int row)
    {
      Vec4* base = vol == 0 ? &vol0WorldTransform0 : &vol1WorldTransform0;
      return base[row];
    }
  };

  // GraphicConstantsGpuBuffer
  //////////////////////////////////////////

  struct GraphicConstatsDataLayout
  {
    float shadowDistance;
    float shadowAtlasSize;
    int iblMaxReflectionLod;
    int cascadeCount;
    Vec4 cascadeDistances;
  };

  typedef GpuBufferBase<GraphicConstatsDataLayout> GraphicConstantsGpuBuffer;

  // PerDrawGpuBuffer (slot 2)
  //////////////////////////////////////////

  /**
   * std140-packed CPU mirror for the PerDrawData UBO. Holds every per-draw uniform that today
   * is fed scatter-style via glUniform*; once a shader is migrated, it reads these fields out
   * of `layout(std140) uniform PerDrawData { ... }` instead. Layout intentionally pads to 16-byte
   * lines so the GPU view matches the C++ memcpy byte-for-byte.
   *
   * Shader-side mirror lives in `Resources/Engine/Shaders/perDrawDataInc.shader`. Any field added
   * here must be added to that file in the same order or the layouts drift.
   */
  struct PerDrawUboLayout
  {
    // Transform matrices — 6×64 = 384 bytes
    Mat4 model;
    Mat4 modelWithoutTranslate;
    Mat4 inverseModel;
    Mat4 inverseTransposeModel;
    Mat4 iblRotation;
    Mat4 iblSecondaryRotation;

    /** .xy = viewportSize, .zw = pad. Bundled into a vec4 so the next struct lands on a
        16-byte boundary without an implicit gap. */
    Vec4 viewportSizeAndPad;

    /** 24 vec4 — already std140-clean. */
    DrawCommand drawCommand;

    /** 4 vec4 — MaterialCacheItem::Data is documented std140 layout. */
    MaterialCacheItem::Data materialData;

    /** 24 ints packed as 6 ivec4 — std140 would otherwise waste 12 bytes per int. */
    IVec4 activePointLightIndices[6];
    IVec4 activeSpotLightIndices[6];
    /** .x = activePointLightCount, .y = activeSpotLightCount. */
    IVec4 lightCounts;

    Vec4 keyFrameData;
    Vec4 blendFrameData;
    Vec4 skinParams;
    /** .x = animationBlendFactor. Padded to vec4 for std140 alignment. */
    Vec4 animBlendFactorAndPad;
  };

  typedef GpuBufferBase<PerDrawUboLayout> PerDrawUboBuffer;

  // Instanced transport — Phase 2a (see `rendering-roadmap.md` §Phase 2a + §5).
  //
  // 2a proves the instance-data-buffer transport with a *throwaway, byte-for-byte mirror* of
  // `PerDrawUboLayout`: the same struct is written to both the per-draw UBO and the instance
  // texture in one frame, so the (UBO-bound) fragment shader reads bytes identical to legacy
  // and any divergence isolates to the new transport. The lean `InstanceRecord` + `RenderObject`
  // + global-table split replaces this alias in 2b.
  using InstanceRecord2a = PerDrawUboLayout;

  // RGBA32F texel stride of one instance record. Anchors the GLSL texel layout declared in
  // `Resources/Engine/Shaders/instanceDataInc.shader` (`TK_INSTANCE_STRIDE`) to this C++ layout
  // — a future compiler-padding change cannot drift silently (GLSL texel layouts are unforgiving;
  // see roadmap §5 mirroring rule).
  //
  // These three guards are the ones the 2a *texture/memcpy* transport needs: we copy
  // `InstanceRecord2a` bytes into an RGBA32F texture and the GPU reads them as texels, so only
  // the byte layout matters — C++ alignment is irrelevant here. glm's gentypes use natural
  // 4-byte alignment in this project (verified: `alignof(glm::mat4) == 4`), so `InstanceRecord2a`
  // — a typedef of the existing `PerDrawUboLayout` — is 4-byte aligned, which is correct for
  // texel transport. The `alignof >= 16` SSBO/Vulkan-binding guard (roadmap v8) belongs on the
  // *lean* 2b `InstanceRecord`, which will be authored with explicit `alignas(16)`; it is
  // intentionally not asserted against this reuse alias.
  static_assert(sizeof(InstanceRecord2a) % 16 == 0,
                "InstanceRecord2a stride must be a whole number of RGBA32F texels.");
  static_assert(offsetof(InstanceRecord2a, model) == 0,
                "InstanceRecord2a.model must be the first field (texel offset 0).");
  static_assert(sizeof(InstanceRecord2a) == 70 * 16,
                "InstanceRecord2a must be 70 RGBA32F texels (1120 bytes) — mirrors PerDrawUboLayout.");
  static constexpr uint InstanceRecord2aStride = sizeof(InstanceRecord2a) / 16; // == 70

  // Pass-specific UBOs (slot 7)
  //////////////////////////////////////////
  //
  // Pass-specific UBOs all share GL slot 7. Each pass
  // owns its own buffer instance; no two passes are active simultaneously so the slot can be
  // re-bound by whichever pass is rendering. Each layout is intentionally tiny — only the
  // values one pass writes per frame.

  /** Gaussian blur shader UBO. Used by `Renderer::ApplyGaussianBlur*` family with the shared
      `gausBlurVert/Frag.shader` pair. Compact: just the per-tap scale + optional layer + UV
      clamp window. The shader uses `#if` guards on TextureArray / BlurClampEnabled defines to
      ignore unused fields, so the same UBO works across all blur variants. */
  struct GaussBlurPassDataLayout
  {
    /** .xyz = BlurScale, .w = BlurLayer (used only when TextureArray==1). */
    Vec4 blurScaleAndLayer;
    /** .xy = BlurClampMin, .zw = BlurClampMax (used only when BlurClampEnabled==1). */
    Vec4 blurClampMinMax;
  };

  typedef GpuBufferBase<GaussBlurPassDataLayout> GaussBlurPassDataBuffer;

  struct CubemapEquirectPassDataLayout
  {
    Vec4 exposureAndPad;
    IVec4 lodLevelAndPad;
  };

  typedef GpuBufferBase<CubemapEquirectPassDataLayout> CubemapEquirectPassDataBuffer;

  struct PreFilterEnvMapPassDataLayout
  {
    /** .x = resPerFace, .y = roughness. */
    Vec4 params;
  };

  typedef GpuBufferBase<PreFilterEnvMapPassDataLayout> PreFilterEnvMapPassDataBuffer;

  /** GradientSky cubemap fragment UBO (`gradientSkyboxFrag.shader`). 3 vec3 colors + 1 float
      exponent — packed into 4 vec4s. */
  struct GradientSkyboxPassDataLayout
  {
    /** .xyz = top color, .w = unused. */
    Vec4 topColor;
    /** .xyz = middle color, .w = unused. */
    Vec4 middleColor;
    /** .xyz = bottom color, .w = unused. */
    Vec4 bottomColor;
    /** .x = exponent. */
    Vec4 exponentAndPad;
  };

  typedef GpuBufferBase<GradientSkyboxPassDataLayout> GradientSkyboxPassDataBuffer;

  // GlobalGpuBuffers
  //////////////////////////////////////////

  struct GlobalBufferInfo
  {
    const char* name;
    int slot;
    UniformBuffer* buffer;
  };

  struct GlobalGpuBuffers
  {
    /** Uniform buffer for camera data. */
    CameraGpuBuffer cameraGpuBuffer;

    /** Uniform buffer for graphic constants. */
    GraphicConstantsGpuBuffer graphicConstantBuffer;

    /** Active directional lights in gpu. */
    DirectionalLightBuffer directionalLightBuffer;

    /** Cached point lights in gpu. */
    PointLightCache pointLighBuffer;

    /** Cached spot lights in gpu. */
    SpotLightCache spotLightBuffer;

    /** Per-draw UBO — fed each draw via SubmitPerDrawData. */
    PerDrawUboBuffer perDrawBuffer;

    void InitGlobalGpuBuffers();

    /** Return corresponding global buffer, if not found returns null. */
    const GlobalBufferInfo* FindGlobalBufferInfo(const char* blockName) const;

   private:
    GlobalBufferInfo m_bufferTable[ReservedUniformBufferSlots::GlobalBufferCount];
  };

  // Renderer
  //////////////////////////////////////////

  class TK_API Renderer
  {
   public:
    Renderer();
    ~Renderer();

    /** If the back buffer is srgb, enables auto encoding from linear render target to backbuffer. */
    void SrgbAutoEncoding(bool enable);

    /** Performs required operations per frame at the begging of a full render cycle. */
    void BeginRenderFrame();

    /** Performs required operations per frame at the end of a full render cycle. */
    void EndRenderFrame();

    /** Allows application to re map graphics constants. */
    void InvalidateGraphicsConstants();

    void Init();

    void StartTimerQuery();
    void EndTimerQuery();

    /** Returns elapsed time between start - end time query in milliseconds.*/
    void GetElapsedTime(float& cpu, float& gpu);

    void ClearColorBuffer(const Vec4& color);
    void ClearBuffer(GraphicBitFields fields, const Vec4& value = Vec4(0.0f));

    /** Returns an opaque native texture handle as uint, obtained from the backend. */
    /** Returns an opaque native texture handle as uint64. GL backend returns a GLuint zero-extended
     *  to 64 bits; Vulkan backend returns a `VulkanTexture*` (pointer), cast to uint64. */
    static uint64 GetNativeTextureHandle(const TexturePtr& tex);

    // FrameBuffer Operations
    //////////////////////////////////////////

    FramebufferPtr GetFrameBuffer();

    void SetFramebuffer(FramebufferPtr frameBuffer,
                        GraphicBitFields attachmentsToClear,
                        const Vec4& clearColor       = Vec4(0.0f),
                        GraphicBitFields discardBits = GraphicBitFields::None);

    void FinishPass();

    /**
     * Resolves source multi sample buffer to single sample target buffer.
     * Source attachment must exist, target attachment will be created if not existing.
     */
    void ResolveFramebuffer(FramebufferPtr source, FramebufferPtr target, const IntArray& attachments);

    /**
     * Sets the src and dest frame buffers and copies the given fields.
     * After the operation sets the previous frame buffer back.
     */
    void CopyFrameBuffer(FramebufferPtr src, FramebufferPtr dest, GraphicBitFields fields);

    /**
     * Copies src to dst texture using a copy frame buffer.
     *
     * @param alphaToOne When true, forces dst's alpha channel to 1.0 during the copy.
     *                   Useful when copying color attachments that may have been
     *                   partially-cleared or alpha-blended; downstream passes that
     *                   treat the texture as opaque (e.g. bloom, DOF) shouldn't see a
     *                   premultiplied or zero alpha.
     */
    void CopyTexture(TexturePtr src, TexturePtr dst, bool alphaToOne = false);

    //////////////////////////////////////////

    void SetViewport(Viewport* viewport);
    void SetViewportRect(uint x, uint y, uint width, uint height);
    void SetScissor(uint x, uint y, uint width, uint height);

    void DrawFullQuad(ShaderPtr fragmentShader);
    void DrawFullQuad(MaterialPtr mat);
    void DrawCube(CameraPtr cam, MaterialPtr mat, const Mat4& transform = Mat4(1.0f));

    void SetTexture(ubyte slotIndx, TexturePtr texture);

    /** Bind a pass-specific uniform buffer (custom slot, not a reserved global UBO).
     *  Preferred entry point for new code — PassRequirements::customUbos drives this. */
    void BindUniformBuffer(int slot, UniformBuffer* ubo);

    /** Binds a texture to the slot declared for the given semantic sampler name in the
     *  currently bound program. No-op if the program does not declare that sampler. */
    void SetTexture(const char* semanticName, TexturePtr texture);

    /** Reads an equirectengular hdr image and creates a cube map from it. */
    CubeMapPtr GenerateCubemapFrom2DTexture(TexturePtr texture,
                                            uint size,
                                            float exposure         = 1.0f,
                                            GraphicTypes minfilter = GraphicTypes::SampleNearest);

    /**
     * Projects a cubemap to an 2d texture using equirectengular projection.
     * If a non null, pointer address provided, fills the pixel content in it.
     * Life time management of the buffer belongs to caller.
     */
    TexturePtr GenerateEquiRectengularProjection(CubeMapPtr cubemap, int level, float exposure, void** pixels);

    /** Copies the source cube map into destination cube map's given mip level. Expects cubemaps tobe rgba float. */
    void CopyCubeMapToMipLevel(CubeMapPtr src, CubeMapPtr dst, int mipLevel);

    /** Generates specular environment for given number of mip levels. */
    CubeMapPtr GenerateSpecularEnvMap(CubeMapPtr cubemap, int size, int mipMaps);

    /** Generates irradiance map. */
    CubeMapPtr GenerateDiffuseEnvMap(CubeMapPtr cubemap, int size);

    /**
     * Renders the scene into a cubemap using the provided render path.
     * Camera is placed at the entity origin (with originOffset applied in local space),
     * oriented along the entity's world transform. Each cubemap face looks along a
     * local-space axis rotated into world space by the entity transform.
     * @param renderPath The render path to use for rendering each face.
     * @param worldTransform World transform of the environment volume entity.
     * @param originOffset Local-space offset from entity origin to capture position.
     * @param near Near clip plane.
     * @param far Far clip plane.
     * @param resolution Resolution of each cubemap face.
     * @param perFaceClipDist Optional per-face far clip distances (6 floats, in local face order: +X,-X,+Y,-Y,+Z,-Z).
     * @return The generated cubemap.
     */
    CubeMapPtr RenderToCubeMap(ForwardSceneRenderPath* renderPath,
                               const Mat4& worldTransform,
                               const Vec3& originOffset,
                               float near,
                               float far,
                               uint resolution,
                               const float* perFaceClipDist = nullptr);

    // Giving nullptr as argument means no shadows
    void SetShadowAtlas(TexturePtr shadowAtlas);

    void Render(const struct RenderJob& job);
    void Render(const RenderJobArray& jobs);

    void RenderWithProgramFromMaterial(const RenderJobArray& jobs);
    void RenderWithProgramFromMaterial(const RenderJob& job);

    /** Apply one tap of gauss blur via setting a temporary frame buffer. Does not reset frame buffer back. */
    void ApplyGaussianBlur(const TexturePtr src, RenderTargetPtr dst, const Vec3& axis, const float amount);

    /**
     * Applies separable Gaussian blur on a sub-region (slot) of a specific layer of a 2D array texture.
     * UV coordinates are clamped to [clampMin, clampMax] to prevent cross-slot bleeding.
     * @param srcArray Source 2D array texture (e.g. shadow atlas).
     * @param tempRT Temporary 2D render target for ping-pong (same width/height as srcArray).
     * @param framebuffer Framebuffer to use for rendering.
     * @param layer The array layer to blur.
     * @param kernelSize Kernel size: 3, 5, or 7.
     * @param tapCount Number of blur passes (horizontal + vertical per tap).
     * @param amount Blur scale amount (texel size multiplier).
     * @param slotCoord Top-left pixel coordinate of the slot in the atlas.
     * @param slotSize Pixel size of the slot (width = height).
     */
    void ApplyGaussianBlurToArrayLayerSlot(RenderTargetPtr srcArray,
                                           RenderTargetPtr tempRT,
                                           FramebufferPtr framebuffer,
                                           int layer,
                                           int kernelSize,
                                           int tapCount,
                                           float amount,
                                           const Vec2& slotCoord,
                                           int slotSize);

    /**
     * Sets the camera to be used for rendering. Also calculates camera related parameters, such as view, transform,
     * viewTransform etc...
     * if setLense is true sets the lens to fit aspect ratio to frame buffer.
     * Invalidates gpu program's related caches.
     */
    void SetCamera(CameraPtr camera, bool setLens);

    int GetMaxArrayTextureLayers();

    /** Sets the passive pipeline state (depth test/write, depth func, stencil, color mask,
     *  depth clamp) that will be merged with each render job's active material state at draw time.
     *  Active fields (cullMode, blendFunction, drawType, alphaMaskTreshold, lineWidth) are
     *  ignored; they always come from the job's material. */
    void SetPassState(const RenderState& state);

    void BindProgramOfMaterial(Material* material);
    void BindProgram(const GpuProgramPtr& program);
    void ResetUsedTextureSlots();

    /** Initialize brdf lut textures. */
    void GenerateBRDFLutTexture();

    /** Ambient occlusion texture to be applied. If ao is not enabled, set this explicitly to null. */
    void SetAmbientOcclusionTexture(TexturePtr aoTexture);

    /** Sets the current material to use in render. */
    void SetMaterial(Material* mat);

    /** Sets active lights to be used in the render. Doesn't include directional lights. */
    void SetLights(const LightRawPtrArray& lights);

    /**
     /** Sets directional lights to be used for render. Should be called once per pass because all objects effected from
      * directional lights. No need to set it per object.
      */
    void SetDirectionalLights(const LightRawPtrArray& lights);

    /** Sets the graphics backend. Ownership is transferred to the Renderer. */
    void SetBackend(IGraphicsBackend* backend) { m_backend = backend; }

    /** Returns current backend. */
    IGraphicsBackend* GetBackend() { return m_backend; }

    /** Convenience wrapper around backend depth-clamp capability query. Callers (e.g. ShadowPass)
     *  use this to skip the pancake shader path when the rasterizer can clamp natively. */
    bool IsDepthClampSupported() { return m_backend != nullptr && m_backend->IsDepthClampSupported(); }

    /** Phase 2a instance-data transport toggle (off = legacy per-draw UBO, byte-identical). */
    bool IsInstancedTransportEnabled() const { return m_instancedTransportEnabled; }

    GpuProgramManager* GetGpuProgramManager() { return m_gpuProgramManager; }

   private:
    /** Set textures to be used in render. SkyBox, Ibl, AmbientOcculution  */
    void SetDataTextures(const RenderJob& job);

    /** Sets the current model and derived transforms to be used in shader. */
    void SetTransforms(const Mat4& model);

    void FeedUniforms(const GpuProgramPtr& program, const RenderJob& job);

   public:
    uint m_frameCount = 0;
    UVec2 m_windowSize; //!< Application window size.
    Vec4 m_clearColor         = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CameraPtr m_uiCamera      = nullptr;
    SkyBasePtr m_sky          = nullptr;

    ShadingMode m_shadingMode = ShadingMode::None;

    /** Global gpu buffers for renderer. */
    GlobalGpuBuffers* m_globalGpuBuffers;

    GradientSkyboxPassDataBuffer m_gradientSkyboxBuffer;
    bool m_gradientSkyboxBufferInitialized = false;

   private:
    GpuProgramPtr m_currentProgram = nullptr;

    /** Current camera cache item. */
    CameraCacheItem m_cameraCacheItem;

    // Transform matrices.
    Mat4 m_model;
    Mat4 m_inverseModel;
    Mat4 m_inverseTransposeModel;
    Mat4 m_modelWithoutTranslate;
    Mat4 m_iblRotation;
    Mat4 m_secondaryIblRotation;

    // Draw data
    std::array<int, RHIConstants::MaxPointLightPerObject> m_activePointLightIndices;
    std::array<int, RHIConstants::MaxSpotLightPerObject> m_activeSpotLightIndices;
    DrawCommand m_drawCommand;

    int m_activePointLightCount   = 0;
    int m_activeSpotLightCount    = 0;
    bool m_ambientOcculusionInUse = false;

    // Phase 2a instance-data transport (see rendering-roadmap.md §Phase 2a).
    // When false (default), every draw uses the legacy per-draw UBO path — byte-identical.
    // Step 6 flips this to true for the single-instance A/B verification.
    bool m_instancedTransportEnabled = false;

    /** Instance-data buffer for the GL/WebGL VTF transport (2a single-instance proof).
     *  The underlying RGBA32F texture carries one InstanceRecord2a (= PerDrawUboLayout) row;
     *  each flagged draw overwrites slot 0, Flushes, and binds s_instanceData before
     *  glDrawElements (gl_InstanceID=0 -> LoadInstance(0)). 2b replaces with per-batch /
     *  lean-record send.  Held by pointer to avoid a circular include between Renderer.h
     *  and InstanceDataBuffer.h (InstanceDataBuffer needs InstanceRecord2a which lives
     *  above in this file). */
    std::unique_ptr<class InstanceDataBuffer> m_instanceBuffer;

    FramebufferPtr m_framebuffer  = nullptr;
    TexturePtr m_shadowAtlas      = nullptr;
    RenderTargetPtr m_brdfLut     = nullptr;
    TexturePtr m_aoTexture        = nullptr;

    /** Extra textures bound by Render(job) *after* BindPipeline so they survive the Vulkan
     * descriptor wipe in VulkanBackend::BindPipeline. Key is the shader-side sampler name
     * (e.g. "s_sourceArray"); value is the texture to bind. Used by utility passes that
     * sample from a slot the standard material / SetDataTextures flow does not bind.
     * The owning pass must clear its entry when done. */
    std::unordered_map<String, TexturePtr> m_postPipelineTextures;

    std::array<int, RHIConstants::TextureSlotCount> m_textureSlots;

    /** Current viewport size (x,y) and position (z,w) */
    UVec4 m_viewportRect;

    /*
     * This framebuffer can ONLY have 1 color attachment and no other attachments.
     * This way, we can use without needing to resize or reInit.
     */
    FramebufferPtr m_oneColorAttachmentFramebuffer = nullptr;
    MaterialPtr m_gaussianBlurMaterial             = nullptr;
    /** Pass UBO (slot 7) shared with the gaussian blur material. Lazy-init on first
        ApplyGaussianBlur* call together with the material itself. */
    GaussBlurPassDataBuffer m_gaussianBlurBuffer;
    bool m_gaussianBlurBufferInitialized = false;
    /** Shared by GenerateCubemapFrom2DTexture / GenerateEquiRectengularProjection paths. */
    CubemapEquirectPassDataBuffer m_cubemapEquirectBuffer;
    bool m_cubemapEquirectBufferInitialized = false;
    PreFilterEnvMapPassDataBuffer m_preFilterEnvMapBuffer;
    bool m_preFilterEnvMapBufferInitialized = false;
    MaterialPtr m_averageBlurMaterial       = nullptr;
    QuadPtr m_tempQuad                      = nullptr;
    MaterialPtr m_tempQuadMaterial          = nullptr;

    FramebufferPtr m_copyFrameBuffer        = nullptr;
    MaterialPtr m_copyMaterial              = nullptr;

    /** Passive pipeline state merged with each job's active state in Render(). Set by passes
     *  once per pass (or per group) instead of looping over every job. */
    RenderState m_passiveState;

    int m_maxArrayTextureLayers            = -1;

    // Dummy objects for draw commands.
    CubePtr m_dummyDrawCube                = nullptr;

    GpuProgramManager* m_gpuProgramManager = nullptr;

    IGraphicsBackend* m_backend            = nullptr;

    /** Per-frame draw counters keyed by framebuffer ObjectId. */
    std::unordered_map<ObjectId, int> m_drawnFrameBufferStats;
  };

} // namespace ToolKit
