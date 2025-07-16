/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "EditorBillboard.h"

#include <Primative.h>

namespace ToolKit
{
  namespace Editor
  {

    // Cursor
    //////////////////////////////////////////

    class TK_EDITOR_API Cursor : public EditorBillboardBase
    {
     public:
      TKDeclareClass(Cursor, EditorBillboardBase);

      Cursor();
      virtual ~Cursor();
      BillboardType GetBillboardType() const override;

     protected:
      void Generate() override;
    };

    // Axis3d
    //////////////////////////////////////////

    class TK_EDITOR_API Axis3d : public EditorBillboardBase
    {
     public:
      TKDeclareClass(Axis3d, EditorBillboardBase);

      Axis3d();
      virtual ~Axis3d();
      BillboardType GetBillboardType() const override;

     private:
      void Generate() override;
    };

    // GizmoHandle
    //////////////////////////////////////////

    class TK_EDITOR_API GizmoHandle
    {
     public:
      enum class SolidType
      {
        Cube,
        Cone,
        Circle
      };

      struct Params
      {
        // World space data.
        Vec3 worldLoc;
        Vec3 grabPnt;
        Vec3 initialPnt;
        Mat3 normals;
        // Billboard values.
        Vec3 scale;
        Vec3 translate;
        // Geometry.
        AxisLabel axis;
        Vec3 toeTip;
        Vec3 solidDim;
        Vec3 color;
        SolidType type;
      };

     public:
      GizmoHandle();
      virtual ~GizmoHandle();

      virtual void Generate(const Params& params);
      virtual bool HitTest(const Ray& ray, float& t) const;
      Mat4 GetTransform() const;

     public:
      Vec3 m_tangentDir;
      Params m_params;
      MeshPtr m_mesh = nullptr;
    };

    // PolarHandle
    //////////////////////////////////////////

    class TK_EDITOR_API PolarHandle : public GizmoHandle
    {
     public:
      void Generate(const Params& params) override;
      bool HitTest(const Ray& ray, float& t) const override;
    };

    // QuadHandle
    //////////////////////////////////////////

    class TK_EDITOR_API QuadHandle : public GizmoHandle
    {
     public:
      void Generate(const Params& params) override;
      bool HitTest(const Ray& ray, float& t) const override;
    };

    // Gizmo
    //////////////////////////////////////////

    class TK_EDITOR_API Gizmo : public EditorBillboardBase
    {
     public:
      TKDeclareClass(Gizmo, EditorBillboardBase);

      Gizmo();
      explicit Gizmo(const Billboard::Settings& set);
      virtual ~Gizmo();
      void NativeConstruct() override;
      BillboardType GetBillboardType() const override;

      virtual AxisLabel HitTest(const Ray& ray) const;
      virtual void Update(float deltaTime) = 0;
      bool IsLocked(AxisLabel axis) const;
      void Lock(AxisLabel axis);
      void UnLock(AxisLabel axis);
      bool IsGrabbed(AxisLabel axis) const;
      void Grab(AxisLabel axis);
      AxisLabel GetGrabbedAxis() const;

      void LookAt(CameraPtr cam, float windowHeight) override;

     protected:
      virtual GizmoHandle::Params GetParam() const;

      /** Collects all the handles under a non empty root mesh for drawing. */
      void Consume();

     public:
      Vec3 m_grabPoint;     //!< Grab location of the gizmo.
      Vec3 m_initialPoint;  //!< Gizmo's entities initial center before any movement.
      Mat3 m_normalVectors; //!< Gizmo's entities normal axes.
      AxisLabel m_lastHovered;
      std::vector<GizmoHandle*> m_handles;

     protected:
      std::vector<AxisLabel> m_lockedAxis;
      AxisLabel m_grabbedAxis = AxisLabel::None;
    };

    // LinearGizmo
    //////////////////////////////////////////

    class TK_EDITOR_API LinearGizmo : public Gizmo
    {
     public:
      TKDeclareClass(LinearGizmo, Gizmo);

      LinearGizmo();
      virtual ~LinearGizmo();

      void Update(float deltaTime) override;

     protected:
      GizmoHandle::Params GetParam() const override;
    };

    // MoveGizmo
    //////////////////////////////////////////

    class TK_EDITOR_API MoveGizmo : public LinearGizmo
    {
     public:
      TKDeclareClass(MoveGizmo, Gizmo);

      MoveGizmo();
      virtual ~MoveGizmo();
      BillboardType GetBillboardType() const override;
    };

    // ScaleGizmo
    //////////////////////////////////////////

    class TK_EDITOR_API ScaleGizmo : public LinearGizmo
    {
     public:
      TKDeclareClass(ScaleGizmo, Gizmo);

      ScaleGizmo();
      virtual ~ScaleGizmo();
      BillboardType GetBillboardType() const override;

     protected:
      GizmoHandle::Params GetParam() const override;
    };

    // PolarGizmo
    //////////////////////////////////////////

    class TK_EDITOR_API PolarGizmo : public Gizmo
    {
     public:
      TKDeclareClass(PolarGizmo, Gizmo);

      PolarGizmo();
      virtual ~PolarGizmo();
      BillboardType GetBillboardType() const override;

      void Update(float deltaTime) override;
    };

  } // namespace Editor
} // namespace ToolKit
