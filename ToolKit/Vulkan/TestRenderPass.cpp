/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "TestRenderPass.h"

#include "Renderer.h"
#include "Stats.h"

#ifdef TK_VULKAN
  #include "VulkanBackend.h"
#endif

#include "DebugNew.h"

namespace ToolKit
{

  TestRenderPass::TestRenderPass() : Pass("TestRenderPass") {}

  void TestRenderPass::Render()
  {
#ifdef TK_VULKAN
    // Stage 2b scaffold: emit the fullscreen test triangle into the active offscreen pass that
    // PreRender opened. Removed in Stage 3 once mesh draws replace it.
    if (auto* backend = static_cast<VulkanBackend*>(GetRenderer()->GetBackend()))
    {
      backend->DrawTestTriangle();
    }
#endif
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
    renderer->FinishPass();
  }
} // namespace ToolKit