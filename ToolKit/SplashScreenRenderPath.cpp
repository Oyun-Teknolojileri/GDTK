#include "SplashScreenRenderPath.h"

#include "DebugNew.h"

namespace ToolKit
{

  SplashScreenRenderPath::SplashScreenRenderPath() {}

  SplashScreenRenderPath::~SplashScreenRenderPath()
  {
    if (UIManager* uiMan = GetUIManager())
    {
      uiMan->UnRegisterViewport(m_viewport);
    }
  }

  void SplashScreenRenderPath::Init(UVec2 screenSize)
  {
    m_uiPass              = MakeNewPtr<ForwardRenderPass>();
    // TODO(erendgrmnc): Gamma pass removed - causes white texture on macOS
    // m_gammaPass           = MakeNewPtr<GammaTonemapFxaaPass>();
    m_viewport            = MakeNewPtr<GameViewport>((float) screenSize.x, (float) screenSize.y);
    m_splashScreen        = MakeNewPtr<UILayer>(LayerPath("ToolKit/splash-screen.layer"));
    m_resolvedFramebuffer = MakeNewPtr<Framebuffer>();

    if (UIManager* uiMan = GetUIManager())
    {
      uiMan->RegisterViewport(m_viewport);
      uiMan->AddLayer(m_viewport->m_viewportId, m_splashScreen);
      m_uiPass->m_params.Cam = uiMan->GetUICamera();
    }

    m_uiPass->m_params.FrameBuffer = m_viewport->m_framebuffer;

    // TODO(erendgrmnc): Gamma pass removed - causes white texture on macOS
    // m_gammaPass = MakeNewPtr<GammaTonemapFxaaPass>();
    // m_gammaPass->m_params.enableGammaCorrection = GetRenderSystem()->IsGammaCorrectionNeeded();
    // m_gammaPass->m_params.enableTonemapping = false;
    // m_gammaPass->m_params.enableFxaa = false;
    // m_gammaPass->m_params.screenSize = Vec2((float) screenSize.x, (float) screenSize.y);
    // m_gammaPass->m_params.frameBuffer = m_viewport->m_framebuffer;
  }

  void SplashScreenRenderPath::PreRender(Renderer* renderer)
  {
    RenderPath::PreRender(renderer);
    renderer->SetFramebuffer(m_viewport->m_framebuffer, GraphicBitFields::AllBits);

    EntityRawPtrArray rawEntities = ToEntityRawPtrArray(m_splashScreen->m_scene->GetEntities());
    RenderJobProcessor::CreateRenderJobs(m_uiRenderData.jobs, rawEntities);
    RenderJobProcessor::SeperateRenderData(m_uiRenderData, true);
    m_uiPass->m_params.renderData         = &m_uiRenderData;
    m_uiPass->m_params.clearBuffer        = GraphicBitFields::AllBits;
    m_uiPass->m_params.FrameBuffer        = m_viewport->m_framebuffer;
    m_uiPass->m_params.resolveFrameBuffer = nullptr;

    if (m_viewport->m_framebuffer->IsMultiSampled())
    {
      FramebufferSettings settings = m_viewport->m_framebuffer->GetSettings();
      settings.msaaCount           = 1;

      m_resolvedFramebuffer->ReconstructIfNeeded(settings);
      m_uiPass->m_params.resolveFrameBuffer = m_resolvedFramebuffer;
      // TODO(erendgrmnc): Gamma pass removed - causes white texture on macOS
      // m_gammaPass->m_params.frameBuffer     = m_resolvedFramebuffer;
    }

    m_passArray.clear();
    m_passArray.push_back(m_uiPass);
    // TODO(erendgrmnc): Gamma pass removed - causes white texture on macOS
    // if (m_gammaPass->IsEnabled())
    // {
    //   m_passArray.push_back(m_gammaPass);
    // }
  }

  void SplashScreenRenderPath::Render(Renderer* renderer)
  {
    PreRender(renderer);
    RenderPath::Render(renderer);
    PostRender(renderer);
  }

  void SplashScreenRenderPath::PostRender(Renderer* renderer)
  {
    // 1. Handle Multisampling
    FramebufferPtr srcBuffer = m_viewport->m_framebuffer;
    if (m_viewport->m_framebuffer->IsMultiSampled())
    {
      srcBuffer = m_resolvedFramebuffer;
    }
    // 2. Calculate Center
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    int windowWidth                       = viewport[2];
    int windowHeight                      = viewport[3];
    const FramebufferSettings& fbSettings = srcBuffer->GetSettings(); // Use srcBuffer instead of m_viewport
    int offsetX                           = (windowWidth - (int) fbSettings.width) / 2;
    int offsetY                           = (windowHeight - (int) fbSettings.height) / 2;
    // 3. Manual Blit
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcBuffer->GetFboId());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,
                      0,
                      fbSettings.width,
                      fbSettings.height,
                      offsetX,
                      offsetY,
                      offsetX + fbSettings.width,
                      offsetY + fbSettings.height,
                      GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);

    RenderPath::PostRender(renderer);
  }

} // namespace ToolKit