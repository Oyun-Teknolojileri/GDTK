/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Pass.h"
#include "Texture.h"

#include <vector>

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
        (slot 7) may have been re-bound by those earlier passes (e.g. ShadowPass uses gauss blur);
        any draw inside this forward pass that owns its own pass-specific UBO (slot 7) must Map() it here so the
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

    /**
     * Builds the per object and the instanced program from the defines the configuration material carries
     * at this moment. Both are used for the same job range - opaque and alpha masked each draw through the
     * two - so they have to be built together, per range, or the batched draws of a range run with another
     * range's fragment defines.
     * @param perObjectProgram receives the program the per object path draws with.
     */
    void BuildPrograms(GpuProgramPtr& perObjectProgram);

    /**
     * Draws a run of opaque jobs as instanced batches.
     *
     * Jobs are already sorted by material, so consecutive jobs with the same mesh, material,
     * light set and environment volumes become one draw with their transforms in the instance
     * table. Everything else falls back to the per object path.
     */
    void RenderOpaqueBatches(RenderData* renderData,
                             RenderJobItr begin,
                             RenderJobItr end,
                             GpuProgramPtr defaultGpuProgram);

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

    /** Pass-owned passive RenderState for the opaque + alpha-masked path. */
    RenderState m_opaquePassState;

    /** Pass-owned passive RenderState for the translucent path. */
    RenderState m_translucentPassState;

    /** Instanced variant of the forward program, built from a copy of the config material's
     *  vertex shader with INSTANCE_TABLE set, and the table its transforms live in. */
    GpuProgramPtr m_instancedProgram  = nullptr;
    ShaderPtr m_instancedVertexShader = nullptr;
    DataTexturePtr m_instanceTable    = nullptr;
    std::vector<float> m_instanceData;

    /** Must match INSTANCE_TEX_WIDTH and INSTANCE_TEXELS_PER_ITEM in instanceDataInc.shader. */
    static constexpr int InstanceTexWidth      = 256;
    static constexpr int InstanceTexelsPerItem = 8;
  };

  typedef std::shared_ptr<ForwardRenderPass> ForwardRenderPassPtr;

} // namespace ToolKit