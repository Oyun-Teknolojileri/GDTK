/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "TestRenderPass.h"

#include "Renderer.h"
#include "Stats.h"

#include "DebugNew.h"

namespace ToolKit
{

  TestRenderPass::TestRenderPass() : Pass("TestRenderPass") {}

  void TestRenderPass::Render()
  {
    // Nothing to draw yet, the pass just clears the framebuffer.
  }

  void TestRenderPass::PreRender()
  {
    Pass::PreRender();

    Renderer* renderer = GetRenderer();
    renderer->SetFramebuffer(m_params.FrameBuffer, GraphicBitFields::AllBits, m_params.clearColor);
  }

  void TestRenderPass::PostRender()
  {
    Pass::PostRender();

    Renderer* renderer = GetRenderer();
    renderer->EndPass();
  }
} // namespace ToolKit