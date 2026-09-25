/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "DopeSheetView.h"

#include "Action.h"
#include "App.h"
#include "EditorScene.h"
#include "FolderWindow.h"
#include "PopupWindows.h"
#include "UI.h"

#include <Animation.h>
#include <Entity.h>
#include <MathUtil.h>
#include <Mesh.h>
#include <MeshComponent.h>
#include <Node.h>
#include <Scene.h>
#include <ToolKit.h>
#include <Util.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ToolKit
{
  namespace Editor
  {

    // Sheet metrics. Everything the sheet draws is derived from these, so a row and its keys stay
    // in sync with the ruler.
    const float g_rowHeight         = 22.0f;   //!< Height of one track row.
    const float g_rulerHeight       = 24.0f;   //!< Height of the frame ruler strip.
    const float g_keyRadius         = 5.0f;    //!< Half size of a key diamond.
    const float g_minPxPerFrame     = 1.0f;    //!< Zoom out limit.
    const float g_maxPxPerFrame     = 80.0f;   //!< Zoom in limit.
    const float g_zoomStep          = 1.15f;   //!< Zoom factor per wheel notch.
    const float g_panWheelFrames    = 4.0f;    //!< Frames panned per shift + wheel notch.
    const float g_scrollWheelRows   = 3.0f;    //!< Rows scrolled per wheel notch.
    const float g_minNameColumn     = 80.0f;   //!< Narrowest track name column.
    const float g_maxNameColumn     = 460.0f;  //!< Widest track name column.
    const float g_splitterWidth     = 6.0f;    //!< Grab width of the name column splitter.
    const float g_rulerLabelSpacing = 55.0f;   //!< Minimum pixels between two ruler labels.
    const float g_scrollMargin      = 24.0f;   //!< Slack after the last frame when panning.
    const int g_defaultFps          = 30;      //!< fps of a clip created by the sheet.
    const int g_defaultEndFrame     = 60;      //!< Frame range of a clip created by the sheet.
    const float g_fitPaddingPx      = 14.0f;   //!< Slack the Fit zoom leaves inside the lane.
    const float g_trackColumnTint   = 0.06f;   //!< Faint tint that sets the track name column apart.
    const float g_columnLineTint    = 0.18f;   //!< Alpha of the line between the name column and the lanes.

    // Wash over the frames past the last one. A mid gray reads as "inactive" on a dark and on a light
    // theme alike, while a theme background colour would blend into the sheet and show nothing.
    const ImVec4 g_outOfRangeVeil(0.5f, 0.5f, 0.5f, 0.22f);

    // Key markers, one shape per interpolation mode, so the sheet reads at a glance: a diamond for
    // the cubic Smooth key (what the sheet has always drawn), a circle for Linear, a square for
    // Stepped and a diamond with a bar for Flat ("no speed at this key").
    // DrawKeyMarker is the only place that maps a mode to a shape, both the key and the drag ghost
    // go through it.
    namespace
    {
      const char* InterpLabel(KeyInterp interp)
      {
        switch (interp)
        {
        case KeyInterp::Stepped:
          return "Stepped";
        case KeyInterp::Smooth:
          return "Smooth (Auto)";
        case KeyInterp::Flat:
          return "Flat";
        case KeyInterp::Linear:
        default:
          return "Linear";
        }
      }

      /** The four modes in menu and combo order. */
      const KeyInterp g_keyInterps[] = {KeyInterp::Stepped, KeyInterp::Linear, KeyInterp::Smooth, KeyInterp::Flat};

      void DrawKeyMarker(ImDrawList* dl,
                         const ImVec2& center,
                         float radius,
                         KeyInterp interp,
                         ImU32 color,
                         bool filled)
      {
        const float thickness = filled ? 1.0f : 2.0f;

        switch (interp)
        {
        case KeyInterp::Linear:
          if (filled)
          {
            dl->AddCircleFilled(center, radius * 0.85f, color);
          }
          else
          {
            dl->AddCircle(center, radius * 0.85f, color, 0, thickness);
          }
          break;

        case KeyInterp::Stepped:
          if (filled)
          {
            dl->AddRectFilled(ImVec2(center.x - radius * 0.8f, center.y - radius * 0.8f),
                              ImVec2(center.x + radius * 0.8f, center.y + radius * 0.8f),
                              color);
          }
          else
          {
            dl->AddRect(ImVec2(center.x - radius * 0.8f, center.y - radius * 0.8f),
                        ImVec2(center.x + radius * 0.8f, center.y + radius * 0.8f),
                        color,
                        0.0f,
                        0,
                        thickness);
          }
          break;

        case KeyInterp::Flat:
        case KeyInterp::Smooth:
        default:
          if (filled)
          {
            dl->AddQuadFilled(ImVec2(center.x, center.y - radius),
                              ImVec2(center.x + radius, center.y),
                              ImVec2(center.x, center.y + radius),
                              ImVec2(center.x - radius, center.y),
                              color);
          }
          else
          {
            dl->AddQuad(ImVec2(center.x, center.y - radius),
                        ImVec2(center.x + radius, center.y),
                        ImVec2(center.x, center.y + radius),
                        ImVec2(center.x - radius, center.y),
                        color,
                        thickness);
          }

          if (interp == KeyInterp::Flat)
          {
            // The bar reads as "velocity is zero here", on top of the diamond of the cubic modes.
            dl->AddLine(ImVec2(center.x - radius, center.y),
                        ImVec2(center.x + radius, center.y),
                        color,
                        filled ? 1.5f : thickness);
          }
          break;
        }
      }
    } // namespace

    // DopeSheetView
    //////////////////////////////////////////

    DopeSheetView::DopeSheetView() : View("Dope Sheet")
    {
      m_viewID  = 6;
      m_viewIcn = UI::m_clipIcon;
    }

    DopeSheetView::~DopeSheetView()
    {
      // The clip and the preview snapshot hold engine objects; drop the snapshot so the node
      // transforms the sheet touched are restored before the window goes away.
      RestoreBaseTransforms();
      m_clip = nullptr;
    }

    void DopeSheetView::SetAnimation(AnimationPtr anim)
    {
      if (m_clip == anim)
      {
        return;
      }

      // The previous clip moved entities around while it was previewed; put them back first.
      Stop();

      m_clip = anim;

      // A selection or a drag from the previous clip does not belong to the new one.
      m_dragging = false;
      m_selectedTrack.clear();
      m_selectedFrame = -1;

      m_trackEntities.clear();
      m_entityTracks.clear();

      if (m_clip == nullptr)
      {
        m_frame = 0;
        m_time  = 0.0f;
        return;
      }

      const float fps = glm::max(1.0f, m_clip->m_fps);

      // The sheet range covers the clip duration and every key that lives in it.
      int lastKeyFrame = 0;
      for (const auto& track : m_clip->m_keys)
      {
        if (!track.second.empty())
        {
          lastKeyFrame = glm::max(lastKeyFrame, track.second.back().m_frame);
        }
      }

      const int durationFrames = (int) glm::round(glm::max(m_clip->m_duration, 0.0f) * fps);
      m_endFrame               = glm::max(glm::max(durationFrames, lastKeyFrame), 1);
      m_frame                  = 0;
      m_time                   = 0.0f;
      m_scrollX                = 0.0f;
      m_scrollY                = 0.0f;
      m_playState              = PlayState::Stopped;

      ResolveTracks();
    }

    bool DopeSheetView::CanEdit() const
    {
      const App* app = GetApp();
      return app != nullptr && app->m_gameMod == GameMod::Stop;
    }

    float DopeSheetView::CurrentTime() const
    {
      if (m_clip == nullptr)
      {
        return 0.0f;
      }

      return m_frame / glm::max(1.0f, m_clip->m_fps);
    }

    void DopeSheetView::Show()
    {
      HandleSceneChange();

      // The sheet is drawn once per frame, so it advances its own playhead. No engine update hook
      // is needed and playback stops when the window is hidden.
      Update(ImGui::GetIO().DeltaTime);
      ResolveTracks();
      ValidateSelection();

      ShowClipHeader();
      ShowTransport();
      ShowKeyTools();

      ImGui::Separator();
      ShowSheet();

      // A key drag ends on the mouse release even when it happens outside the sheet, so the drag is
      // finished here rather than inside the lane that started it.
      if (m_dragging)
      {
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
          CommitKeyDrag();
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
          m_dragging = false; // Cancelled, the key stays where it was.
        }
      }
    }

    void DopeSheetView::Update(float deltaTime)
    {
      if (m_clip == nullptr || m_playState != PlayState::Playing || !CanEdit())
      {
        return;
      }

      const float fps     = glm::max(1.0f, m_clip->m_fps);
      const float endTime = m_endFrame / fps;

      m_time += deltaTime * m_speed;

      if (m_time >= endTime)
      {
        if (m_loop && endTime > 0.0f)
        {
          m_time = fmodf(m_time, endTime);
        }
        else
        {
          // One shot: hold the last frame and stay there until the user plays again.
          m_time      = endTime;
          m_playState = PlayState::Paused;
        }
      }

      if (m_time < 0.0f)
      {
        m_time = 0.0f;
      }

      m_frame = (int) glm::round(m_time * fps);
      ApplyPoseAt(m_time);
    }

    void DopeSheetView::SetFrame(int frame, bool applyPose)
    {
      // A manual scrub takes over from playback.
      if (m_playState == PlayState::Playing)
      {
        m_playState = PlayState::Paused;
      }

      m_frame = glm::clamp(frame, 0, glm::max(m_endFrame, 0));

      if (m_clip != nullptr)
      {
        m_time = m_frame / glm::max(1.0f, m_clip->m_fps);
      }

      if (applyPose)
      {
        BeginPreviewSession();
        ApplyPoseAt(m_time);
      }
    }

    void DopeSheetView::Play()
    {
      if (m_clip == nullptr || !CanEdit())
      {
        return;
      }

      BeginPreviewSession();
      m_playState = PlayState::Playing;
    }

    void DopeSheetView::Pause()
    {
      if (m_playState == PlayState::Playing)
      {
        m_playState = PlayState::Paused;
      }
    }

    void DopeSheetView::Stop()
    {
      RestoreBaseTransforms();
      m_playState = PlayState::Stopped;
      m_frame     = 0;
      m_time      = 0.0f;
    }

    void DopeSheetView::StepFrame(int delta)
    {
      SetFrame(m_frame + delta, true);
    }

    void DopeSheetView::HandleSceneChange()
    {
      EditorScenePtr scene   = GetApp()->GetCurrentScene();
      const ObjectId sceneId = scene != nullptr ? scene->GetIdVal() : NullHandle;

      if (sceneId == m_sceneId)
      {
        return;
      }

      // The cached mapping and the preview snapshot belong to a scene that is gone. The clip is a
      // resource and survives the switch, so it stays bound to the sheet.
      m_sceneId       = sceneId;
      m_sessionActive = false;
      m_dragging      = false;
      m_trackEntities.clear();
      m_entityTracks.clear();
      m_baseTransforms.clear();

      if (m_playState == PlayState::Playing)
      {
        m_playState = PlayState::Paused;
      }
    }

    void DopeSheetView::ResolveTracks()
    {
      m_trackEntities.clear();
      m_entityTracks.clear();

      if (m_clip == nullptr)
      {
        return;
      }

      EditorScenePtr scene = GetApp()->GetCurrentScene();
      if (scene == nullptr)
      {
        return;
      }

      // Bucket the entities by name once, then let each track claim the first entity that is still
      // free. Scene order decides, so the mapping stays stable while the scene is edited, and an
      // entity is claimed at most once, which keeps two entities that share a name from pointing at
      // the same track. The sheet runs this every frame, so the buckets keep it linear.
      std::unordered_map<String, EntityPtrArray> entitiesByName;
      for (const EntityPtr& ntt : scene->GetEntities())
      {
        if (ntt != nullptr)
        {
          entitiesByName[ntt->GetNameVal()].push_back(ntt);
        }
      }

      for (const auto& track : m_clip->m_keys)
      {
        auto bucket = entitiesByName.find(track.first);
        if (bucket == entitiesByName.end())
        {
          continue;
        }

        for (EntityPtr& ntt : bucket->second)
        {
          if (ntt == nullptr)
          {
            continue; // Already claimed by another track.
          }

          m_trackEntities[track.first]    = ntt;
          m_entityTracks[ntt->GetIdVal()] = track.first;
          ntt                             = nullptr; // Claim it for this track.
          break;
        }
      }
    }

    EntityPtr DopeSheetView::EntityForTrack(const String& trackName) const
    {
      auto it = m_trackEntities.find(trackName);
      return it != m_trackEntities.end() ? it->second : nullptr;
    }

    String DopeSheetView::TrackNameForEntity(EntityPtr ntt, bool create)
    {
      if (m_clip == nullptr || ntt == nullptr)
      {
        return "";
      }

      EditorScenePtr scene = GetApp()->GetCurrentScene();
      if (scene == nullptr)
      {
        return "";
      }

      // The engine does not enforce unique entity names. The first entity in scene order keeps the
      // plain name, the next ones are suffixed, so every track still belongs to exactly one entity.
      String baseName = ntt->GetNameVal();
      if (baseName.empty())
      {
        baseName = "Entity_" + std::to_string(ntt->GetIdVal());
      }

      int duplicateIndx = 0;
      for (const EntityPtr& other : scene->GetEntities())
      {
        if (other == nullptr || other.get() == ntt.get())
        {
          break; // Nothing after this entity can take the name away from it.
        }

        if (other->GetNameVal() == ntt->GetNameVal())
        {
          duplicateIndx++;
        }
      }

      const String trackName = duplicateIndx == 0 ? baseName : baseName + "_" + std::to_string(duplicateIndx);

      if (create && m_clip->m_keys.Find(trackName) == nullptr)
      {
        m_clip->m_keys.Insert(trackName, KeyArray());
      }

      return m_clip->m_keys.Find(trackName) != nullptr ? trackName : "";
    }

    bool DopeSheetView::SampleTrack(const KeyArray& keys, float time, Vec3& pos, Quaternion& rot, Vec3& scale)
    {
      if (m_clip == nullptr)
      {
        return false;
      }

      // The engine's own sampler, so the sheet previews the curve playback plays, interpolation
      // modes included, instead of keeping a second copy of the math in step with it.
      return m_clip->SampleTrack(keys, time, pos, rot, scale);
    }

    bool DopeSheetView::IsSkinned(EntityPtr ntt)
    {
      if (ntt == nullptr)
      {
        return false;
      }

      MeshComponentPtr meshComp = ntt->GetMeshComponent();
      if (meshComp == nullptr)
      {
        return false;
      }

      const MeshPtr mesh = meshComp->GetMeshVal();
      return mesh != nullptr && mesh->IsSkinned();
    }

    int DopeSheetView::TickStep(float pxPerFrame)
    {
      static const int steps[] = {1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000};

      for (int step : steps)
      {
        if (step * pxPerFrame >= g_rulerLabelSpacing)
        {
          return step;
        }
      }

      return steps[sizeof(steps) / sizeof(steps[0]) - 1];
    }

    void DopeSheetView::BeginPreviewSession()
    {
      if (m_sessionActive || m_clip == nullptr)
      {
        return;
      }

      ResolveTracks();

      // Remember what the entities looked like before the sheet starts moving them, so Stop can
      // put them back exactly where the user left them.
      m_baseTransforms.clear();
      for (const auto& entry : m_trackEntities)
      {
        EntityPtr ntt = entry.second;
        if (ntt == nullptr || IsSkinned(ntt))
        {
          continue;
        }

        m_baseTransforms[ntt->GetIdVal()] = ntt->m_node->GetTransform(TransformationSpace::TS_LOCAL);
      }

      m_sessionActive = true;
    }

    void DopeSheetView::RestoreBaseTransforms()
    {
      if (!m_sessionActive)
      {
        return;
      }

      if (EditorScenePtr scene = GetApp()->GetCurrentScene())
      {
        for (const auto& entry : m_baseTransforms)
        {
          if (EntityPtr ntt = scene->GetEntity(entry.first))
          {
            ntt->m_node->SetTransform(entry.second, TransformationSpace::TS_LOCAL);
          }
        }
      }

      m_baseTransforms.clear();
      m_sessionActive = false;
    }

    void DopeSheetView::ApplyPoseAt(float time)
    {
      if (m_clip == nullptr || !CanEdit())
      {
        return;
      }

      ResolveTracks();

      for (auto& entry : m_clip->m_keys)
      {
        const KeyArray& keys = entry.second;
        if (keys.empty())
        {
          continue; // An empty track would assert inside GetNearestKeys.
        }

        EntityPtr ntt = EntityForTrack(entry.first);
        if (ntt == nullptr || IsSkinned(ntt))
        {
          continue; // Unmatched or skinned: Phase 1 only drives plain entity nodes.
        }

        Vec3 pos, scale;
        Quaternion rot;
        if (SampleTrack(keys, time, pos, rot, scale))
        {
          ntt->m_node->SetLocalTransforms(pos, rot, scale);
        }
      }
    }

    void DopeSheetView::SetKeyOnSelection()
    {
      App* editor = GetApp();

      if (m_clip == nullptr)
      {
        if (editor != nullptr)
        {
          editor->SetStatusMsg(g_statusFailed);
        }
        TK_WRN("Dope sheet has no clip. Drop an animation or create a new one.");
        return;
      }

      if (!CanEdit())
      {
        if (editor != nullptr)
        {
          editor->SetStatusMsg(g_statusFailed);
        }
        TK_WRN("Keying is disabled while the simulation is running.");
        return;
      }

      EditorScenePtr scene = GetApp()->GetCurrentScene();
      if (scene == nullptr)
      {
        return;
      }

      EntityPtrArray selection;
      scene->GetSelectedEntities(selection);
      if (selection.empty())
      {
        editor->SetStatusMsg(g_statusFailed);
        TK_WRN("Select an entity to key.");
        return;
      }

      ResolveTracks();

      const float fps  = glm::max(1.0f, m_clip->m_fps);
      const float time = m_frame / fps;

      // Skinned meshes carry their keys on bone tracks, which the sheet does not edit yet.
      EntityPtrArray keyable;
      int skippedSkinned = 0;

      for (EntityPtr ntt : selection)
      {
        if (IsSkinned(ntt))
        {
          skippedSkinned++;
        }
        else
        {
          keyable.push_back(ntt);
        }
      }

      if (keyable.empty())
      {
        editor->SetStatusMsg(g_statusFailed);
        TK_WRN("Dope sheet skips skinned meshes, their tracks are not editable yet: %d skipped.",
               skippedSkinned);
        return;
      }

      // One key press is one undo step, even when several entities are keyed: the per entity edits
      // are added as a group.
      const bool groupEdits = keyable.size() > 1;
      if (groupEdits)
      {
        ActionManager::GetInstance()->BeginActionGroup();
      }

      int keyed = 0;

      for (EntityPtr ntt : keyable)
      {
        const String trackName = TrackNameForEntity(ntt, true);
        if (trackName.empty())
        {
          continue;
        }

        KeyArray* keys = m_clip->m_keys.Find(trackName);
        if (keys == nullptr)
        {
          continue;
        }

        // Channels the mask leaves out keep what the curve already holds at this frame, so a
        // rotation only key never freezes a translation curve. A track with no keys yet falls back
        // to the node's current values.
        const Vec3 pos          = ntt->m_node->GetTranslation(TransformationSpace::TS_LOCAL);
        const Quaternion rot    = ntt->m_node->GetOrientation(TransformationSpace::TS_LOCAL);
        const Vec3 scale        = ntt->m_node->GetScale();

        Vec3 curvePos           = pos;
        Quaternion curveRot     = rot;
        Vec3 curveScale         = scale;
        SampleTrack(*keys, time, curvePos, curveRot, curveScale);

        Key key;
        key.m_frame    = m_frame;
        key.m_position = m_keyTranslation ? pos : curvePos;
        key.m_rotation = m_keyRotation ? rot : curveRot;
        key.m_scale    = m_keyScale ? scale : curveScale;
        key.m_interp   = m_newKeyInterp;

        // Undoable: the action replaces the key that may already sit on this frame.
        KeyEditAction::SetKey(m_clip, trackName, key);

        // A fresh key becomes the selection, so it can be dragged right away.
        m_selectedTrack = trackName;
        m_selectedFrame = m_frame;
        keyed++;
      }

      if (groupEdits && keyed > 0)
      {
        ActionManager::GetInstance()->GroupLastActions(keyed);
      }

      if (keyed > 0)
      {
        m_endFrame         = glm::max(m_endFrame, m_frame);
        m_clip->m_duration = glm::max(m_clip->m_duration, m_endFrame / fps);

        editor->SetStatusMsg(g_statusSucceeded);
        TK_LOG("Dope sheet: key set at frame %d for %d entities.", m_frame, keyed);
      }

      if (skippedSkinned > 0)
      {
        TK_WRN("Dope sheet skips skinned meshes, their tracks are not editable yet: %d skipped.",
               skippedSkinned);
      }
    }

    bool DopeSheetView::HasSelectedKey() const
    {
      return m_clip != nullptr && !m_selectedTrack.empty() && m_selectedFrame >= 0;
    }

    void DopeSheetView::DeleteSelectedKey()
    {
      if (!HasSelectedKey() || !CanEdit())
      {
        return;
      }

      const String track = m_selectedTrack;
      const int frame    = m_selectedFrame;

      m_selectedTrack.clear();
      m_selectedFrame = -1;

      if (!TrackHasKey(track, frame))
      {
        // Undo already removed it, nothing left to delete.
        return;
      }

      KeyEditAction::DeleteKey(m_clip, track, frame);
      TK_LOG("Dope sheet: key deleted at frame %d on track %s.", frame, track.c_str());
    }

    void DopeSheetView::SetKeyInterp(const String& trackName, int frame, KeyInterp interp)
    {
      if (m_clip == nullptr || !CanEdit() || frame < 0)
      {
        return;
      }

      // The action reads the key, changes its mode and writes it back, so undo restores the key as
      // a whole. It refuses to stack anything when the mode is already the one asked for.
      KeyEditAction::SetInterp(m_clip, trackName, frame, interp);

      if (m_sessionActive)
      {
        // Rewrite the pose so the new curve is visible without having to scrub.
        ApplyPoseAt(CurrentTime());
      }

      TK_LOG("Dope sheet: key at frame %d on track %s is %s.",
             frame,
             trackName.c_str(),
             InterpLabel(interp));
    }

    void DopeSheetView::SmoothAllKeys()
    {
      if (m_clip == nullptr || !CanEdit())
      {
        return;
      }

      // Counted first: an untouched key stacks nothing, and a group is only opened when more than
      // one key is going to change (the same rule Set Key follows for several entities).
      int pending = 0;
      for (const auto& track : m_clip->m_keys)
      {
        for (const Key& key : track.second)
        {
          if (key.m_interp != KeyInterp::Smooth)
          {
            pending++;
          }
        }
      }

      if (pending == 0)
      {
        GetApp()->SetStatusMsg("Every key is already Smooth.");
        return;
      }

      ActionManager* actionManager = ActionManager::GetInstance();
      const bool group             = pending > 1;
      if (group)
      {
        actionManager->BeginActionGroup();
      }

      for (const auto& track : m_clip->m_keys)
      {
        // SetInterp replaces the key in place and the track vector keeps its size, so walking it
        // here is safe.
        for (const Key& key : track.second)
        {
          if (key.m_interp != KeyInterp::Smooth)
          {
            KeyEditAction::SetInterp(m_clip, track.first, key.m_frame, KeyInterp::Smooth);
          }
        }
      }

      if (group)
      {
        actionManager->GroupLastActions(pending);
      }

      if (m_sessionActive)
      {
        ApplyPoseAt(CurrentTime());
      }

      TK_LOG("Dope sheet: %d keys set to Smooth.", pending);
      GetApp()->SetStatusMsg(g_statusSucceeded);
    }

    KeyInterp DopeSheetView::KeyInterpAt(const String& trackName, int frame) const
    {
      if (m_clip == nullptr || frame < 0)
      {
        return KeyInterp::Linear;
      }

      const KeyArray* keys = m_clip->m_keys.Find(trackName);
      if (keys == nullptr)
      {
        return KeyInterp::Linear;
      }

      for (const Key& key : *keys)
      {
        if (key.m_frame == frame)
        {
          return key.m_interp;
        }
      }

      return KeyInterp::Linear;
    }

    bool DopeSheetView::TrackHasKey(const String& trackName, int frame) const    {
      if (m_clip == nullptr || frame < 0)
      {
        return false;
      }

      const KeyArray* keys = m_clip->m_keys.Find(trackName);
      if (keys == nullptr)
      {
        return false;
      }

      for (const Key& key : *keys)
      {
        if (key.m_frame == frame)
        {
          return true;
        }
      }

      return false;
    }

    void DopeSheetView::ValidateSelection()
    {
      if (!HasSelectedKey())
      {
        return;
      }

      if (!TrackHasKey(m_selectedTrack, m_selectedFrame))
      {
        // An undo removed the key under the selection.
        m_selectedTrack.clear();
        m_selectedFrame = -1;
      }
    }

    void DopeSheetView::CommitKeyDrag()
    {
      if (!m_dragging)
      {
        return;
      }

      const int fromFrame = m_dragFromFrame;
      const int toFrame   = m_dragToFrame;

      m_dragging = false;

      if (fromFrame < 0 || toFrame < 0 || fromFrame == toFrame)
      {
        return;
      }

      // The action moves the key and replaces whatever sat on the target frame.
      KeyEditAction::MoveKey(m_clip, m_dragTrack, fromFrame, toFrame);

      m_selectedTrack = m_dragTrack;
      m_selectedFrame = toFrame;

      TK_LOG("Dope sheet: key moved from frame %d to %d on track %s.",
             fromFrame,
             toFrame,
             m_dragTrack.c_str());
    }

    float DopeSheetView::FrameToX(int frame, float laneLeft) const
    {
      return laneLeft + frame * m_pxPerFrame - m_scrollX;
    }

    int DopeSheetView::XToFrame(float x, float laneLeft) const
    {
      if (m_pxPerFrame <= 0.0f)
      {
        return 0;
      }

      return (int) glm::round((x - laneLeft + m_scrollX) / m_pxPerFrame);
    }

    void DopeSheetView::ClampScroll(float laneWidth)
    {
      const float contentWidth = glm::max(0.0f, m_endFrame * m_pxPerFrame);
      const float maxScroll    = glm::max(0.0f, contentWidth - laneWidth + g_scrollMargin);
      m_scrollX                = glm::clamp(m_scrollX, 0.0f, maxScroll);
    }

    void DopeSheetView::FitView(float laneWidth)
    {
      // One frame past the range plus a little slack, so the last frame and its key diamond sit
      // inside the lane instead of on the edge of the window.
      const float frames = glm::max(1.0f, (float) m_endFrame + 1.0f);
      const float usable = glm::max(40.0f, laneWidth) - g_fitPaddingPx;

      m_pxPerFrame       = glm::clamp(usable / frames, g_minPxPerFrame, g_maxPxPerFrame);
      m_scrollX          = 0.0f;
    }

    void DopeSheetView::CreateClip(const String& name)
    {
      if (name.empty())
      {
        return;
      }

      // Animations live next to meshes in this engine, AnimationPath() resolves the Meshes folder
      // of the project's resource tree (a workspace project always creates it).
      const String clipName = name.size() > ANIM.size() && name.compare(name.size() - ANIM.size(), ANIM.size(), ANIM) == 0
                                  ? name.substr(0, name.size() - ANIM.size())
                                  : name;

      // Every press makes a clip. CreateIncrementalFileFullPath() returns the path untouched when it
      // is free and appends "(n)" when a file is already there, so an existing clip is never
      // overwritten. No postfix: this is a new clip, not a copy.
      const String path = CreateIncrementalFileFullPath(AnimationPath(clipName + ANIM), "");

      // The clip that is being replaced keeps its unsaved keys in memory (it stays in the animation
      // manager), but leaving it behind is worth a warning.
      if (m_clip != nullptr && m_clip->m_dirty)
      {
        TK_WRN("Dope sheet: the clip being replaced has unsaved keys (%s).",
               GetRelativeResourcePath(m_clip->GetFile()).c_str());
      }

      AnimationPtr clip = MakeNewPtr<Animation>();
      clip->SetFile(path);
      clip->m_fps      = (float) g_defaultFps;
      clip->m_duration = g_defaultEndFrame / clip->m_fps;
      clip->m_dirty    = true;

      GetAnimationManager()->Manage(clip);
      clip->Save(false);

      SetAnimation(clip);

      // The new file shows up in the asset browsers without waiting for a manual refresh.
      for (FolderWindow* folderWnd : GetApp()->GetAssetBrowsers())
      {
        folderWnd->UpdateContent();
      }

      GetApp()->SetStatusMsg(g_statusSucceeded);
      TK_LOG("Dope sheet: created animation %s", GetRelativeResourcePath(path).c_str());
    }

    void DopeSheetView::ShowClipHeader()
    {
      App* editor = GetApp();

      // Clip slot: drop an .anim here or create one. The drop zone is reused from the material and
      // animation inspectors.
      View::DropZone(EditorImGuiTextureCache::Acquire(UI::m_clipIcon),
                     m_clip != nullptr ? m_clip->GetFile() : String(),
                     [this](DirectoryEntry& entry) -> void
                     {
                       if (GetResourceType(entry.m_ext) != Animation::StaticClass())
                       {
                         GetApp()->SetStatusMsg(g_statusFailed);
                         TK_ERR("Dope sheet accepts animations only.");
                         return;
                       }

                       SetAnimation(GetAnimationManager()->Create<Animation>(entry.GetFullPath()));
                     },
                     "Clip",
                     CanEdit());

      ImGui::SameLine();
      ImGui::BeginGroup();

      if (m_clip == nullptr)
      {
        ImGui::TextUnformatted("No clip. Drop an .anim here or create a new one.");
      }
      else
      {
        String name, ext, path;
        DecomposePath(m_clip->GetFile(), &path, &name, &ext);
        ImGui::Text("%s%s   |   %d track%s   |   %.3f s",
                    name.c_str(),
                    ext.c_str(),
                    (int) m_clip->m_keys.size(),
                    m_clip->m_keys.size() == 1 ? "" : "s",
                    m_endFrame / glm::max(1.0f, m_clip->m_fps));
      }

      ImGui::BeginDisabled(!CanEdit() || m_clip == nullptr);
      ImGui::PushItemWidth(80.0f);

      float fps = m_clip != nullptr ? m_clip->m_fps : (float) g_defaultFps;
      if (ImGui::DragFloat("fps", &fps, 1.0f, 1.0f, 240.0f, "%.0f") && m_clip != nullptr)
      {
        m_clip->m_fps   = glm::max(1.0f, fps);
        m_clip->m_dirty = true;
      }

      ImGui::SameLine();
      int endFrame = m_endFrame;
      if (ImGui::DragInt("end", &endFrame, 1.0f, 1, 100000) && m_clip != nullptr)
      {
        m_endFrame         = glm::max(1, endFrame);
        m_clip->m_duration = m_endFrame / glm::max(1.0f, m_clip->m_fps);
        m_clip->m_dirty    = true;
      }

      ImGui::PopItemWidth();
      ImGui::EndDisabled();

      ImGui::SameLine();
      ImGui::BeginDisabled(m_clip == nullptr);
      if (ImGui::Button("Save"))
      {
        m_clip->Save(false);
        editor->SetStatusMsg(g_statusSucceeded);
      }
      ImGui::EndDisabled();

      ImGui::SameLine();
      // Always available while the sheet may write: a press makes a new clip and binds it, replacing
      // whatever clip is loaded.
      ImGui::BeginDisabled(!CanEdit());
      if (ImGui::Button("New Clip"))
      {
        StringInputWindowPtr inputWnd = MakeNewPtr<StringInputWindow>("NewClip##DopeSheetNewClip", true);
        inputWnd->m_inputVal          = "NewAnimation";
        inputWnd->m_inputLabel        = "Name";
        inputWnd->m_hint              = "Clip name";
        inputWnd->m_taskFn            = [this](const String& val) -> void { CreateClip(val); };
        inputWnd->AddToUI();
      }
      ImGui::EndDisabled();

      ImGui::EndGroup();

      if (m_clip == nullptr)
      {
        return;
      }

      ImGui::BeginDisabled(!CanEdit());
      if (m_clip->m_dirty)
      {
        ImGui::SameLine();
        ImGui::TextDisabled("(unsaved keys)");
      }
      ImGui::EndDisabled();
    }

    void DopeSheetView::ShowTransport()
    {
      const Vec2 btnSize(24.0f, 24.0f);
      const bool editable = m_clip != nullptr && CanEdit();

      ImGui::BeginDisabled(!editable);

      if (UI::ButtonDecorless(ICON_FA_FAST_BACKWARD, btnSize))
      {
        SetFrame(0, true);
      }

      ImGui::SameLine();
      if (UI::ButtonDecorless(ICON_FA_STEP_BACKWARD, btnSize))
      {
        StepFrame(-1);
      }

      ImGui::SameLine();
      // Font icons for the whole transport: the texture based buttons used to sit at a different
      // height than the step buttons next to them, a text button of the same size lines up with them.
      const bool playing = m_playState == PlayState::Playing;
      if (UI::ButtonDecorless(playing ? ICON_FA_PAUSE : ICON_FA_PLAY, btnSize))
      {
        if (playing)
        {
          Pause();
        }
        else
        {
          Play();
        }
      }

      ImGui::SameLine();
      if (UI::ButtonDecorless(ICON_FA_STOP, btnSize))
      {
        Stop();
      }

      ImGui::SameLine();
      if (UI::ButtonDecorless(ICON_FA_STEP_FORWARD, btnSize))
      {
        StepFrame(1);
      }

      ImGui::SameLine();
      if (UI::ButtonDecorless(ICON_FA_FAST_FORWARD, btnSize))
      {
        SetFrame(m_endFrame, true);
      }

      ImGui::SameLine();
      ImGui::PushItemWidth(90.0f);
      int frame = m_frame;
      if (ImGui::DragInt("##dopeSheetFrame", &frame, 0.5f, 0, m_endFrame, "frame %d"))
      {
        SetFrame(frame, true);
      }

      ImGui::SameLine();
      ImGui::DragFloat("##dopeSheetSpeed", &m_speed, 0.01f, 0.1f, 5.0f, "x%.2f");

      ImGui::SameLine();
      ImGui::TextDisabled("%.3f s", CurrentTime());

      ImGui::SameLine();
      ImGui::Checkbox("Loop", &m_loop);
      ImGui::PopItemWidth();

      ImGui::EndDisabled();
    }

    void DopeSheetView::ShowKeyTools()
    {
      ImGui::TextUnformatted("Set Key channels:");
      ImGui::SameLine();
      ImGui::Checkbox("T", &m_keyTranslation);
      UI::HelpMarker("DopeSheetKeyT", "Writes the entity translation to the key.");
      ImGui::SameLine();
      ImGui::Checkbox("R", &m_keyRotation);
      UI::HelpMarker("DopeSheetKeyR", "Writes the entity rotation to the key.");
      ImGui::SameLine();
      ImGui::Checkbox("S", &m_keyScale);
      UI::HelpMarker("DopeSheetKeyS", "Writes the entity scale to the key.");

      ImGui::SameLine();
      ImGui::BeginDisabled(m_clip == nullptr || !CanEdit());
      if (UI::ButtonDecorless(ICON_FA_KEY " Set Key (K)", Vec2(130.0f, 0.0f)))
      {
        SetKeyOnSelection();
      }
      ImGui::EndDisabled();

      // Mode a key created by the sheet gets. It defaults to Linear, the engine default, so keying
      // never changes how a clip plays unless the animator asks for a mode.
      ImGui::SameLine();
      ImGui::TextUnformatted("|  New key:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(130.0f);

      if (ImGui::BeginCombo("##dopeSheetNewKeyInterp", InterpLabel(m_newKeyInterp)))
      {
        for (KeyInterp interp : g_keyInterps)
        {
          const bool selected = interp == m_newKeyInterp;
          if (ImGui::Selectable(InterpLabel(interp), selected))
          {
            m_newKeyInterp = interp;
          }

          if (selected)
          {
            ImGui::SetItemDefaultFocus();
          }
        }

        ImGui::EndCombo();
      }

      UI::HelpMarker("DopeSheetNewKeyInterp",
                     "Interpolation mode written with every new key. Linear leaves the curve as it "
                     "is; Smooth blends through the key, Flat eases into it and Stepped holds the "
                     "value until the next key. Existing keys are changed from the row context menu.");

      ImGui::SameLine();
      ImGui::BeginDisabled(m_clip == nullptr || !CanEdit());
      if (ImGui::Button("Smooth All Keys"))
      {
        SmoothAllKeys();
      }
      UI::HelpMarker("DopeSheetSmoothAll",
                     "Sets every key of every track to Smooth as one undo step. This is the way to "
                     "smooth a clip that was keyed before interpolation modes existed.");
      ImGui::EndDisabled();

      ImGui::SameLine();
      ImGui::BeginDisabled(m_clip == nullptr);
      if (ImGui::Button("Fit"))
      {
        // Fit the whole range in the lane area, which is the window minus the name column.
        const float laneWidth = glm::max(40.0f, ImGui::GetWindowSize().x -
                                                   (2.0f * ImGui::GetStyle().WindowPadding.x) -
                                                   m_nameColumnWidth);
        FitView(laneWidth);
      }
      ImGui::EndDisabled();

      // What the clip currently matches in the scene. Tracks without an entity are listed in the
      // sheet, they just never drive anything.
      int matched   = 0;
      int unmatched = 0;

      if (m_clip != nullptr)
      {
        for (const auto& track : m_clip->m_keys)
        {
          if (EntityForTrack(track.first) != nullptr)
          {
            matched++;
          }
          else
          {
            unmatched++;
          }
        }
      }

      ImGui::SameLine();
      ImGui::TextDisabled("|  %d matched, %d unmatched", matched, unmatched);
    }

    void DopeSheetView::ShowSheet()
    {
      if (m_clip == nullptr)
      {
        ImGui::TextUnformatted("Drop an animation clip above to edit its keys.");
        return;
      }

      const ImVec2 avail   = ImGui::GetContentRegionAvail();
      const float originX  = ImGui::GetCursorScreenPos().x;
      const float laneLeft = originX + m_nameColumnWidth;
      const float laneW    = glm::max(40.0f, avail.x - m_nameColumnWidth);

      ShowRuler(laneLeft, laneW);

      // The lanes own their scrolling so the wheel can pan and zoom the timeline instead of
      // scrolling the child. Ruler and lanes share the sheet's horizontal transform.
      const ImGuiWindowFlags laneFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
      if (ImGui::BeginChild("##dopeSheetLanes", Vec2(0.0f, 0.0f), ImGuiChildFlags_Borders, laneFlags))
      {
        ShowLanes(laneLeft, laneW);
      }
      ImGui::EndChild();
    }

    void DopeSheetView::ShowRuler(float laneLeft, float laneWidth)
    {
      ImDrawList* dl          = ImGui::GetWindowDrawList();
      ImGuiIO& io             = ImGui::GetIO();
      const ImVec2 origin     = ImGui::GetCursorScreenPos();
      const ImVec2 size(ImGui::GetContentRegionAvail().x, g_rulerHeight);
      const float rulerBottom = origin.y + size.y;

      // Name column splitter, grabbed by its right edge.
      ImGui::SetCursorScreenPos(ImVec2(laneLeft - g_splitterWidth, origin.y));
      ImGui::InvisibleButton("##dopeSheetSplitter", ImVec2(g_splitterWidth, size.y));
      if (ImGui::IsItemActive())
      {
        m_nameColumnWidth = glm::clamp(m_nameColumnWidth + io.MouseDelta.x, g_minNameColumn, g_maxNameColumn);
      }

      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
      {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }

      // Scrub area: press and drag anywhere on the ruler to move the playhead.
      ImGui::SetCursorScreenPos(ImVec2(laneLeft, origin.y));
      ImGui::InvisibleButton("##dopeSheetRuler", ImVec2(laneWidth, size.y));

      if (ImGui::IsItemActive())
      {
        SetFrame(XToFrame(io.MousePos.x, laneLeft), true);
      }

      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
      {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }

      const ImU32 barColor     = ImGui::GetColorU32(ImGuiCol_MenuBarBg);
      const ImU32 trackColumnTint = ImGui::GetColorU32(ImGuiCol_Text, g_trackColumnTint);
      const ImU32 columnLine   = ImGui::GetColorU32(ImGuiCol_Text, g_columnLineTint);
      const ImU32 tickColor    = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const ImU32 labelColor   = ImGui::GetColorU32(ImGuiCol_Text);
      const ImU32 outOfRange   = ImGui::GetColorU32(g_outOfRangeVeil);
      const ImU32 cursorColor  = ImGui::GetColorU32(ImVec4(g_selectHighLightPrimaryColor));

      dl->AddRectFilled(origin, ImVec2(origin.x + size.x, rulerBottom), barColor);

      // The strip above the track names carries the same tint as the column below it.
      dl->AddRectFilled(origin, ImVec2(laneLeft, rulerBottom), trackColumnTint);

      // Everything past the last frame is outside the clip.
      const float endX = FrameToX(m_endFrame, laneLeft);
      if (endX < laneLeft + laneWidth)
      {
        dl->AddRectFilled(ImVec2(glm::max(endX, laneLeft), origin.y),
                          ImVec2(laneLeft + laneWidth, rulerBottom),
                          outOfRange);
      }

      const int tickStep   = TickStep(m_pxPerFrame);
      const float leftFrame  = m_scrollX / m_pxPerFrame;
      const float rightFrame = leftFrame + laneWidth / m_pxPerFrame;

      const int firstTick = (int) std::floor(leftFrame / tickStep) * tickStep;
      const int lastTick  = (int) std::ceil(rightFrame / tickStep) * tickStep;

      for (int frame = firstTick; frame <= lastTick; frame += tickStep)
      {
        const float x = FrameToX(frame, laneLeft);
        if (x < laneLeft || x > laneLeft + laneWidth)
        {
          continue;
        }

        dl->AddLine(ImVec2(x, rulerBottom - 8.0f), ImVec2(x, rulerBottom), tickColor);

        if (frame >= 0)
        {
          const String label = std::to_string(frame);
          dl->AddText(ImVec2(x + 3.0f, origin.y + 4.0f), labelColor, label.c_str());
        }
      }

      // Playhead handle.
      const float playheadX = FrameToX(m_frame, laneLeft);
      if (playheadX >= laneLeft - 6.0f && playheadX <= laneLeft + laneWidth + 6.0f)
      {
        dl->AddTriangleFilled(ImVec2(playheadX - 6.0f, origin.y),
                              ImVec2(playheadX + 6.0f, origin.y),
                              ImVec2(playheadX, origin.y + 8.0f),
                              cursorColor);
        dl->AddLine(ImVec2(playheadX, origin.y + 8.0f), ImVec2(playheadX, rulerBottom), cursorColor, 2.0f);
      }

      dl->AddLine(ImVec2(laneLeft - g_splitterWidth, origin.y),
                  ImVec2(laneLeft - g_splitterWidth, rulerBottom),
                  tickColor);

      dl->AddLine(ImVec2(laneLeft, origin.y), ImVec2(laneLeft, rulerBottom), columnLine);

      // Keep the next section below the ruler.
      ImGui::SetCursorScreenPos(ImVec2(origin.x, rulerBottom));
    }

    void DopeSheetView::ShowLanes(float laneLeft, float laneWidth)
    {
      ImDrawList* dl          = ImGui::GetWindowDrawList();
      ImGuiIO& io             = ImGui::GetIO();
      const ImVec2 origin     = ImGui::GetCursorScreenPos();
      const ImVec2 avail      = ImGui::GetContentRegionAvail();
      const float viewHeight  = avail.y;
      const float contentH    = m_clip->m_keys.size() * g_rowHeight;
      const float maxScrollY  = glm::max(0.0f, contentH - viewHeight);
      const int rowCount      = (int) m_clip->m_keys.size();

      // Plain wheel scrolls the rows, shift pans the timeline, ctrl zooms around the cursor.
      if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MouseWheel != 0.0f)
      {
        if (io.KeyCtrl)
        {
          const float anchorFrame = (io.MousePos.x - laneLeft + m_scrollX) / m_pxPerFrame;
          const float zoom        = io.MouseWheel > 0.0f ? g_zoomStep : 1.0f / g_zoomStep;

          m_pxPerFrame            = glm::clamp(m_pxPerFrame * zoom, g_minPxPerFrame, g_maxPxPerFrame);
          m_scrollX               = anchorFrame * m_pxPerFrame - (io.MousePos.x - laneLeft);
        }
        else if (io.KeyShift)
        {
          m_scrollX -= io.MouseWheel * m_pxPerFrame * g_panWheelFrames;
        }
        else
        {
          m_scrollY -= io.MouseWheel * g_rowHeight * g_scrollWheelRows;
        }
      }

      m_scrollY = glm::clamp(m_scrollY, 0.0f, maxScrollY);
      ClampScroll(laneWidth);

      if (rowCount == 0)
      {
        dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 6.0f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "No keys yet. Select an entity, scrub to a frame and press Set Key.");
        return;
      }

      const int firstRow = glm::clamp((int) (m_scrollY / g_rowHeight), 0, rowCount - 1);
      const int lastRow  = glm::clamp((int) ((m_scrollY + viewHeight) / g_rowHeight) + 1, 0, rowCount);

      ImGui::PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + viewHeight), true);

      const ImU32 trackColumnTint = ImGui::GetColorU32(ImGuiCol_Text, g_trackColumnTint);
      const ImU32 columnLine      = ImGui::GetColorU32(ImGuiCol_Text, g_columnLineTint);
      const ImU32 rowEven         = ImGui::GetColorU32(ImGuiCol_FrameBg, 0.28f);
      const ImU32 rowOdd          = ImGui::GetColorU32(ImGuiCol_FrameBg, 0.14f);
      const ImU32 rowHover        = ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.35f);
      const ImU32 keyColor        = ImGui::GetColorU32(ImGuiCol_Text);
      const ImU32 cursorColor     = ImGui::GetColorU32(ImVec4(g_selectHighLightPrimaryColor));
      const ImU32 outOfRange      = ImGui::GetColorU32(g_outOfRangeVeil);

      // The track column is only tinted, the row stripes stay in the lane area, so the two do not
      // read as one surface.
      dl->AddRectFilled(origin, ImVec2(laneLeft, origin.y + viewHeight), trackColumnTint);
      dl->AddLine(ImVec2(laneLeft, origin.y), ImVec2(laneLeft, origin.y + viewHeight), columnLine);

      const float lineHeight = ImGui::GetTextLineHeight();

      for (int row = firstRow; row < lastRow; row++)
      {
        const String& trackName = m_clip->m_keys[row].first;
        const KeyArray& keys    = m_clip->m_keys[row].second;
        const float rowY        = origin.y + row * g_rowHeight - m_scrollY;

        ImGui::PushID(row);

        // One hit area per row. Phase 1 uses it to scrub the playhead; key selection and dragging
        // arrive with the later phases.
        ImGui::SetCursorScreenPos(ImVec2(origin.x, rowY));
        ImGui::InvisibleButton("##dopeSheetLane", ImVec2(avail.x, g_rowHeight));

        const bool rowHovered = ImGui::IsItemHovered();
        const bool rowActive  = ImGui::IsItemActive();

        const EntityPtr ntt = EntityForTrack(trackName);
        const bool skinned  = IsSkinned(ntt);
        bool openCtx        = false;

        ImU32 background    = (row % 2 == 0) ? rowEven : rowOdd;
        if (rowHovered)
        {
          background = rowHover;
        }

        dl->AddRectFilled(ImVec2(laneLeft, rowY),
                          ImVec2(origin.x + avail.x, rowY + g_rowHeight),
                          background);

        // Keys.
        const float keyY           = rowY + g_rowHeight * 0.5f;
        int hoveredKeyFrame        = -1;
        KeyInterp hoveredKeyInterp = KeyInterp::Linear;
        const ImU32 holdColor      = ImGui::GetColorU32(ImGuiCol_Text, 0.35f);

        for (size_t keyIndex = 0; keyIndex < keys.size(); keyIndex++)
        {
          const Key& key   = keys[keyIndex];
          const float keyX = FrameToX(key.m_frame, laneLeft);

          // A held segment draws a bar to the next key, the classic "this does not move until
          // here" read. Which segment is held is the engine's rule, not a guess: either endpoint
          // being Stepped holds it, so the sheet cannot disagree with playback.
          if (keyIndex + 1 < keys.size() &&
              Interpolation::ResolveSegment(key.m_interp, keys[keyIndex + 1].m_interp) == SegmentKind::Hold)
          {
            const float holdX = FrameToX(keys[keyIndex + 1].m_frame, laneLeft);
            dl->AddLine(ImVec2(keyX, keyY), ImVec2(glm::min(holdX, laneLeft + laneWidth), keyY), holdColor, 2.0f);
          }

          if (keyX < laneLeft - g_keyRadius || keyX > laneLeft + laneWidth + g_keyRadius)
          {
            continue;
          }

          if (rowHovered && glm::abs(io.MousePos.x - keyX) <= g_keyRadius &&
              glm::abs(io.MousePos.y - keyY) <= g_keyRadius)
          {
            hoveredKeyFrame  = key.m_frame;
            hoveredKeyInterp = key.m_interp;
          }

          const bool draggingSource = m_dragging && trackName == m_dragTrack && key.m_frame == m_dragFromFrame;
          if (draggingSource)
          {
            // While it is being dragged, the key is drawn as a ghost on the target frame below.
            DrawKeyMarker(dl, ImVec2(keyX, keyY), g_keyRadius, key.m_interp, cursorColor, false);
            continue;
          }

          const bool selected = (trackName == m_selectedTrack && key.m_frame == m_selectedFrame);
          const bool current  = key.m_frame == m_frame;

          DrawKeyMarker(dl,
                        ImVec2(keyX, keyY),
                        g_keyRadius,
                        key.m_interp,
                        (selected || current) ? cursorColor : keyColor,
                        true);

          if (selected)
          {
            DrawKeyMarker(dl, ImVec2(keyX, keyY), g_keyRadius, key.m_interp,
                          ImGui::GetColorU32(ImGuiCol_Text), false);
          }
        }

        // Ghost of the key being dragged, drawn on the frame it would land on.
        if (m_dragging && trackName == m_dragTrack && m_dragToFrame >= 0)
        {
          const float ghostX = FrameToX(m_dragToFrame, laneLeft);

          DrawKeyMarker(dl, ImVec2(ghostX, keyY), g_keyRadius, m_dragInterp, cursorColor, true);
          DrawKeyMarker(dl, ImVec2(ghostX, keyY), g_keyRadius, m_dragInterp, ImGui::GetColorU32(ImGuiCol_Text), false);

          const String frameLabel = Format("%d", m_dragToFrame);
          dl->AddText(ImVec2(ghostX + g_keyRadius + 4.0f, rowY + 2.0f),
                      ImGui::GetColorU32(ImGuiCol_Text),
                      frameLabel.c_str());
        }

        // Name column.
        const ImU32 nameColor = ntt == nullptr ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                                              : ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddText(ImVec2(origin.x + 6.0f, rowY + (g_rowHeight - lineHeight) * 0.5f), nameColor, trackName.c_str());

        String info = Format("%d key%s", (int) keys.size(), keys.size() == 1 ? "" : "s");
        if (skinned)
        {
          info += "  [skinned]";
        }
        else if (ntt == nullptr)
        {
          info += "  [no entity]";
        }

        const float infoWidth = ImGui::CalcTextSize(info.c_str()).x;
        dl->AddText(ImVec2(laneLeft - 10.0f - infoWidth, rowY + (g_rowHeight - lineHeight) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    info.c_str());

        // Tooltip on a hovered key only; while the user is scrubbing the row it would follow the
        // drag.
        if (hoveredKeyFrame >= 0 && !rowActive)
        {
          ImGui::SetTooltip("%s\nframe %d  (%.3f s)\n%s",
                            trackName.c_str(),
                            hoveredKeyFrame,
                            hoveredKeyFrame / glm::max(1.0f, m_clip->m_fps),
                            InterpLabel(hoveredKeyInterp));
        }

        if (rowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
          m_ctxTrack = trackName;
          m_ctxFrame = hoveredKeyFrame;
          openCtx    = true;
        }

        // Left click on a key selects it and starts a time drag; a click anywhere else scrubs the
        // playhead and drops the selection.
        if (rowHovered && hoveredKeyFrame >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
          m_selectedTrack = trackName;
          m_selectedFrame = hoveredKeyFrame;
          m_dragTrack     = trackName;
          m_dragFromFrame = hoveredKeyFrame;
          m_dragToFrame   = hoveredKeyFrame;
          m_dragInterp    = hoveredKeyInterp;
          m_dragging      = true;
        }
        else if (rowActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_dragging)
        {
          m_selectedTrack.clear();
          m_selectedFrame = -1;
        }

        if (m_dragging && m_dragTrack == trackName)
        {
          // The row that owns the drag keeps following the mouse, even when it leaves the row.
          m_dragToFrame = glm::clamp(XToFrame(io.MousePos.x, laneLeft), 0, m_endFrame);
        }
        else if (rowActive)
        {
          SetFrame(XToFrame(io.MousePos.x, laneLeft), true);
        }

        ImGui::PopID();

        // The popup is opened outside the row's id scope, so it matches the BeginPopup below.
        if (openCtx)
        {
          ImGui::OpenPopup("##dopeSheetRowCtx");
        }
      }

      // Frame grid and playhead over the rows.
      const float endX      = FrameToX(m_endFrame, laneLeft);
      const float playheadX = FrameToX(m_frame, laneLeft);

      // Everything past the last frame is outside the clip: it is dimmed so the inactive part of the
      // timeline reads as inactive, keys sitting there included. The name column is left alone, it
      // belongs to no frame.
      if (endX < laneLeft + laneWidth)
      {
        dl->AddRectFilled(ImVec2(glm::max(endX, laneLeft), origin.y),
                          ImVec2(laneLeft + laneWidth, origin.y + viewHeight),
                          outOfRange);
      }

      if (endX > laneLeft && endX < laneLeft + laneWidth)
      {
        dl->AddLine(ImVec2(endX, origin.y), ImVec2(endX, origin.y + viewHeight), cursorColor, 1.0f);
      }

      if (playheadX >= laneLeft && playheadX <= laneLeft + laneWidth)
      {
        dl->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, origin.y + viewHeight), cursorColor, 1.0f);
      }

      ImGui::PopClipRect();

      // Vertical scroll indicator: the lane child hides its scrollbar so the wheel stays free for
      // panning and zooming.
      if (maxScrollY > 0.0f)
      {
        const float barHeight = glm::max(24.0f, viewHeight * (viewHeight / contentH));
        const float barY      = origin.y + (viewHeight - barHeight) * (m_scrollY / maxScrollY);
        const float barX      = origin.x + avail.x - 4.0f;

        dl->AddRectFilled(ImVec2(barX, barY),
                          ImVec2(barX + 4.0f, barY + barHeight),
                          ImGui::GetColorU32(ImGuiCol_ScrollbarGrab));
      }

      if (ImGui::BeginPopup("##dopeSheetRowCtx"))
      {
        ImGui::TextUnformatted(m_ctxTrack.c_str());
        ImGui::Separator();

        const int ctxFrame = m_ctxFrame;

        ImGui::BeginDisabled(!CanEdit());
        if (ImGui::MenuItem("Delete Key", nullptr, false, ctxFrame >= 0))
        {
          m_selectedTrack.clear();
          m_selectedFrame = -1;
          KeyEditAction::DeleteKey(m_clip, m_ctxTrack, ctxFrame);
        }
        ImGui::EndDisabled();

        // Interpolation of the key under the cursor. The menu stays open while the sheet is only
        // previewing, so the mode can still be read; the modes themselves are disabled then.
        if (ctxFrame >= 0 && ImGui::BeginMenu("Interpolation"))
        {
          const KeyInterp current = KeyInterpAt(m_ctxTrack, ctxFrame);

          ImGui::BeginDisabled(!CanEdit());
          for (KeyInterp interp : g_keyInterps)
          {
            if (ImGui::MenuItem(InterpLabel(interp), nullptr, interp == current))
            {
              m_selectedTrack = m_ctxTrack;
              m_selectedFrame = ctxFrame;
              SetKeyInterp(m_ctxTrack, ctxFrame, interp);
            }
          }
          ImGui::EndDisabled();

          ImGui::Separator();
          ImGui::TextDisabled("Smooth blends through the key,\nFlat eases into it, Stepped holds.");

          ImGui::EndMenu();
        }

        ImGui::BeginDisabled(!CanEdit());
        // Track removal is not undoable yet; it drops every key of the track.
        if (ImGui::MenuItem("Delete Track"))
        {
          // Stop first: the preview snapshot of the track that is going away must be restored
          // before its keys are gone.
          Stop();

          if (m_clip->m_keys.Erase(m_ctxTrack))
          {
            m_clip->m_dirty = true;
            m_selectedTrack.clear();
            m_selectedFrame = -1;
            ResolveTracks();
          }
        }
        ImGui::EndDisabled();

        ImGui::EndPopup();
      }
    }

    // DopeSheetWindow
    //////////////////////////////////////////

    TKDefineClass(DopeSheetWindow, Window);

    DopeSheetWindow::DopeSheetWindow()
    {
      m_view = MakeNewPtr<DopeSheetView>();
    }

    DopeSheetWindow::~DopeSheetWindow() { m_view = nullptr; }

    void DopeSheetWindow::SetAnimation(AnimationPtr anim)
    {
      if (m_view != nullptr)
      {
        m_view->SetAnimation(anim);
      }
    }

    void DopeSheetWindow::SetKeyOnSelection()
    {
      if (m_view != nullptr)
      {
        m_view->SetKeyOnSelection();
      }
    }

    void DopeSheetWindow::Show()
    {
      const String windowId = m_name + "##" + std::to_string(GetIdVal());

      ImGuiIO& io           = ImGui::GetIO();
      ImGui::SetNextWindowSize(Vec2(900.0f, 420.0f), ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowPos(Vec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver,
                              Vec2(0.5f, 0.5f));

      if (ImGui::Begin(windowId.c_str(), &m_visible))
      {
        HandleStates();

        if (m_view != nullptr)
        {
          m_view->Show();
        }
      }
      ImGui::End();
    }

    void DopeSheetWindow::DispatchSignals() const
    {
      // The sheet handles its own keys; Window::ModShortCutSignals() is deliberately not called
      // here, it would steal S/R/G/C/X as transform modes while the mouse hovers the sheet.
      if (m_view == nullptr || !CanDispatchSignals() || UI::IsKeyboardCaptured() || ImGui::GetIO().WantTextInput)
      {
        return;
      }

      if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
      {
        if (m_view->m_playState == DopeSheetView::PlayState::Playing)
        {
          m_view->Pause();
        }
        else
        {
          m_view->Play();
        }
      }

      if (ImGui::IsKeyPressed(ImGuiKey_K, false))
      {
        m_view->SetKeyOnSelection();
      }

      if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
      {
        m_view->DeleteSelectedKey();
      }

      // Undo / redo of the key edits. The sheet does not call Window::ModShortCutSignals(), which is
      // where the other windows pick these up, so they are handled here. The stack is the editor
      // wide one, so an undo may also step back an edit made elsewhere.
      if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && ImGui::IsKeyDown(ImGuiMod_Ctrl))
      {
        if (ImGui::IsKeyDown(ImGuiMod_Shift))
        {
          ActionManager::GetInstance()->Redo();
        }
        else
        {
          ActionManager::GetInstance()->Undo();
        }
      }

      if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
      {
        m_view->StepFrame(-1);
      }

      if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
      {
        m_view->StepFrame(1);
      }

      if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
      {
        m_view->SetFrame(0, true);
      }

      if (ImGui::IsKeyPressed(ImGuiKey_End, false))
      {
        m_view->SetFrame(m_view->m_endFrame, true);
      }
    }

  } // namespace Editor
} // namespace ToolKit
