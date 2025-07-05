/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "EditorScene.h"

#include <StateMachine.h>

namespace ToolKit
{
  namespace Editor
  {

    // BaseMod
    //////////////////////////////////////////

    enum class ModId
    {
      Base,
      Select,
      Cursor,
      Move,
      Rotate,
      Scale,
      Anchor
    };

    class TK_EDITOR_API BaseMod
    {
     public:
      explicit BaseMod(ModId id);
      virtual ~BaseMod();
      virtual void Init();
      virtual void UnInit();
      virtual void Update(float deltaTime);
      virtual void Signal(SignalId signal);

     protected:
      static int GetNextSignalId();

     public:
      ModId m_id;
      StateMachine* m_stateMachine;

      // Signals.
      static SignalId m_leftMouseBtnDownSgnl;
      static SignalId m_leftMouseBtnUpSgnl;
      static SignalId m_leftMouseBtnDragSgnl;
      static SignalId m_mouseMoveSgnl;
      static SignalId m_backToStart;
      static SignalId m_delete;
      static SignalId m_duplicate;
    };

    // ModManager
    //////////////////////////////////////////

    class TK_EDITOR_API ModManager
    {
     public:
      ~ModManager();

      ModManager(const ModManager&)     = delete;
      void operator=(const ModManager&) = delete;

      void Init();
      void UnInit();
      static ModManager* GetInstance();
      void Update(float deltaTime);
      void DispatchSignal(SignalId signal);
      // If set true, sets the given mod. Else does nothing.
      void SetMod(bool set, ModId mod);

     private:
      ModManager();

     private:
      static ModManager m_instance;
      bool m_initiated;

     public:
      std::vector<BaseMod*> m_modStack;
    };

    // StateType
    //////////////////////////////////////////

    class TK_EDITOR_API StateType
    {
     public:
      static const String Null;
      static const String StateBeginPick;
      static const String StateBeginBoxPick;
      static const String StateEndPick;
      static const String StateDeletePick;
      static const String StateTransformBegin;
      static const String StateTransformTo;
      static const String StateTransformEnd;
      static const String StateDuplicate;
      static const String StateAnchorBegin;
      static const String StateAnchorTo;
      static const String StateAnchorEnd;
    };

    // StatePickingBase
    //////////////////////////////////////////

    class TK_EDITOR_API StatePickingBase : public State
    {
     public:
      StatePickingBase();
      void TransitionIn(State* prevState) override;
      void TransitionOut(State* nextState) override;
      bool IsIgnored(ObjectId id);
      void PickDataToEntityId(IDArray& ids);

     public:
      // Picking data.
      std::vector<Vec2> m_mouseData;
      std::vector<EditorScene::PickData> m_pickData;
      IDArray m_ignoreList;

      // Debug models.
    };

    // StateBeginPick
    //////////////////////////////////////////

    class TK_EDITOR_API StateBeginPick : public StatePickingBase
    {
     public:
      void TransitionIn(State* prevState) override;
      SignalId Update(float deltaTime) override;
      String Signaled(SignalId signal) override;

      String GetType() override { return StateType::StateBeginPick; }
    };

    // StateBeginBoxPick
    //////////////////////////////////////////

    class TK_EDITOR_API StateBeginBoxPick : public StatePickingBase
    {
     public:
      SignalId Update(float deltaTime) override;
      String Signaled(SignalId signal) override;

      String GetType() override { return StateType::StateBeginBoxPick; }

     private:
      void GetMouseRect(Vec2& min, Vec2& max);
    };

    // StateEndPick
    //////////////////////////////////////////

    class TK_EDITOR_API StateEndPick : public StatePickingBase
    {
     public:
      SignalId Update(float deltaTime) override;
      String Signaled(SignalId signal) override;

      String GetType() override { return StateType::StateEndPick; }
    };

    // StateDeletePick
    //////////////////////////////////////////

    class TK_EDITOR_API StateDeletePick : public StatePickingBase
    {
     public:
      SignalId Update(float deltaTime) override;
      String Signaled(SignalId signal) override;

      String GetType() override { return StateType::StateDeletePick; }
    };

    // StateDuplicate
    //////////////////////////////////////////

    class TK_EDITOR_API StateDuplicate : public State
    {
     public:
      void TransitionIn(State* prevState) override;
      void TransitionOut(State* nextState) override;
      SignalId Update(float deltaTime) override;
      String Signaled(SignalId signal) override;

      String GetType() override { return StateType::StateDuplicate; };
    };

    // SelectMod
    //////////////////////////////////////////

    class TK_EDITOR_API SelectMod : public BaseMod
    {
     public:
      SelectMod();

      void Init() override;
      void Update(float deltaTime) override;
    };

    // CursorMod
    //////////////////////////////////////////

    class TK_EDITOR_API CursorMod : public BaseMod
    {
     public:
      CursorMod();

      void Init() override;
      void Update(float deltaTime) override;
    };

  } // namespace Editor
} // namespace ToolKit
