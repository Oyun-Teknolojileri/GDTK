/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "ForwardSceneRenderPath.h"
#include "GammaTonemapFxaaPass.h"
#include "Scene.h"
#include "UIManager.h"
#include "Viewport.h"

namespace ToolKit
{

  struct GameRendererParams
  {
    GameRendererParams()
    {
      scene               = nullptr;
      viewport            = nullptr;
      postProcessSettings = MakeNewPtr<PostProcessingSettings>();
    }

    ViewportPtr viewport = nullptr;
    ScenePtr scene       = nullptr;
    PostProcessingSettingsPtr postProcessSettings;
  };

  /** This class is being used in games to render. */
  class TK_API GameRenderer : public RenderPath
  {
   public:
    GameRenderer();
    virtual ~GameRenderer();

    void SetParams(const GameRendererParams& gameRendererParams);
    void Render(Renderer* renderer) override;

    /**
     * Framebuffer the last Render() composited into, before the engine copies it to the
     * back buffer. Hosts that read the frame back (the benchmark, a thumbnail renderer)
     * read this one: back buffer content is undefined when the window is occluded or
     * covered, which reads back as a blank or stale frame.
     */
    FramebufferPtr GetOutputFramebuffer() const { return m_outputFramebuffer; }

   private:
    void PreRender(Renderer* renderer) override;
    void PostRender(Renderer* renderer) override;

   private:
    GameRendererParams m_params;

    SceneRenderPathPtr m_sceneRenderPath = nullptr;
    ForwardRenderPassPtr m_uiPass        = nullptr;
    GammaTonemapFxaaPassPtr m_gammaPass  = nullptr;

    RenderJobArray m_uiRenderJobs;
    RenderData m_uiRenderData;

    /** Last framebuffer Render() composited into; see GetOutputFramebuffer(). */
    FramebufferPtr m_outputFramebuffer = nullptr;
  };

} // namespace ToolKit
