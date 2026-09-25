/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Action.h"

#include "App.h"
#include "EditorScene.h"

#include <AnimationControllerComponent.h>
#include <Prefab.h>

namespace ToolKit
{
  namespace Editor
  {

    // Action
    //////////////////////////////////////////

    Action::Action() {}

    Action::~Action()
    {
      for (Action* a : m_group)
      {
        SafeDel(a);
      }
      m_group.clear();
    }

    // DeleteAction
    //////////////////////////////////////////

    DeleteAction::DeleteAction(EntityPtr ntt)
    {
      m_parentId = NullHandle;
      m_ntt      = ntt;
      Redo();
    }

    DeleteAction::~DeleteAction()
    {
      if (m_actionComitted)
      {
        m_ntt = nullptr;
      }
    }

    void HandleCompSpecificOps(EntityPtr ntt, bool isActionCommitted)
    {
      if (isActionCommitted)
      {
        if (AnimControllerComponentPtr comp = ntt->GetComponent<AnimControllerComponent>())
        {
          comp->Stop();
        }
      }
    }

    void DeleteAction::Undo()
    {
      assert(m_ntt != nullptr);

      EditorScenePtr currScene = GetApp()->GetCurrentScene();
      currScene->AddEntity(m_ntt);

      if (m_parentId != NullHandle)
      {
        if (EntityPtr parent = currScene->GetEntity(m_parentId))
        {
          m_ntt->m_node->OrphanSelf();
          parent->m_node->AddChild(m_ntt->m_node);
        }
      }

      m_actionComitted = false;
      HandleCompSpecificOps(m_ntt, m_actionComitted);
    }

    void DeleteAction::Redo()
    {
      if (Node* pNode = m_ntt->m_node->m_parent)
      {
        if (EntityPtr ntt = pNode->OwnerEntity())
        {
          m_parentId = ntt->GetIdVal();
        }
        pNode->Orphan(m_ntt->m_node);
      }

      GetApp()->GetCurrentScene()->RemoveEntity(m_ntt->GetIdVal());

      m_actionComitted = true;
      HandleCompSpecificOps(m_ntt, m_actionComitted);
    }

    // CreateAction
    //////////////////////////////////////////

    CreateAction::CreateAction(EntityPtr ntt)
    {
      m_ntt                    = ntt;
      m_actionComitted         = true;

      EditorScenePtr currScene = GetApp()->GetCurrentScene();
      currScene->GetSelectedEntities(m_selecteds);
      currScene->AddEntity(ntt);
    }

    CreateAction::~CreateAction()
    {
      if (!m_actionComitted)
      {
        m_ntt = nullptr;
      }
    }

    void CreateAction::Undo()
    {
      SwapSelection();

      EditorScenePtr currScene = GetApp()->GetCurrentScene();
      currScene->RemoveEntity(m_ntt->GetIdVal());

      m_actionComitted = false;
    }

    void CreateAction::Redo()
    {
      EditorScenePtr currScene = GetApp()->GetCurrentScene();
      currScene->AddEntity(m_ntt);

      SwapSelection();
      m_actionComitted = true;
    }

    void CreateAction::SwapSelection()
    {
      IDArray selection;
      EditorScenePtr currScene = GetApp()->GetCurrentScene();
      currScene->GetSelectedEntities(selection);
      currScene->AddToSelection(m_selecteds, false);
      std::swap(m_selecteds, selection);
    }

    DeleteComponentAction::DeleteComponentAction(ComponentPtr com)
    {
      m_com = com;
      if (AnimControllerComponent* ac = com->As<AnimControllerComponent>())
      {
        ac->Stop();
      }

      Redo();
    }

    // DeleteComponentAction
    //////////////////////////////////////////

    void DeleteComponentAction::Undo()
    {
      if (EntityPtr owner = m_com->OwnerEntity())
      {
        owner->AddComponent(m_com);

        EditorScenePtr currScene = GetApp()->GetCurrentScene();
        currScene->ValidateBillboard(m_com->OwnerEntity());
      }
    }

    void DeleteComponentAction::Redo()
    {
      if (EntityPtr owner = m_com->OwnerEntity())
      {
        owner->RemoveComponent(m_com->Class());
      }
    }

    // KeyEditAction
    //////////////////////////////////////////

    KeyEditAction::KeyEditAction(AnimationPtr clip, const String& trackName)
        : m_clip(clip),
          m_trackName(trackName)
    {
    }

    KeyEditAction::~KeyEditAction() { m_clip = nullptr; }

    KeyEditAction::FrameState KeyEditAction::Read(int frame) const
    {
      FrameState state;
      state.frame = frame;

      if (m_clip == nullptr || frame < 0)
      {
        return state;
      }

      const KeyArray* keys = m_clip->m_keys.Find(m_trackName);
      if (keys == nullptr)
      {
        return state;
      }

      for (const Key& key : *keys)
      {
        if (key.m_frame == frame)
        {
          state.hasKey = true;
          state.key    = key;
          break;
        }
      }

      return state;
    }

    void KeyEditAction::Write(const FrameState& state)
    {
      if (m_clip == nullptr || state.frame < 0)
      {
        return;
      }

      // The stored frame is forced to match the sort position the track keeps, the sampler walks
      // the keys in order and would read a mismatched frame as a corrupt curve.
      Key key     = state.key;
      key.m_frame = state.frame;

      // Animation::SetKey keeps the track sorted by frame and creates it when it is missing.
      m_clip->SetKey(m_trackName, state.frame, state.hasKey ? &key : nullptr);
    }

    void KeyEditAction::Undo()
    {
      Write(m_beforeFrom);
      Write(m_beforeTo);
    }

    void KeyEditAction::Redo()
    {
      Write(m_afterFrom);
      Write(m_afterTo);
    }

    void KeyEditAction::SetKey(AnimationPtr clip, const String& trackName, const Key& key)
    {
      KeyEditAction* action = new KeyEditAction(clip, trackName);

      action->m_beforeFrom = action->Read(key.m_frame);
      action->m_beforeTo   = action->m_beforeFrom;

      action->m_afterFrom        = action->m_beforeFrom;
      action->m_afterFrom.hasKey = true;
      action->m_afterFrom.key    = key;
      action->m_afterTo          = action->m_afterFrom;

      action->Redo();
      ActionManager::GetInstance()->AddAction(action);
    }

    void KeyEditAction::DeleteKey(AnimationPtr clip, const String& trackName, int frame)
    {
      KeyEditAction* action = new KeyEditAction(clip, trackName);

      action->m_beforeFrom = action->Read(frame);
      if (!action->m_beforeFrom.hasKey)
      {
        SafeDel(action); // Nothing to remove, do not pollute the undo stack.
        return;
      }

      action->m_beforeTo         = action->m_beforeFrom;
      action->m_afterFrom        = action->m_beforeFrom;
      action->m_afterFrom.hasKey = false;
      action->m_afterTo          = action->m_afterFrom;

      action->Redo();
      ActionManager::GetInstance()->AddAction(action);
    }

    void KeyEditAction::MoveKey(AnimationPtr clip, const String& trackName, int fromFrame, int toFrame)
    {
      if (fromFrame == toFrame)
      {
        return;
      }

      KeyEditAction* action = new KeyEditAction(clip, trackName);

      action->m_beforeFrom = action->Read(fromFrame);
      if (!action->m_beforeFrom.hasKey)
      {
        SafeDel(action);
        return;
      }

      action->m_beforeTo = action->Read(toFrame);

      // The key leaves its old frame and lands on the new one, replacing whatever was there.
      action->m_afterFrom        = action->m_beforeFrom;
      action->m_afterFrom.hasKey = false;
      action->m_afterTo          = action->m_beforeFrom;
      action->m_afterTo.frame    = toFrame;
      action->m_afterTo.key.m_frame = toFrame;

      action->Redo();
      ActionManager::GetInstance()->AddAction(action);
    }

    void KeyEditAction::SetInterp(AnimationPtr clip, const String& trackName, int frame, KeyInterp interp)
    {
      if (frame < 0)
      {
        return;
      }

      KeyEditAction* action = new KeyEditAction(clip, trackName);

      action->m_beforeFrom = action->Read(frame);
      if (!action->m_beforeFrom.hasKey || action->m_beforeFrom.key.m_interp == interp)
      {
        // Nothing to change: an unchanged key must not push an undo step.
        SafeDel(action);
        return;
      }

      // Only the mode changes, the frame the key sits on and its values are left alone.
      action->m_beforeTo               = action->m_beforeFrom;
      action->m_afterFrom              = action->m_beforeFrom;
      action->m_afterFrom.key.m_interp = interp;
      action->m_afterTo                = action->m_afterFrom;

      action->Redo();
      ActionManager::GetInstance()->AddAction(action);
    }

    // ActionManager
    //////////////////////////////////////////

    ActionManager ActionManager::m_instance;

    ActionManager::ActionManager()
    {
      m_initiated      = false;
      m_stackPointer   = 0;
      m_actionGrouping = false;
    }

    ActionManager::~ActionManager() { assert(m_initiated == false && "Call ActionManager::UnInit."); }

    void ActionManager::Init()
    {
      m_initiated      = true;
      m_actionGrouping = false;
    }

    void ActionManager::UnInit()
    {
      ClearAllActions();
      m_initiated = false;
    }

    void ActionManager::AddAction(Action* action)
    {
      if (m_stackPointer > -1)
      {
        if (m_stackPointer < static_cast<int>(m_actionStack.size()) - 1)
        {
          // All actions above stack pointer are invalidated.
          for (size_t i = m_stackPointer + 1; i < m_actionStack.size(); i++)
          {
            Action* a = m_actionStack[i];
            SafeDel(a);
          }

          m_actionStack.erase(m_actionStack.begin() + m_stackPointer + 1, m_actionStack.end());
        }
      }
      else
      {
        for (size_t i = 0; i < m_actionStack.size(); i++)
        {
          Action* a = m_actionStack[i];
          SafeDel(a);
        }

        m_actionStack.clear();
      }

      m_actionStack.push_back(action);
      if (m_actionStack.size() > g_maxUndoCount && !m_actionGrouping)
      {
        Action* a = m_actionStack.front();
        SafeDel(a);

        pop_front(m_actionStack);
      }
      m_stackPointer = static_cast<int>(m_actionStack.size()) - 1;
    }

    void ActionManager::GroupLastActions(int n)
    {
      if (n == 0)
      {
        return;
      }

      // Sanity Checks.
      assert(m_stackPointer == static_cast<int>(m_actionStack.size()) - 1 && "Call grouping right after add.");
      if (n >= static_cast<int>(m_actionStack.size()) && !m_actionGrouping)
      {
        assert(static_cast<int>(m_actionStack.size()) >= n &&
               "We can't stack more than we have. Try using BeginActionGroup()"
               " to pass a series of action as a group");
        return;
      }

      int begIndx  = static_cast<int>(m_actionStack.size()) - n;
      Action* root = m_actionStack[begIndx++];
      root->m_group.reserve(n - 1);
      for (int i = begIndx; i < static_cast<int>(m_actionStack.size()); i++)
      {
        root->m_group.push_back(m_actionStack[i]);
      }

      m_actionStack.erase(m_actionStack.begin() + begIndx, m_actionStack.end());
      m_stackPointer   = static_cast<int>(m_actionStack.size()) - 1;
      m_actionGrouping = false;
    }

    void ActionManager::BeginActionGroup() { m_actionGrouping = true; }

    void ActionManager::RemoveLastAction()
    {
      if (!m_actionStack.empty())
      {
        if (m_stackPointer > -1)
        {
          Action* action = m_actionStack[m_stackPointer];
          SafeDel(action);
          m_actionStack.erase(m_actionStack.begin() + m_stackPointer);
          m_stackPointer--;
        }
      }
    }

    void ActionManager::Undo()
    {
      if (!m_actionStack.empty())
      {
        if (m_stackPointer > -1)
        {
          Action* action = m_actionStack[m_stackPointer];

          // Undo in reverse order.
          for (int i = static_cast<int>(action->m_group.size()) - 1; i > -1; i--)
          {
            action->m_group[i]->Undo();
          }
          action->Undo();
          m_stackPointer--;
        }
      }
    }

    void ActionManager::Redo()
    {
      if (m_actionStack.empty())
      {
        return;
      }

      if (m_stackPointer < static_cast<int>(m_actionStack.size()) - 1)
      {
        Action* action = m_actionStack[m_stackPointer + 1];
        action->Redo();
        for (Action* ga : action->m_group)
        {
          ga->Redo();
        }

        m_stackPointer++;
      }
    }

    void ActionManager::ClearAllActions()
    {
      for (Action* action : m_actionStack)
      {
        SafeDel(action);
      }
      m_actionStack.clear();
      m_stackPointer = 0;
    }

    ActionManager* ActionManager::GetInstance() { return &m_instance; }

  } // namespace Editor
} // namespace ToolKit
