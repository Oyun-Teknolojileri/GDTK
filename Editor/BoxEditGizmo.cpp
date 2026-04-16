/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "BoxEditGizmo.h"

#include "App.h"
#include "EditorViewport.h"

#include <Material.h>
#include <MathUtil.h>
#include <Mesh.h>

namespace ToolKit
{
  namespace Editor
  {

    // BoxEditGizmo
    //////////////////////////////////////////

    TKDefineClass(BoxEditGizmo, Gizmo);

    BoxEditGizmo::BoxEditGizmo() : Gizmo({false, 1.0f, 1.0f, false})
    {
      // 6 handles for 6 faces: +X, -X, +Y, -Y, +Z, -Z
      // We map them to AxisLabel X(0)..XY(5) just to reuse the existing grab mechanism.
      m_handles.resize(6);
      for (int i = 0; i < 6; i++)
      {
        GizmoHandle* handle   = new GizmoHandle();
        handle->m_params.axis = static_cast<AxisLabel>(i);
        handle->m_params.type = GizmoHandle::SolidType::Cube;
        m_handles[i]          = handle;
      }
    }

    BoxEditGizmo::~BoxEditGizmo() {}

    EditorBillboardBase::BillboardType BoxEditGizmo::GetBillboardType() const { return BillboardType::BoxEdit; }

    void BoxEditGizmo::SetTargetBox(const BoundingBox& box)
    {
      if (box.min != m_targetBox.min || box.max != m_targetBox.max)
      {
        m_targetBox    = box;
        m_handlesDirty = true;
      }
    }

    const BoundingBox& BoxEditGizmo::GetTargetBox() const { return m_targetBox; }

    BoxFace BoxEditGizmo::GetGrabbedFace() const
    {
      AxisLabel grabbed = GetGrabbedAxis();
      if (grabbed == AxisLabel::None)
      {
        return BoxFace::None;
      }

      int index = static_cast<int>(grabbed);
      if (index >= 0 && index < 6)
      {
        return static_cast<BoxFace>(index);
      }

      return BoxFace::None;
    }

    void BoxEditGizmo::SetWorldTransform(const Mat4& transform) { m_worldTransform = transform; }

    const Mat4& BoxEditGizmo::GetWorldTransform() const { return m_worldTransform; }

    Vec3 BoxEditGizmo::GetFaceNormalLocal(BoxFace face)
    {
      switch (face)
      {
        case BoxFace::PosX:
          return Vec3(1.0f, 0.0f, 0.0f);
        case BoxFace::NegX:
          return Vec3(-1.0f, 0.0f, 0.0f);
        case BoxFace::PosY:
          return Vec3(0.0f, 1.0f, 0.0f);
        case BoxFace::NegY:
          return Vec3(0.0f, -1.0f, 0.0f);
        case BoxFace::PosZ:
          return Vec3(0.0f, 0.0f, 1.0f);
        case BoxFace::NegZ:
          return Vec3(0.0f, 0.0f, -1.0f);
        default:
          return ZERO;
      }
    }

    Vec3 BoxEditGizmo::GetFaceNormalWorld(BoxFace face) const
    {
      Vec3 localNormal = GetFaceNormalLocal(face);
      Vec3 worldNormal = Vec3(m_worldTransform * Vec4(localNormal, 0.0f));
      return glm::normalize(worldNormal);
    }

    AxisLabel BoxEditGizmo::HitTest(const Ray& ray) const
    {
      float closestT    = TK_FLT_MAX;
      AxisLabel closest = AxisLabel::None;

      for (int i = 0; i < 6; i++)
      {
        if (m_handles[i]->m_mesh == nullptr)
        {
          continue;
        }

        // Direct world-space AABB hit test using the handle's offset mesh.
        m_handles[i]->m_mesh->CalculateAABB();
        float t;
        if (RayBoxIntersection(ray, m_handles[i]->m_mesh->m_boundingBox, t))
        {
          if (t < closestT)
          {
            closestT = t;
            closest  = static_cast<AxisLabel>(i);
          }
        }
      }

      return closest;
    }

    void BoxEditGizmo::LookAt(CameraPtr cam, float windowHeight)
    {
      // Sync target box from the component before rendering so handles don't lag.
      if (m_preRenderCallback)
      {
        m_preRenderCallback();
      }

      if (m_handlesDirty)
      {
        GenerateHandles();
      }

      // No billboard behavior — gizmo is world-space.
      // Keep node at identity so world-space handle vertices render correctly.
      m_node->SetTransform(Mat4(1.0f));
    }

    void BoxEditGizmo::Update(float deltaTime) { GenerateHandles(); }

    void BoxEditGizmo::GenerateHandles()
    {
      Vec3 boxCenter   = m_targetBox.GetCenter();
      Vec3 halfSize    = (m_targetBox.max - m_targetBox.min) * 0.5f;

      // Transform box center to world space.
      Vec3 worldCenter = Vec3(m_worldTransform * Vec4(boxCenter, 1.0f));

      // Compute handle size based on camera distance.
      float handleDim  = m_handleSize;
      if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
      {
        CameraPtr cam = vp->GetCamera();
        if (cam)
        {
          float dist = glm::length(cam->m_node->GetTranslation() - worldCenter);
          handleDim  = dist * 0.015f;
          handleDim  = glm::clamp(handleDim, 0.05f, 0.5f);
        }
      }

      // Local-space face center offsets from box center.
      Vec3 localOffsets[6] = {
          Vec3(halfSize.x, 0.0f, 0.0f),  // +X
          Vec3(-halfSize.x, 0.0f, 0.0f), // -X
          Vec3(0.0f, halfSize.y, 0.0f),  // +Y
          Vec3(0.0f, -halfSize.y, 0.0f), // -Y
          Vec3(0.0f, 0.0f, halfSize.z),  // +Z
          Vec3(0.0f, 0.0f, -halfSize.z), // -Z
      };

      Vec3 faceColors[6] = {
          g_gizmoColor[0],
          g_gizmoColor[0], // X faces = red
          g_gizmoColor[1],
          g_gizmoColor[1], // Y faces = green
          g_gizmoColor[2],
          g_gizmoColor[2], // Z faces = blue
      };

      for (int i = 0; i < 6; i++)
      {
        // Transform face center to world space.
        Vec3 worldFaceCenter = Vec3(m_worldTransform * Vec4(boxCenter + localOffsets[i], 1.0f));

        GizmoHandle::Params p;
        p.axis      = static_cast<AxisLabel>(i);
        p.type      = GizmoHandle::SolidType::Cube;
        p.solidDim  = Vec3(handleDim);
        p.translate = worldFaceCenter;
        p.scale     = Vec3(1.0f);
        p.normals   = Mat3(1.0f);
        p.toeTip    = Vec3(0.0f);
        p.worldLoc  = worldFaceCenter;
        p.grabDir   = ZERO;
        p.grabPnt   = ZERO;

        if (IsGrabbed(p.axis))
        {
          p.color = g_selectHighLightPrimaryColor;
        }
        else if (m_lastHovered == p.axis)
        {
          p.color       = g_selectHighLightSecondaryColor;
          m_lastHovered = AxisLabel::None;
        }
        else
        {
          p.color = faceColors[i];
        }

        // Generate handle mesh as a simple cube at face center.
        MaterialPtr material = GetMaterialManager()->GetCopyOfUnlitColorMaterial(false);
        material->SetColorVal(p.color);

        CubePtr solid = MakeNewPtr<Cube>();
        solid->SetCubeScaleVal(p.solidDim);

        MeshPtr mesh     = solid->GetComponent<MeshComponent>()->GetMeshVal();
        mesh->m_material = material;

        // Rotate and translate vertices to world space.
        // Extract rotation from world transform (no scale, no translation).
        Mat3 rotation    = Mat3(m_worldTransform);
        mesh->UnInit();
        for (Vertex& v : mesh->m_clientSideVertices)
        {
          v.pos   = rotation * v.pos; // Rotate cube to match entity orientation.
          v.pos  += worldFaceCenter;  // Translate to face center.
          v.norm  = rotation * v.norm;
        }
        mesh->Init();

        m_handles[i]->m_params = p;
        m_handles[i]->m_mesh   = mesh;
      }

      Consume();
      m_handlesDirty = false;
    }

  } // namespace Editor
} // namespace ToolKit
