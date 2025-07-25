/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Gizmo.h"

namespace ToolKit
{
  namespace Editor
  {

    // AnchorHandle
    //////////////////////////////////////////

    class TK_EDITOR_API AnchorHandle
    {
     public:
      enum class SolidType
      {
        Quad,
        Circle
      };

      struct Params
      {
        // Worldspace data.
        Vec3 worldLoc;
        Vec3 grabPnt;
        // Billboard values.
        Vec3 scale;
        Vec3 translate;
        float angle = 0.f;
        // Geometry.
        DirectionLabel direction;
        Vec3 color;
        SolidType type;
      };

     public:
      AnchorHandle();

      virtual void Generate(const Params& params);
      virtual bool HitTest(const Ray& ray, float& t) const;
      Mat4 GetTransform() const;

     public:
      Params m_params;
      MeshPtr m_mesh;
    };

    // Anchor
    //////////////////////////////////////////

    class TK_EDITOR_API Anchor : public EditorBillboardBase
    {
     public:
      TKDeclareClass(Anchor, EditorBillboardBase);

      Anchor();
      BillboardType GetBillboardType() const override;

      virtual DirectionLabel HitTest(const Ray& ray) const;
      virtual void Update(float deltaTime);
      bool IsGrabbed(DirectionLabel direction) const;
      void Grab(DirectionLabel direction);
      DirectionLabel GetGrabbedDirection() const;

     protected:
      virtual AnchorHandle::Params GetParam() const;

     public:
      Vec3 m_grabPoint;
      DirectionLabel m_lastHovered;
      std::vector<AnchorHandlePtr> m_handles;

     protected:
      DirectionLabel m_grabbedDirection;
    };

  } // namespace Editor
} // namespace ToolKit
