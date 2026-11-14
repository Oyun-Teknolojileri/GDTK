/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file VelocityPass.h Header for VelocityPass
 */

#include "Pass.h"

#include <unordered_map>

namespace ToolKit
{

  /**
   * Writes the screen motion of the objects that moved since the last frame.
   *
   * The pass draws the same geometry twice: once with the transform the job carries this frame and once
   * with the transform it had a frame ago. Both draws use the camera of this frame, so the difference
   * between the two targets is the motion the object itself made, with the camera motion factored out -
   * which is exactly what a reflection needs to follow the thing it shows instead of the surface it
   * lands on.
   *
   * Nothing is drawn for a job whose transform matches the cache, and both targets are cleared every
   * frame, so a static object reads as zero motion.
   */
  class TK_API VelocityPass : public Pass
  {
   public:
    VelocityPass();
    virtual ~VelocityPass();

    void PreRender() override;
    void Render() override;

    /** Screen position of this frame's geometry, in UV. */
    TexturePtr GetCurrentVelocity() const;
    /** Screen position of the previous frame's geometry, in UV. */
    TexturePtr GetPreviousVelocity() const;

   public:
    struct Params
    {
      const RenderJobArray* RenderJobs = nullptr;
      UVec2 RenderSize                 = UVec2(0, 0);
    } m_params;

   private:
    RenderTargetPtr m_currentRt    = nullptr;
    RenderTargetPtr m_previousRt   = nullptr;
    FramebufferPtr m_currentFb     = nullptr;
    FramebufferPtr m_previousFb    = nullptr;
    MaterialPtr m_material         = nullptr;
    ShaderPtr m_vertexShader       = nullptr;
    ShaderPtr m_fragmentShader     = nullptr;
    RenderJobArray m_currentJobs;
    RenderJobArray m_previousJobs;
    std::unordered_map<Entity*, Mat4> m_previousTransforms;
  };

  typedef std::shared_ptr<VelocityPass> VelocityPassPtr;

} // namespace ToolKit
