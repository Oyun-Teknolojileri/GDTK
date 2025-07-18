/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Mod.h"

#include "AnchorMod.h"
#include "App.h"
#include "ConsoleWindow.h"
#include "EditorViewport2d.h"
#include "Grid.h"
#include "TransformMod.h"

#include <Camera.h>
#include <DirectionComponent.h>
#include <Entity.h>
#include <MathUtil.h>
#include <Mesh.h>
#include <Prefab.h>
#include <Surface.h>

namespace ToolKit
{
  namespace Editor
  {

    // ModManager
    //////////////////////////////////////////

    ModManager ModManager::m_instance;

    ModManager::~ModManager() { assert(m_initiated == false && "Call UnInit."); }

    ModManager* ModManager::GetInstance() { return &m_instance; }

    void ModManager::Update(float deltaTime)
    {
      if (m_modStack.empty())
      {
        return;
      }

      BaseMod* currentMod = m_modStack.back();
      currentMod->Update(deltaTime);
    }

    void ModManager::DispatchSignal(SignalId signal)
    {
      if (m_modStack.empty())
      {
        return;
      }

      m_modStack.back()->Signal(signal);
    }

    void ModManager::SetMod(bool set, ModId mod)
    {
      if (set)
      {
        if (m_modStack.back()->m_id != ModId::Base)
        {
          BaseMod* prevMod = m_modStack.back();
          prevMod->UnInit();
          SafeDel(prevMod);
          m_modStack.pop_back();
        }

        static String modNameDbg;
        BaseMod* nextMod = nullptr;
        switch (mod)
        {
        case ModId::Select:
          nextMod    = new SelectMod();
          modNameDbg = "Mod: Select";
          break;
        case ModId::Cursor:
          nextMod    = new CursorMod();
          modNameDbg = "Mod: Cursor";
          break;
        case ModId::Move:
          nextMod    = new TransformMod(mod);
          modNameDbg = "Mod: Move";
          break;
        case ModId::Rotate:
          nextMod    = new TransformMod(mod);
          modNameDbg = "Mod: Rotate";
          break;
        case ModId::Scale:
          nextMod    = new TransformMod(mod);
          modNameDbg = "Mod: Scale";
          break;
        case ModId::Anchor:
          nextMod    = new AnchorMod(mod);
          modNameDbg = "Mod: Anchor";
          break;
        case ModId::Base:
        default:
          break;
        }

        assert(nextMod);
        if (nextMod != nullptr)
        {
          nextMod->Init();
          m_modStack.push_back(nextMod);

          // #ConsoleDebug_Mod
          if (GetApp()->m_showStateTransitionsDebug)
          {
            if (ConsoleWindowPtr console = GetApp()->GetConsole())
            {
              console->AddLog(modNameDbg, "ModDbg");
            }
          }
        }

        // If the state is changed while the previous state is being actively
        // used (in StateTransitionTo state), delete the last function
        // pointers from the array, since the function
        // parameters are not valid anymore.
        if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
        {
          vp->m_drawCommands.clear();
        }
      }
    }

    ModManager::ModManager() { m_initiated = false; }

    void ModManager::Init()
    {
      if (m_initiated)
      {
        return;
      }

      m_modStack.push_back(new BaseMod(ModId::Base));
      m_initiated = true;
    }

    void ModManager::UnInit()
    {
      for (BaseMod* mod : m_modStack)
      {
        SafeDel(mod);
      }
      m_modStack.clear();

      m_initiated = false;
    }

    // BaseMod
    //////////////////////////////////////////

    // Signal definitions.
    SignalId BaseMod::m_leftMouseBtnDownSgnl = BaseMod::GetNextSignalId();
    SignalId BaseMod::m_leftMouseBtnUpSgnl   = BaseMod::GetNextSignalId();
    SignalId BaseMod::m_leftMouseBtnDragSgnl = BaseMod::GetNextSignalId();
    SignalId BaseMod::m_mouseMoveSgnl        = BaseMod::GetNextSignalId();
    SignalId BaseMod::m_backToStart          = BaseMod::GetNextSignalId();
    SignalId BaseMod::m_delete               = BaseMod::GetNextSignalId();
    SignalId BaseMod::m_duplicate            = BaseMod::GetNextSignalId();

    BaseMod::BaseMod(ModId id)
    {
      m_id           = id;
      m_stateMachine = new StateMachine();
    }

    BaseMod::~BaseMod() { SafeDel(m_stateMachine); }

    void BaseMod::Init() {}

    void BaseMod::UnInit() {}

    void BaseMod::Update(float deltaTime) { m_stateMachine->Update(deltaTime); }

    void BaseMod::Signal(SignalId signal)
    {
      State* prevStateDbg = m_stateMachine->m_currentState;

      m_stateMachine->Signal(signal);

      // #ConsoleDebug_Mod
      if (GetApp()->m_showStateTransitionsDebug)
      {
        State* nextState = m_stateMachine->m_currentState;
        if (prevStateDbg != nextState)
        {
          if (prevStateDbg && nextState)
          {
            if (ConsoleWindowPtr consol = GetApp()->GetConsole())
            {
              String log = "\t" + prevStateDbg->GetType() + " -> " + nextState->GetType();
              consol->AddLog(log, "ModDbg");
            }
          }
        }
      }
    }

    int BaseMod::GetNextSignalId()
    {
      static int signalCounter = 100;
      return ++signalCounter;
    }

    // State Types
    //////////////////////////////////////////

    const String StateType::Null                = "";
    const String StateType::StateBeginPick      = "StateBeginPick";
    const String StateType::StateBeginBoxPick   = "StateBeginBoxPick";
    const String StateType::StateEndPick        = "StateEndPick";
    const String StateType::StateDeletePick     = "StateDeletePick";
    const String StateType::StateTransformBegin = "StateTransformBegin";
    const String StateType::StateTransformTo    = "StateTransformTo";
    const String StateType::StateTransformEnd   = "StateTransformEnd";
    const String StateType::StateDuplicate      = "StateDuplicate";
    const String StateType::StateAnchorBegin    = "StateAnchorBegin";
    const String StateType::StateAnchorTo       = "StateAnchorTo";
    const String StateType::StateAnchorEnd      = "StateAnchorEnd";

    // StatePickingBase
    //////////////////////////////////////////

    StatePickingBase::StatePickingBase() { m_mouseData.resize(2); }

    void StatePickingBase::TransitionIn(State* prevState) {}

    void StatePickingBase::TransitionOut(State* nextState)
    {
      if (StatePickingBase* baseState = dynamic_cast<StatePickingBase*>(nextState))
      {
        baseState->m_ignoreList = m_ignoreList;
        baseState->m_mouseData  = m_mouseData;

        if (nextState->GetType() != StateType::StateBeginPick)
        {
          baseState->m_pickData = m_pickData;
        }
      }

      m_pickData.clear();
    }

    bool StatePickingBase::IsIgnored(ObjectId id) { return contains(m_ignoreList, id); }

    void StatePickingBase::PickDataToEntityId(IDArray& ids)
    {
      for (EditorScene::PickData& pd : m_pickData)
      {
        if (pd.entity != nullptr)
        {
          ids.push_back(pd.entity->GetIdVal());
        }
      }
    }

    void StateBeginPick::TransitionIn(State* prevState)
    {
      // Construct the ignore list.
      m_ignoreList.clear();
      EntityPtrArray ignores;
      if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
      {
        if (vp->IsA<EditorViewport2d>())
        {
          ignores = GetApp()->GetCurrentScene()->Filter([](EntityPtr ntt) -> bool { return !ntt->IsA<Surface>(); });
        }
        else if (vp->IsA<EditorViewport>())
        {
          ignores = GetApp()->GetCurrentScene()->Filter([](EntityPtr ntt) -> bool { return ntt->IsA<Surface>(); });
        }
      }

      m_ignoreList = ToEntityIdArray(ignores);
      m_ignoreList.push_back(GetApp()->m_grid->GetIdVal());
    }

    SignalId StateBeginPick::Update(float deltaTime) { return NullSignal; }

    String StateBeginPick::Signaled(SignalId signal)
    {
      if (signal == BaseMod::m_leftMouseBtnDownSgnl)
      {
        if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
        {
          m_mouseData[0] = vp->GetLastMousePosScreenSpace();
        }
      }

      if (signal == BaseMod::m_leftMouseBtnUpSgnl)
      {
        if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
        {
          m_mouseData[0]           = vp->GetLastMousePosScreenSpace();

          Ray ray                  = vp->RayFromMousePosition();
          EditorScenePtr currScene = GetApp()->GetCurrentScene();
          EditorScene::PickData pd = currScene->PickObject(ray, m_ignoreList);
          m_pickData.push_back(pd);

          if (GetApp()->m_showPickingDebug)
          {
            GetApp()->m_cursor->m_worldLocation = pd.pickPos;

            if (GetApp()->m_dbgArrow != nullptr)
            {
              GetApp()->m_dbgArrow->m_node->SetTranslation(pd.pickPos + (ray.position - pd.pickPos) * 0.1f);
              GetApp()->m_dbgArrow->m_node->SetOrientation(RotationTo(X_AXIS, ray.direction));
            }
          }

          return StateType::StateEndPick;
        }
      }

      if (signal == BaseMod::m_leftMouseBtnDragSgnl)
      {
        return StateType::StateBeginBoxPick;
      }

      if (signal == BaseMod::m_delete)
      {
        return StateType::StateDeletePick;
      }

      return StateType::Null;
    }

    SignalId StateBeginBoxPick::Update(float deltaTime) { return NullSignal; }

    String StateBeginBoxPick::Signaled(SignalId signal)
    {
      if (signal == BaseMod::m_leftMouseBtnUpSgnl)
      {
        // Frustum - AABB test.
        if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
        {
          CameraPtr cam = vp->GetCamera();

          // Constructs mouse rectangle from lower left CCW.
          Vec2 rect[4];
          GetMouseRect(rect[0], rect[2]);

          rect[1].x = rect[2].x;
          rect[1].y = rect[0].y;
          rect[3].x = rect[0].x;
          rect[3].y = rect[2].y;

          std::vector<Ray> rays;
          Vec3Array rect3d;

          // Front rectangle in world space. From Upper left corner CW
          // Conversion from lower left ccw to upper left cw happens during screen to viewport conversion.
          Vec3 lensLoc = cam->m_node->GetTranslation(TransformationSpace::TS_WORLD);
          for (int i = 0; i < 4; i++)
          {
            Vec2 p  = vp->TransformScreenToViewportSpace(rect[i]); // ccw to cw.
            Vec3 p0 = vp->TransformViewportToWorldSpace(p);
            rect3d.push_back(p0);
            if (cam->IsOrtographic())
            {
              rays.push_back({lensLoc, cam->GetComponent<DirectionComponent>()->GetDirection()});
            }
            else
            {
              rays.push_back({lensLoc, glm::normalize(p0 - lensLoc)});
            }
          }

          // Back rectangle in world space.
          float depth = 1000.0f;
          for (int i = 0; i < 4; i++)
          {
            Vec3 p = rect3d[i] + rays[i].direction * depth;
            rect3d.push_back(p);
          }

          // Frustum from 8 points.
          Frustum frustum;
          Vec3Array planePnts;

          // Create plane equations such that normals point in to frustum.
          planePnts         = {rect3d[3], rect3d[7], rect3d[4]}; // Left plane.
          frustum.planes[0] = PlaneFrom(planePnts.data());

          planePnts         = {rect3d[2], rect3d[5], rect3d[6]}; // Right plane.
          frustum.planes[1] = PlaneFrom(planePnts.data());

          planePnts         = {rect3d[1], rect3d[4], rect3d[5]}; // Top plane.
          frustum.planes[2] = PlaneFrom(planePnts.data());

          planePnts         = {rect3d[2], rect3d[6], rect3d[7]}; // Bottom plane.
          frustum.planes[3] = PlaneFrom(planePnts.data());

          planePnts         = {rect3d[3], rect3d[1], rect3d[2]}; // Near plane.
          frustum.planes[4] = PlaneFrom(planePnts.data());

          planePnts         = {rect3d[7], rect3d[6], rect3d[5]}; // Far plane.
          frustum.planes[5] = PlaneFrom(planePnts.data());

          // Perform picking.
          std::vector<EditorScene::PickData> entities;
          EditorScenePtr currScene = GetApp()->GetCurrentScene();
          currScene->PickObject(frustum, entities, m_ignoreList);
          m_pickData.insert(m_pickData.end(), entities.begin(), entities.end());

          // Debug draw the picking frustum.
          if (GetApp()->m_showPickingDebug)
          {
            Vec3Array corners = {rect3d[0],
                                 rect3d[1],
                                 rect3d[1],
                                 rect3d[2],
                                 rect3d[2],
                                 rect3d[3],
                                 rect3d[3],
                                 rect3d[0],
                                 rect3d[0],
                                 rect3d[0] + rays[0].direction * depth,
                                 rect3d[1],
                                 rect3d[1] + rays[1].direction * depth,
                                 rect3d[2],
                                 rect3d[2] + rays[2].direction * depth,
                                 rect3d[3],
                                 rect3d[3] + rays[3].direction * depth,
                                 rect3d[0] + rays[0].direction * depth,
                                 rect3d[1] + rays[1].direction * depth,
                                 rect3d[1] + rays[1].direction * depth,
                                 rect3d[2] + rays[2].direction * depth,
                                 rect3d[2] + rays[2].direction * depth,
                                 rect3d[3] + rays[3].direction * depth,
                                 rect3d[3] + rays[3].direction * depth,
                                 rect3d[0] + rays[0].direction * depth};

            // Generate debug frustum.
            if (GetApp()->m_dbgFrustum == nullptr)
            {
              GetApp()->m_dbgFrustum = MakeNewPtr<LineBatch>();
              GetApp()->m_dbgFrustum->Generate(corners, X_AXIS, DrawType::Line);
            }
            else
            {
              GetApp()->m_dbgFrustum->Generate(corners, X_AXIS, DrawType::Line);
            }
          }
        }

        return StateType::StateEndPick;
      }

      if (signal == BaseMod::m_leftMouseBtnDragSgnl)
      {
        if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
        {
          m_mouseData[1] = vp->GetLastMousePosScreenSpace();

          if (!vp->IsMoving())
          {
            auto drawSelectionRectangleFn = [this](ImDrawList* drawList) -> void
            {
              Vec2 min, max;
              GetMouseRect(min, max);

              ImU32 col = ImColor(g_selectBoxWindowColor);
              drawList->AddRectFilled(min, max, col, 5.0f, ImDrawFlags_RoundCornersAll);

              col = ImColor(g_selectBoxBorderColor);
              drawList->AddRect(min, max, col, 5.0f, ImDrawFlags_RoundCornersAll);
            };

            vp->m_drawCommands.push_back(drawSelectionRectangleFn);
          }
        }
      }

      return StateType::Null;
    }

    void StateBeginBoxPick::GetMouseRect(Vec2& min, Vec2& max)
    {
      min = Vec2(TK_FLT_MAX);
      max = Vec2(-TK_FLT_MAX);

      for (int i = 0; i < 2; i++)
      {
        min = glm::min(min, m_mouseData[i]);
        max = glm::max(max, m_mouseData[i]);
      }
    }

    // StateEndPick
    //////////////////////////////////////////

    SignalId StateEndPick::Update(float deltaTime) { return NullSignal; }

    String StateEndPick::Signaled(SignalId signal) { return StateType::Null; }

    // StateDeletePick
    //////////////////////////////////////////

    SignalId StateDeletePick::Update(float deltaTime)
    {
      WindowPtr activeWnd = GetApp()->GetActiveWindow();

      // Prevent delete key during the text edit from deleting entities.
      if (UI::IsKeyboardCaptured())
      {
        return NullSignal;
      }

      // Gather the selection hierarchy.
      EntityPtrArray deleteList;
      GetApp()->GetCurrentScene()->GetSelectedEntities(deleteList);

      EntityPtrArray roots;
      GetRootEntities(deleteList, roots);

      deleteList.clear();
      for (EntityPtr ntt : roots)
      {
        // Gather hierarchy from parent to child.
        deleteList.push_back(ntt);
        if (ntt->IsA<Prefab>())
        {
          // Entity will already delete its own children.
          continue;
        }
        GetChildren(ntt, deleteList);
      }

      // Revert to recover hierarchies.
      std::reverse(deleteList.begin(), deleteList.end());

      uint deleteActCount = 0;
      if (!deleteList.empty())
      {
        ActionManager::GetInstance()->BeginActionGroup();

        for (EntityPtr ntt : deleteList)
        {
          ActionManager::GetInstance()->AddAction(new DeleteAction(ntt));
          deleteActCount++;
        }
        ActionManager::GetInstance()->GroupLastActions(deleteActCount);
      }

      return NullSignal;
    }

    String StateDeletePick::Signaled(SignalId signal) { return StateType::Null; }

    // StateDuplicate
    //////////////////////////////////////////

    void StateDuplicate::TransitionIn(State* prevState)
    {
      EntityPtrArray selecteds;
      EditorScenePtr currScene = GetApp()->GetCurrentScene();
      currScene->GetSelectedEntities(selecteds);
      if (!selecteds.empty())
      {
        currScene->ClearSelection();
        if (selecteds.size())
        {
          ActionManager::GetInstance()->BeginActionGroup();
        }

        EntityPtrArray selectedRoots;
        GetRootEntities(selecteds, selectedRoots);

        int cpyCount = 0;
        bool copy    = ImGui::GetIO().KeyCtrl;
        if (copy)
        {
          for (EntityPtr ntt : selectedRoots)
          {
            EntityPtrArray copies;

            // Prefab will already create its own child prefab scene entities,
            // So don't need to copy them too.
            if (ntt->IsA<Prefab>())
            {
              EntityPtr cpyPrefab = Cast<Entity>(ntt->Copy());
              copies.push_back(cpyPrefab);
            }
            else
            {
              DeepCopy(ntt, copies);
            }

            copies[0]->m_node->SetTransform(ntt->m_node->GetTransform(), TransformationSpace::TS_WORLD);

            for (EntityPtr cpy : copies)
            {
              ActionManager::GetInstance()->AddAction(new CreateAction(cpy));
            }

            currScene->AddToSelection(copies.front()->GetIdVal(), true);
            cpyCount += (int) copies.size();

            GetApp()->SetStatusMsg(std::to_string(cpyCount) + " " + g_statusEntitiesCopied);
          }
        }
        ActionManager::GetInstance()->GroupLastActions(cpyCount);
      }
    }

    void StateDuplicate::TransitionOut(State* nextState) {}

    SignalId StateDuplicate::Update(float deltaTime) { return NullSignal; }

    String StateDuplicate::Signaled(SignalId signal) { return StateType::Null; }

    // SelectMod
    //////////////////////////////////////////

    SelectMod::SelectMod() : BaseMod(ModId::Select) {}

    void SelectMod::Init()
    {
      StateBeginPick* initialState   = new StateBeginPick();
      m_stateMachine->m_currentState = initialState;

      m_stateMachine->PushState(initialState);
      m_stateMachine->PushState(new StateBeginBoxPick());

      State* state                  = new StateEndPick();
      state->m_links[m_backToStart] = StateType::StateBeginPick;
      m_stateMachine->PushState(state);

      state                         = new StateDeletePick();
      state->m_links[m_backToStart] = StateType::StateBeginPick;
      m_stateMachine->PushState(state);
    }

    void SelectMod::Update(float deltaTime)
    {
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

    // CursorMod
    //////////////////////////////////////////

    CursorMod::CursorMod() : BaseMod(ModId::Cursor) {}

    void CursorMod::Init()
    {
      State* state                   = new StateBeginPick();
      m_stateMachine->m_currentState = state;
      m_stateMachine->PushState(state);

      state                                  = new StateEndPick();
      state->m_links[BaseMod::m_backToStart] = StateType::StateBeginPick;
      m_stateMachine->PushState(state);
    }

    void CursorMod::Update(float deltaTime)
    {
      BaseMod::Update(deltaTime);

      if (m_stateMachine->m_currentState->GetType() == StateType::StateEndPick)
      {
        StateEndPick* endPick               = static_cast<StateEndPick*>(m_stateMachine->m_currentState);
        EditorScene::PickData& pd           = endPick->m_pickData.back();
        GetApp()->m_cursor->m_worldLocation = pd.pickPos;

        m_stateMachine->Signal(BaseMod::m_backToStart);
      }
    }

  } // namespace Editor
} // namespace ToolKit
