/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "BoxEditMod.h"

#include "App.h"
#include "EditorViewport.h"

#include <DirectionComponent.h>
#include <EnvironmentComponent.h>
#include <MathUtil.h>
#include <SDL.h>

namespace ToolKit
{
  namespace Editor
  {

    BoxEditMod::BoxEditMod(ModId id) : BaseMod(id) {}

    BoxEditMod::~BoxEditMod() { GetApp()->m_gizmo = nullptr; }

    void BoxEditMod::Init()
    {
      State* state                   = new StateBeginPick();
      m_stateMachine->m_currentState = state;

      m_stateMachine->PushState(state);

      State* endPick                  = new StateEndPick();
      endPick->m_links[m_backToStart] = StateType::StateBeginPick;
      m_stateMachine->PushState(endPick);

      State* del                     = new StateDeletePick();
      del->m_links[m_backToStart]    = StateType::StateBeginPick;
      m_stateMachine->PushState(del);

      m_gizmo = MakeNewPtr<BoxEditGizmo>();
    }

    void BoxEditMod::UnInit() {}

    void BoxEditMod::Update(float deltaTime)
    {
      if (m_dragging)
      {
        // While dragging, update target box from the component so handles follow.
        TryUpdateGizmoFromSelection();
        m_gizmo->Update(deltaTime);
        GetApp()->m_gizmo = m_gizmo;
        return;
      }

      // Try to set up gizmo from current selection.
      if (TryUpdateGizmoFromSelection())
      {
        if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
        {
          m_gizmo->LookAt(vp->GetCamera(), vp->GetBillboardScale());
        }
        m_gizmo->Update(deltaTime);
        GetApp()->m_gizmo = m_gizmo;
      }
      else
      {
        GetApp()->m_gizmo = nullptr;
      }

      // Normal state machine for picking.
      BaseMod::Update(deltaTime);

      if (m_stateMachine->m_currentState->GetType() == StateType::StateEndPick)
      {
        StateEndPick* endPick = static_cast<StateEndPick*>(m_stateMachine->m_currentState);
        IDArray entities;
        endPick->PickDataToEntityId(entities);
        GetApp()->GetCurrentScene()->AddToSelection(entities, ImGui::GetIO().KeyShift);
        ModManager::GetInstance()->DispatchSignal(BaseMod::m_backToStart);
      }

      if (m_stateMachine->m_currentState->GetType() == StateType::StateDeletePick)
      {
        ModManager::GetInstance()->DispatchSignal(BaseMod::m_backToStart);
      }
    }

    void BoxEditMod::Signal(SignalId signal)
    {
      if (signal == m_leftMouseBtnDownSgnl)
      {
        // Check if clicking on a gizmo handle.
        if (m_gizmo && GetApp()->m_gizmo == m_gizmo)
        {
          if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
          {
            Ray ray       = vp->RayFromMousePosition();
            AxisLabel hit = m_gizmo->HitTest(ray);
            if (hit != AxisLabel::None)
            {
              Vec2 mousePos = vp->GetLastMousePosScreenSpace();
              m_gizmo->Grab(hit);
              BeginDrag(mousePos);
              return; // Don't pass to state machine.
            }
          }
        }
      }

      if (signal == m_leftMouseBtnDragSgnl && m_dragging)
      {
        if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
        {
          Vec2 mousePos = vp->GetLastMousePosScreenSpace();
          UpdateDrag(mousePos);
        }
        return;
      }

      if (signal == m_leftMouseBtnUpSgnl && m_dragging)
      {
        EndDrag();
        return;
      }

      // Not dragging, forward to state machine.
      BaseMod::Signal(signal);
    }

    bool BoxEditMod::TryUpdateGizmoFromSelection()
    {
      EditorScenePtr scene = GetApp()->GetCurrentScene();
      if (scene->GetSelectedEntityCount() == 0)
      {
        return false;
      }

      EntityPtr ntt = scene->GetCurrentSelection();
      if (ntt == nullptr)
      {
        return false;
      }

      EnvironmentComponentPtr envComp = ntt->GetComponent<EnvironmentComponent>();
      if (envComp == nullptr)
      {
        return false;
      }

      const BoundingBox& bb = envComp->GetBoundingBox();
      m_gizmo->SetTargetBox(bb);

      return true;
    }

    void BoxEditMod::BeginDrag(const Vec2& mousePos)
    {
      m_dragging  = true;
      m_dragFace  = m_gizmo->GetGrabbedFace();

      if (m_dragFace == BoxFace::None)
      {
        m_dragging = false;
        return;
      }

      EditorScenePtr scene = GetApp()->GetCurrentScene();
      m_dragEntity         = scene->GetCurrentSelection();
      if (m_dragEntity == nullptr)
      {
        m_dragging = false;
        return;
      }

      m_dragEnvComp = m_dragEntity->GetComponent<EnvironmentComponent>();
      if (m_dragEnvComp == nullptr)
      {
        m_dragging = false;
        return;
      }

      m_dragStartSize   = m_dragEnvComp->GetSizeVal();
      m_dragStartOffset = m_dragEnvComp->GetPositionOffsetVal();

      // Build drag plane that contains the face normal direction but faces the camera.
      // This avoids near-parallel ray-plane intersection failures.
      Vec3 faceNormal       = BoxEditGizmo::GetFaceNormal(m_dragFace);
      const BoundingBox& bb = m_gizmo->GetTargetBox();
      Vec3 faceCenter       = bb.GetCenter();
      Vec3 halfSize         = (bb.max - bb.min) * 0.5f;
      faceCenter           += faceNormal * (halfSize.x * glm::abs(faceNormal.x) +
                                            halfSize.y * glm::abs(faceNormal.y) +
                                            halfSize.z * glm::abs(faceNormal.z));

      EditorViewportPtr vp = GetApp()->GetActiveViewport();
      if (vp == nullptr)
      {
        m_dragging = false;
        return;
      }

      // Choose a plane that contains the drag axis (faceNormal) but is most visible to the camera.
      // Get camera direction.
      Vec3 camDir = vp->GetCamera()->GetComponent<DirectionComponent>()->GetDirection();

      // Find the best plane normal: perpendicular to faceNormal, and most aligned with camDir.
      // Two candidate normals are the other two world axes.
      Vec3 candidates[2];
      int ci = 0;
      for (int i = 0; i < 3; i++)
      {
        if (glm::abs(faceNormal[i]) < 0.5f)
        {
          Vec3 axis(0.0f);
          axis[i]          = 1.0f;
          candidates[ci++] = axis;
          if (ci == 2) break;
        }
      }

      // Pick the candidate whose dot with camDir is largest in absolute value.
      Vec3 planeNormal;
      if (glm::abs(glm::dot(candidates[0], camDir)) >= glm::abs(glm::dot(candidates[1], camDir)))
      {
        planeNormal = candidates[0];
      }
      else
      {
        planeNormal = candidates[1];
      }

      // Make sure plane normal points towards camera.
      if (glm::dot(planeNormal, camDir) > 0.0f)
      {
        planeNormal = -planeNormal;
      }

      m_dragPlane.normal = planeNormal;
      m_dragPlane.d      = -glm::dot(planeNormal, faceCenter);

      // Find initial intersection point.
      Ray ray = vp->RayFromMousePosition();
      float t;
      if (RayPlaneIntersection(ray, m_dragPlane, t))
      {
        m_dragStartPoint = PointOnRay(ray, t);
      }
      else
      {
        m_dragging = false;
      }
    }

    void BoxEditMod::UpdateDrag(const Vec2& mousePos)
    {
      if (!m_dragging || m_dragFace == BoxFace::None)
      {
        return;
      }

      EditorViewportPtr vp = GetApp()->GetActiveViewport();
      if (vp == nullptr)
      {
        return;
      }

      Ray ray = vp->RayFromMousePosition();
      float t;
      if (!RayPlaneIntersection(ray, m_dragPlane, t))
      {
        return;
      }

      Vec3 currentPoint = PointOnRay(ray, t);
      Vec3 faceNormal   = BoxEditGizmo::GetFaceNormal(m_dragFace);

      // Project delta onto face normal to get 1D movement.
      float delta = glm::dot(currentPoint - m_dragStartPoint, faceNormal);

      // Snap.
      if (GetApp()->m_snapsEnabled)
      {
        float spacing = GetApp()->m_moveDelta;
        delta         = glm::round(delta / spacing) * spacing;
      }

      // Compute new Size and PositionOffset.
      Vec3 newSize   = m_dragStartSize;
      Vec3 newOffset = m_dragStartOffset;

      int axisIndex  = static_cast<int>(m_dragFace) / 2; // 0=X, 1=Y, 2=Z
      bool isPositiveFace = (static_cast<int>(m_dragFace) % 2) == 0;

      // Delta is always positive when dragging outward along the face normal.
      // Size always grows with positive delta.
      // Offset shifts in the direction of the face normal.
      newSize[axisIndex] += delta;
      if (isPositiveFace)
      {
        newOffset[axisIndex] += delta * 0.5f;
      }
      else
      {
        newOffset[axisIndex] -= delta * 0.5f;
      }

      // Clamp size to minimum.
      newSize[axisIndex] = glm::max(newSize[axisIndex], 0.01f);

      m_dragEnvComp->SetSizeVal(newSize);
      m_dragEnvComp->SetPositionOffsetVal(newOffset);
    }

    void BoxEditMod::EndDrag()
    {
      m_dragging = false;
      m_dragFace = BoxFace::None;
      if (m_gizmo)
      {
        m_gizmo->Grab(AxisLabel::None);
      }
      m_dragEntity  = nullptr;
      m_dragEnvComp = nullptr;
    }

  } // namespace Editor
} // namespace ToolKit
