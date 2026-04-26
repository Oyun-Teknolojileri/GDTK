/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Pass.h"

#include <functional>

namespace ToolKit
{

  struct ForwardRenderPassParams
  {
    RenderData* renderData            = nullptr;
    CameraPtr Cam                     = nullptr;
    FramebufferPtr FrameBuffer        = nullptr;
    RenderTargetPtr SsaoTexture       = nullptr;
    GraphicBitFields clearBuffer      = GraphicBitFields::AllBits;
    bool hasForwardPrePass            = false;
    uint activeDirectionalLightCount  = 0;
    FramebufferPtr resolveFrameBuffer = nullptr;
    bool invalidateDepthBuffer        = true;

    /** Invoked at the end of PreRender, after shadow/ssao/sky passes have run. Pass-specific UBOs
        (slot 5) may have been re-bound by those earlier passes (e.g. ShadowPass uses gauss blur);
        any draw inside this forward pass that owns its own slot-5 UBO must Map() it here so the
        slot is restored before the actual draw. Currently the editor grid uses this to refresh
        its GridPassData buffer right before the forward render pass renders the grid entity. */
    std::function<void()> onPreRender;
  };

  /** Renders given entities with given lights using forward rendering. */
  class TK_API ForwardRenderPass : public Pass
  {
   public:
    ForwardRenderPass();

    void Render() override;
    void PreRender() override;
    void PostRender() override;

   protected:
    void RenderOpaque(RenderData* renderData);
    void RenderTranslucent(RenderData* renderData);

    void RenderOpaqueHelper(RenderData* renderData,
                            RenderJobItr begin,
                            RenderJobItr end,
                            GpuProgramPtr defaultGpuProgram);

    void ConfigureProgram();

   public:
    ForwardRenderPassParams m_params;

   private:
    int m_shadowPCF = 0;

    MaterialPtr m_programConfigMat;
  };

  typedef std::shared_ptr<ForwardRenderPass> ForwardRenderPassPtr;

} // namespace ToolKit