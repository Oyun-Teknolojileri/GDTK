/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "GameRenderer.h"

#include "DebugNew.h"

namespace ToolKit
{

  GameRenderer::GameRenderer()
  {
    m_sceneRenderPath = MakeNewPtr<ForwardSceneRenderPath>();
    m_uiPass          = MakeNewPtr<ForwardRenderPass>();
    m_uiPass->SetName("UI Pass");

    m_gammaPass                             = MakeNewPtr<GammaTonemapFxaaPass>();
    m_gammaPass->m_params.enableTonemapping = false;
    m_gammaPass->m_params.enableFxaa        = false;
    m_gammaPass->SetName("Gamma Pass");
  }

  GameRenderer::~GameRenderer()
  {
    m_sceneRenderPath = nullptr;
    m_uiPass          = nullptr;
    m_gammaPass       = nullptr;
    m_presentTarget   = nullptr;
  }

  void GameRenderer::PreRender(Renderer* renderer)
  {
    RenderPath::PreRender(renderer);

    // Scene pass params
    m_sceneRenderPath->m_params.Cam                 = m_params.viewport->GetCamera();
    m_sceneRenderPath->m_params.MainFramebuffer     = m_params.viewport->m_framebuffer;
    m_sceneRenderPath->m_params.Scene               = m_params.scene;
    m_sceneRenderPath->m_params.postProcessSettings = m_params.postProcessSettings;

    // The last composite - tonemap, gamma, fxaa - is this renderer's present pass, which also scales the
    // frame up to the window. The scene path therefore leaves the hdr buffer as the bloom chain wrote it, so the
    // tone curve is applied once, after the scale. The settings object is not mutated here any more: the pass that
    // used to need gamma switched off no longer runs.
    m_sceneRenderPath->m_params.CompositeElsewhere  = true;

    // UI params
    UILayerPtrArray layers;
    m_uiRenderData.jobs.clear();
    GetUIManager()->GetLayers(m_params.viewport->m_viewportId, layers);

    for (const UILayerPtr& layer : layers)
    {
      const EntityPtrArray& uiNtties = layer->m_scene->GetEntities();

      EntityRawPtrArray rawUINtties  = ToEntityRawPtrArray(uiNtties);
      RenderJobProcessor::CreateRenderJobs(m_uiRenderData.jobs, rawUINtties);
    }

    RenderJobProcessor::SeperateRenderData(m_uiRenderData, true);

    m_uiPass->m_params.renderData               = &m_uiRenderData;
    m_uiPass->m_params.Cam                      = GetUIManager()->GetUICamera();
    m_uiPass->m_params.FrameBuffer              = m_params.viewport->m_framebuffer;
    m_uiPass->m_params.clearBuffer              = GraphicBitFields::DepthBits;

    // Gamma Pass.
    PostProcessingSettingsPtr pps               = m_params.postProcessSettings;
    m_gammaPass->m_params.enableGammaCorrection = GetRenderSystem()->IsGammaCorrectionNeeded();
    m_gammaPass->m_params.gamma                 = pps->GetGammaVal();

    // This pass is the composite, not a second gamma: the scene path leaves the hdr buffer alone (see
    // SceneRenderPathParams::CompositeElsewhere) and the tone curve, the gamma and the edges are applied here,
    // once, after the frame is scaled up to the window. The settings are the scene's own.
    m_gammaPass->m_params.enableTonemapping = pps->GetTonemappingEnabledVal();
    m_gammaPass->m_params.enableFxaa        = pps->GetFXAAEnabledVal();
    m_gammaPass->m_params.tonemapMethod     = pps->GetTonemapperModeVal().GetEnum<TonemapMethod>();
  }

  void GameRenderer::PostRender(Renderer* renderer) { RenderPath::PostRender(renderer); }

  void GameRenderer::PreparePresentTarget()
  {
    UVec2 size = GetRenderSystem()->GetAppWindowSize();
    if (size.x == 0 || size.y == 0)
    {
      // A window that reports no size yet still has to present something: the viewport's own target is
      // the closest thing to what the window will be.
      const FramebufferSettings& vpSettings = m_params.viewport->m_framebuffer->GetSettings();
      size                                  = UVec2((uint) vpSettings.width, (uint) vpSettings.height);
    }

    if (m_presentSize == size && m_presentTarget != nullptr && m_presentTarget->Initialized())
    {
      return;
    }

    if (m_presentTarget == nullptr)
    {
      m_presentTarget = MakeNewPtr<Framebuffer>("PresentTarget");
    }

    // No msaa and no depth: this is one quad over the finished image, nothing to resolve or test.
    m_presentTarget->ReconstructIfNeeded({(int) size.x, (int) size.y, false, false, MsaaSampleCount::x0});

    if (m_presentTarget->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0) == nullptr)
    {
      TextureSettings settings;
      settings.InternalFormat = GraphicTypes::FormatRGBA8;
      settings.Type           = GraphicTypes::TypeUnsignedByte;

      RenderTargetPtr rt      = MakeNewPtr<RenderTarget>((int) size.x, (int) size.y, settings, "PresentTargetRT");
      rt->Init();
      m_presentTarget->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, rt);
    }

    m_presentSize = size;
  }

  void GameRenderer::SetParams(const GameRendererParams& gameRendererParams) { m_params = gameRendererParams; }

  void GameRenderer::Render(Renderer* renderer)
  {
    PreRender(renderer);

    if (m_params.scene == nullptr || m_params.viewport == nullptr)
    {
      return;
    }

    // Scene renderer
    SceneRenderPathPtr sceneRenderer = m_sceneRenderPath;
    sceneRenderer->Render(renderer);

    m_passArray.clear();

    // Draw UI on top of scene.
    FramebufferPtr mainBuffer = m_params.viewport->m_framebuffer;
    if (!m_uiPass->m_params.renderData->jobs.empty())
    {
      m_uiPass->m_params.resolveFrameBuffer = nullptr;

      // Draw resolved framebuffer on to msaa buffer if needed.
      using Attachment                      = Framebuffer::Attachment;
      if (mainBuffer->IsMultiSampled() && sceneRenderer->m_resolvedFramebuffer)
      {
        RenderTargetPtr rRT = sceneRenderer->m_resolvedFramebuffer->GetColorAttachment(Attachment::ColorAttachment0);
        RenderTargetPtr mRT = mainBuffer->GetColorAttachment(Attachment::ColorAttachment0);
        renderer->CopyTexture(rRT, mRT);

        m_uiPass->m_params.resolveFrameBuffer = sceneRenderer->m_resolvedFramebuffer;
      }

      m_passArray.push_back(m_uiPass);
      RenderPath::Render(renderer);
      m_passArray.clear();
    }

    // Continue from resolved buffer if msaa is enabled.
    if (mainBuffer->IsMultiSampled() && sceneRenderer->m_resolvedFramebuffer)
    {
      mainBuffer = sceneRenderer->m_resolvedFramebuffer;
    }

    // Post processings. The composite reads the rendered target and writes the window sized present
    // target, so it scales the picture up on the way out: that is what puts a RenderResolutionScale
    // below 1 across the whole window instead of in its corner (half resolution used to leave a half
    // size picture in the bottom left of the screen), and it is also why the pass can sample the
    // rendered attachment directly - a pass cannot read the attachment it writes, so compositing in
    // place needs a copy of the whole target first.
    //
    // The pass runs even with every feature off, where it is a straight copy: that copy is the scale
    // up, and the blit below cannot do it - a blit copies at its source's size.
    PreparePresentTarget();

    m_gammaPass->m_params.sourceFrameBuffer = mainBuffer;
    m_gammaPass->m_params.frameBuffer       = m_presentTarget;
    m_gammaPass->m_params.screenSize        = Vec2((float) m_presentSize.x, (float) m_presentSize.y);
    m_passArray.push_back(m_gammaPass);
    RenderPath::Render(renderer);
    m_passArray.clear();

    // The window is filled by a copy and not by a draw. On the reference device a frame that is drawn
    // into the window's back buffer can reach the display before the draw that fills it has landed, and
    // the display then shows a buffer from an older frame; a copy never did, in the two camera test of
    // PerformanceDemo/PerfAnalysis/stage3-two-camera-report.md. See gdtk-overview.md, 8.3.
    renderer->CopyFrameBuffer(m_presentTarget, nullptr, GraphicBitFields::ColorBits);

    PostRender(renderer);
  }

} // namespace ToolKit
