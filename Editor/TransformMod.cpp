/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "TransformMod.h"

#include "App.h"
#include "EditorViewport.h"

#include <MathUtil.h>
#include <SDL.h>

namespace ToolKit
{
  namespace Editor
  {

    // StateMoveBase
    //////////////////////////////////////////

    StateTransformBase::StateTransformBase()
    {
      m_gizmo = nullptr;
      m_type  = TransformType::Translate;

      m_mouseData.resize(2);
    }

    SignalId StateTransformBase::Update(float deltaTime)
    {
      EditorScenePtr currScene = GetApp()->GetCurrentScene();
      if (currScene->GetSelectedEntityCount() == 0)
      {
        GetApp()->m_gizmo = nullptr;
        return NullSignal;
      }

      EntityPtr ntt = currScene->GetCurrentSelection();
      if (ntt != nullptr)
      {
        // Get world location as gizmo origin.
        m_gizmo->m_worldLocation = ntt->m_node->GetTranslation();

        if (GetApp()->m_transformSpace == TransformationSpace::TS_LOCAL)
        {
          m_gizmo->m_normalVectors = ntt->m_node->GetTransformAxes();
        }
        else
        {
          m_gizmo->m_normalVectors = Mat3();
        }
      }

      m_gizmo->Update(deltaTime);
      return NullSignal;
    }

    void StateTransformBase::TransitionIn(State* prevState) {}

    void StateTransformBase::TransitionOut(State* nextState)
    {
      StateTransformBase* baseState = dynamic_cast<StateTransformBase*>(nextState);

      if (baseState != nullptr)
      {
        baseState->m_gizmo             = m_gizmo;
        baseState->m_mouseData         = m_mouseData;
        baseState->m_intersectionPlane = m_intersectionPlane;
        baseState->m_type              = m_type;
      }
    }

    void StateTransformBase::MakeSureGizmoIsValid()
    {
      if (GetApp()->m_gizmo == nullptr)
      {
        EntityPtr ntt = GetApp()->GetCurrentScene()->GetCurrentSelection();
        if (ntt != nullptr)
        {
          GetApp()->m_gizmo = m_gizmo;
        }
      }
    }

    Vec3 StateTransformBase::GetGrabbedAxis(int n)
    {
      Vec3 axes[3];
      ExtractAxes(m_gizmo->m_normalVectors, axes[0], axes[1], axes[2]);

      int first = (int) (m_gizmo->GetGrabbedAxis()) % 3;
      if (n == 0)
      {
        return axes[first];
      }

      int second = (first + 1) % 3;
      return axes[second];
    }

    bool StateTransformBase::IsPlaneMod() { return m_gizmo->GetGrabbedAxis() > AxisLabel::Z; }

    // StateTransformBegin
    //////////////////////////////////////////

    void StateTransformBegin::TransitionIn(State* prevState) { StateTransformBase::TransitionIn(prevState); }

    void StateTransformBegin::TransitionOut(State* nextState)
    {
      StateTransformBase::TransitionOut(nextState);

      if (nextState->ThisIsA<StateBeginPick>())
      {
        StateBeginPick* baseNext = static_cast<StateBeginPick*>(nextState);
        baseNext->m_mouseData    = m_mouseData;

        if (!baseNext->IsIgnored(m_gizmo->GetIdVal()))
        {
          baseNext->m_ignoreList.push_back(m_gizmo->GetIdVal());
        }
      }
    }

    SignalId StateTransformBegin::Update(float deltaTime)
    {
      StateTransformBase::Update(deltaTime); // Update gizmo's loc & view.

      MakeSureGizmoIsValid();

      EntityPtr ntt = GetApp()->GetCurrentScene()->GetCurrentSelection();
      if (ntt != nullptr)
      {
        EditorViewportPtr vp = GetApp()->GetActiveViewport();
        if (vp == nullptr)
        {
          // Console commands may put the process here whit out active viewport.
          return NullSignal;
        }

        Vec3 camOrg             = vp->GetCamera()->m_node->GetTranslation(TransformationSpace::TS_WORLD);
        Vec3 gizmOrg            = m_gizmo->m_node->GetTranslation(TransformationSpace::TS_WORLD);

        Vec3 dir                = glm::normalize(camOrg - gizmOrg);
        m_gizmo->m_initialPoint = gizmOrg;

        float safetyMeasure     = glm::abs(glm::cos(glm::radians(5.0f)));
        AxisLabel axisLabes[3]  = {AxisLabel::X, AxisLabel::Y, AxisLabel::Z};

        Vec3 axes[3];
        ExtractAxes(m_gizmo->m_normalVectors, axes[0], axes[1], axes[2]);

        if (m_type != TransformType::Rotate)
        {
          for (int i = 0; i < 3; i++)
          {
            if (safetyMeasure < glm::abs(glm::dot(dir, axes[i])))
            {
              m_gizmo->Lock(axisLabes[i]);
            }
            else
            {
              m_gizmo->UnLock(axisLabes[i]);
            }
          }
        }

        // Highlight on mouse over.
        AxisLabel axis = m_gizmo->HitTest(vp->RayFromMousePosition());
        if (axis != AxisLabel::None)
        {
          if (!m_gizmo->IsLocked(axis))
          {
            m_gizmo->m_lastHovered = axis;
          }
        }
      }

      return NullSignal;
    }

    String StateTransformBegin::Signaled(SignalId signal)
    {
      if (signal == BaseMod::m_leftMouseBtnDownSgnl)
      {
        if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
        {
          m_mouseData[0] = vp->GetLastMousePosScreenSpace();
          AxisLabel axis = m_gizmo->HitTest(vp->RayFromMousePosition());
          if (!m_gizmo->IsLocked(axis))
          {
            m_gizmo->Grab(axis);
          }
        }

        EntityPtr ntt = GetApp()->GetCurrentScene()->GetCurrentSelection();
        if (m_gizmo->IsGrabbed(AxisLabel::None) || ntt == nullptr)
        {
          return StateType::StateBeginPick;
        }
        else
        {
          CalculateIntersectionPlane();
          CalculateGrabPoint();
        }
      }

      if (signal == BaseMod::m_leftMouseBtnUpSgnl)
      {
        m_gizmo->Grab(AxisLabel::None);
        m_gizmo->m_grabPoint = ZERO;
      }

      if (signal == BaseMod::m_leftMouseBtnDragSgnl)
      {
        EntityPtr ntt = GetApp()->GetCurrentScene()->GetCurrentSelection();
        if (ntt == nullptr)
        {
          return StateType::Null;
        }

        if (!m_gizmo->IsGrabbed(AxisLabel::None))
        {
          return StateType::StateTransformTo;
        }
      }

      if (signal == BaseMod::m_delete)
      {
        return StateType::StateDeletePick;
      }

      if (signal == BaseMod::m_duplicate)
      {
        return StateType::StateDuplicate;
      }

      return StateType::Null;
    }

    String StateTransformBegin::GetType() { return StateType::StateTransformBegin; }

    void StateTransformBegin::CalculateIntersectionPlane()
    {
      if (PolarGizmo* pg = m_gizmo->As<PolarGizmo>())
      {
        // Polar intersection plane.
        if (m_gizmo->GetGrabbedAxis() <= AxisLabel::Z)
        {
          assert(m_gizmo->GetGrabbedAxis() != AxisLabel::None);

          Vec3 p              = m_gizmo->m_worldLocation;
          Vec3 axis           = GetGrabbedAxis(0);
          m_intersectionPlane = PlaneFrom(p, axis);
        }
      }
      else
      {
        // Linear intersection plane.
        Vec3 camOrg;
        if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
        {
          camOrg = vp->GetCamera()->m_node->GetTranslation(TransformationSpace::TS_WORLD);
        }

        Vec3 gizmOrg = m_gizmo->m_worldLocation;
        Vec3 dir     = glm::normalize(camOrg - gizmOrg);

        Vec3 x, px, y, py, z, pz;
        ExtractAxes(m_gizmo->m_normalVectors, x, y, z);
        switch (m_gizmo->GetGrabbedAxis())
        {
        case AxisLabel::X:
          px = x;
          break;
        case AxisLabel::Y:
          px = y;
          break;
        case AxisLabel::Z:
          px = z;
          break;
        case AxisLabel::XY:
          m_intersectionPlane = PlaneFrom(gizmOrg, z);
          break;
        case AxisLabel::YZ:
          m_intersectionPlane = PlaneFrom(gizmOrg, x);
          break;
        case AxisLabel::ZX:
          m_intersectionPlane = PlaneFrom(gizmOrg, y);
          break;
        case AxisLabel::XYZ:
          m_intersectionPlane = PlaneFrom(gizmOrg, x);
          break;
        default:
          assert(false);
        }

        if (m_gizmo->GetGrabbedAxis() <= AxisLabel::Z)
        {
          py                  = glm::normalize(glm::cross(px, dir));
          pz                  = glm::normalize(glm::cross(py, px));
          m_intersectionPlane = PlaneFrom(gizmOrg, pz);
        }
      }
    }

    void StateTransformBegin::CalculateGrabPoint()
    {
      assert(m_gizmo->GetGrabbedAxis() != AxisLabel::None);
      m_gizmo->m_grabPoint = ZERO;

      if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
      {
        float t;
        Ray ray = vp->RayFromMousePosition();
        if (RayPlaneIntersection(ray, m_intersectionPlane, t))
        {
          m_gizmo->m_grabPoint = PointOnRay(ray, t);
          if (m_gizmo->IsA<PolarGizmo>())
          {
            m_gizmo->m_grabPoint -= m_gizmo->m_worldLocation;
            m_gizmo->m_grabPoint  = glm::normalize(m_gizmo->m_grabPoint);
          }
        }
      }
    }

    // TransformAction
    //////////////////////////////////////////

    TransformAction::TransformAction(EntityPtr ntt)
    {
      m_entity    = ntt;
      m_transform = ntt->m_node->GetTransform();
    }

    TransformAction::~TransformAction() {}

    void TransformAction::Undo() { Swap(); }

    void TransformAction::Redo() { Swap(); }

    void TransformAction::Swap()
    {
      Mat4 backUp = m_entity->m_node->GetTransform();
      m_entity->m_node->SetTransform(m_transform, TransformationSpace::TS_WORLD);
      m_transform = backUp;
    }

    // StateTransformTo
    //////////////////////////////////////////

    void StateTransformTo::TransitionIn(State* prevState)
    {
      StateTransformBase::TransitionIn(prevState);

      EntityPtrArray entities, selecteds;
      EditorScenePtr currScene = GetApp()->GetCurrentScene();
      currScene->GetSelectedEntities(selecteds);
      GetRootEntities(selecteds, entities);
      if (!entities.empty())
      {
        if (entities.size() > 1)
        {
          ActionManager::GetInstance()->BeginActionGroup();
        }

        int actionEntityCount = 0;
        for (EntityPtr ntt : entities)
        {
          if (ntt->GetTransformLockVal())
          {
            continue;
          }

          actionEntityCount++;
          ActionManager::GetInstance()->AddAction(new TransformAction(ntt));
        }
        ActionManager::GetInstance()->GroupLastActions(actionEntityCount);
      }

      m_delta      = ZERO;
      m_deltaAccum = ZERO;
      m_initialLoc = currScene->GetCurrentSelection()->m_node->GetTranslation();
      SDL_GetGlobalMouseState(&m_mouseInitialLoc.x, &m_mouseInitialLoc.y);
    }

    void StateTransformTo::TransitionOut(State* prevState)
    {
      StateTransformBase::TransitionOut(prevState);
      m_gizmo->m_grabPoint = ZERO;

      // Set the mouse position roughly.
      SDL_WarpMouseGlobal((int) (m_mouseData[1].x), (int) (m_mouseData[1].y));
    }

    SignalId StateTransformTo::Update(float deltaTime)
    {
      Transform(m_delta);
      StateTransformBase::Update(deltaTime);
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
      if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
      {
        Vec2 contentMin, contentMax;
        vp->GetContentAreaScreenCoordinates(&contentMin, &contentMax);

        auto drawMoveCursorFn = [this, contentMin, contentMax](ImDrawList* drawList) -> void
        {
          // Clamp the mouse pos.
          Vec2 pos = m_mouseData[1];
          pos      = glm::clamp(pos, contentMin, contentMax);

          // Draw cursor.
          Vec2 size(28.0f);
          drawList->AddImage(Convert2ImGuiTexture(UI::m_moveIcn), pos - size * 0.5f, pos + size * 0.5f);
        };

        vp->m_drawCommands.push_back(drawMoveCursorFn);
      }
      return NullSignal;
    }

    String StateTransformTo::Signaled(SignalId signal)
    {
      if (signal == BaseMod::m_leftMouseBtnDragSgnl)
      {
        CalculateDelta();
      }

      if (signal == BaseMod::m_leftMouseBtnUpSgnl)
      {
        return StateType::StateTransformEnd;
      }

      return StateType::Null;
    }

    String StateTransformTo::GetType() { return StateType::StateTransformTo; }

    void StateTransformTo::CalculateDelta()
    {
      // Calculate delta.
      IVec2 mouseLoc;
      SDL_GetGlobalMouseState(&mouseLoc.x, &mouseLoc.y);
      m_mouseData[1] = m_mouseData[0] + Vec2(mouseLoc - m_mouseInitialLoc);

      // Warp mouse move.
      SDL_WarpMouseGlobal(m_mouseInitialLoc.x, m_mouseInitialLoc.y);

      if (m_type == TransformType::Rotate)
      {
        // Calculate angular offset.
        if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
        {
          Ray ray0         = vp->RayFromScreenSpacePoint(m_mouseData[0]);
          Ray ray1         = vp->RayFromScreenSpacePoint(m_mouseData[1]);

          Vec3 gizmoCenter = m_gizmo->m_worldLocation;

          float t          = 0.0f;
          Vec3 p0; // Point 0 on gizmo.
          if (RayPlaneIntersection(ray0, m_intersectionPlane, t))
          {
            p0 = PointOnRay(ray0, t);
            p0 = glm::normalize(p0 - gizmoCenter);
          }

          Vec3 p1; // Point 1 on gizmo.
          if (RayPlaneIntersection(ray0, m_intersectionPlane, t))
          {
            p1                   = PointOnRay(ray1, t);
            p1                   = glm::normalize(p1 - gizmoCenter);
            m_gizmo->m_grabPoint = p1;
          }

          m_delta       = ZERO;
          m_delta.z     = AngleBetweenVectors(p0, p1);

          // Detect signature.
          Vec3 rotNorm  = glm::cross(p0, p1);
          float sig     = glm::sign(glm::dot(rotNorm, m_intersectionPlane.normal));
          m_delta.z    *= sig;
        }
      }
      else
      {
        float t;
        if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
        {
          Ray ray = vp->RayFromScreenSpacePoint(m_mouseData[1]);
          if (RayPlaneIntersection(ray, m_intersectionPlane, t))
          {
            // This point.
            Vec3 p = PointOnRay(ray, t);

            // Previous. point.
            ray    = vp->RayFromScreenSpacePoint(m_mouseData[0]);
            RayPlaneIntersection(ray, m_intersectionPlane, t);
            Vec3 p0 = PointOnRay(ray, t);
            m_delta = p - p0;
          }
          else
          {
            assert(false && "Intersection expected.");
            m_delta = ZERO;
          }
        }
      }

      std::swap(m_mouseData[0], m_mouseData[1]);
    }

    void StateTransformTo::Transform(const Vec3& delta)
    {
      EntityPtrArray roots;
      EditorScenePtr currScene = GetApp()->GetCurrentScene();
      currScene->GetSelectedEntities(roots);

      EntityPtr currentNtt = currScene->GetCurrentSelection();
      if (currentNtt == nullptr)
      {
        return;
      }

      NodeRawPtrArray parents;

      // Make all selected entities child of current selection & store their original parents.
      for (EntityPtr ntt : roots)
      {
        parents.push_back(ntt->m_node->m_parent);
        ntt->m_node->OrphanSelf(true);
      }

      for (EntityPtr ntt : roots)
      {
        if (ntt != currentNtt)
        {
          currentNtt->m_node->AddChild(ntt->m_node, true);
        }
      }

      // Apply transform.
      if (!currentNtt->GetTransformLockVal())
      {
        if (m_type == TransformType::Translate)
        {
          Translate(currentNtt);
        }
        else if (m_type == TransformType::Rotate)
        {
          Rotate(currentNtt);
        }
        else if (m_type == TransformType::Scale)
        {
          Scale(currentNtt);
        }
      }
      else
      {
        // Warn user
        GetApp()->SetStatusMsg(g_statusFailed);
        TK_ERR("Transform failed. Transform locked.");
      }

      // Set original parents back.
      for (size_t i = 0; i < roots.size(); i++)
      {
        roots[i]->m_node->OrphanSelf(true);
        Node* parent = parents[i];
        if (parent != nullptr)
        {
          parent->AddChild(roots[i]->m_node, true);
        }
      }
    }

    void StateTransformTo::Translate(EntityPtr ntt)
    {
      Vec3 delta = m_delta;
      if (!IsPlaneMod())
      {
        int axis = (int) m_gizmo->GetGrabbedAxis();
        Vec3 dir = m_gizmo->m_normalVectors[axis];
        dir      = glm::normalize(dir);
        delta    = glm::dot(dir, m_delta) * dir;
      }

      m_deltaAccum += delta;
      Vec3 target   = ntt->m_node->GetTranslation(TransformationSpace::TS_WORLD);

      // Snap for pos.
      if (GetApp()->m_snapsEnabled)
      {
        target                = m_initialLoc + m_deltaAccum;
        float spacing         = GetApp()->m_moveDelta;
        Vec3 snapped          = glm::round(target / spacing) * spacing;

        // Apply axis lock.
        AxisLabel grabbedAxis = m_gizmo->GetGrabbedAxis();
        switch (grabbedAxis)
        {
        case AxisLabel::X:
        case AxisLabel::Y:
        case AxisLabel::Z:
          target[int(grabbedAxis)] = snapped[int(grabbedAxis)];
          break;
        case AxisLabel::YZ:
        case AxisLabel::ZX:
        case AxisLabel::XY:
          snapped[int(grabbedAxis) % 3] = target[int(grabbedAxis) % 3];
          target                        = snapped;
          break;
        default:
          break;
        }
      }
      else
      {
        target += delta;
      }

      ntt->m_node->SetTranslation(target, TransformationSpace::TS_WORLD);
    }

    void StateTransformTo::Rotate(EntityPtr ntt)
    {
      float delta     = m_delta.z;

      m_deltaAccum.x += delta;
      float spacing   = glm::radians(GetApp()->m_rotateDelta);
      if (GetApp()->m_snapsEnabled)
      {
        if (glm::abs(m_deltaAccum.x) < spacing)
        {
          return;
        }

        delta = glm::round(m_deltaAccum.x / spacing) * spacing;
      }

      m_deltaAccum.x = 0.0f;

      if (glm::notEqual(delta, 0.0f))
      {
        PolarGizmo* pg      = static_cast<PolarGizmo*>(m_gizmo.get());
        int axisInd         = (int) (m_gizmo->GetGrabbedAxis());

        Quaternion rotation = glm::angleAxis(delta, m_gizmo->m_normalVectors[axisInd]);
        ntt->m_node->Rotate(rotation, TransformationSpace::TS_WORLD);
      }
    }

    void StateTransformTo::Scale(EntityPtr ntt)
    {
      Vec3 scaleAxes[7];
      scaleAxes[(int) AxisLabel::X]    = X_AXIS;
      scaleAxes[(int) AxisLabel::Y]    = Y_AXIS;
      scaleAxes[(int) AxisLabel::Z]    = Z_AXIS;
      scaleAxes[(int) AxisLabel::XY]   = XY_AXIS;
      scaleAxes[(int) AxisLabel::YZ]   = YZ_AXIS;
      scaleAxes[(int) AxisLabel::ZX]   = ZX_AXIS;
      scaleAxes[(int) AxisLabel::XYZ]  = Vec3(1.0f);

      const BoundingBox& box           = ntt->GetBoundingBox();
      Vec3 aabbSize                    = box.max - box.min;

      int axisIndex                    = int(m_gizmo->GetGrabbedAxis());
      Vec3 axis                        = scaleAxes[axisIndex];

      aabbSize                        *= axis;
      aabbSize                         = glm::max(aabbSize, 0.0001f);
      Vec3 delta                       = Vec3(glm::length(m_delta) / glm::length(aabbSize));

      delta                           *= glm::normalize(axis);
      m_deltaAccum                    += delta;

      float spacing                    = GetApp()->m_scaleDelta;
      if (GetApp()->m_snapsEnabled)
      {
        if (IsPlaneMod())
        { // Snapping on, 2 dimension grabbed
          if (length(m_deltaAccum) < length(Vec3(spacing, spacing, 0)))
          {
            return;
          }
          delta        = m_deltaAccum;
          m_deltaAccum = Vec3(0);
        }
        else
        { // Snapping on, 1 dimension grabbed
          if (length(m_deltaAccum) < spacing)
          {
            return;
          }
          delta        = m_deltaAccum;
          m_deltaAccum = Vec3(0);
        }
      }
      else
      {
        delta        = m_deltaAccum;
        m_deltaAccum = Vec3(0);
      }

      // Transfer world space delta to local axis.
      if (axisIndex <= (int) AxisLabel::Z)
      {
        Vec3 axisDir  = m_gizmo->m_normalVectors[axisIndex % 3];
        delta        *= glm::sign(glm::dot(m_delta, axisDir));
      }
      else
      {
        // Calc. major axis sign.
        Mat3& axes   = m_gizmo->m_normalVectors;
        float mas    = 1.0f;
        float maxPrj = -1.0f;
        for (int i = 0; i < 2; i++)
        {
          float prj   = glm::dot(m_delta, axes[i]);
          float abPrj = glm::abs(prj);
          if (maxPrj < abPrj)
          {
            maxPrj = abPrj;
            mas    = glm::sign(prj);
          }
        }

        delta *= mas;
      }

      if (GetApp()->m_snapsEnabled)
      {
        for (uint i = 0; i < 3; i++)
        {
          delta[i] = glm::round(delta[i] / spacing) * spacing;
        }
      }

      Vec3 scale = Vec3(1.0f) + delta;
      if (!VecAllEqual(delta, ZERO))
      {
        ntt->m_node->Scale(scale);
      }
    }

    // StateTransformEnd
    //////////////////////////////////////////

    void StateTransformEnd::TransitionOut(State* nextState)
    {
      if (nextState->ThisIsA<StateTransformBegin>())
      {
        StateTransformBegin* baseNext = static_cast<StateTransformBegin*>(nextState);

        baseNext->m_gizmo->Grab(AxisLabel::None);
        baseNext->m_mouseData[0] = Vec2();
        baseNext->m_mouseData[1] = Vec2();
      }
    }

    SignalId StateTransformEnd::Update(float deltaTime)
    {
      StateTransformBase::Update(deltaTime);
      return NullSignal;
    }

    String StateTransformEnd::Signaled(SignalId signal)
    {
      if (signal == BaseMod::m_backToStart)
      {
        return StateType::StateTransformBegin;
      }

      return StateType::Null;
    }

    String StateTransformEnd::GetType() { return StateType::StateTransformEnd; }

    // MoveMod
    //////////////////////////////////////////

    TransformMod::TransformMod(ModId id) : BaseMod(id) { m_gizmo = nullptr; }

    TransformMod::~TransformMod() { GetApp()->m_gizmo = nullptr; }

    void TransformMod::Init()
    {
      State* state                  = new StateTransformBegin();
      StateTransformBase* baseState = static_cast<StateTransformBase*>(state);
      switch (m_id)
      {
      case ModId::Move:
        m_gizmo           = MakeNewPtr<MoveGizmo>();
        baseState->m_type = StateTransformBase::TransformType::Translate;
        break;
      case ModId::Rotate:
        m_gizmo           = MakeNewPtr<PolarGizmo>();
        baseState->m_type = StateTransformBase::TransformType::Rotate;
        break;
      case ModId::Scale:
        m_gizmo           = MakeNewPtr<ScaleGizmo>();
        baseState->m_type = StateTransformBase::TransformType::Scale;
        break;
      default:
        assert(false);
        return;
      }

      baseState->m_gizmo             = m_gizmo;
      m_stateMachine->m_currentState = state;

      m_stateMachine->PushState(state);
      m_stateMachine->PushState(new StateTransformTo());
      m_stateMachine->PushState(new StateTransformEnd());

      state                         = new StateBeginPick();
      state->m_links[m_backToStart] = StateType::StateTransformBegin;
      m_stateMachine->PushState(state);
      state                         = new StateEndPick();
      state->m_links[m_backToStart] = StateType::StateTransformBegin;
      m_stateMachine->PushState(state);

      state                         = new StateDeletePick();
      state->m_links[m_backToStart] = StateType::StateTransformBegin;
      m_stateMachine->PushState(state);

      state                         = new StateDuplicate();
      state->m_links[m_backToStart] = StateType::StateTransformBegin;
      m_stateMachine->PushState(state);

      m_prevTransformSpace = GetApp()->m_transformSpace;
      if (m_id == ModId::Scale)
      {
        GetApp()->m_transformSpace = TransformationSpace::TS_LOCAL;
      }
    }

    void TransformMod::UnInit()
    {
      if (m_id == ModId::Scale)
      {
        GetApp()->m_transformSpace = m_prevTransformSpace;
      }
    }

    void TransformMod::Update(float deltaTime)
    {
      // Set transform of the gizmo with respect to active viewport.
      // Important for proper picking.
      if (m_gizmo != nullptr)
      {
        if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
        {
          m_gizmo->LookAt(vp->GetCamera(), vp->GetBillboardScale());
        }
      }

      BaseMod::Update(deltaTime);

      if (m_stateMachine->m_currentState->ThisIsA<StateEndPick>())
      {
        StateEndPick* endPick = static_cast<StateEndPick*>(m_stateMachine->m_currentState);

        IDArray entities;
        endPick->PickDataToEntityId(entities);
        GetApp()->GetCurrentScene()->AddToSelection(entities, ImGui::GetIO().KeyShift);

        ModManager::GetInstance()->DispatchSignal(m_backToStart);
      }

      if (m_stateMachine->m_currentState->ThisIsA<StateTransformEnd>())
      {
        ModManager::GetInstance()->DispatchSignal(BaseMod::m_backToStart);
      }

      if (m_stateMachine->m_currentState->ThisIsA<StateDeletePick>())
      {
        ModManager::GetInstance()->DispatchSignal(BaseMod::m_backToStart);
      }

      if (m_stateMachine->m_currentState->ThisIsA<StateDuplicate>())
      {
        ModManager::GetInstance()->DispatchSignal(BaseMod::m_backToStart);
      }
    }

  } // namespace Editor
} // namespace ToolKit
