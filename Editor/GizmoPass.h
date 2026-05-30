/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Gizmo.h"

#include <Pass.h>

namespace ToolKit
{
  namespace Editor
  {

    struct TK_EDITOR_API GizmoPassParams
    {
      Viewport* Viewport = nullptr;
      BillboardPtrArray GizmoArray;
    };

    class TK_EDITOR_API GizmoPass : public Pass
    {
     public:
      GizmoPass();
      explicit GizmoPass(const GizmoPassParams& params);

      void Render() override;
      void PreRender() override;
      void PostRender() override;

     public:
      GizmoPassParams m_params;

     private:
      SpherePtr m_depthMaskSphere = nullptr;
      CameraPtr m_camera          = nullptr;

      /** Default pass state used when reverting passive overrides between gizmo sub-draws. */
      RenderState m_defaultPassState;
      /** Depth-mask sphere variant: colorMaskEnabled=false (depth-only write). */
      RenderState m_depthMaskPassState;
      /** Guide-mesh variant: depthFunction=FuncAlways so guides draw on top. */
      RenderState m_guideMeshPassState;
    };

  } // namespace Editor
} // namespace ToolKit