/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "PreviewViewport.h"

#include "EditorRenderer.h"
#include "EditorScene.h"

#include <DirectionComponent.h>

namespace ToolKit
{
  namespace Editor
  {

    TKDefineClass(PreviewViewport, EditorViewport);

    PreviewViewport::PreviewViewport()
    {
      m_previewRenderer                           = MakeNewPtr<ForwardSceneRenderPath>();
      m_previewRenderer->m_params.Cam             = GetCamera();
      m_previewRenderer->m_params.MainFramebuffer = m_framebuffer;

      // Post process settings.
      m_previewRenderer->m_params.postProcessSettings->SetFXAAEnabledVal(false);
      m_previewRenderer->m_params.postProcessSettings->SetTonemappingEnabledVal(false);

      if (!GetRenderSystem()->m_backbufferFormatIsSRGB)
      {
        // If a linear space back buffer is used, we need to apply gamma after imgui, so skip it in scene render path.
        m_previewRenderer->m_params.postProcessSettings->SetGammaCorrectionEnabledVal(false);
      }
    }

    PreviewViewport::~PreviewViewport() { m_previewRenderer = nullptr; }

    void PreviewViewport::Show()
    {
      HandleStates();
      DrawCommands();

      m_previewRenderer->m_params.MainFramebuffer = m_framebuffer;
      GetRenderSystem()->AddRenderTask({[this](Renderer* renderer) -> void { m_previewRenderer->Render(renderer); }});

      // Render color attachment as rounded image
      const FramebufferSettings& fbSettings = m_framebuffer->GetSettings();
      Vec2 imageSize                        = Vec2(fbSettings.width, fbSettings.height);

      Vec2 wndPos                           = ImGui::GetWindowPos();
      Vec2 wndLowerLeft                     = ImGui::GetWindowContentRegionMin();
      Vec2 cursorPos                        = ImGui::GetCursorPos();
      Vec2 currentCursorPos                 = wndLowerLeft + cursorPos + wndPos;

      if (m_isTempView)
      {
        currentCursorPos.y -= 24.0f;
      }

      ImGui::Dummy(imageSize);

      TexturePtr texture = m_renderTarget;
      if (texture != nullptr && texture->IsMultiSampled())
      {
        TexturePtr resolved = m_renderTarget->GetResolvedTexture();
        if (resolved)
        {
          texture = resolved;
        }
      }
      if (texture == nullptr)
      {
        texture = GetTextureManager()->GetBlackTexture();
      }

      ImGui::GetWindowDrawList()->AddImageRounded(Convert2ImGuiTexture(texture),
                                                  currentCursorPos,
                                                  currentCursorPos + imageSize,
                                                  UI::GetUVLL(),
                                                  UI::GetUVUR(),
                                                  ImGui::GetColorU32(Vec4(1, 1, 1, 1)),
                                                  5.0f);
    }

    ScenePtr PreviewViewport::GetScene() { return m_previewRenderer->m_params.Scene; }

    void PreviewViewport::SetScene(ScenePtr scene)
    {
      scene->Update(0.0f);
      m_previewRenderer->m_params.Scene = scene;
    }

    void PreviewViewport::ResetCamera()
    {
      ScenePtr scene = m_previewRenderer->m_params.Scene;
      scene->Update(0.0f);

      CameraPtr cam = GetCamera();
      cam->FocusToBoundingBox(scene->GetSceneBoundary(), 1.1f);
    }

    void PreviewViewport::SetViewportSize(uint width, uint height)
    {
      if (width != m_size.x || height != m_size.y)
      {
        m_size = UVec2(width, height);
        OnResizeContentArea((float) width, (float) height);
      }
    }

  } // namespace Editor
} // namespace ToolKit