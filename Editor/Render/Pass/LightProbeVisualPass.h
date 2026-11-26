/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, please visit [otyazilim.com].
 */

#pragma once

/**
 * @file LightProbeVisualPass.h
 */

#include "EditorTypes.h"
#include "Pass.h"

namespace ToolKit
{
  namespace Editor
  {

    /**
     * Draws a light probe volume's sample points as one instanced draw.
     *
     * Every point is the same sphere, placed and scaled in the vertex shader from a table this pass
     * uploads, so a sixteen by eight by sixteen grid costs one draw call instead of two thousand.
     * The colour of a point is the irradiance it holds - the DC term of its spherical harmonics,
     * which is the ambient light arriving there - drawn unlit, so the picture is the bake and not
     * the material.
     *
     * The table travels in the material's diffuse texture slot. That is the one sampler the
     * renderer binds per job by itself, and it is the only way in for a pass that draws through
     * Renderer::Render: a texture bound before it is wiped by the pipeline bind on Vulkan.
     */
    class TK_EDITOR_API LightProbeVisualPass : public Pass
    {
     public:
      LightProbeVisualPass();

      void Render() override;
      void PreRender() override;
      void PostRender() override;

      struct Params
      {
        CameraPtr Cam              = nullptr;
        FramebufferPtr FrameBuffer = nullptr;

        /**
         * Two entries per point: .xyz the world position and .w the radius of the marker, then
         * .rgb its colour. The points are already in world space; the draw uses an identity
         * transform and the vertex shader places every instance from this table.
         */
        std::vector<Vec4> Samples;
      } m_params;

     private:
      /** Rebuilds the table when the point count no longer fits it, uploads the points otherwise. */
      void UpdateTable();

     private:
      MaterialPtr m_material = nullptr;
      MeshPtr m_sphereMesh   = nullptr;
      DataTexturePtr m_table = nullptr;

      /** Row padded copy of the samples; nothing may read past the size the texture was made for. */
      std::vector<float> m_upload;
      uint m_pointCount      = 0;

      /** Passive state for the draw: the markers are solid geometry among the editor objects. */
      RenderState m_passState;
    };

    typedef std::shared_ptr<LightProbeVisualPass> LightProbeVisualPassPtr;

  } // namespace Editor
} // namespace ToolKit
