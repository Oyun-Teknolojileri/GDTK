/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
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

    // KeyEditAction
    //////////////////////////////////////////

    /**
     * One undoable edit of a single animation key.
     *
     * A key edit touches at most two frames: the frame the key came from and the frame it went to
     * (the same frame for an insert, an update or a removal). The action remembers what sat on both
     * frames before and after the edit, so undo and redo replay it exactly -- including a key that
     * was replaced on the target frame.
     *
     * The editor action convention applies here: the constructor performs the edit (through Redo()),
     * so a caller mutates the clip and pushes the action in one step.
     */
    class TK_EDITOR_API KeyEditAction : public Action
    {
     public:
      /**
       * Inserts or updates a key on a track.
       * @param clip Clip that owns the track.
       * @param trackName Track to edit.
       * @param key Key to write at its own frame.
       */
      static void SetKey(AnimationPtr clip, const String& trackName, const Key& key);

      /**
       * Removes the key at the given frame.
       * @param clip Clip that owns the track.
       * @param trackName Track to edit.
       * @param frame Frame of the key to remove.
       */
      static void DeleteKey(AnimationPtr clip, const String& trackName, int frame);

      /**
       * Moves a key in time. Whatever sits on the target frame is replaced.
       * @param clip Clip that owns the track.
       * @param trackName Track to edit.
       * @param fromFrame Frame the key is on now.
       * @param toFrame Frame the key moves to.
       */
      static void MoveKey(AnimationPtr clip, const String& trackName, int fromFrame, int toFrame);

      /**
       * Changes the interpolation mode of the key on a frame. Nothing is stacked when the key is
       * missing or already carries that mode.
       * @param clip Clip that owns the track.
       * @param trackName Track to edit.
       * @param frame Frame of the key.
       * @param interp Interpolation mode to set.
       */
      static void SetInterp(AnimationPtr clip, const String& trackName, int frame, KeyInterp interp);

      void Undo() override;
      void Redo() override;

     private:
      /** Content of one of the two frames an edit touches. */
      struct FrameState
      {
        int frame   = -1;
        bool hasKey = false;
        Key key;
      };

      KeyEditAction(AnimationPtr clip, const String& trackName);
      virtual ~KeyEditAction();

      /** Reads the key that currently sits on the given frame. */
      FrameState Read(int frame) const;

      /** Writes a frame state back to the track. */
      void Write(const FrameState& state);

     private:
      AnimationPtr m_clip;
      String m_trackName;

      FrameState m_beforeFrom; //!< Source frame before the edit.
      FrameState m_afterFrom;  //!< Source frame after the edit.
      FrameState m_beforeTo;   //!< Target frame before the edit.
      FrameState m_afterTo;    //!< Target frame after the edit.
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
