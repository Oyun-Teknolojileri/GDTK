/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Anchor.h"

#include "App.h"
#include "EditorTypes.h"
#include "EditorViewport.h"

#include <Canvas.h>
#include <Material.h>
#include <MathUtil.h>
#include <Mesh.h>

namespace ToolKit
{
  namespace Editor
  {

    TKDefineClass(Anchor, EditorBillboardBase);

    Anchor::Anchor() : EditorBillboardBase({false, 0.0f, 0.0f})
    {
      for (int i = 0; i < 9; i++)
      {
        m_handles.push_back(MakeNewPtr<AnchorHandle>());
        constexpr int val                = (int) DirectionLabel::N;
        m_handles[i]->m_params.direction = static_cast<DirectionLabel>(val + i);
      }

      m_grabbedDirection = DirectionLabel::None;
      m_lastHovered      = DirectionLabel::None;
    }

    EditorBillboardBase::BillboardType Anchor::GetBillboardType() const { return BillboardType::Anchor; }

    DirectionLabel Anchor::HitTest(const Ray& ray) const
    {
      float t, tMin = TK_FLT_MAX;
      DirectionLabel hit = DirectionLabel::None;

      for (size_t i = 0; i < m_handles.size(); i++)
      {
        if (!m_handles[i]->m_mesh)
        {
          continue;
        }
        if (m_handles[i]->HitTest(ray, t))
        {
          if (t < tMin)
          {
            tMin = t;
            hit  = static_cast<DirectionLabel>(m_handles[i]->m_params.direction);
          }
        }
      }

      return hit;
    }

    bool Anchor::IsGrabbed(DirectionLabel direction) const { return m_grabbedDirection == direction; }

    void Anchor::Grab(DirectionLabel direction) { m_grabbedDirection = direction; }

    DirectionLabel Anchor::GetGrabbedDirection() const { return m_grabbedDirection; }

    AnchorHandle::Params Anchor::GetParam() const
    {
      AnchorHandle::Params p;
      p.worldLoc = m_worldLocation;
      p.color    = g_anchorColor;
      Mat4 ts    = m_node->GetTransform(TransformationSpace::TS_WORLD);
      DecomposeMatrix(ts, &p.translate, nullptr, &p.scale);

      return p;
    }

    void Anchor::Update(float deltaTime)
    {
      if (m_entity == nullptr || !m_entity->IsA<Surface>())
      {
        return;
      }

      EntityPtr parentNtt = m_entity->Parent();
      if (parentNtt == nullptr)
      {
        return;
      }

      Canvas* canvasPanel = parentNtt->As<Canvas>();
      if (canvasPanel == nullptr)
      {
        return;
      }

      Vec3 pos;
      float w = 0, h = 0;
      {
        const BoundingBox& bb = canvasPanel->GetBoundingBox(true);
        w                     = bb.GetWidth();
        h                     = bb.GetHeight();
        pos                   = Vec3(bb.min.x, bb.max.y, pos.z);
      }

      Surface* surface    = static_cast<Surface*>(m_entity.get());
      float* anchorRatios = surface->m_anchorParams.m_anchorRatios;

      const Vec3 axis[3]  = {
          {1.f, 0.f, 0.f},
          {0.f, 1.f, 0.f},
          {0.f, 0.f, 1.f}
      };

      Vec3 guideLines[8];
      {
        guideLines[0] = pos + axis[0] * ((1.f - anchorRatios[1]) * w);
        guideLines[1] = guideLines[0] - axis[1] * h;

        guideLines[2] = pos - axis[1] * (anchorRatios[2] * h);
        guideLines[3] = guideLines[2] + axis[0] * w;

        guideLines[4] = pos + axis[0] * ((anchorRatios[0]) * w);
        guideLines[5] = guideLines[4] - axis[1] * h;

        guideLines[6] = pos - axis[1] * ((1.f - anchorRatios[3]) * h);
        guideLines[7] = guideLines[6] + axis[0] * w;
      }

      const float shapeSize = 15.0f;
      float handleTranslate = shapeSize;

      if ((anchorRatios[0] + anchorRatios[1] < 0.99f) || (anchorRatios[2] + anchorRatios[3] < 0.99f))
      {
        handleTranslate = shapeSize * (2.0f / 3.0f);
      }

      for (AnchorHandlePtr handle : m_handles)
      {
        AnchorHandle::Params p         = GetParam();
        p.type                         = AnchorHandle::SolidType::Quad;

        p.worldLoc                     = pos;

        const DirectionLabel direction = handle->m_params.direction;

        if (m_grabbedDirection == direction)
        {
          p.color = g_selectHighLightPrimaryColor;
        }
        else
        {
          p.color = g_anchorColor;
        }

        if (m_lastHovered == direction)
        {
          p.color = g_selectHighLightSecondaryColor;
        }

        p.direction = direction;
        if (IsGrabbed(p.direction))
        {
          p.grabPnt = m_grabPoint;
        }
        else
        {
          p.grabPnt = ZERO;
        }

        if (direction == DirectionLabel::CENTER)
        {
          p.worldLoc -= axis[1] * ((anchorRatios[2]) * h);
          p.worldLoc += axis[0] * ((anchorRatios[0]) * w);

          if ((anchorRatios[0] + anchorRatios[1] < 0.99f) || (anchorRatios[2] + anchorRatios[3] < 0.99f))
          {
            handle->m_mesh.reset();
            continue;
          }
          p.type = AnchorHandle::SolidType::Circle;
        }

        if (direction == DirectionLabel::NE)
        {
          p.worldLoc  -= axis[1] * ((anchorRatios[2]) * h);
          p.worldLoc  += axis[0] * ((1.f - anchorRatios[1]) * w);

          p.translate  = Vec3(0.f, handleTranslate, 0.f);
          p.scale      = Vec3(0.5f, 1.1f, 1.f);
          p.angle      = glm::radians(-45.f);
        }
        if (direction == DirectionLabel::SE)
        {
          p.worldLoc  -= axis[1] * ((1.f - anchorRatios[3]) * h);
          p.worldLoc  += axis[0] * ((1.f - anchorRatios[1]) * w);

          p.translate  = Vec3(0.f, -handleTranslate, 0.f);
          p.scale      = Vec3(0.5f, 1.1f, 1.f);
          p.angle      = glm::radians(45.f);
        }
        if (direction == DirectionLabel::NW)
        {
          p.worldLoc  -= axis[1] * ((anchorRatios[2]) * h);
          p.worldLoc  += axis[0] * ((anchorRatios[0]) * w);

          p.translate  = Vec3(0.f, handleTranslate, 0.f);
          p.scale      = Vec3(0.5f, 1.1f, 1.f);
          p.angle      = glm::radians(45.f);
        }
        if (direction == DirectionLabel::SW)
        {
          p.worldLoc  -= axis[1] * ((1.f - anchorRatios[3]) * h);
          p.worldLoc  += axis[0] * ((anchorRatios[0]) * w);

          p.translate  = Vec3(0.f, handleTranslate, 0.f);
          p.scale      = Vec3(0.5f, 1.1f, 1.f);
          p.angle      = glm::radians(135.f);
        }

        if (direction == DirectionLabel::E)
        {
          p.worldLoc -= axis[1] * ((anchorRatios[2] + (1.f - anchorRatios[2] - anchorRatios[3]) / 2.f) * h);
          p.worldLoc += axis[0] * ((1.f - anchorRatios[1]) * w);

          if (anchorRatios[2] + anchorRatios[3] < 0.99f)
          {
            handle->m_mesh.reset();
            continue;
          }
          p.translate = Vec3(shapeSize, 0.f, 0.f);
          p.scale     = Vec3(1.1f, 0.5f, 1.f);
        }
        if (direction == DirectionLabel::W)
        {
          p.worldLoc -= axis[1] * ((anchorRatios[2] + (1.f - anchorRatios[2] - anchorRatios[3]) / 2.f) * h);
          p.worldLoc += axis[0] * (anchorRatios[0] * w);

          if (anchorRatios[2] + anchorRatios[3] < 0.99f)
          {
            handle->m_mesh.reset();
            continue;
          }
          p.translate = Vec3(-shapeSize, 0.f, 0.f);
          p.scale     = Vec3(1.1f, 0.5f, 1.f);
        }
        if (direction == DirectionLabel::N)
        {
          p.worldLoc -= axis[1] * ((anchorRatios[2]) * h);
          p.worldLoc += axis[0] * ((anchorRatios[0] + (1.f - anchorRatios[0] - anchorRatios[1]) / 2.f) * w);

          if (anchorRatios[0] + anchorRatios[1] < 1.f)
          {
            handle->m_mesh.reset();
            continue;
          }
          p.translate = Vec3(0.f, shapeSize, 0.f);
          p.scale     = Vec3(0.5f, 1.1f, 1.f);
        }
        if (direction == DirectionLabel::S)
        {
          p.worldLoc += axis[0] * ((anchorRatios[0] + (1.f - anchorRatios[0] - anchorRatios[1]) / 2.f) * w);
          p.worldLoc -= axis[1] * ((1.f - anchorRatios[3]) * h);

          if (anchorRatios[0] + anchorRatios[1] < 1.f)
          {
            handle->m_mesh.reset();
            continue;
          }
          p.translate = Vec3(0.f, -shapeSize, 0.f);
          p.scale     = Vec3(0.5f, 1.1f, 1.f);
        }

        if (EditorViewportPtr vp = GetApp()->GetViewport(g_2dViewport))
        {
          if (vp->IsVisible())
          {
            assert(vp->IsOrthographic() && "Viewport must be a 2d orthographic view.");

            float zoomScale  = vp->GetBillboardScale();
            float s          = shapeSize;
            p.translate     *= zoomScale;
            p.scale         *= Vec3(s * zoomScale, s * zoomScale, 1.f);
            handle->Generate(p);
          }
        }
      }

      MeshPtr mesh               = MakeNewPtr<Mesh>();
      // Dummy inverted triangle to by pass empty mesh.
      mesh->m_clientSideVertices = {
          Vertex {Vec3(glm::epsilon<float>(), 0.0f, 0.0f), Vec3(0.0f), Vec3(0.0f, glm::epsilon<float>(), 0.0f)}
      };

      for (int i = 0; i < m_handles.size(); i++)
      {
        if (m_handles[i]->m_mesh)
          mesh->m_subMeshes.push_back(m_handles[i]->m_mesh);
      }

      if (m_lastHovered != DirectionLabel::None || GetGrabbedDirection() != DirectionLabel::None)
      {
        Vec3Array pnts = {
            guideLines[0],
            guideLines[1],
            guideLines[2],
            guideLines[3],
            guideLines[4],
            guideLines[5],
            guideLines[6],
            guideLines[7],
        };

        LineBatchPtr guide = MakeNewPtr<LineBatch>();
        guide->Generate(pnts, g_anchorGuideLineColor, DrawType::Line, 2.5f);
        MeshPtr guideMesh = guide->GetComponent<MeshComponent>()->GetMeshVal();
        mesh->m_subMeshes.push_back(guideMesh);
      }

      m_lastHovered = DirectionLabel::None;

      mesh->Init(false);
      GetComponent<MeshComponent>()->SetMeshVal(mesh);
    }

    // AnchorHandle
    //////////////////////////////////////////

    AnchorHandle::AnchorHandle() { m_params.color = g_anchorColor; }

    void AnchorHandle::Generate(const Params& params)
    {
      m_params = params;

      MeshPtr meshPtr;
      if (params.type == AnchorHandle::SolidType::Circle)
      {
        SpherePtr sphere = MakeNewPtr<Sphere>();
        sphere->SetRadiusVal(0.35f);
        meshPtr = sphere->GetMeshComponent()->GetMeshVal();
      }
      else
      {
        assert(params.type == AnchorHandle::SolidType::Quad); // A new primitive type?
        QuadPtr quad = MakeNewPtr<Quad>();
        meshPtr      = quad->GetMeshComponent()->GetMeshVal();
      }

      Mat4 mat(1.0f);
      mat = glm::translate(mat, params.worldLoc);
      mat = glm::rotate(mat, params.angle, Vec3(0.f, 0.f, 1.f));
      mat = glm::translate(mat, params.translate);
      mat = glm::scale(mat, params.scale);
      meshPtr->ApplyTransform(mat);

      m_mesh = MakeNewPtr<Mesh>();
      m_mesh.swap(meshPtr);
      m_mesh->Init(false);

      MaterialPtr matPtr = GetMaterialManager()->GetCopyOfUnlitColorMaterial();
      matPtr->UnInit();
      matPtr->SetColorVal(params.color);
      matPtr->GetRenderState()->blendFunction = BlendFunction::ONE_TO_ONE;
      matPtr->Init();
      m_mesh->m_material = matPtr;
    }

    bool AnchorHandle::HitTest(const Ray& ray, float& t) const
    {
      // Hit test done in object space bounding boxes.
      Mat4 transform = GetTransform();
      Mat4 its       = glm::inverse(transform);

      Ray rayInObj;
      rayInObj.position  = Vec4(ray.position, 1.0f);
      rayInObj.direction = Vec4(ray.direction, 0.0f);

      m_mesh->CalculateAABB();
      return RayBoxIntersection(rayInObj, m_mesh->m_boundingBox, t);
    }

    Mat4 AnchorHandle::GetTransform() const
    {
      Mat4 sc        = glm::scale(Mat4(), m_params.scale);
      Mat4 rt        = Mat4(1.f);
      Mat4 ts        = glm::translate(Mat4(), m_params.translate);
      Mat4 transform = ts * rt * sc;

      return transform;
    }

  } // namespace Editor
} // namespace ToolKit
