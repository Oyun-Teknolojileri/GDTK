/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "BloomPass.h"

#include "Shader.h"
#include "Stats.h"
#include "ToolKit.h"

#include <DebugNew.h>

namespace ToolKit
{

  namespace
  {
    /** Levels the composite can weight. The pyramid always carries this many, because every level
        is a halving of the one below and level zero is a quarter of the frame. */
    constexpr int BloomLevelCount = 6;

    /**
     * Tightness the radius can reach.
     *
     * A level's weight is exp(-tightness * t * t) where t runs from 0 at the tight level to 1 at the
     * widest one. At the full radius tightness is zero and every level weighs one, which is the flat
     * sum the composite always had; at zero the widest level weighs exp(-8), so the energy sits almost
     * entirely on the levels against the source. The curve is smooth on purpose: switching a level on
     * and off at a boundary is what made the halo pop when the level count moved.
     */
    constexpr float BloomRadiusMaxTightness = 8.0f;

    /** Weight of every glow level for the given reach and radius. The sum is what the composite
        divides by, so the radius changes the shape of the halo and not how bright it is. */
    void GlowWeights(int levelCount, float radius, float weights[BloomLevelCount])
    {
      const float tightness = (1.0f - glm::clamp(radius, 0.0f, 1.0f)) * BloomRadiusMaxTightness;

      for (int i = 0; i < BloomLevelCount; ++i)
      {
        if (i >= levelCount)
        {
          weights[i] = 0.0f;
          continue;
        }

        const float t = levelCount > 1 ? float(i) / float(levelCount - 1) : 0.0f;
        weights[i]    = glm::exp(-tightness * t * t);
      }
    }
  } // namespace

  BloomPass::BloomPass() : Pass("BloomPass")
  {
    m_glowShader      = GetShaderManager()->Create<Shader>(ShaderPath("bloomGlow.shader", true));
    m_compositeShader = GetShaderManager()->Create<Shader>(ShaderPath("bloomComposite.shader", true));
    m_addShader       = GetShaderManager()->Create<Shader>(ShaderPath("bloomAdd.shader", true));

    m_glowPass        = MakeNewPtr<FullQuadPass>();
    m_compositePass   = MakeNewPtr<FullQuadPass>();
    m_addPass         = MakeNewPtr<FullQuadPass>();

    m_glowRt          = MakeNewPtr<RenderTarget>("BloomGlowRT");
    m_glowFb          = MakeNewPtr<Framebuffer>("BloomGlowFB");

    m_gatherRt        = MakeNewPtr<RenderTarget>("BloomGatherRT");
    m_gatherFb        = MakeNewPtr<Framebuffer>("BloomGatherFB");
  }

  void BloomPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    if (m_params.FrameBuffer == nullptr)
    {
      return;
    }

    RenderTargetPtr src = m_params.FrameBuffer->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);
    if (src == nullptr)
    {
      return;
    }

    if (!m_passDataBufferInitialized)
    {
      m_passDataBuffer.Init(7);
      m_passDataBufferInitialized = true;
    }

    // Level zero is a quarter of the frame in each axis. The glow wants the broad shape of what is
    // bright, not its detail, and the bright pass averages a 4x4 block of the frame into each
    // texel here - so the detail is folded in once, by the pass, instead of being sampled and
    // missed by the chain.
    const UVec2 glowRes(glm::max(1, src->m_width / 4), glm::max(1, src->m_height / 4));

    if (glowRes != m_cachedGlowRes || m_glowFb->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0) == nullptr)
    {
      // Half float so a highlight above one survives into the glow, and a mip chain so level i is
      // the box average of level i-1 without a pass per level.
      TextureSettings set;
      set.InternalFormat = GraphicTypes::FormatRGBA16F;
      set.Format         = GraphicTypes::FormatRGBA;
      set.Type           = GraphicTypes::TypeFloat;
      set.MagFilter      = GraphicTypes::SampleLinear;
      set.MinFilter      = GraphicTypes::SampleLinearMipmapLinear;
      set.WarpS          = GraphicTypes::UVClampToEdge;
      set.WarpT          = GraphicTypes::UVClampToEdge;
      set.GenerateMipMap = true;

      m_glowRt->ReconstructIfNeeded(glowRes.x, glowRes.y, &set);

      m_glowFb->ReconstructIfNeeded({(int) glowRes.x, (int) glowRes.y, false, false});
      m_glowFb->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_glowRt);
      m_cachedGlowRes = glowRes;
    }

    // The weighted sum is a low frequency signal: level zero already carries one texel per 4x4 block of
    // the frame, so sampling it once per frame pixel was four times the fetch work for detail the halo
    // does not have. Half the frame is still twice the density of level zero, and the one tap that adds
    // it back to the frame is bilinear, so the halo loses no visible shape.
    const UVec2 gatherRes(glm::max(1, src->m_width / 2), glm::max(1, src->m_height / 2));

    if (gatherRes != m_cachedGatherRes || m_gatherFb->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0) == nullptr)
    {
      // Half float as well: the weighted sum carries the highlight energy the bright pass kept.
      TextureSettings set;
      set.InternalFormat = GraphicTypes::FormatRGBA16F;
      set.Format         = GraphicTypes::FormatRGBA;
      set.Type           = GraphicTypes::TypeFloat;
      set.MagFilter      = GraphicTypes::SampleLinear;
      set.MinFilter      = GraphicTypes::SampleLinear;
      set.WarpS          = GraphicTypes::UVClampToEdge;
      set.WarpT          = GraphicTypes::UVClampToEdge;
      set.GenerateMipMap = false;

      m_gatherRt->ReconstructIfNeeded(gatherRes.x, gatherRes.y, &set);

      m_gatherFb->ReconstructIfNeeded({(int) gatherRes.x, (int) gatherRes.y, false, false});
      m_gatherFb->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_gatherRt);
      m_cachedGatherRes = gatherRes;
    }

    const int levels = glm::clamp(m_params.iterationCount, 1, BloomLevelCount);
    float weights[BloomLevelCount];
    GlowWeights(levels, m_params.radius, weights);

    float weightSum = 0.0f;
    for (int i = 0; i < BloomLevelCount; ++i)
    {
      weightSum += weights[i];
    }

    m_passDataBuffer.m_data.params    = Vec4(m_params.minThreshold, m_params.luminanceCap, 0.0f, 0.0f);
    // The sum is divided by the total weight, so Intensity means the same thing whatever the reach and
    // the radius are: the radius would otherwise dim the halo as it makes it tighter, and every extra
    // level would brighten it, which is what a project that widened the bloom used to get a blowout for.
    m_passDataBuffer.m_data.composite = Vec4(glm::max(m_params.intensity, 0.0f),
                                             weightSum > 0.0f ? 1.0f / weightSum : 0.0f,
                                             1.0f / float(glowRes.x),
                                             1.0f / float(glowRes.y));
    m_passDataBuffer.m_data.weightsA  = Vec4(weights[0], weights[1], weights[2], weights[3]);
    m_passDataBuffer.m_data.weightsB  = Vec4(weights[4], weights[5], 0.0f, 0.0f);
    m_passDataBuffer.Invalidate();
    m_passDataBuffer.Map();

    // Picks the composite variant. A define rather than a uniform: the two filters fetch at different
    // offsets, so the choice has to reach the shader as a compile time constant. The shader is shared
    // with every other bloom pass, and they all read this one setting, so the last writer wins with the
    // value everybody wanted.
    m_compositeShader->SetDefine("BLOOM_BICUBIC", m_params.bicubicUpscale ? "1" : "0");

    m_glowPass->m_params.frameBuffer      = m_glowFb;
    m_glowPass->m_params.clearFrameBuffer = GraphicBitFields::None;
    m_glowPass->m_params.blendFunc        = BlendFunction::NONE;

    m_compositePass->m_params.frameBuffer      = m_gatherFb;
    m_compositePass->m_params.clearFrameBuffer = GraphicBitFields::None;
    m_compositePass->m_params.blendFunc        = BlendFunction::NONE;

    m_addPass->m_params.frameBuffer      = m_params.FrameBuffer;
    m_addPass->m_params.clearFrameBuffer = GraphicBitFields::None;
    m_addPass->m_params.blendFunc        = BlendFunction::ONE_TO_ONE;
  }

  void BloomPass::Render()
  {
    TK_PROFILE_FUNCTION();

    if (m_params.FrameBuffer == nullptr || m_cachedGlowRes == UVec2(0, 0) || m_cachedGatherRes == UVec2(0, 0))
    {
      return;
    }

    Renderer* renderer  = GetRenderer();
    RenderTargetPtr src = m_params.FrameBuffer->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);

    if (src == nullptr)
    {
      return;
    }

    // Level zero: the bright pass over the frame.
    m_glowPass->m_material->SetDiffuseTextureVal(src);
    m_glowPass->SetFragmentShader(m_glowShader, renderer);
    m_glowPass->GetRequirements().customUbos[7] = &m_passDataBuffer.GetBuffer();
    RenderSubPass(m_glowPass);

    // Every level above zero is the box average of the level below, and the driver builds the
    // whole chain from what the pass just wrote. Binding the destination first keeps the pyramid
    // texture out of the framebuffer it is attached to while it is read for the mip generation.
    renderer->SetFramebuffer(m_params.FrameBuffer, GraphicBitFields::None);
    m_glowRt->GenerateMipMaps();

    // Composite: the weighted sum of the levels, into the half resolution glow target.
    m_compositePass->m_material->SetDiffuseTextureVal(m_glowRt);
    m_compositePass->SetFragmentShader(m_compositeShader, renderer);

    // The program depends on the BLOOM_BICUBIC variant, and the quad keeps the program it built on the
    // first draw, so it is refreshed here instead of once. Without this the filter switch would keep
    // drawing the variant that was compiled first.
    m_compositePass->GetRequirements().program       = m_compositePass->GetProgram();
    m_compositePass->GetRequirements().customUbos[7] = &m_passDataBuffer.GetBuffer();

    RenderSubPass(m_compositePass);

    // Add: the glow target over the frame, one bilinear tap per pixel. Additive, and the shader writes
    // an alpha of zero so the frame's own alpha is left to the host.
    m_addPass->m_material->SetDiffuseTextureVal(m_gatherRt);
    m_addPass->SetFragmentShader(m_addShader, renderer);
    RenderSubPass(m_addPass);
  }

  void BloomPass::PostRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PostRender();
  }

} // namespace ToolKit
