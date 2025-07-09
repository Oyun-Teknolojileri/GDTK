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

      // Use canvas material.
      GetMaterialComponent()->SetFirstMaterial(canvasBorderMaterial);
      AddComponent<MeshComponent>(); // This will be used for editor boundary.
    }

    void EditorCanvas::UpdateGeometry(bool byTexture)
    {
      Super::UpdateGeometry(byTexture);
      CreateQuat();
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
      GetMeshComponent()->SetMeshVal(mesh);
    }

  } // namespace Editor
} // namespace ToolKit
