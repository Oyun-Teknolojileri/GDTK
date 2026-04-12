/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Camera.h"
#include "GenericBuffers.h"
#include "GpuProgram.h"
#include "Material.h"
#include "Primative.h"
#include "RHI.h"
#include "RenderState.h"
#include "Sky.h"
#include "Types.h"
#include "Viewport.h"

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

    /** x: intensity, y: fadeDistance, z: iblMode (0=both,1=specOnly,2=diffOnly), w: pccEnabled */
    Vec4 vol0Params;
    Vec4 vol0Min; /**< xyz: volume min (local space) */
    Vec4 vol0Max; /**< xyz: volume max (local space) */
    Vec4 vol0InvTransform0, vol0InvTransform1, vol0InvTransform2, vol0InvTransform3;
    Vec4 vol0WorldTransform0, vol0WorldTransform1, vol0WorldTransform2, vol0WorldTransform3;

    // --- Volume 1 / Secondary (11 Vec4) ---

    /** x: intensity, y: fadeDistance, z: iblMode (0=both,1=specOnly,2=diffOnly), w: pccEnabled */
    Vec4 vol1Params;
    Vec4 vol1Min; /**< xyz: volume min (local space) */
    Vec4 vol1Max; /**< xyz: volume max (local space) */
    Vec4 vol1InvTransform0, vol1InvTransform1, vol1InvTransform2, vol1InvTransform3;
    Vec4 vol1WorldTransform0, vol1WorldTransform1, vol1WorldTransform2, vol1WorldTransform3;

    // --- Global setters ---

    void SetIblInUse(bool inUse) { global0.x = inUse ? 1.0f : 0.0f; }
    void SetAmbientOcclusionInUse(bool inUse) { global0.y = inUse ? 1.0f : 0.0f; }

    void SetActivePointLightCount(int count) { global1.x = (float) count; }
    void SetActiveSpotLightCount(int count) { global1.y = (float) count; }
    void SetActiveDirectionalLightCount(int count) { global1.z = (float) count; }

    // --- Per-volume setters ---

    void SetVolumeIntensity(int vol, float intensity)
    {
      Params(vol).x = intensity;
    }

    void SetVolumeFadeDistance(int vol, float fade)
    {
      Params(vol).y = fade;
    }

    void SetVolumeIblMode(int vol, float mode)
    {
      Params(vol).z = mode;
    }

    void SetVolumePccEnabled(int vol, bool enabled)
    {
      Params(vol).w = enabled ? 1.0f : 0.0f;
    }

    void SetVolumeMin(int vol, const Vec3& minVal)
    {
      Min(vol) = Vec4(minVal, 0.0f);
    }

    void SetVolumeMax(int vol, const Vec3& maxVal)
    {
      Max(vol) = Vec4(maxVal, 0.0f);
    }

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
    Vec4& Min(int vol)    { return vol == 0 ? vol0Min : vol1Min; }
    Vec4& Max(int vol)    { return vol == 0 ? vol0Max : vol1Max; }

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

  typedef GpuBufferBase<GraphicConstatsDataLayout, 4> GraphicConstantsGpuBuffer;

  // GlobalGpuBuffers
  //////////////////////////////////////////

  struct GlobalGpuBuffers
  {
    /** Uniform buffer for camera data. */
    CameraGpuBuffer cameraGpuBuffer;
    int cameraBufferId = 0;

    /** Uniform buffer for graphic constants. */
    GraphicConstantsGpuBuffer graphicConstantBuffer;
    int graphicConstantBufferId = 0;

    /** Active directional lights in gpu. */
    DirectionalLightBuffer directionalLightBuffer;
    int directionalLightBufferId    = 0;
    int directionalLightPVMBufferId = 0;

    /** Cached point lights in gpu. */
    PointLightCache pointLighBuffer;
    int pointLightBufferId = 0;

    /** Cached spot lights in gpu. */
    SpotLightCache spotLightBuffer;
    int spotLightBufferId = 0;

    void InitGlobalGpuBuffers()
    {
      graphicConstantBuffer.Init();
      graphicConstantBufferId = graphicConstantBuffer.Id();

      cameraGpuBuffer.Init();
      cameraBufferId = cameraGpuBuffer.Id();

      directionalLightBuffer.Init();
      directionalLightBufferId    = directionalLightBuffer.m_lightDataBuffer.m_id;
      directionalLightPVMBufferId = directionalLightBuffer.m_pvms.m_id;

      pointLighBuffer.Init();
      pointLightBufferId = pointLighBuffer.m_gpuBuffer.m_id;

      spotLightBuffer.Init();
      spotLightBufferId = spotLightBuffer.m_gpuBuffer.m_id;
    }
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
    void SetRenderState(const RenderState* const state, bool cullFlip = false);

    void SetStencilOperation(StencilOperation op);

    void StartTimerQuery();
    void EndTimerQuery();

    /** Returns elapsed time between start - end time query in milliseconds.*/
    void GetElapsedTime(float& cpu, float& gpu);

    void ClearColorBuffer(const Vec4& color);
    void ClearBuffer(GraphicBitFields fields, const Vec4& value = Vec4(0.0f));
    void ColorMask(bool r, bool g, bool b, bool a);

    // FrameBuffer Operations
    //////////////////////////////////////////

    FramebufferPtr GetFrameBuffer();

    void SetFramebuffer(FramebufferPtr frameBuffer,
                        GraphicBitFields attachmentsToClear,
                        const Vec4& clearColor                  = Vec4(0.0f),
                        GraphicFramebufferTypes frameBufferType = GraphicFramebufferTypes::Framebuffer);

    /** Tries to invalidate given bits of the framebuffer. Verifies if buffer actually has the specified attachments. */
    void InvalidateFramebuffer(GraphicBitFields bits, FramebufferPtr frameBuffer);

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
     * After the operation sets the previous frame buffer back.
     */
    void CopyTexture(TexturePtr src, TexturePtr dst);

    //////////////////////////////////////////

    void SetViewport(Viewport* viewport);
    void SetViewportRect(uint x, uint y, uint width, uint height);

    void DrawFullQuad(ShaderPtr fragmentShader);
    void DrawFullQuad(MaterialPtr mat);
    void DrawCube(CameraPtr cam, MaterialPtr mat, const Mat4& transform = Mat4(1.0f));

    void SetTexture(ubyte slotIndx, TexturePtr texture);

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
     * Renders the scene into a cubemap from the given position using the provided render path.
     * @param renderPath The render path to use for rendering each face.
     * @param position World position to render from.
     * @param near Near clip plane.
     * @param far Far clip plane.
     * @param resolution Resolution of each cubemap face.
     * @return The generated cubemap.
     */
    CubeMapPtr RenderToCubeMap(ForwardSceneRenderPath* renderPath,
                               const Vec3& position,
                               float near,
                               float far,
                               uint resolution,
                               const float* perFaceClipDist = nullptr);

    /**
     * Sets the blend state directly which causes by passing material system.
     * @param enableOverride when set true, disables the material system setting blend state per material.
     * @param func is the BlendFunction to use.
     */
    void OverrideBlendState(bool enableOverride, BlendFunction func);

    void EnableBlending(bool enable);
    void EnableDepthWrite(bool enable);
    void EnableDepthTest(bool enable);
    void SetDepthTestFunc(CompareFunctions func);
    bool EnableDepthClamp(bool enable);

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
     * Sets directional lights to be used for render. Should be called once per pass because all objects effected from
     * directional lights. No need to set it per object.
     */
    void SetDirectionalLights(const LightRawPtrArray& lights);

   private:
    /** Set textures to be used in render. SkyBox, Ibl, AmbientOcculution  */
    void SetDataTextures(const RenderJob& job);

    /** Sets the current model and derived transforms to be used in shader. */
    void SetTransforms(const Mat4& model);

    void FeedUniforms(const GpuProgramPtr& program, const RenderJob& job);
    void FeedAnimationUniforms(const GpuProgramPtr& program, const RenderJob& job);

    /** Validates sRGB automatic encoding on backbuffer by clearing and reading a pixel back. */
    void ValidateBackbufferSrgbEncoding();

   public:
    uint m_frameCount = 0;
    UVec2 m_windowSize; //!< Application window size.
    Vec4 m_clearColor         = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CameraPtr m_uiCamera      = nullptr;
    SkyBasePtr m_sky          = nullptr;

    ShadingMode m_shadingMode = ShadingMode::None;

    /** Global gpu buffers for renderer. */
    GlobalGpuBuffers* m_globalGpuBuffers;

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

    FramebufferPtr m_framebuffer  = nullptr;
    TexturePtr m_shadowAtlas      = nullptr;
    RenderTargetPtr m_brdfLut     = nullptr;
    TexturePtr m_aoTexture        = nullptr;

    std::array<int, RHIConstants::TextureSlotCount> m_textureSlots;

    RenderState m_renderState;

    /** Current viewport size (x,y) and position (z,w) */
    UVec4 m_viewportRect;

    /*
     * This framebuffer can ONLY have 1 color attachment and no other attachments.
     * This way, we can use without needing to resize or reInit.
     */
    FramebufferPtr m_oneColorAttachmentFramebuffer = nullptr;
    MaterialPtr m_gaussianBlurMaterial             = nullptr;
    MaterialPtr m_averageBlurMaterial              = nullptr;
    QuadPtr m_tempQuad                             = nullptr;
    MaterialPtr m_tempQuadMaterial                 = nullptr;

    FramebufferPtr m_copyFrameBuffer               = nullptr;
    MaterialPtr m_copyMaterial                     = nullptr;

    int m_maxArrayTextureLayers                    = -1;

    // Dummy objects for draw commands.
    CubePtr m_dummyDrawCube                        = nullptr;

    GpuProgramManager* m_gpuProgramManager         = nullptr;

    uint m_gpuTimerQuery                           = 0;
    float m_cpuTime                                = 1.0f;
    float m_gpuTime                                = 1.0f;
    bool m_timerQueryActive                        = false;
    bool m_timerQueryWaiting                       = false;
    bool m_blendStateOverrideEnable                = false;

    /** Frame buffer stats for each frame. */
    std::map<uint, int> m_drawnFrameBufferStats;
  };

} // namespace ToolKit
