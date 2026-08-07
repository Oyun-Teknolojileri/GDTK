/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "BoxEditMod.h"

#include "Action.h"
#include "App.h"
#include "EditorViewport.h"
#include "TransformMod.h"

#include <AABBOverrideComponent.h>
#include <DirectionComponent.h>
#include <EnvironmentComponent.h>
#include <MathUtil.h>
#include <SDL.h>

namespace ToolKit
{
  namespace Editor
  {

    BoxEditMod::BoxEditMod(ModId id) : BaseMod(id) {}

    BoxEditMod::~BoxEditMod()
    {
      GetApp()->m_gizmo = nullptr;
      if (m_dragAction != nullptr)
      {
        SafeDel(m_dragAction);
      }
    }

    void BoxEditMod::Init()
    {
      State* state                   = new StateBeginPick();
      m_stateMachine->m_currentState = state;

      m_stateMachine->PushState(state);

      State* endPick                  = new StateEndPick();
      endPick->m_links[m_backToStart] = StateType::StateBeginPick;
      m_stateMachine->PushState(endPick);

      State* del                  = new StateDeletePick();
      del->m_links[m_backToStart] = StateType::StateBeginPick;
      m_stateMachine->PushState(del);

      m_gizmo                      = MakeNewPtr<BoxEditGizmo>();
      m_gizmo->m_preRenderCallback = [this]() { TryUpdateGizmoFromSelection(); };
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

          AxisLabel hit = m_gizmo->HitTest(vp->RayFromMousePosition());
          if (hit != AxisLabel::None)
          {
            m_gizmo->m_lastHovered = hit;
          }
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

    BoxEditContext BoxEditMod::BuildContextFromEntity(EntityPtr ntt)
    {
      BoxEditContext ctx;

      // Priority 1: EnvironmentComponent.
      if (EnvironmentComponentPtr envComp = ntt->GetComponent<EnvironmentComponent>())
      {
        ctx.GetBoundingBox = [envComp]()
        {
          // Local-space BB: PositionOffset +/- Size*0.5, without entity world position.
          Vec3 offset = envComp->GetPositionOffsetVal();
          Vec3 half   = envComp->GetSizeVal() * 0.5f;
          BoundingBox bb;
          bb.min = offset - half;
          bb.max = offset + half;
          return bb;
        };
        ctx.GetWorldTransform = [ntt]()
        {
          // Entity rotation + translation so volume becomes oriented.
          return ntt->m_node->GetTransform(TransformationSpace::TS_WORLD);
        };
        ctx.GetSize           = [envComp]() { return envComp->GetSizeVal(); };
        ctx.GetPositionOffset = [envComp]() { return envComp->GetPositionOffsetVal(); };
        ctx.SetSize           = [envComp](const Vec3& s) { envComp->SetSizeVal(s); };
        ctx.SetPositionOffset = [envComp](const Vec3& o) { envComp->SetPositionOffsetVal(o); };
        return ctx;
      }

      // Priority 2: AABBOverrideComponent.
      if (AABBOverrideComponentPtr aabbComp = ntt->GetComponent<AABBOverrideComponent>())
      {
        ctx.GetBoundingBox    = [aabbComp]() { return aabbComp->GetBoundingBox(); };
        ctx.GetWorldTransform = [ntt]() { return ntt->m_node->GetTransform(TransformationSpace::TS_WORLD); };
        ctx.GetSize           = [aabbComp]() { return aabbComp->GetSizeVal(); };
        ctx.GetPositionOffset = [aabbComp]() { return aabbComp->GetPositionOffsetVal(); };
        ctx.SetSize           = [aabbComp](const Vec3& s) { aabbComp->SetSizeVal(s); };
        ctx.SetPositionOffset = [aabbComp](const Vec3& o) { aabbComp->SetPositionOffsetVal(o); };
        return ctx;
      }

      // Priority 3: Any entity with a bounding box use local-axis scale + displacement.
      // Scale grows both directions, so we compensate with a position offset to
      // make it appear as single-face movement.
      // GetSize/SetSize work in world-space extents (localBBSize * scale) so that
      // delta from UpdateDrag (in world units) can be applied directly.
      ctx.GetBoundingBox = [ntt]()
      {
        // Return scaled local BB so handles appear at the correct world-space extents.
        BoundingBox localBB  = ntt->GetBoundingBox(false);
        Vec3 localSize       = localBB.max - localBB.min;
        localBB.max          = localBB.min + glm::max(localSize, Vec3(0.0001f));

        Vec3 scale           = ntt->m_node->GetScale();
        localBB.min         *= scale;
        localBB.max         *= scale;
        return localBB;
      };
      ctx.GetWorldTransform = [ntt]()
      {
        // Return rotation + translation only (no scale) so handle positions stay correct.
        Mat4 world = ntt->m_node->GetTransform(TransformationSpace::TS_WORLD);
        Vec3 pos, scale;
        Quaternion rot;
        DecomposeMatrix(world, &pos, &rot, &scale);
        return glm::translate(Mat4(1.0f), pos) * Mat4(glm::toMat3(rot));
      };
      ctx.GetSize = [ntt]()
      {
        // Return world-space extents: localBBSize * scale.
        BoundingBox localBB = ntt->GetBoundingBox(false);
        Vec3 localSize      = localBB.max - localBB.min;

        // Prevent size from being exactly zero on any axis
        localSize           = glm::max(localSize, Vec3(0.0001f));

        Vec3 scale          = ntt->m_node->GetScale();
        return localSize * scale;
      };
      ctx.GetPositionOffset = [ntt]() { return ntt->m_node->GetTranslation(TransformationSpace::TS_WORLD); };
      ctx.SetSize           = [ntt](const Vec3& worldSize)
      {
        // Convert world-space extents back to scale.
        BoundingBox localBB = ntt->GetBoundingBox(false);
        Vec3 localSize      = localBB.max - localBB.min;
        localSize           = glm::max(localSize, Vec3(0.0001f));

        Vec3 newScale       = worldSize / localSize;
        ntt->m_node->SetScale(newScale);
      };
      ctx.SetPositionOffset = [ntt](const Vec3& o) { ntt->m_node->SetTranslation(o, TransformationSpace::TS_WORLD); };
      ctx.worldSpaceOffset  = true;

      return ctx;
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

      BoxEditContext ctx = BuildContextFromEntity(ntt);
      if (!ctx.IsValid())
      {
        return false;
      }

      BoundingBox bb = ctx.GetBoundingBox();
      m_gizmo->SetTargetBox(bb);
      m_gizmo->SetWorldTransform(ctx.GetWorldTransform());

      return true;
    }

    void BoxEditMod::BeginDrag(const Vec2& mousePos)
    {
      m_dragging = true;
      m_dragFace = m_gizmo->GetGrabbedFace();

      if (m_dragFace == BoxFace::None)
      {
        m_dragging = false;
        return;
      }

      EditorScenePtr scene = GetApp()->GetCurrentScene();
      EntityPtr ntt        = scene->GetCurrentSelection();
      if (ntt == nullptr)
      {
        m_dragging = false;
        return;
      }

      m_dragContext = BuildContextFromEntity(ntt);
      if (!m_dragContext.IsValid())
      {
        m_dragging = false;
        return;
      }

      m_dragStartSize   = m_dragContext.GetSize();
      m_dragStartOffset = m_dragContext.GetPositionOffset();

      if (m_dragAction != nullptr)
      {
        SafeDel(m_dragAction);
      }
      m_dragAction          = new BoxEditAction(ntt);

      // Build drag plane that contains the face normal direction but faces the camera.
      Vec3 faceNormal       = m_gizmo->GetFaceNormalWorld(m_dragFace);
      const BoundingBox& bb = m_gizmo->GetTargetBox();
      Vec3 localCenter      = bb.GetCenter();
      Vec3 halfSize         = (bb.max - bb.min) * 0.5f;
      Vec3 localFaceNormal  = BoxEditGizmo::GetFaceNormalLocal(m_dragFace);
      Vec3 localFaceCenter  = localCenter + localFaceNormal * (halfSize.x * glm::abs(localFaceNormal.x) +
                                                               halfSize.y * glm::abs(localFaceNormal.y) +
                                                               halfSize.z * glm::abs(localFaceNormal.z));
      Vec3 faceCenter       = Vec3(m_gizmo->GetWorldTransform() * Vec4(localFaceCenter, 1.0f));

      EditorViewportPtr vp  = GetApp()->GetActiveViewport();
      if (vp == nullptr)
      {
        m_dragging = false;
        SafeDel(m_dragAction);
        return;
      }

      // Choose a plane that contains the drag axis (faceNormal) but is most visible to the camera.
      Vec3 camDir           = vp->GetCamera()->GetComponent<DirectionComponent>()->GetDirection();

      // Get the two local axes perpendicular to the face normal, transform to world space.
      int faceAxis          = static_cast<int>(m_dragFace) / 2;
      int candAxis0         = (faceAxis + 1) % 3;
      int candAxis1         = (faceAxis + 2) % 3;
      Vec3 localCand0       = Vec3(0.0f);
      localCand0[candAxis0] = 1.0f;
      Vec3 localCand1       = Vec3(0.0f);
      localCand1[candAxis1] = 1.0f;
      Vec3 worldCand0       = glm::normalize(Vec3(m_gizmo->GetWorldTransform() * Vec4(localCand0, 0.0f)));
      Vec3 worldCand1       = glm::normalize(Vec3(m_gizmo->GetWorldTransform() * Vec4(localCand1, 0.0f)));

      // Pick the candidate whose dot with camDir is largest in absolute value.
      Vec3 planeNormal;
      if (glm::abs(glm::dot(worldCand0, camDir)) >= glm::abs(glm::dot(worldCand1, camDir)))
      {
        planeNormal = worldCand0;
      }
      else
      {
        planeNormal = worldCand1;
      }

      // Make sure plane normal points towards camera.
      if (glm::dot(planeNormal, camDir) > 0.0f)
      {
        planeNormal = -planeNormal;
      }

      m_dragPlane.normal = planeNormal;
      m_dragPlane.d      = -glm::dot(planeNormal, faceCenter);

      // Find initial intersection point.
      Ray ray            = vp->RayFromMousePosition();
      float t;
      if (RayPlaneIntersection(ray, m_dragPlane, t))
      {
        m_dragStartPoint = PointOnRay(ray, t);
      }
      else
      {
        m_dragging = false;
        SafeDel(m_dragAction);
      }
    }

    void BoxEditMod::UpdateDrag(const Vec2& mousePos)
    {
      if (!m_dragging || m_dragFace == BoxFace::None || !m_dragContext.IsValid())
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
      Vec3 faceNormal   = m_gizmo->GetFaceNormalWorld(m_dragFace);

      // Project delta onto face normal to get 1D movement.
      float delta       = glm::dot(currentPoint - m_dragStartPoint, faceNormal);

      // Snap.
      if (GetApp()->m_snapsEnabled)
      {
        float spacing = GetApp()->m_moveDelta;
        delta         = glm::round(delta / spacing) * spacing;
      }

      // Compute new Size and PositionOffset.
      Vec3 newSize         = m_dragStartSize;
      Vec3 newOffset       = m_dragStartOffset;

      int axisIndex        = static_cast<int>(m_dragFace) / 2; // 0=X, 1=Y, 2=Z
      bool isPositiveFace  = (static_cast<int>(m_dragFace) % 2) == 0;

      // Size delta is applied on the local axis.
      newSize[axisIndex]  += delta;

      // Position offset compensates so only the dragged face moves.
      // faceNormal already points in the correct direction for both positive and negative faces.
      if (m_dragContext.worldSpaceOffset)
      {
        // World-space offset: displace along rotated face normal.
        newOffset += faceNormal * (delta * 0.5f);
      }
      else
      {
        // Local-space offset: displace along local axis.
        float sign            = isPositiveFace ? 0.5f : -0.5f;
        newOffset[axisIndex] += delta * sign;
      }

      // Clamp size to minimum.
      newSize[axisIndex] = glm::max(newSize[axisIndex], 0.01f);

      m_dragContext.SetSize(newSize);
      m_dragContext.SetPositionOffset(newOffset);
    }

    void BoxEditMod::EndDrag()
    {
      m_dragging = false;
      m_dragFace = BoxFace::None;
      if (m_gizmo)
      {
        m_gizmo->Grab(AxisLabel::None);
      }
      m_dragContext = BoxEditContext();

      if (m_dragAction != nullptr)
      {
        ActionManager::GetInstance()->AddAction(m_dragAction);
        m_dragAction = nullptr;
      }
    }

    // BoxEditAction
    //////////////////////////////////////////

    BoxEditAction::BoxEditAction(EntityPtr ntt)
    {
      m_entity           = ntt;
      m_transform        = ntt->m_node->GetTransform();

      BoxEditContext ctx = BoxEditMod::BuildContextFromEntity(ntt);
      if (ctx.IsValid())
      {
        m_size   = ctx.GetSize();
        m_offset = ctx.GetPositionOffset();
      }
    }

    BoxEditAction::~BoxEditAction() {}

    void BoxEditAction::Undo() { Swap(); }

    void BoxEditAction::Redo() { Swap(); }

    void BoxEditAction::Swap()
    {
      Vec3 currentSize = m_size, currentOffset = m_offset;
      Mat4 currentTransform = m_entity->m_node->GetTransform();

      BoxEditContext ctx    = BoxEditMod::BuildContextFromEntity(m_entity);
      if (ctx.IsValid())
      {
        currentSize   = ctx.GetSize();
        currentOffset = ctx.GetPositionOffset();

        ctx.SetSize(m_size);
        ctx.SetPositionOffset(m_offset);
      }

      m_entity->m_node->SetTransform(m_transform);

      m_transform = currentTransform;
      m_size      = currentSize;
      m_offset    = currentOffset;
    }

  } // namespace Editor
} // namespace ToolKit
