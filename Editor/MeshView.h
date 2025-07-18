/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "PreviewViewport.h"
#include "View.h"

namespace ToolKit
{
  namespace Editor
  {

    class TK_EDITOR_API MeshView : public View
    {
     public:
      MeshView();
      ~MeshView();

      void Show() override;
      void SetMesh(MeshPtr mesh);
      void ResetCamera();

     private:
      PreviewViewportPtr m_viewport = nullptr;
      EntityPtr m_previewEntity     = nullptr;
      MeshPtr m_mesh                = nullptr;
    };

  } // namespace Editor
} // namespace ToolKit
