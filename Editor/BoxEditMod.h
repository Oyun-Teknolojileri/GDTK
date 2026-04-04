/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "BoxEditGizmo.h"
#include "Mod.h"

namespace ToolKit
{
  namespace Editor
  {

    // BoxEditMod
    //////////////////////////////////////////

    class TK_EDITOR_API BoxEditMod : public BaseMod
    {
     public:
      explicit BoxEditMod(ModId id);
      virtual ~BoxEditMod();

      void Init() override;
      void UnInit() override;
      void Update(float deltaTime) override;
      void Signal(SignalId signal) override;

     private:
      /** Returns true if the current selection has a box-editable component. */
      bool TryUpdateGizmoFromSelection();

      /** Begins a drag operation on the gizmo. */
      void BeginDrag(const Vec2& mousePos);

      /** Continues the drag operation. */
      void UpdateDrag(const Vec2& mousePos);

      /** Ends the drag operation. */
      void EndDrag();

     public:
      BoxEditGizmoPtr m_gizmo;

     private:
      bool m_dragging          = false;
      BoxFace m_dragFace       = BoxFace::None;
      PlaneEquation m_dragPlane;
      Vec3 m_dragStartPoint;
      Vec3 m_dragStartSize;
      Vec3 m_dragStartOffset;
      EntityPtr m_dragEntity;
      EnvironmentComponentPtr m_dragEnvComp;
    };

  } // namespace Editor
} // namespace ToolKit
