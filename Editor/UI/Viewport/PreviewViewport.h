/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "EditorTypes.h"
#include "EditorViewport.h"

namespace ToolKit
{
  namespace Editor
  {

    class TK_EDITOR_API PreviewViewport : public EditorViewport
    {
     public:
      TKDeclareClass(PreviewViewport, EditorViewport);

      PreviewViewport();
      ~PreviewViewport();
      void Show() override;
      ScenePtr GetScene();
      void SetScene(ScenePtr scene);
      void ResetCamera();
      void SetViewportSize(uint width, uint height);

     private:
      SceneRenderPathPtr m_previewRenderer = nullptr;

     public:
      bool m_isTempView = false;
    };

  } // namespace Editor
} // namespace ToolKit