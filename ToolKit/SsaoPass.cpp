/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "SsaoPass.h"

#include "Camera.h"
#include "Material.h"
#include "MathUtil.h"
#include "Mesh.h"
#include "RHI.h"
#include "Shader.h"
#include "Stats.h"
#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{

  // SSAOPass
  //////////////////////////////////////////

  SSAOPass::SSAOPass() : Pass("SSAOPass")
  {
    m_ssaoFramebuffer = MakeNewPtr<Framebuffer>("SSAOPassFB");
    m_blurFramebuffer = MakeNewPtr<Framebuffer>("SSAOBlurFB");
    m_rawSsaoRt       = MakeNewPtr<RenderTarget>("SSAORawRT");
    m_ssaoTexture     = MakeNewPtr<RenderTarget>("SSAORT");
    m_quadPass        = MakeNewPtr<FullQuadPass>();
    m_blurPass        = MakeNewPtr<FullQuadPass>();

    m_ssaoShader      = GetShaderManager()->Create<Shader>(ShaderPath("ssaoCalcFrag.shader", true));
    m_blurShader      = GetShaderManager()->Create<Shader>(ShaderPath("ssaoBlurFrag.shader", true));
  }

  SSAOPass::SSAOPass(const SSAOPassParams& params) : SSAOPass() { m_params = params; }

  SSAOPass::~SSAOPass()
  {
    m_ssaoFramebuffer = nullptr;
    m_blurFramebuffer = nullptr;
    m_rawSsaoRt       = nullptr;
    m_quadPass        = nullptr;
    m_blurPass        = nullptr;
    m_ssaoShader      = nullptr;
    m_blurShader      = nullptr;
  }

  void SSAOPass::Render()
  {
    TK_PROFILE_FUNCTION();

    Renderer* renderer           = GetRenderer();

    // Use resolved texture if multisampled, otherwise use the original render target.
    TexturePtr normalDepthBuffer = m_params.GNormalDepthBuffer;
    if (normalDepthBuffer->IsMultiSampled())
    {
      normalDepthBuffer = m_params.GNormalDepthBuffer->GetResolvedTexture();
    }

    // Generate SSAO texture. calc + blur share GL slot 7 (pass-specific UBO convention) —
    // Map each one immediately before its draw so the slot has the right buffer at consume time.
    renderer->SetTexture("s_normalDepth", normalDepthBuffer);

    m_calcPassDataBuffer.Invalidate();
    m_calcPassDataBuffer.Map();
    RenderSubPass(m_quadPass);

    // Single-pass bilinear 5x5 blur (reads raw SSAO, writes to m_ssaoTexture)
    renderer->SetTexture("s_diffuseColor", m_rawSsaoRt);

    m_blurPassDataBuffer.Invalidate();
    m_blurPassDataBuffer.Map();
    RenderSubPass(m_blurPass);
  }

  void SSAOPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    // Use resolved texture if multisampled to get correct dimensions.
    TexturePtr normalDepthBuffer = m_params.GNormalDepthBuffer->IsMultiSampled()
                                       ? m_params.GNormalDepthBuffer->GetResolvedTexture()
                                       : m_params.GNormalDepthBuffer;

    int fullWidth                = normalDepthBuffer->m_width;
    int fullHeight               = normalDepthBuffer->m_height;

    // Optionally render SSAO at half resolution for performance.
    int renderWidth              = m_params.HalfResolution ? glm::max(1, fullWidth / 2) : fullWidth;
    int renderHeight             = m_params.HalfResolution ? glm::max(1, fullHeight / 2) : fullHeight;

    // Clamp kernel size to valid values (8, 16 or 32).
    if (m_params.KernelSize <= 8)
    {
      m_params.KernelSize = 8;
    }
    else if (m_params.KernelSize <= 16)
    {
      m_params.KernelSize = 16;
    }
    else
    {
      m_params.KernelSize = 32;
    }

    GenerateSSAONoise();

    // No need destroy and re init framebuffer when size is changed, because
    // the only render target is already being resized.
    m_ssaoFramebuffer->ReconstructIfNeeded({renderWidth, renderHeight, false, false});

    TextureSettings oneChannelSet;
    oneChannelSet.WarpS          = GraphicTypes::UVClampToEdge;
    oneChannelSet.WarpT          = GraphicTypes::UVClampToEdge;
    oneChannelSet.InternalFormat = GraphicTypes::FormatR8;
    oneChannelSet.Format         = GraphicTypes::FormatRed;
    oneChannelSet.Type           = GraphicTypes::TypeUnsignedByte;
    oneChannelSet.MinFilter      = GraphicTypes::SampleLinear;
    oneChannelSet.MagFilter      = GraphicTypes::SampleLinear;
    oneChannelSet.GenerateMipMap = false;

    // Init raw SSAO texture (bilinear filtering required for blur pass)
    m_rawSsaoRt->Settings(oneChannelSet);
    m_rawSsaoRt->ReconstructIfNeeded(renderWidth, renderHeight);

    m_ssaoFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_rawSsaoRt);

    // Init blurred output render target (consumed by forward pass)
    TextureSettings blurOutSet = oneChannelSet;
    blurOutSet.MinFilter       = GraphicTypes::SampleNearest;
    blurOutSet.MagFilter       = GraphicTypes::SampleNearest;
    m_ssaoTexture->Settings(blurOutSet);
    m_ssaoTexture->ReconstructIfNeeded(renderWidth, renderHeight);

    m_blurFramebuffer->ReconstructIfNeeded({renderWidth, renderHeight, false, false});
    m_blurFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_ssaoTexture);

    // Set shader define for kernel size when it changes.
    if (m_params.KernelSize != m_currentKernelSize)
    {
      m_ssaoShader->Init();
      m_ssaoShader->SetDefine("KERNEL_SIZE", std::to_string(m_params.KernelSize));
    }

    m_quadPass->m_params.frameBuffer      = m_ssaoFramebuffer;
    m_quadPass->m_params.clearFrameBuffer = GraphicBitFields::None;

    m_quadPass->SetFragmentShader(m_ssaoShader, GetRenderer());

    if (!m_calcPassDataBufferInitialized)
    {
      m_calcPassDataBuffer.Init(7);
      m_calcPassDataBufferInitialized = true;
    }

    if (m_params.KernelSize != m_currentKernelSize || m_prevSpread != m_params.spread)
    {
      for (int i = 0; i < m_params.KernelSize; i++)
      {
        m_calcPassDataBuffer.m_data.samples[i] = Vec4(m_ssaoKernel[i], 0.0f);
      }
      m_prevSpread = m_params.spread;
    }

    const Mat4& proj                              = m_params.Cam->GetProjectionMatrix();
    m_calcPassDataBuffer.m_data.inverseProjection = glm::inverse(proj);

    // Precompute projection params.
    // projParams = (P00, P11, P20, P21)
    // clip.x = P00*x_view + P20*z_view, clip.y = P11*y_view + P21*z_view, w_clip = -z_view
    m_calcPassDataBuffer.m_data.projParams        = Vec4(proj[0][0], proj[1][1], proj[2][0], proj[2][1]);

    // Stored as Mat4 (std140 mat3 padding would be 48 bytes anyway); shader extracts mat3().
    m_calcPassDataBuffer.m_data.normalToView      = Mat4(Mat3(m_params.Cam->GetViewMatrix()));
    m_calcPassDataBuffer.m_data.radiusBiasAndPad  = Vec4(m_params.Radius, m_params.Bias, 0.0f, 0.0f);

    // Setup blur pass
    m_blurPass->m_params.frameBuffer              = m_blurFramebuffer;
    m_blurPass->m_params.clearFrameBuffer         = GraphicBitFields::None;

    m_blurPass->SetFragmentShader(m_blurShader, GetRenderer());

    if (!m_blurPassDataBufferInitialized)
    {
      m_blurPassDataBuffer.Init(7);
      m_blurPassDataBufferInitialized = true;
    }
    m_blurPassDataBuffer.m_data.texelSizeAndPad = Vec4(1.0f / renderWidth, 1.0f / renderHeight, 0.0f, 0.0f);
  }

  void SSAOPass::PostRender()
  {
    TK_PROFILE_FUNCTION();

    m_currentKernelSize = m_params.KernelSize;
    Pass::PostRender();
  }

  void SSAOPass::GenerateSSAONoise()
  {
    if (m_prevSpread != m_params.spread)
    {
      GenerateRandomSamplesInHemisphere(m_maximumKernelSize, m_params.spread, m_ssaoKernel);
    }
  }

} // namespace ToolKit