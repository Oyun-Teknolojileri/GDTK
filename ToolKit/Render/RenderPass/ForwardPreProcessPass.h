/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "ForwardPass.h"
#include "Material.h"
#include "Pass.h"
#include "Texture.h"

namespace ToolKit
{
  /**
   * This pass creates a frame buffer that contains normal of the scene, depth in the view space.
   * If this pass exist, the depth of the scene is constructed and upcoming passes uses the existing
   * depth buffer without write to enable early z kill in the fragment shader.
   **/
  class TK_API ForwardPreProcessPass : public Pass
  {
   public:
    ForwardPreProcessPass();

    void InitBuffers(int width, int height, MsaaSampleCount sampleCount);
    void Render() override;
    void PreRender() override;
    void PostRender() override;

   public:
    ForwardRenderPassParams m_params;
    MaterialPtr m_linearMaterial        = nullptr;
    FramebufferPtr m_framebuffer        = nullptr;
    FramebufferPtr m_resolveFramebuffer = nullptr;

    RenderTargetPtr m_normalDepthRt     = nullptr;

    /** Pass-owned passive RenderState. Forces depth test/write on and FuncLess so a material
     *  with depth-write off cannot corrupt the prepass normal/depth output. */
    RenderState m_passState;

   private:
    /**
     * Draws a run of jobs as instanced batches.
     *
     * This pass writes normals and linear depth only, so the material does not take part in the
     * draw: for opaque jobs the batch key is just the mesh and the cull side, which is what lets
     * a few hundred draws replace thousands.
     */
    void RenderBatches(RenderJobItr begin, RenderJobItr end, bool masked);

    /** Instanced variant of the prepass program and the table its transforms live in. */
    GpuProgramPtr m_instancedProgram = nullptr;
    DataTexturePtr m_instanceTable    = nullptr;
    std::vector<float> m_instanceData;

    /** Must match INSTANCE_TEX_WIDTH and INSTANCE_TEXELS_PER_ITEM in instanceDataInc.shader. */
    static constexpr int InstanceTexWidth      = 256;
    static constexpr int InstanceTexelsPerItem = 8;
  };

  typedef std::shared_ptr<ForwardPreProcessPass> ForwardPreProcessPassPtr;

} // namespace ToolKit
