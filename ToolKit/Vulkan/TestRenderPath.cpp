/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "TestRenderPath.h"

#include "Framebuffer.h"
#include "Renderer.h"

#include "DebugNew.h"

namespace ToolKit
{

  TestRenderPath::TestRenderPath() { m_testPass = MakeNewPtr<TestRenderPass>(); }

  TestRenderPath::~TestRenderPath() { m_testPass = nullptr; }

  void TestRenderPath::Render(Renderer* renderer)
  {
    PreRender(renderer);

    m_passArray.clear();
    m_passArray.push_back(m_testPass);

    RenderPath::Render(renderer);

    // Resolve MSAA if needed so the viewport can display the result.
    if (m_framebuffer != nullptr && m_framebuffer->IsMultiSampled())
    {
      if (m_resolveFramebuffer == nullptr)
      {
        m_resolveFramebuffer = MakeNewPtr<Framebuffer>("TestResolve");
      }

      FramebufferSettings settings = m_framebuffer->GetSettings();
      settings.msaaCount           = MsaaSampleCount::x0;
      m_resolveFramebuffer->ReconstructIfNeeded(settings);
      renderer->ResolveFramebuffer(m_framebuffer, m_resolveFramebuffer, {0});
      renderer->FinishPass();
    }

    PostRender(renderer);
  }

  void TestRenderPath::PreRender(Renderer* renderer)
  {
    RenderPath::PreRender(renderer);

    m_testPass->m_params.FrameBuffer = m_framebuffer;
    m_testPass->m_params.clearColor  = m_clearColor;
  }

  void TestRenderPath::PostRender(Renderer* renderer) { RenderPath::PostRender(renderer); }

} // namespace ToolKit