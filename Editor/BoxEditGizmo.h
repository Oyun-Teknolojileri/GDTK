/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Gizmo.h"

#include <EnvironmentComponent.h>

namespace ToolKit
{
  namespace Editor
  {

    /**
     * Represents an axis-aligned face of a bounding box for handle identification.
     * Values 0-5 map to: +X, -X, +Y, -Y, +Z, -Z.
     */
    enum class BoxFace
    {
      PosX = 0,
      NegX,
      PosY,
      NegY,
      PosZ,
      NegZ,
      None
    };

    // BoxEditGizmo
    //////////////////////////////////////////

    /**
     * A world-space gizmo that displays 6 cube handles at the face centers of an AABB.
     * Dragging a handle moves that face along its outward normal, modifying the underlying
     * component's Size and PositionOffset.
     *
     * This gizmo is generic: any component that provides a BoundingBox and Size/Offset
     * parameters can be edited with it.
     */
    class TK_EDITOR_API BoxEditGizmo : public Gizmo
    {
     public:
      TKDeclareClass(BoxEditGizmo, Gizmo);

      BoxEditGizmo();
      virtual ~BoxEditGizmo();
      BillboardType GetBillboardType() const override;

      void Update(float deltaTime) override;
      AxisLabel HitTest(const Ray& ray) const override;
      void LookAt(CameraPtr cam, float windowHeight) override;

      /** Returns the BoxFace corresponding to a grabbed AxisLabel, or BoxFace::None. */
      BoxFace GetGrabbedFace() const;

      /** Returns the outward normal direction for the given face. */
      static Vec3 GetFaceNormal(BoxFace face);

      /** Sets the bounding box to display handles for. Call each frame before Update. */
      void SetTargetBox(const BoundingBox& box);

      /** Returns the current target box. */
      const BoundingBox& GetTargetBox() const;

     private:
      void GenerateHandles();

     public:
      /** Handle size in world units. Adjusts with camera distance. */
      float m_handleSize = 0.15f;

     private:
      BoundingBox m_targetBox;
      bool m_handlesDirty = true;
    };

    typedef std::shared_ptr<BoxEditGizmo> BoxEditGizmoPtr;

  } // namespace Editor
} // namespace ToolKit
