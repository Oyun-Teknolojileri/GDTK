/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file DopeSheetView.h Header for DopeSheetView and DopeSheetWindow.
 */

#include "EditorTypes.h"
#include "View.h"

#include <Animation.h>

#include <unordered_map>

namespace ToolKit
{
  namespace Editor
  {

    /**
     * Frame based keyframe sheet for entity node transforms.
     *
     * One track belongs to one entity and stores that entity's translation, rotation and scale
     * together, because the engine's Key carries the three channels in a single entry
     * (ToolKit/Resources/Animation.h). Tracks are linked to scene entities by name, which is the
     * convention the importer already uses (track name == node name == entity name). Two entities
     * that share a name are disambiguated with a numeric suffix, so a track never belongs to two
     * entities.
     *
     * Playback is editor side. The engine has no path that applies node tracks
     * (Animation::GetPose(Node*) samples the first track only and nothing calls it for plain
     * entities), so the view samples the clip and writes the pose to the matching entity nodes.
     *
     * Phase 1 scope: non skinned entity nodes. Tracks that only match a skinned mesh are listed
     * but skipped by the pose applier, and keying them is refused.
     */
    class TK_EDITOR_API DopeSheetView : public View
    {
     public:
      /** Transport state of the sheet playhead. */
      enum class PlayState
      {
        Stopped,
        Playing,
        Paused
      };

      /** What the clip the sheet edits holds for one parameter track. */
      enum class ParamKeyState
      {
        NoClip,      //!< no clip bound, nothing can be keyed
        NotAnimated, //!< no track, or a track without keys
        Animated,    //!< the track has keys, none on the playhead frame
        KeyAtFrame   //!< a key sits on the playhead frame
      };

      DopeSheetView();
      virtual ~DopeSheetView();

      void Show() override;

      /** Binds the clip to edit. Passing nullptr clears the sheet. */
      void SetAnimation(AnimationPtr anim);

      /** States if the sheet holds a clip and may write to it (not during a play session). */
      bool CanEdit() const;

      /** Playhead position in seconds, the value the pose applier samples. */
      float CurrentTime() const;

      /** Playhead position in frames, the frame a new key lands on. */
      int GetFrame() const { return m_frame; }

      /**
       * Moves the playhead to the given frame, clamped to the sheet range. A manual scrub pauses
       * playback.
       * @param frame Frame to move the playhead to.
       * @param applyPose Applies the clip pose at the new frame when true.
       */
      void SetFrame(int frame, bool applyPose);

      void Play();
      void Pause();

      /** Restores the transforms the preview session overwrote and rewinds the playhead. */
      void Stop();

      /** Moves the playhead by the given number of frames. */
      void StepFrame(int delta);

      /** Advances the playhead while playing and applies the pose. */
      void Update(float deltaTime);

      /** Writes keys for every selected entity at the current frame. */
      void SetKeyOnSelection();

      /** States if a key is selected in the sheet. */
      bool HasSelectedKey() const;

      /** Removes the selected key through an undoable action. */
      void DeleteSelectedKey();

      // Parameter tracks.
      //////////////////////////////////////////

      /**
       * Reports what the clip holds for a parameter track, which is what the inspector diamond
       * shows. The track id is "<entity>.<param>", "<entity>.<componentClass>.<param>" or
       * "<entity>.MaterialComponent.<index>.<param>".
       */
      ParamKeyState GetParamKeyState(const String& trackId) const;

      /**
       * Keys the parameter the track id addresses at the playhead frame, undoably. The value that is
       * written is the one the parameter holds right now, so an edit in the inspector followed by a
       * Set Key records exactly what is on screen.
       * @return False when the track does not address a parameter of the current scene.
       */
      bool SetParamKey(const String& trackId);

      /** Removes the parameter key at the given frame, undoably. */
      void DeleteParamKey(const String& trackId, int frame);

      /** States if the clip holds a param key on the given frame of the track. */
      bool ParamTrackHasKey(const String& trackId, int frame) const;

      /** Applies the clip pose at the given time to every entity matched by a track. */
      void ApplyPoseAt(float time);

      /**
       * Applies the parameter tracks of the clip at the given time. They are independent of the node
       * tracks, a light color or a material parameter is not posed by a transform.
       */
      void ApplyParamTracksAt(float time);

      /**
       * Restores the transforms and the parameter values the preview session overwrote and rewinds
       * the playhead.
       */
      void RestorePreviewState();

     private:
      // Sheet sections, drawn top to bottom.
      void ShowClipHeader();
      void ShowTransport();
      void ShowKeyTools();
      void ShowSheet();
      void ShowRuler(float laneLeft, float laneWidth);
      void ShowLanes(float laneLeft, float laneWidth);

      // Sheet helpers.
      void HandleSceneChange();
      void ResolveTracks();
      EntityPtr EntityForTrack(const String& trackName) const;
      String TrackNameForEntity(EntityPtr ntt, bool create);
      bool SampleTrack(const KeyArray& keys, float time, Vec3& pos, Quaternion& rot, Vec3& scale);
      static bool IsSkinned(EntityPtr ntt);
      static int TickStep(float pxPerFrame);
      void BeginPreviewSession();
      void ClampScroll(float laneWidth);
      void FitView(float laneWidth);
      void CreateClip(const String& name);
      float FrameToX(int frame, float laneLeft) const;
      int XToFrame(float x, float laneLeft) const;

      /** Applies a finished key drag, undoably. */
      void CommitKeyDrag();

      /** States if the clip holds a key on the given frame of the given track. */
      bool TrackHasKey(const String& trackName, int frame, bool paramTrack) const;

      /** Drops the selection when the selected key is not part of the clip anymore. */
      void ValidateSelection();

      /**
       * Finds the entity a parameter track id belongs to. Entity names may contain dots, so the
       * longest name that prefixes the track wins.
       */
      EntityPtr EntityForParamTrack(const String& trackId) const;

     public:
      AnimationPtr m_clip = nullptr;       //!< Clip the sheet edits.

      int m_frame           = 0;           //!< Playhead in frames.
      int m_endFrame        = 60;          //!< Last frame of the sheet range, drives clip duration.
      float m_speed         = 1.0f;        //!< Playback speed multiplier.
      bool m_loop           = true;        //!< Wraps playback at the last frame.
      PlayState m_playState = PlayState::Stopped;

      /** Horizontal timeline transform. The sheet owns it so ruler and lanes always agree. */
      float m_pxPerFrame = 8.0f;
      float m_scrollX    = 0.0f;

      /** Set Key channel mask: which channels of the entity transform overwrite the key. */
      bool m_keyTranslation = true;
      bool m_keyRotation    = true;
      bool m_keyScale       = true;

     private:
      float m_time            = 0.0f;        //!< Continuous playhead time in seconds.
      float m_scrollY         = 0.0f;        //!< Vertical lane scroll in pixels.
      float m_nameColumnWidth = 190.0f;      //!< Width of the track name column.
      ObjectId m_sceneId      = NullHandle;  //!< Scene the cached track mapping belongs to.
      bool m_sessionActive    = false;       //!< True once m_baseTransforms holds a snapshot.
      std::unordered_map<String, EntityPtr> m_trackEntities; //!< Track name -> entity.
      std::unordered_map<ObjectId, String> m_entityTracks;   //!< Entity id -> track name.
      std::unordered_map<ObjectId, Mat4> m_baseTransforms;   //!< Pre preview local transforms.

      /** A parameter value as the preview session found it, packed like a ParamKey. */
      struct ParamSnapshot
      {
        ParameterVariant::VariantType type = ParameterVariant::VariantType::Float;
        Vec4 value;
      };

      std::unordered_map<String, ParamSnapshot> m_baseParams; //!< Pre preview parameter values.

      // Key selection and dragging. Phase 1.5 selects a single key; later phases extend this to a
      // set with copy/paste and bulk moves. A selected key belongs either to a transform track or to
      // a parameter track, which the two flags below record.
      String m_selectedTrack;             //!< Track of the selected key, empty when nothing is selected.
      int m_selectedFrame      = -1;      //!< Frame of the selected key.
      bool m_selectedParam     = false;   //!< The selected key is a parameter key.
      bool m_dragging          = false;   //!< True while a selected key is dragged in time.
      String m_dragTrack;                 //!< Track the drag started on.
      bool m_dragParam         = false;   //!< The drag moves a parameter key.
      int m_dragFromFrame      = -1;      //!< Frame the dragged key came from.
      int m_dragToFrame        = -1;      //!< Frame the dragged key would land on.

      // Right click context, deferred until the row has been drawn.
      String m_ctxTrack;
      int m_ctxFrame  = -1;
      bool m_ctxParam = false; //!< The row under the cursor is a parameter track.
    };

    // DopeSheetWindow
    //////////////////////////////////////////

    class TK_EDITOR_API DopeSheetWindow : public Window
    {
     public:
      TKDeclareClass(DopeSheetWindow, Window);

      DopeSheetWindow();
      virtual ~DopeSheetWindow();

      /** Binds the clip the sheet edits. */
      void SetAnimation(AnimationPtr anim);

      /**
       * Writes keys for the selected entities at the playhead frame. Exposed so the viewport's
       * shortcut can key without the sheet being hovered.
       */
      void SetKeyOnSelection();

      // Parameter tracks, forwarded for the inspector's key diamonds.
      //////////////////////////////////////////

      /** What the clip holds for a parameter track, see DopeSheetView::GetParamKeyState. */
      DopeSheetView::ParamKeyState GetParamKeyState(const String& trackId);

      /** Keys the addressed parameter at the playhead frame. */
      bool SetParamKey(const String& trackId);

      /** Removes the parameter key at the given frame. */
      void DeleteParamKey(const String& trackId, int frame);

      /** Playhead frame of the sheet. */
      int GetFrame();

      void Show() override;
      void DispatchSignals() const override;

     private:
      DopeSheetViewPtr m_view;
    };

  } // namespace Editor
} // namespace ToolKit
