/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Action.h"
#include "Anchor.h"
#include "Mod.h"

namespace ToolKit
{
  namespace Editor
  {

    // StateAnchorBase
    //////////////////////////////////////////

    class TK_EDITOR_API StateAnchorBase : public State
    {
     public:
      enum class TransformType
      {
        Translate
      };

     public:
      StateAnchorBase();
      SignalId Update(float deltaTime) override;
      void TransitionIn(State* prevState) override;
      void TransitionOut(State* nextState) override;

     protected:
      void MakeSureAnchorIsValid();
      void ReflectAnchorTransform(EntityPtr ntt);

     public:
      AnchorPtr m_anchor;
      std::vector<Vec2> m_mouseData;
      PlaneEquation m_intersectionPlane;
      TransformType m_type;
      bool m_signalConsumed = false;

     protected:
      Vec3 m_anchorDeltaTransform; // Anchor medallion change.
      Vec3 m_deltaAccum;
    };

    // StateAnchorBegin
    //////////////////////////////////////////

    class TK_EDITOR_API StateAnchorBegin : public StateAnchorBase
    {
     public:
      void TransitionIn(State* prevState) override;
      void TransitionOut(State* nextState) override;

      SignalId Update(float deltaTime) override;
      String Signaled(SignalId signal) override;
      String GetType() override;

     private:
      void CalculateIntersectionPlane();
      void CalculateGrabPoint();
    };

    // AnchorAction
    //////////////////////////////////////////

    class TK_EDITOR_API AnchorAction : public Action
    {
     public:
      explicit AnchorAction(EntityPtr ntt);
      virtual ~AnchorAction();

      virtual void Undo();
      virtual void Redo();

     private:
      void Swap();

     private:
      EntityPtr m_entity;
      Mat4 m_transform;
    };

    // StateAnchorTo
    //////////////////////////////////////////

    class TK_EDITOR_API StateAnchorTo : public StateAnchorBase
    {
     public:
      void TransitionIn(State* prevState) override;
      void TransitionOut(State* prevState) override;
      SignalId Update(float deltaTime) override;
      String Signaled(SignalId signal) override;
      String GetType() override;

     private:
      void CalculateDelta();
      void Transform(const Vec3& delta);

     public:
      Vec3 m_initialLoc;

     private:
      IVec2 m_mouseInitialLoc;
    };

    // StateAnchorEnd
    //////////////////////////////////////////

    class TK_EDITOR_API StateAnchorEnd : public StateAnchorBase
    {
     public:
      void TransitionOut(State* nextState) override;
      SignalId Update(float deltaTime) override;
      String Signaled(SignalId signal) override;
      String GetType() override;
    };

    // AnchorMod
    //////////////////////////////////////////

    class TK_EDITOR_API AnchorMod : public BaseMod
    {
     public:
      explicit AnchorMod(ModId id);
      virtual ~AnchorMod();

      void Init() override;
      void UnInit() override;
      void Update(float deltaTime) override;

     public:
      AnchorPtr m_anchor;
      TransformationSpace m_prevTransformSpace;
    };

  } // namespace Editor
} // namespace ToolKit
