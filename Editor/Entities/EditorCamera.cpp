/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "EditorCamera.h"

#include "App.h"
#include "EditorViewport.h"

#include <Material.h>
#include <Mesh.h>

namespace ToolKit
{
  namespace Editor
  {

    TKDefineClass(EditorCamera, Camera);

    EditorCamera::EditorCamera() {}

    EditorCamera::EditorCamera(const EditorCamera* cam) { cam->CopyTo(this); }

    EditorCamera::~EditorCamera() {}

    void EditorCamera::NativeConstruct()
    {
      Super::NativeConstruct();
      CreateGizmo();
    }

    ObjectPtr EditorCamera::Copy() const
    {
      EditorCameraPtr cpy = MakeNewPtr<EditorCamera>();
      Camera::CopyTo(cpy.get());
      cpy->CreateGizmo();

      // Camera::CopyTo() ends in Entity::WeakCopy(), which replaces the whole parameter block with
      // the source's, so the copy would keep a Poses callback bound to this camera. Re-bind only
      // that callback. Re-running ParameterConstructor() would also generate a second handle,
      // leaving the first one allocated for the rest of the session, and would reset the copied
      // Name, Tag, Visible and TransformLock parameters to their defaults.
      cpy->DefinePoses();

      return cpy;
    }

    void EditorCamera::PostDeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
    {
      Super::PostDeSerializeImp(info, parent);
      CreateGizmo();
    }

    void EditorCamera::GenerateFrustum()
    {
      // Line frustum.
      Vec3Array corners = {
          Vec3(-0.5f, 0.3f, -1.6f),
          Vec3(0.5f, 0.3f, -1.6f),
          Vec3(0.5f, -0.3f, -1.6f),
          Vec3(-0.5f, -0.3f, -1.6f),
      };

      // Below is a tricky frustum construction.
      // I am forcing to create triangles to use in ray/triangle intersection
      // in picking.
      // At the same time, not causing additional lines for frustom drawing.

      Vec3 eye;
      Vec3Array lines              = {eye,        corners[0], corners[1],
                                      corners[1], // Triangle widhout line.
                                      corners[1], corners[1],

                                      eye,        corners[1], corners[2], corners[2], corners[2], corners[2],

                                      eye,        corners[2], corners[3], corners[3], corners[3], corners[3],

                                      eye,        corners[3], corners[0], corners[0], corners[0], corners[0],

                                      corners[0], corners[1], corners[1], corners[2], corners[3], corners[3],
                                      corners[2], corners[3], corners[3], corners[0]};

      MeshComponentPtr camMeshComp = GetComponent<MeshComponent>();
      LineBatchPtr frusta          = MakeNewPtr<LineBatch>();
      frusta->Generate(lines, g_cameraGizmoColor, DrawType::Line);
      camMeshComp->SetMeshVal(frusta->GetComponent<MeshComponent>()->GetMeshVal());

      // Triangle part.
      VertexArray vertices;
      vertices.resize(3);

      vertices[0].pos               = Vec3(-0.3f, 0.35f, -1.6f);
      vertices[1].pos               = Vec3(0.3f, 0.35f, -1.6f);
      vertices[2].pos               = Vec3(0.0f, 0.65f, -1.6f);

      MeshPtr subMesh               = MakeNewPtr<Mesh>();
      subMesh->m_vertexCount        = (uint) vertices.size();
      subMesh->m_clientSideVertices = vertices;
      subMesh->m_material           = GetMaterialManager()->GetCopyOfUnlitColorMaterial();
      subMesh->m_material->SetColorVal(ZERO);
      subMesh->m_material->SetColorVal(ZERO);
      subMesh->m_material->cullMode = CullingType::TwoSided;
      subMesh->ConstructFaces();

      camMeshComp->GetMeshVal()->m_subMeshes.push_back(subMesh);
      camMeshComp->GetMeshVal()->CalculateAABB();

      // Do not expose camera mesh component
      camMeshComp->ParamMesh().m_exposed = false;
    }

    void EditorCamera::CreateGizmo()
    {
      // Recreate frustum.
      RemoveComponent<MeshComponent>();
      MeshComponentPtr meshCom = AddComponent<MeshComponent>(false);
      meshCom->SetCastShadowVal(false);

      GenerateFrustum();
    }

    void EditorCamera::ParameterConstructor()
    {
      Super::ParameterConstructor();
      DefinePoses();
    }

    void EditorCamera::DefinePoses()
    {
      Poses_Define(
          [this]() -> void
          {
            if (EditorViewportPtr av = GetApp()->GetActiveViewport())
            {
              if (m_posessed)
              {
                av->AttachCamera(NullHandle);
                ParamPoses().m_name = "Poses";
              }
              else
              {
                av->AttachCamera(GetIdVal());
                ParamPoses().m_name = "Free";
              }

              m_posessed = !m_posessed;
            }
          },
          CameraCategory.Name,
          CameraCategory.Priority,
          true,
          true);
    }

  } // namespace Editor
} // namespace ToolKit
