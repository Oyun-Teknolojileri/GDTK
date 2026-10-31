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

   private:
    void PreRender(Renderer* renderer) override;
    void PostRender(Renderer* renderer) override;

    /** Creates or resizes the window sized, 8 bit frame the composite writes, see Render. */
    void PreparePresentTarget();

   private:
    GameRendererParams m_params;

    SceneRenderPathPtr m_sceneRenderPath = nullptr;
    ForwardRenderPassPtr m_uiPass        = nullptr;
    GammaTonemapFxaaPassPtr m_gammaPass  = nullptr;

    /**
     * Window sized, 8 bit frame that the composite writes and that the present blit copies out.
     *
     * Sized to the window rather than to the render target, because those differ as soon as
     * RenderResolutionScale is below 1: the composite samples the smaller rendered target across this
     * one, so the scale up happens on the way out. Copying the render target straight into the window
     * instead - which is what this replaces - copies at the render target's size and so leaves a reduced
     * picture in the corner of the screen (half resolution put a half size frame in the bottom left).
     *
     * Writing a separate target also lets the composite sample the rendered attachment directly: an
     * in-place composite cannot read the attachment it writes and copies the whole target first.
     */
    FramebufferPtr m_presentTarget       = nullptr;
    UVec2 m_presentSize                  = UVec2(0, 0);

    RenderJobArray m_uiRenderJobs;
    RenderData m_uiRenderData;
  };

} // namespace ToolKit
