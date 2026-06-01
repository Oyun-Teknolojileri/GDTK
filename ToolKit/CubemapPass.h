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

  struct CubeMapPassParams
  {
    FramebufferPtr FrameBuffer   = nullptr;
    CameraPtr Cam                = nullptr;
    MaterialPtr Material         = nullptr;
    GraphicBitFields clearBuffer = GraphicBitFields::AllBits;
    Mat4 Transform;

    /** Invoked at the end of PreRender (after shadow/preProcess/ssao have run). Lets a sky owner
        Map its pass-specific UBO (slot 7) so the sky cubemap draw reads its own data instead of whatever pass
        last touched that slot (typically gauss blur from ShadowPass). Used by GradientSky. */
    std::function<void()> onPreRender;
  };

  class TK_API CubeMapPass : public Pass
  {
   public:
    CubeMapPass();

    void Render() override;
    void PreRender() override;
    void PostRender() override;

   public:
    CubeMapPassParams m_params;

   private:
    CubePtr m_cube = nullptr;

    /** Pass-owned passive RenderState. Forces FuncLequal so the skybox cube's far-plane
     *  fragments survive the depth test against the cleared depth buffer. */
    RenderState m_passState;
  };

  typedef std::shared_ptr<CubeMapPass> CubeMapPassPtr;

} // namespace ToolKit