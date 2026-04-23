/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "RenderSystem.h"
#include "Vulkan/TestRenderPass.h"

namespace ToolKit
{

  class TK_API TestRenderPath : public RenderPath
  {
   public:
    TestRenderPath();
    ~TestRenderPath() override;

    void Render(Renderer* renderer) override;
    void PreRender(Renderer* renderer) override;
    void PostRender(Renderer* renderer) override;

   public:
    FramebufferPtr m_framebuffer = nullptr;
    Vec4 m_clearColor            = Vec4(0.4f, 0.2f, 0.6f, 1.0f);

   private:
    TestRenderPassPtr m_testPass        = nullptr;
    FramebufferPtr m_resolveFramebuffer = nullptr;
  };

} // namespace ToolKit