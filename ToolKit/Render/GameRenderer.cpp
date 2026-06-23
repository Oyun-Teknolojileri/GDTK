/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
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
  }

  void GameRenderer::PreRender(Renderer* renderer)
  {
    RenderPath::PreRender(renderer);

    // Scene pass params
    m_sceneRenderPath->m_params.Cam                 = m_params.viewport->GetCamera();
    m_sceneRenderPath->m_params.MainFramebuffer     = m_params.viewport->m_framebuffer;
    m_sceneRenderPath->m_params.Scene               = m_params.scene;
    m_sceneRenderPath->m_params.postProcessSettings = m_params.postProcessSettings;

    // Post process pass. Gamma will be applied in the final pass.
    m_sceneRenderPath->m_params.postProcessSettings->SetGammaCorrectionEnabledVal(false);

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
    m_gammaPass->m_params.frameBuffer           = m_params.viewport->m_framebuffer;
    m_gammaPass->m_params.gamma                 = pps->GetGammaVal();
  }

  void GameRenderer::PostRender(Renderer* renderer) { RenderPath::PostRender(renderer); }

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

    // Post processings
    if (m_gammaPass->IsEnabled())
    {
      m_gammaPass->m_params.frameBuffer = mainBuffer;
      m_passArray.push_back(m_gammaPass);
    }

    RenderPath::Render(renderer);

    renderer->CopyFrameBuffer(mainBuffer, nullptr, GraphicBitFields::ColorBits);

    PostRender(renderer);
  }

} // namespace ToolKit
