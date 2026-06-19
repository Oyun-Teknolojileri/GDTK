/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "BloomPass.h"

#include "Shader.h"
#include "Stats.h"
#include "ToolKit.h"

#include <DebugNew.h>

namespace ToolKit
{

  BloomPass::BloomPass() : Pass("BloomPass")
  {
    m_downsampleShader = GetShaderManager()->Create<Shader>(ShaderPath("bloomDownsample.shader", true));
    m_upsampleShader   = GetShaderManager()->Create<Shader>(ShaderPath("bloomUpsample.shader", true));

    // Two full-quad passes — one for the downsample chain, one for the upsample chain.
    // Each binds its own fragment shader in PreRender (not the constructor — the
    // backend isn't alive yet, so GetRenderer() returns null and SetFragmentShader
    // would crash). After that the program attached to m_downPass is always the
    // downsample program and m_upPass always carries the upsample program. Sharing
    // one quad across both phases let downsample state leak into the upsample draw
    // under the refactor.
    m_downPass         = MakeNewPtr<FullQuadPass>();
    m_upPass           = MakeNewPtr<FullQuadPass>();

    // Cache initialization
    m_cachedMainRes    = UVec2(0, 0);
    m_cachedIterCount  = -1;
    m_resourcesValid   = false;
  }

  void BloomPass::Render()
  {
    TK_PROFILE_FUNCTION();

    RenderTargetPtr mainRt = m_params.FrameBuffer->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);

    if (m_invalidRenderParams)
    {
      return;
    }

    UVec2 mainRes      = UVec2(mainRt->m_width, mainRt->m_height);

    Renderer* renderer = GetRenderer();

    // Lazy-init the pass UBO on first render   backend is alive by now.
    if (!m_passDataBufferInitialized)
    {
      m_passDataBuffer.Init(7);
      m_passDataBufferInitialized = true;
    }

    // Helper: stamp the relevant UBO fields and push to GPU. Each iteration calls this with
    // freshly computed values so the shader sees per-pass data.
    auto pushUbo = [&]()
    {
      m_passDataBuffer.Invalidate();
      m_passDataBuffer.Map();

      // Re-stage slot 7's UBO so the descriptor set points at this buffer when the
      // quad's draw records. Without this, prior passes (GradientSky, SSAO, DoF,
      // GammaTonemapFxaa) sharing slot 7 leave the Vulkan descriptor pointing at
      // their own VkBuffer, and bloom reads garbage.
      renderer->BindUniformBuffer(7, &m_passDataBuffer.GetBuffer());
    };

    // Filter pass
    {
      m_passDataBuffer.m_data.passIndxAndPad.x = 0;
      m_passDataBuffer.m_data.downsampleParams =
          Vec4((float) mainRes.x, (float) mainRes.y, m_params.minThreshold, 0.0f);
      pushUbo();

      renderer->SetTexture((ubyte) 0, mainRt);
      m_downPass->m_params.frameBuffer      = m_resampleFrameBuffers[0];
      m_downPass->m_params.blendFunc        = BlendFunction::NONE;
      m_downPass->m_params.clearFrameBuffer = GraphicBitFields::None;

      RenderSubPass(m_downPass);
    }

    // Downsample Pass
    {
      for (int i = 0; i < m_currentIterationCount; i++)
      {
        // Calculate current and previous resolutions

        float powVal = glm::pow(2.0f, float(i + 1));
        const Vec2 factor(1.0f / powVal);
        const UVec2 curRes             = Vec2(mainRes) * factor;

        powVal                         = glm::pow(2.0f, float(i));
        const Vec2 prevRes             = Vec2(mainRes) * Vec2((1.0f / powVal));

        // Find previous framebuffer & RT
        FramebufferPtr prevFramebuffer = m_resampleFrameBuffers[i];
        RenderTargetPtr prevRt         = prevFramebuffer->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);

        m_passDataBuffer.m_data.passIndxAndPad.x = i + 1;
        m_passDataBuffer.m_data.downsampleParams =
            Vec4(prevRes.x, prevRes.y, m_passDataBuffer.m_data.downsampleParams.z, 0.0f);
        pushUbo();

        renderer->SetTexture((ubyte) 0, prevRt);

        // Set pass parameters
        m_downPass->m_params.clearFrameBuffer = GraphicBitFields::None;
        m_downPass->m_params.frameBuffer      = m_resampleFrameBuffers[i + 1];
        m_downPass->m_params.blendFunc        = BlendFunction::NONE;

        RenderSubPass(m_downPass);
      }
    }

    // Upsample Pass
    {
      const float filterRadius               = 0.002f;
      m_passDataBuffer.m_data.upsampleParams = Vec4(filterRadius, 1.0f, 0.0f, 0.0f);
      pushUbo();

      for (int i = m_currentIterationCount; i > 0; i--)
      {

        FramebufferPtr prevFramebuffer = m_resampleFrameBuffers[i];
        RenderTargetPtr prevRt         = prevFramebuffer->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);
        renderer->SetTexture((ubyte) 0, prevRt);

        m_upPass->m_params.blendFunc        = BlendFunction::ONE_TO_ONE;
        m_upPass->m_params.clearFrameBuffer = GraphicBitFields::None;
        m_upPass->m_params.frameBuffer      = m_resampleFrameBuffers[i - 1];

        RenderSubPass(m_upPass);
      }
    }

    // Merge Pass
    {
      FramebufferPtr prevFramebuffer = m_resampleFrameBuffers[0];
      RenderTargetPtr prevRt         = prevFramebuffer->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);
      renderer->SetTexture((ubyte) 0, prevRt);

      m_upPass->m_params.blendFunc               = BlendFunction::ONE_TO_ONE;
      m_upPass->m_params.clearFrameBuffer        = GraphicBitFields::None;
      m_upPass->m_params.frameBuffer             = m_params.FrameBuffer;

      // Final merge uses the user-set intensity instead of 1.0.
      m_passDataBuffer.m_data.upsampleParams.y = m_params.intensity;
      pushUbo();

      RenderSubPass(m_upPass);
    }
  }

  void BloomPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    // Pin each quad to its fragment shader exactly once — the backend is now alive.
    // After this point, m_downPass and m_upPass carry their respective programs
    // and never swap them, so the program can never accidentally drift between
    // downsample and upsample draws.
    if (!m_fragmentsPinned)
    {
      Renderer* pinRenderer = GetRenderer();
      m_downPass->SetFragmentShader(m_downsampleShader, pinRenderer);
      m_upPass->SetFragmentShader(m_upsampleShader, pinRenderer);
      m_fragmentsPinned = true;
    }

    RenderTargetPtr mainRt = m_params.FrameBuffer->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);

    // Set to minimum iteration count
    Vec2 mainRes           = UVec2(mainRt->m_width, mainRt->m_height);
    const IVec2 maxIterCounts(glm::log2(mainRes) - 1.0f);
    int iterationCount = glm::min(m_params.iterationCount, glm::min(maxIterCounts.x, maxIterCounts.y));

    if (iterationCount < 0)
    {
      m_invalidRenderParams = true;
      return;
    }

    // Check if we need to recreate resources
    bool needsRecreation =
        (m_cachedMainRes != UVec2(mainRes)) || (m_cachedIterCount != iterationCount) || !m_resourcesValid;

    if (needsRecreation)
    {
      m_resampleRenderTargets.resize(iterationCount + 1);
      m_resampleFrameBuffers.resize(iterationCount + 1);

      for (int i = 0; i < iterationCount + 1; i++)
      {
        const Vec2 factor(1.0f / glm::pow(2.0f, float(i)));
        const UVec2 curRes    = Vec2(mainRes) * factor;

        m_invalidRenderParams = false;
        if (curRes.x == 1 || curRes.y == 1)
        {
          m_invalidRenderParams = true;
          return;
        }

        TextureSettings set;
        set.InternalFormat = GraphicTypes::FormatRGBA16F;
        set.Format         = GraphicTypes::FormatRGBA;
        set.Type           = GraphicTypes::TypeFloat;
        set.MagFilter      = GraphicTypes::SampleLinear;
        set.MinFilter      = GraphicTypes::SampleLinear;
        set.WarpR          = GraphicTypes::UVClampToEdge;
        set.WarpS          = GraphicTypes::UVClampToEdge;
        set.WarpT          = GraphicTypes::UVClampToEdge;
        set.GenerateMipMap = false;

        RenderTargetPtr rt = MakeNewPtr<RenderTarget>(curRes.x, curRes.y, set);
        rt->Init();

        FramebufferPtr& frameBuffer = m_resampleFrameBuffers[i];
        if (frameBuffer == nullptr)
        {
          FramebufferSettings fbSettings;
          fbSettings.depthStencil    = false;
          fbSettings.useDefaultDepth = false;
          fbSettings.width           = curRes.x;
          fbSettings.height          = curRes.y;

          frameBuffer                = MakeNewPtr<Framebuffer>(fbSettings, "BloomDownSampleFB");
          frameBuffer->Init();
        }

        frameBuffer->ReconstructIfNeeded(curRes.x, curRes.y);
        frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, rt);
      }

      // Update cache
      m_cachedMainRes   = UVec2(mainRes);
      m_cachedIterCount = iterationCount;
      m_resourcesValid  = true;
    }

    m_currentIterationCount = iterationCount;
  }

  void BloomPass::PostRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PostRender();
  }

} // namespace ToolKit