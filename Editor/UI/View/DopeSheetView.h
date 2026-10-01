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
#include <vector>

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

      /**
       * One key of the selection. A key is addressed by its track, its frame and which of the clip's
       * two key containers it lives in, because a track name may name a transform track and a
       * parameter track at the same time and a group is allowed to mix both kinds.
       */
      struct KeyRef
      {
        String m_track;       //!< Track that owns the key.
        int m_frame = -1;     //!< Frame the key sits on.
        bool m_param = false; //!< The key belongs to a parameter track.
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

      /** States if at least one key is selected in the sheet. */
      bool HasSelectedKey() const;

      /** Removes every selected key through undoable actions, grouped into one undo step. */
      void DeleteSelectedKey();

      /** Copies the primary selected key into the sheet's clipboard. Undo does not touch the clipboard. */
      void CopySelectedKey();

      /**
       * Writes the clipboard key at the playhead, undoably. The target is the selected row, or the
       * row the key was copied from when nothing is selected.
       */
      void PasteKey();

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

      /**
       * Changes the interpolation mode of one key, undoably, and refreshes the preview pose.
       * @param trackName Track that owns the key.
       * @param frame Frame of the key.
       * @param interp Mode to set.
       */
      void SetKeyInterp(const String& trackName, int frame, KeyInterp interp);

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

      /**
       * Draws the sheet area as read only curves instead of key lanes: translation, rotation and
       * scale of one transform track, x / y / z per channel group. The pose is sampled through the
       * engine's own interpolation, so a Stepped, Smooth or Flat key shows in the shape.
       */
      void ShowCurves(float laneLeft, float laneWidth);

      /** Name of the track the curve view plots, empty when the clip has no transform track. */
      String CurveTrackName() const;

      /**
       * Ctrl + wheel zooms and shift + wheel pans the timeline. The lane and the curve view share it
       * so switching the view never moves the timeline.
       */
      void HandleTimelineWheel(float laneLeft);

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

      /** Same mapping for a fractional frame, which is what the curve plot samples on. */
      float FrameToX(float frame, float laneLeft) const;
      int XToFrame(float x, float laneLeft) const;

      /** Applies a finished key drag, undoably. */
      void CommitKeyDrag();

      /**
       * Frame offset a running drag applies to the whole selection. The lowest key of the group decides
       * how far left the drag may go, so a bulk move can not push a key before frame zero, and the ghost
       * markers are drawn from this same offset, so the preview and the release agree.
       * @param fromFrames Frames the dragged keys started on. They are passed in rather than read from
       * the member, because the commit clears that snapshot before it asks for the offset.
       */
      int DragDelta(const std::vector<int>& fromFrames) const;

      /**
       * Handles Ctrl + A and Escape. The sheet's other shortcuts live in DopeSheetWindow, these two
       * belong to the lane area because the key set they act on is drawn there.
       */
      void HandleSelectionShortcuts();

      /** States if the key is part of the selection. */
      bool IsKeySelected(const String& track, int frame, bool param) const;

      /** Makes the key the whole selection. */
      void SelectOnly(const String& track, int frame, bool param);

      /** Adds the key to the selection, or removes it when it is already part of it. */
      void ToggleSelection(const String& track, int frame, bool param);

      /** Drops the selection and the drag that is moving it. */
      void ClearSelection();

      /** Selects every key of the clip, transform tracks and parameter tracks alike. */
      void SelectAllKeys();

      /** Last selected key, which is the one the clipboard works on. Null when nothing is selected. */
      const KeyRef* PrimarySelection() const;

      /** States if the clip holds a key on the given frame of the given track. */
      bool TrackHasKey(const String& trackName, int frame, bool paramTrack) const;

      /** Interpolation mode of the key on a frame. Linear when there is no key there. */
      KeyInterp KeyInterpAt(const String& trackName, int frame) const;

      /** Reads the key on a frame of a transform track. False when the track has none there. */
      bool KeyAt(const String& trackName, int frame, Key& key) const;

      /** Reads the key on a frame of a parameter track. False when the track has none there. */
      bool ParamKeyAt(const String& trackName, int frame, ParamKey& key) const;

      /** Copies the key on a frame of a track into the clipboard. */
      void CopyKey(const String& trackName, int frame, bool paramTrack);

      /** Writes the clipboard key at the playhead on the given row, undoably. */
      void PasteKeyOn(const String& trackName, bool paramTrack);

      /** Drops every selected key that is no longer part of the clip. */
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

      /**
       * Mode a key the sheet creates gets. Smooth is the one an animator expects from a fresh key,
       * the curve blends through it instead of arriving and leaving on straight lines. The engine's
       * own default stays Linear, so a clip authored by code or by an older build keeps the motion it
       * was made with; the combo switches the sheet to Linear, Stepped or Flat when a key needs it.
       */
      KeyInterp m_newKeyInterp = KeyInterp::Smooth;

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

      // Key selection and dragging. Several keys can be selected at once and moved or deleted as a
      // group, while the clipboard holds a single key: the primary (last selected) one, see KeyRef.
      std::vector<KeyRef> m_selection; //!< Selected keys, in the order they were picked.

      bool m_dragging     = false;  //!< True while the selection is dragged in time.
      int m_dragFromFrame = -1;     //!< Frame the key the drag was started on came from.
      int m_dragToFrame   = -1;     //!< Frame that key sits on right now, the drag delta and the ghost.
      size_t m_dragAnchor = 0;      //!< Index of that key in m_selection, the one the mouse follows.
      KeyInterp m_dragInterp = KeyInterp::Linear; //!< Mode of the dragged key, for the ghost marker.

      /**
       * Frames the selected keys came from, one entry per selection entry in the same order, frozen
       * when the drag starts. m_selection holds the frames the keys are on right now, which move with
       * the drag, so the snapshot is what a repeated drag and the commit read from.
       */
      std::vector<int> m_dragFromFrames;

      // Rubber-band box selection, started by Ctrl + drag in the lane area.
      bool m_boxSelecting = false;      //!< True while a box is dragged.
      bool m_boxAdditive  = false;      //!< Shift was held when the box started, so it adds to the set.
      ImVec2 m_boxStart   = ImVec2();   //!< Where the box started, in screen space.
      float m_boxScrollY  = 0.0f;       //!< Lane scroll the box was started with, in pixels.

      // Right click context, deferred until the row has been drawn.
      String m_ctxTrack;
      int m_ctxFrame  = -1;
      bool m_ctxParam = false; //!< The row under the cursor is a parameter track.

      bool m_curveView = false;    //!< The sheet area plots curves instead of drawing key lanes.
      String m_curveTrack;         //!< Track the curve view plots, picked in the name column.

      /** One key held by the clipboard, either the transform or the parameter payload. */
      struct KeyClipboard
      {
        bool m_valid    = false; //!< False until a key is copied.
        bool m_paramKey = false; //!< The copied key belongs to a parameter track.
        String m_track;          //!< Track the key came from.
        int m_frame = 0;         //!< Frame it was copied from.
        Key m_key;               //!< Transform payload.
        ParamKey m_param;        //!< Parameter payload.
      };

      KeyClipboard m_keyClipboard; //!< Pasted as often as the animator needs it.
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

     protected:
      /** Stores the clip the sheet edits, so the window opens with the animation it was left on. */
      XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
      XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

     private:
      DopeSheetViewPtr m_view;
    };

  } // namespace Editor
} // namespace ToolKit
