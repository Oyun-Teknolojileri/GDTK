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
    EngineSettings& settings = GetEngineSettings();
    float dpiScale           = settings.m_window->GetDpiScaleVal();

    m_uiPass              = MakeNewPtr<ForwardRenderPass>();
    m_gammaPass           = MakeNewPtr<GammaTonemapFxaaPass>();
    m_viewport            = MakeNewPtr<GameViewport>((float) screenSize.x * dpiScale, (float) screenSize.y * dpiScale);
    m_splashScreen        = MakeNewPtr<UILayer>(LayerPath("ToolKit/splash-screen.layer"));
    m_resolvedFramebuffer = MakeNewPtr<Framebuffer>();

    if (UIManager* uiMan = GetUIManager())
    {
      uiMan->RegisterViewport(m_viewport);
      uiMan->AddLayer(m_viewport->m_viewportId, m_splashScreen);
      m_uiPass->m_params.Cam = uiMan->GetUICamera();
    }

    m_gammaPass->m_params.enableGammaCorrection = GetRenderSystem()->IsGammaCorrectionNeeded();
    m_gammaPass->m_params.enableTonemapping     = false;
    m_gammaPass->m_params.enableFxaa            = false;
    m_gammaPass->m_params.screenSize            = Vec2((float) screenSize.x, (float) screenSize.y) * dpiScale;
    m_gammaPass->m_params.frameBuffer           = m_viewport->m_framebuffer;
  }

  void SplashScreenRenderPath::PreRender(Renderer* renderer)
  {
    RenderPath::PreRender(renderer);

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
      settings.msaaCount           = MsaaSampleCount::x0;

      m_resolvedFramebuffer->ReconstructIfNeeded(settings);
      m_uiPass->m_params.resolveFrameBuffer = m_resolvedFramebuffer;
      m_gammaPass->m_params.frameBuffer     = m_resolvedFramebuffer;
    }

    m_passArray.clear();
    m_passArray.push_back(m_uiPass);
    if (m_gammaPass->IsEnabled())
    {
      m_passArray.push_back(m_gammaPass);
    }
  }

  void SplashScreenRenderPath::Render(Renderer* renderer)
  {
    PreRender(renderer);
    RenderPath::Render(renderer);
    PostRender(renderer);
  }

  void SplashScreenRenderPath::PostRender(Renderer* renderer)
  {
    FramebufferPtr srcBuffer = m_viewport->m_framebuffer;
    if (m_viewport->m_framebuffer->IsMultiSampled())
    {
      srcBuffer = m_resolvedFramebuffer;
    }
    else
    {
      srcBuffer = m_viewport->m_framebuffer;
    }

    renderer->CopyFrameBuffer(srcBuffer, nullptr, GraphicBitFields::ColorBits);

    RenderPath::PostRender(renderer);
  }

} // namespace ToolKit