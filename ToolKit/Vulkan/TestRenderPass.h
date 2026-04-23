/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Pass.h"

namespace ToolKit
{

  struct TestRenderPassParams
  {
    FramebufferPtr FrameBuffer = nullptr;
    Vec4 clearColor            = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
  };

  class TK_API TestRenderPass : public Pass
  {
   public:
    TestRenderPass();

    void Render() override;
    void PreRender() override;
    void PostRender() override;

   public:
    TestRenderPassParams m_params;
  };

  typedef std::shared_ptr<TestRenderPass> TestRenderPassPtr;

} // namespace ToolKit