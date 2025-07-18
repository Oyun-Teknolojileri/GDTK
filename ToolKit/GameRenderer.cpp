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
    m_sceneRenderPath      = MakeNewPtr<ForwardSceneRenderPath>();
    m_uiPass               = MakeNewPtr<ForwardRenderPass>();
    m_gammaTonemapFxaaPass = MakeNewPtr<GammaTonemapFxaaPass>();
    m_fullQuadPass         = MakeNewPtr<FullQuadPass>();
  }

  GameRenderer::~GameRenderer()
  {
    m_sceneRenderPath      = nullptr;
    m_uiPass               = nullptr;
    m_gammaTonemapFxaaPass = nullptr;
    m_fullQuadPass         = nullptr;
    m_quadUnlitMaterial    = nullptr;
  }

  void GameRenderer::PreRender(Renderer* renderer)
  {
    // Scene pass params
    m_sceneRenderPath->m_params.Cam                 = m_params.viewport->GetCamera();
    m_sceneRenderPath->m_params.MainFramebuffer     = m_params.viewport->m_framebuffer;
    m_sceneRenderPath->m_params.Scene               = m_params.scene;
    m_sceneRenderPath->m_params.postProcessSettings = m_params.postProcessSettings;

    // These post processings will be done after ui pass
    m_sceneRenderPath->m_params.postProcessSettings->SetGammaCorrectionEnabledVal(false);
    m_sceneRenderPath->m_params.postProcessSettings->SetTonemappingEnabledVal(false);
    m_sceneRenderPath->m_params.postProcessSettings->SetFXAAEnabledVal(false);

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

    m_uiPass->m_params.renderData                          = &m_uiRenderData;
    m_uiPass->m_params.Cam                                 = GetUIManager()->GetUICamera();
    m_uiPass->m_params.FrameBuffer                         = m_params.viewport->m_framebuffer;
    m_uiPass->m_params.clearBuffer                         = GraphicBitFields::DepthBits;

    // Post Process Pass
    PostProcessingSettingsPtr pps                          = m_params.postProcessSettings;
    m_gammaTonemapFxaaPass->m_params.enableGammaCorrection = GetRenderSystem()->IsGammaCorrectionNeeded();

    m_gammaTonemapFxaaPass->m_params.enableFxaa            = pps->GetFXAAEnabledVal();
    m_gammaTonemapFxaaPass->m_params.enableTonemapping     = pps->GetTonemappingEnabledVal();
    m_gammaTonemapFxaaPass->m_params.frameBuffer           = m_params.viewport->m_framebuffer;
    m_gammaTonemapFxaaPass->m_params.tonemapMethod         = pps->GetTonemapperModeVal().GetEnum<TonemapMethod>();
    m_gammaTonemapFxaaPass->m_params.gamma                 = pps->GetGammaVal();
    m_gammaTonemapFxaaPass->m_params.screenSize            = m_params.viewport->m_wndContentAreaSize;

    // Full quad pass
    m_fullQuadPass->m_params.frameBuffer                   = nullptr; // backbuffer
    m_fullQuadPass->m_params.clearFrameBuffer              = GraphicBitFields::AllBits;

    if (m_quadUnlitMaterial == nullptr)
    {
      m_quadUnlitMaterial = GetMaterialManager()->GetCopyOfUnlitMaterial(false);
      ShaderPtr vert      = GetShaderManager()->Create<Shader>(ShaderPath("fullQuadVert.shader", true));
      m_quadUnlitMaterial->SetVertexShaderVal(vert);
    }

    ViewportPtr viewport = m_params.viewport;
    RenderTargetPtr atc  = viewport->m_framebuffer->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);
    m_quadUnlitMaterial->SetDiffuseTextureVal(Cast<Texture>(atc));
  }

  void GameRenderer::PostRender(Renderer* renderer) { renderer->ResetUsedTextureSlots(); }

  void GameRenderer::SetParams(const GameRendererParams& gameRendererParams) { m_params = gameRendererParams; }

  void GameRenderer::Render(Renderer* renderer)
  {
    if (m_params.scene == nullptr || m_params.viewport == nullptr)
    {
      return;
    }

    PreRender(renderer);

    // Scene renderer
    SceneRenderPathPtr sceneRenderer = m_sceneRenderPath;
    sceneRenderer->Render(renderer);

    m_passArray.clear();

    // UI render pass
    m_passArray.push_back(m_uiPass);

    // Post processings
    if (m_gammaTonemapFxaaPass->IsEnabled())
    {
      m_passArray.push_back(m_gammaTonemapFxaaPass);
    }

    m_fullQuadPass->m_material = m_quadUnlitMaterial;
    m_passArray.push_back(m_fullQuadPass);

    RenderPath::Render(renderer);

    PostRender(renderer);
  }

} // namespace ToolKit
