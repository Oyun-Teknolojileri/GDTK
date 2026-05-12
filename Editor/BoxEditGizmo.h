/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Gizmo.h"

#include <EnvironmentComponent.h>

#include <functional>

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
     * A world-space gizmo that displays 6 cube handles at the face centers of an OBB.
     * Dragging a handle moves that face along its outward normal, modifying the underlying
     * component's Size and PositionOffset.
     *
     * Supports oriented bounding boxes via a world transform matrix.
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

      /** Returns the local-space outward normal for the given face. */
      static Vec3 GetFaceNormalLocal(BoxFace face);

      /** Returns the world-space outward normal for the given face, rotated by the current orientation. */
      Vec3 GetFaceNormalWorld(BoxFace face) const;

      /** Sets the local-space bounding box to display handles for. */
      void SetTargetBox(const BoundingBox& box);

      /** Returns the current local-space target box. */
      const BoundingBox& GetTargetBox() const;

      /** Sets the world transform (translation + rotation) used to orient the OBB. No scale. */
      void SetWorldTransform(const Mat4& transform);

      /** Returns the current world transform. */
      const Mat4& GetWorldTransform() const;

     private:
      void GenerateHandles();

     public:
      float m_handleSize = 0.15f;
      std::function<void()> m_preRenderCallback; //!< Called in LookAt before rendering to sync target box.

     private:
      BoundingBox m_targetBox;
      Mat4 m_worldTransform = Mat4(1.0f);
      bool m_handlesDirty   = true;
    };

    typedef std::shared_ptr<BoxEditGizmo> BoxEditGizmoPtr;

  } // namespace Editor
} // namespace ToolKit
