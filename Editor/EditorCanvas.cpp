#include "EditorCanvas.h"

#include <Mesh.h>

namespace ToolKit
{
  namespace Editor
  {

    TKDefineClass(EditorCanvas, Canvas);

    EditorCanvas::EditorCanvas() {}

    EditorCanvas::~EditorCanvas() {}

    void EditorCanvas::NativeConstruct()
    {
      Super::NativeConstruct();

      // Create canvas material if necessary.
      MaterialManager* matMan          = GetMaterialManager();

      const String materialFile        = "TK::CanvasBorder";
      MaterialPtr canvasBorderMaterial = nullptr;
      if (matMan->Exist(materialFile))
      {
        canvasBorderMaterial = matMan->Create<Material>(materialFile);
      }
      else
      {
        canvasBorderMaterial         = GetMaterialManager()->GetCopyOfUnlitMaterial();
        canvasBorderMaterial->m_name = materialFile;
        canvasBorderMaterial->SetFile(materialFile);
        canvasBorderMaterial->GetRenderState()->drawType  = DrawType::Line;
        canvasBorderMaterial->GetRenderState()->lineWidth = 3.0f;
        matMan->Manage(canvasBorderMaterial);
      }

      // Create border gizmo.
      m_borderGizmo = MakeNewPtr<Entity>();
      m_borderGizmo->AddComponent<MeshComponent>();
      m_borderGizmo->AddComponent<MaterialComponent>();
      m_borderGizmo->GetMaterialComponent()->SetFirstMaterial(canvasBorderMaterial);
    }

    void EditorCanvas::UpdateGeometry(bool byTexture)
    {
      Super::UpdateGeometry(byTexture);
      CreateQuat();
    }

    EntityPtr EditorCanvas::GetBorderGizmo()
    {
      // Sync gizmo transform.
      Mat4 transform = m_node->GetTransform();
      m_borderGizmo->m_node->SetTransform(transform);

      return m_borderGizmo;
    }

    void EditorCanvas::CreateQuat()
    {
      BoundingBox box = GetBoundingBox();
      Vec3 min        = box.min;
      Vec3 max        = box.max;
      float depth     = min.z;

      // Lines of the boundary.
      VertexArray vertices;
      vertices.resize(8);

      vertices[0].pos            = min;
      vertices[1].pos            = Vec3(max.x, min.y, depth);
      vertices[2].pos            = Vec3(max.x, min.y, depth);
      vertices[3].pos            = Vec3(max.x, max.y, depth);
      vertices[4].pos            = Vec3(max.x, max.y, depth);
      vertices[5].pos            = Vec3(min.x, max.y, depth);
      vertices[6].pos            = Vec3(min.x, max.y, depth);
      vertices[7].pos            = min;

      MeshPtr mesh               = MakeNewPtr<Mesh>();
      mesh->m_clientSideVertices = vertices;
      mesh->CalculateAABB();
      mesh->Init();

      // Update border.
      m_borderGizmo->GetMeshComponent()->SetMeshVal(mesh);
    }

  } // namespace Editor
} // namespace ToolKit
