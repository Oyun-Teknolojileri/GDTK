/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Animation.h"
#include "EditorTypes.h"
#include "Types.h"

namespace ToolKit
{
  namespace Editor
  {

    // Action
    //////////////////////////////////////////

    class TK_EDITOR_API Action
    {
     public:
      Action();
      virtual ~Action();

      virtual void Undo() = 0;
      virtual void Redo() = 0;

     public:
      ActionRawPtrArray m_group;
    };

    // DeleteAction
    //////////////////////////////////////////

    class TK_EDITOR_API DeleteAction : public Action
    {
     public:
      explicit DeleteAction(EntityPtr ntt);
      virtual ~DeleteAction();

      void Undo() override;
      void Redo() override;

     private:
      EntityPtr m_ntt;
      ObjectId m_parentId;
      bool m_actionComitted;
    };

    // CreateAction
    //////////////////////////////////////////

    class TK_EDITOR_API CreateAction : public Action
    {
     public:
      explicit CreateAction(EntityPtr ntt);
      virtual ~CreateAction();

      void Undo() override;
      void Redo() override;

     private:
      void SwapSelection();

     private:
      EntityPtr m_ntt;
      bool m_actionComitted;
      IDArray m_selecteds;
    };

    // DeleteComponentAction
    //////////////////////////////////////////

    class TK_EDITOR_API DeleteComponentAction : public Action
    {
     public:
      explicit DeleteComponentAction(ComponentPtr com);

      void Undo() override;
      void Redo() override;

     private:
      ComponentPtr m_com;
    };

    // ActionManager
    //////////////////////////////////////////

    class TK_EDITOR_API ActionManager
    {
     public:
      ~ActionManager();

      ActionManager(const ActionManager&)  = delete;
      void operator=(const ActionManager&) = delete;

      void Init();
      void UnInit();
      void AddAction(Action* action);
      void GroupLastActions(int n);
      void BeginActionGroup();
      void RemoveLastAction();
      void Undo();
      void Redo();
      void ClearAllActions();
      static ActionManager* GetInstance();

     private:
      ActionManager();

     private:
      static ActionManager m_instance;
      ActionRawPtrArray m_actionStack;

      int m_stackPointer;
      bool m_initiated;
      bool m_actionGrouping;
    };

  } // namespace Editor
} // namespace ToolKit
