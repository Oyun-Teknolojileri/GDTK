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

  // DrawCommand
  //////////////////////////////////////////

  struct DrawCommand
  {
    /** x: iblIntensity, y: iblInUse, z: ambientOcclusionInUse, w: pad0 */
    Vec4 data1;

    /** x: activePointLightCount, y: activeSpotLightCount, z: activeDirectionalLightCount, w: pad1 */
    Vec4 data2;

    void SetIblIntensity(float intensity) { data1.x = intensity; }

    void SetIblInUse(bool inUse) { data1.y = inUse ? 1.0f : 0.0f; }

    void SetAmbientOcclusionInUse(bool inUse) { data1.z = inUse ? 1.0f : 0.0f; }

    void SetActivePointLightCount(int count) { data2.x = (float) count; }

    void SetActiveSpotLightCount(int count) { data2.y = (float) count; }

    void SetActiveDirectionalLightCount(int count) { data2.z = (float) count; }
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

    void SetFramebuffer(FramebufferPtr fb,
                        GraphicBitFields attachmentsToClear,
                        const Vec4& clearColor         = Vec4(0.0f),
                        GraphicFramebufferTypes fbType = GraphicFramebufferTypes::Framebuffer);

    void InvalidateFramebufferDepth(FramebufferPtr frameBuffer);
    void InvalidateFramebufferStencil(FramebufferPtr frameBuffer);
    void InvalidateFramebufferDepthStencil(FramebufferPtr frameBuffer);

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
    void SetViewportSize(uint width, uint height);
    void SetViewportSize(uint x, uint y, uint width, uint height);

    void DrawFullQuad(ShaderPtr fragmentShader);
    void DrawFullQuad(MaterialPtr mat);
    void DrawCube(CameraPtr cam, MaterialPtr mat, const Mat4& transform = Mat4(1.0f));

    void SetTexture(ubyte slotIndx, uint textureId);

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
     * Sets the blend state directly which causes by passing material system.
     * @param enableOverride when set true, disables the material system setting blend state per material.
     * @param func is the BlendFunction to use.
     */
    void OverrideBlendState(bool enableOverride, BlendFunction func);

    void EnableBlending(bool enable);
    void EnableDepthWrite(bool enable);
    void EnableDepthTest(bool enable);
    void SetDepthTestFunc(CompareFunctions func);

    // Giving nullptr as argument means no shadows
    void SetShadowAtlas(TexturePtr shadowAtlas);

    void Render(const struct RenderJob& job);
    void Render(const RenderJobArray& jobs);

    void RenderWithProgramFromMaterial(const RenderJobArray& jobs);
    void RenderWithProgramFromMaterial(const RenderJob& job);

    /** Apply one tap of gauss blur via setting a temporary frame buffer. Does not reset frame buffer back. */
    void Apply7x1GaussianBlur(const TexturePtr src, RenderTargetPtr dst, const Vec3& axis, const float amount);
    /** Apply one tap of average blur via setting a temporary frame buffer. Does not reset frame buffer back. */
    void ApplyAverageBlur(const TexturePtr src, RenderTargetPtr dst, const Vec3& axis, const float amount);

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

   public:
    uint m_frameCount = 0;
    UVec2 m_windowSize; //!< Application window size.
    Vec4 m_clearColor         = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CameraPtr m_uiCamera      = nullptr;
    SkyBasePtr m_sky          = nullptr;

    bool m_renderOnlyLighting = false;

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

    // Draw data
    std::array<int, RHIConstants::MaxPointLightPerObject> m_activePointLightIndices;
    std::array<int, RHIConstants::MaxSpotLightPerObject> m_activeSpotLightIndices;
    DrawCommand m_drawCommand;

    int m_activePointLightCount   = 0;
    int m_activeSpotLightCount    = 0;
    bool m_ambientOcculusionInUse = false;
    bool m_normalMapInUse         = false;

    FramebufferPtr m_framebuffer  = nullptr;
    TexturePtr m_shadowAtlas      = nullptr;
    RenderTargetPtr m_brdfLut     = nullptr;
    TexturePtr m_aoTexture        = nullptr;

    std::array<int, RHIConstants::TextureSlotCount> m_textureSlots;

    RenderState m_renderState;

    UVec2 m_viewportSize; //!< Current viewport size.

    /*
     * This framebuffer can ONLY have 1 color attachment and no other attachments.
     * This way, we can use without needing to resize or reInit.
     */
    FramebufferPtr m_oneColorAttachmentFramebuffer = nullptr;
    MaterialPtr m_gaussianBlurMaterial             = nullptr;
    MaterialPtr m_averageBlurMaterial              = nullptr;
    QuadPtr m_tempQuad                             = nullptr;
    MaterialPtr m_tempQuadMaterial                 = nullptr;

    FramebufferPtr m_copyFb                        = nullptr;
    MaterialPtr m_copyMaterial                     = nullptr;

    int m_maxArrayTextureLayers                    = -1;

    // Dummy objects for draw commands.
    CubePtr m_dummyDrawCube                        = nullptr;

    GpuProgramManager* m_gpuProgramManager         = nullptr;

    uint m_gpuTimerQuery                           = 0;
    float m_cpuTime                                = 0.0f;
    bool m_blendStateOverrideEnable                = false;

    /** Frame buffer stats for each frame. */
    std::map<uint, int> m_drawnFrameBufferStats;
  };

} // namespace ToolKit
