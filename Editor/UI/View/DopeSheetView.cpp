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

    // Curve view. The plot is read only, it exists to show what the key modes do to the motion.
    const int g_curveBandCount      = 3;       //!< Translation, rotation and scale bands.
    const float g_curveThickness    = 2.0f;    //!< Width of a plotted curve.
    const float g_curveBandGap      = 8.0f;    //!< Gap between two channel bands.
    const float g_curveRangePad     = 0.08f;   //!< Share of the value range kept as head room.
    const float g_curveMinRange     = 1.0f;    //!< Value range a flat curve is drawn in.
    const float g_curveLabelInset   = 6.0f;    //!< Inset of the band labels from the band edge.

    /** Colours of the plotted curves, x / y / z in that order. */
    const ImU32 g_curveAxisColors[] = {IM_COL32(235, 96, 96, 255),
                                       IM_COL32(126, 214, 106, 255),
                                       IM_COL32(104, 160, 240, 255)};

    // Wash over the frames past the last one. A mid gray reads as "inactive" on a dark and on a light
    // theme alike, while a theme background colour would blend into the sheet and show nothing.
    const ImVec4 g_outOfRangeVeil(0.5f, 0.5f, 0.5f, 0.22f);

    // Key set gestures. A key drag is clamped to the sheet range, so a group can never be pushed past
    // the last frame by accident; the rubber band is padded by the key marker width, because a click
    // that only touches the tip of a diamond is still meant to take the key.
    const float g_rubberBandPadding = 4.0f;  //!< Slack around the rubber band, so it grabs what it covers.
    const float g_rubberBandBorder  = 2.0f;  //!< Width of the rubber band outline.

    /** Rubber band fill: the cursor colour at a faint alpha, so the box reads as a selection. */
    const ImVec4 g_rubberBandFill(g_selectHighLightPrimaryColor.x,
                                  g_selectHighLightPrimaryColor.y,
                                  g_selectHighLightPrimaryColor.z,
                                  0.12f);

    /** Compact textual form of a sampled parameter value, for the sheet rows. */
    String FormatParamValue(ParameterVariant::VariantType type, const Vec4& value)
    {
      switch (type)
      {
        case ParameterVariant::VariantType::Float:
          return Format("%.3g", value.x);
        case ParameterVariant::VariantType::Int:
        case ParameterVariant::VariantType::UInt:
        case ParameterVariant::VariantType::Byte:
        case ParameterVariant::VariantType::Ubyte:
          return Format("%d", (int) glm::round(value.x));
        case ParameterVariant::VariantType::Bool:
          return value.x != 0.0f ? "true" : "false";
        case ParameterVariant::VariantType::Vec2:
          return Format("%.3g, %.3g", value.x, value.y);
        case ParameterVariant::VariantType::Vec3:
          return Format("%.3g, %.3g, %.3g", value.x, value.y, value.z);
        case ParameterVariant::VariantType::Vec4:
          return Format("%.3g, %.3g, %.3g, %.3g", value.x, value.y, value.z, value.w);
        default:
          return "";
      }
    }

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

      /**
       * Euler degrees of a rotation, made continuous with the sample before it. A rotation read as
       * three angles needs two corrections to be usable as a curve:
       *
       * - The angles wrap, so a component that lands a full turn away from the previous sample is
       *   brought back next to it, which is what turns a spinning track into a readable ramp instead
       *   of a sawtooth.
       * - The same rotation can be written by two triples, (x, y, z) and (x + 180, 180 - y, z + 180),
       *   and the one closer to the previous sample is taken. Without this a track that passes through
       *   yaw +- 90 degrees, where pitch and roll are not separately defined, jumps by half a turn in
       *   the middle of a smooth turn and reads as a broken curve.
       *
       * @param rotation Sampled rotation.
       * @param previous Sample before it, null for the first sample of a track.
       */
      Vec3 ContinuousEuler(const Quaternion& rotation, const Vec3* previous)
      {
        const Vec3 euler = glm::degrees(glm::eulerAngles(rotation));
        if (previous == nullptr)
        {
          return euler;
        }

        Vec3 candidates[2] = {euler, Vec3(euler.x + 180.0f, 180.0f - euler.y, euler.z + 180.0f)};

        Vec3 best      = candidates[0];
        float bestCost = -1.0f;

        for (Vec3& candidate : candidates)
        {
          float cost = 0.0f;
          for (int axis = 0; axis < 3; axis++)
          {
            // Next to the sample before it first, then how far it had to move to get there.
            candidate[axis] -= 360.0f * glm::round((candidate[axis] - (*previous)[axis]) / 360.0f);
            cost += glm::abs(candidate[axis] - (*previous)[axis]);
          }

          if (bestCost < 0.0f || cost < bestCost)
          {
            best     = candidate;
            bestCost = cost;
          }
        }

        return best;
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
      RestorePreviewState();
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
      ClearSelection();

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
      HandleSelectionShortcuts();

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
      RestorePreviewState();
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
      // resource and survives the switch, so it stays bound to the sheet, but the selection does not:
      // the tracks it names resolve against the scene that was replaced.
      m_sceneId       = sceneId;
      m_sessionActive = false;
      m_trackEntities.clear();
      m_entityTracks.clear();
      m_baseTransforms.clear();
      ClearSelection();

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

      // The parameter values the preview is about to overwrite, so Stop can put those back as well.
      m_baseParams.clear();
      for (const auto& track : m_clip->m_paramKeys)
      {
        if (track.second.empty())
        {
          continue;
        }

        EntityPtr ntt = EntityForParamTrack(track.first);
        if (ntt == nullptr)
        {
          continue;
        }

        ParameterVariant* var = m_clip->ResolveParamTrack(ntt, track.first);
        if (var == nullptr)
        {
          continue;
        }

        ParamSnapshot snapshot;
        if (!Animation::PackParamValue(*var, snapshot.value))
        {
          continue;
        }

        snapshot.type        = var->GetType();
        m_baseParams[track.first] = snapshot;
      }

      m_sessionActive = true;
    }

    void DopeSheetView::RestorePreviewState()
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

      // Put the parameter values back the same way the transforms are: written through the variant,
      // so the parameter's own change callbacks run again.
      for (const auto& entry : m_baseParams)
      {
        EntityPtr ntt = EntityForParamTrack(entry.first);
        if (ntt == nullptr)
        {
          continue;
        }

        if (ParameterVariant* var = m_clip->ResolveParamTrack(ntt, entry.first))
        {
          Animation::UnpackParamValue(*var, entry.second.type, entry.second.value);
        }
      }

      m_baseTransforms.clear();
      m_baseParams.clear();
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

      // Parameter tracks are independent of the node tracks: a light color or a material parameter
      // does not come from a transform, so the clip has to be applied to the scene for them too.
      ApplyParamTracksAt(time);
    }

    void DopeSheetView::ApplyParamTracksAt(float time)
    {
      if (m_clip == nullptr || m_clip->m_paramKeys.empty() || !CanEdit())
      {
        return;
      }

      // Which entities the clip's parameter tracks address. The engine resolves and writes the
      // values, the sheet only decides who is involved.
      EntityPtrArray targets;
      for (const auto& track : m_clip->m_paramKeys)
      {
        if (track.second.empty())
        {
          continue;
        }

        EntityPtr ntt = EntityForParamTrack(track.first);
        if (ntt == nullptr || std::find(targets.begin(), targets.end(), ntt) != targets.end())
        {
          continue;
        }

        targets.push_back(ntt);
      }

      for (EntityPtr ntt : targets)
      {
        m_clip->ApplyParamTracks(ntt, time);
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

        // A fresh key becomes the selection, so it can be dragged right away. The last keyed row wins,
        // which keeps the gesture an animator expects: set a key, drag the key that just appeared.
        SelectOnly(trackName, m_frame, false);
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

    bool DopeSheetView::HasSelectedKey() const { return m_clip != nullptr && !m_selection.empty(); }

    bool DopeSheetView::IsKeySelected(const String& track, int frame, bool param) const
    {
      for (const KeyRef& keyRef : m_selection)
      {
        if (keyRef.m_frame == frame && keyRef.m_param == param && keyRef.m_track == track)
        {
          return true;
        }
      }

      return false;
    }

    void DopeSheetView::SelectOnly(const String& track, int frame, bool param)
    {
      KeyRef keyRef;
      keyRef.m_track = track;
      keyRef.m_frame = frame;
      keyRef.m_param = param;

      m_selection.clear();
      m_selection.push_back(keyRef);
    }

    void DopeSheetView::ToggleSelection(const String& track, int frame, bool param)
    {
      for (auto it = m_selection.begin(); it != m_selection.end(); ++it)
      {
        if (it->m_frame == frame && it->m_param == param && it->m_track == track)
        {
          m_selection.erase(it);
          return;
        }
      }

      KeyRef keyRef;
      keyRef.m_track = track;
      keyRef.m_frame = frame;
      keyRef.m_param = param;
      m_selection.push_back(keyRef);
    }

    void DopeSheetView::ClearSelection()
    {
      m_selection.clear();

      // A drag carries the keys it is moving, so a cleared selection must end it too, or the release
      // would move keys the user can no longer see.
      m_dragging      = false;
      m_dragFromFrame = -1;
      m_dragToFrame   = -1;
      m_dragFromFrames.clear();
    }

    void DopeSheetView::SelectAllKeys()
    {
      if (m_clip == nullptr)
      {
        return;
      }

      m_selection.clear();

      auto addTrackKeys = [this](const auto& tracks) -> void
      {
        for (const auto& track : tracks)
        {
          for (const auto& key : track.second)
          {
            KeyRef keyRef;
            keyRef.m_track = track.first;
            keyRef.m_frame = key.m_frame;
            // The parameter flag is what tells the two key containers apart, and both Key and ParamKey
            // carry their frame, so one walk serves both.
            keyRef.m_param = std::is_same_v<std::decay_t<decltype(key)>, ParamKey>;
            m_selection.push_back(keyRef);
          }
        }
      };

      addTrackKeys(m_clip->m_keys);
      addTrackKeys(m_clip->m_paramKeys);
    }

    const DopeSheetView::KeyRef* DopeSheetView::PrimarySelection() const
    {
      return m_selection.empty() ? nullptr : &m_selection.back();
    }

    void DopeSheetView::DeleteSelectedKey()
    {
      if (!HasSelectedKey() || !CanEdit())
      {
        return;
      }

      // The keys are bucketed per track first, then read out ascending. Bucketing keeps the delete
      // actions of one track next to each other, which is what makes the undo group walk the clip in a
      // readable order.
      struct TrackFrames
      {
        String m_track;             //!< Track that owns the keys.
        bool m_param = false;       //!< The keys belong to a parameter track.
        std::vector<int> m_frames;  //!< Frames of the selected keys of that track.
      };

      std::vector<TrackFrames> tracks;

      for (const KeyRef& keyRef : m_selection)
      {
        auto entry = std::find_if(tracks.begin(),
                                  tracks.end(),
                                  [&keyRef](const TrackFrames& candidate) -> bool
                                  { return candidate.m_track == keyRef.m_track; });

        if (entry == tracks.end())
        {
          TrackFrames bucket;
          bucket.m_track = keyRef.m_track;
          bucket.m_param = keyRef.m_param;
          tracks.push_back(bucket);
          entry = tracks.end() - 1;
        }

        entry->m_frames.push_back(keyRef.m_frame);
      }

      // One undo step for the whole selection, so the group is opened before the first action lands.
      const bool groupEdits = m_selection.size() > 1;
      if (groupEdits)
      {
        ActionManager::GetInstance()->BeginActionGroup();
      }

      int removed = 0;

      for (TrackFrames& track : tracks)
      {
        std::sort(track.m_frames.begin(), track.m_frames.end());

        for (int frame : track.m_frames)
        {
          if (!TrackHasKey(track.m_track, frame, track.m_param))
          {
            continue; // Undo already removed it, nothing left to delete.
          }

          if (track.m_param)
          {
            KeyEditAction::DeleteParamKey(m_clip, track.m_track, frame);
          }
          else
          {
            KeyEditAction::DeleteKey(m_clip, track.m_track, frame);
          }

          removed++;
        }
      }

      // Same rule as the move: the group is closed as soon as one action landed, and removed only counts
      // the deletes that really pushed one.
      if (groupEdits && removed > 0)
      {
        ActionManager::GetInstance()->GroupLastActions(removed);
      }

      ClearSelection();

      if (removed > 1)
      {
        TK_LOG("Dope sheet: %d keys deleted as one undo step.", removed);
      }
    }

    bool DopeSheetView::KeyAt(const String& trackName, int frame, Key& key) const
    {
      if (m_clip == nullptr)
      {
        return false;
      }

      const KeyArray* keys = m_clip->m_keys.Find(trackName);
      if (keys == nullptr)
      {
        return false;
      }

      for (const Key& candidate : *keys)
      {
        if (candidate.m_frame == frame)
        {
          key = candidate;
          return true;
        }
      }

      return false;
    }

    bool DopeSheetView::ParamKeyAt(const String& trackName, int frame, ParamKey& key) const
    {
      if (m_clip == nullptr)
      {
        return false;
      }

      const ParamKeyArray* keys = m_clip->m_paramKeys.Find(trackName);
      if (keys == nullptr)
      {
        return false;
      }

      for (const ParamKey& candidate : *keys)
      {
        if (candidate.m_frame == frame)
        {
          key = candidate;
          return true;
        }
      }

      return false;
    }

    void DopeSheetView::CopyKey(const String& trackName, int frame, bool paramTrack)
    {
      if (m_clip == nullptr || trackName.empty() || frame < 0)
      {
        return;
      }

      KeyClipboard clipboard;
      if (paramTrack)
      {
        if (!ParamKeyAt(trackName, frame, clipboard.m_param))
        {
          return;
        }
      }
      else if (!KeyAt(trackName, frame, clipboard.m_key))
      {
        return;
      }

      clipboard.m_valid    = true;
      clipboard.m_paramKey = paramTrack;
      clipboard.m_track    = trackName;
      clipboard.m_frame    = frame;

      m_keyClipboard = clipboard;

      // The copied key becomes the selection, so Ctrl+V lands back on the same row without the
      // animator having to select it first. The clipboard holds one key, so the rest of a group goes.
      SelectOnly(trackName, frame, paramTrack);

      GetApp()->SetStatusMsg(Format("Key copied from frame %d.", frame));
      TK_LOG("Dope sheet: key copied from track %s at frame %d.", trackName.c_str(), frame);
    }

    void DopeSheetView::CopySelectedKey()
    {
      const KeyRef* selected = PrimarySelection();

      if (selected == nullptr)
      {
        GetApp()->SetStatusMsg("Select a key to copy.");
        return;
      }

      CopyKey(selected->m_track, selected->m_frame, selected->m_param);
    }

    void DopeSheetView::PasteKeyOn(const String& trackName, bool paramTrack)
    {
      if (!m_keyClipboard.m_valid || m_clip == nullptr || trackName.empty() || !CanEdit())
      {
        return;
      }

      // A transform key and a parameter key do not describe the same thing, so a paste across the two
      // is refused instead of writing a key no code path would ever read.
      if (paramTrack != m_keyClipboard.m_paramKey)
      {
        GetApp()->SetStatusMsg("A transform key cannot be pasted on a parameter track, or the other "
                               "way around.");
        return;
      }

      if (paramTrack)
      {
        // The key has to match the parameter it addresses: a Float key on a Vec3 track would sit in
        // the clip and never drive anything.
        ParameterVariant* var = nullptr;
        if (EntityPtr ntt = EntityForParamTrack(trackName))
        {
          var = m_clip->ResolveParamTrack(ntt, trackName);
        }

        if (var != nullptr && var->GetType() != m_keyClipboard.m_param.m_type)
        {
          GetApp()->SetStatusMsg("The copied key does not match the type of this parameter.");
          return;
        }

        ParamKey key = m_keyClipboard.m_param;
        key.m_frame  = m_frame;
        KeyEditAction::SetParamKey(m_clip, trackName, key);
      }
      else
      {
        Key key     = m_keyClipboard.m_key;
        key.m_frame = m_frame;
        KeyEditAction::SetKey(m_clip, trackName, key);
      }

      // The pasted key becomes the selection, so a repeated paste keeps hitting the same row.
      SelectOnly(trackName, m_frame, paramTrack);

      if (m_sessionActive)
      {
        // Rewrite the pose so the pasted key shows without having to scrub.
        ApplyPoseAt(CurrentTime());
      }

      GetApp()->SetStatusMsg(g_statusSucceeded);
      TK_LOG("Dope sheet: key pasted on track %s at frame %d.", trackName.c_str(), m_frame);
    }

    void DopeSheetView::PasteKey()
    {
      if (!m_keyClipboard.m_valid)
      {
        GetApp()->SetStatusMsg("Copy a key first.");
        return;
      }

      // The selected row wins, otherwise the key goes back to the row it came from, which makes
      // Ctrl+C followed by Ctrl+V on another frame a duplicate in place. The clipboard holds a single
      // key, so the primary selection is the one that decides where it lands.
      const KeyRef* selected = PrimarySelection();

      if (selected != nullptr)
      {
        PasteKeyOn(selected->m_track, selected->m_param);
      }
      else
      {
        PasteKeyOn(m_keyClipboard.m_track, m_keyClipboard.m_paramKey);
      }
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

    bool DopeSheetView::TrackHasKey(const String& trackName, int frame, bool paramTrack) const
    {
      if (m_clip == nullptr || frame < 0)
      {
        return false;
      }

      if (paramTrack)
      {
        const ParamKeyArray* keys = m_clip->m_paramKeys.Find(trackName);
        if (keys == nullptr)
        {
          return false;
        }

        for (const ParamKey& key : *keys)
        {
          if (key.m_frame == frame)
          {
            return true;
          }
        }

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

    // Parameter tracks
    //////////////////////////////////////////

    EntityPtr DopeSheetView::EntityForParamTrack(const String& trackId) const
    {
      EditorScenePtr scene = GetApp()->GetCurrentScene();
      if (scene == nullptr || trackId.empty())
      {
        return nullptr;
      }

      // Entity names may contain dots, so the longest name that prefixes the track owns it.
      EntityPtr best    = nullptr;
      size_t bestLength = 0;

      for (const EntityPtr& ntt : scene->GetEntities())
      {
        if (ntt == nullptr)
        {
          continue;
        }

        const String name = ntt->GetNameVal();
        if (name.empty() || name.length() <= bestLength)
        {
          continue;
        }

        if (StartsWith(trackId, name + "."))
        {
          best       = ntt;
          bestLength = name.length();
        }
      }

      return best;
    }

    DopeSheetView::ParamKeyState DopeSheetView::GetParamKeyState(const String& trackId) const
    {
      if (m_clip == nullptr)
      {
        return ParamKeyState::NoClip;
      }

      const ParamKeyArray* keys = m_clip->m_paramKeys.Find(trackId);
      if (keys == nullptr || keys->empty())
      {
        return ParamKeyState::NotAnimated;
      }

      for (const ParamKey& key : *keys)
      {
        if (key.m_frame == m_frame)
        {
          return ParamKeyState::KeyAtFrame;
        }
      }

      return ParamKeyState::Animated;
    }

    bool DopeSheetView::ParamTrackHasKey(const String& trackId, int frame) const
    {
      if (m_clip == nullptr || frame < 0)
      {
        return false;
      }

      const ParamKeyArray* keys = m_clip->m_paramKeys.Find(trackId);
      if (keys == nullptr)
      {
        return false;
      }

      for (const ParamKey& key : *keys)
      {
        if (key.m_frame == frame)
        {
          return true;
        }
      }

      return false;
    }

    bool DopeSheetView::SetParamKey(const String& trackId)
    {
      if (m_clip == nullptr || !CanEdit())
      {
        return false;
      }

      EntityPtr ntt = EntityForParamTrack(trackId);
      if (ntt == nullptr)
      {
        return false;
      }

      ParameterVariant* var = m_clip->ResolveParamTrack(ntt, trackId);
      if (var == nullptr)
      {
        return false;
      }

      // The value on screen right now becomes the key, so a Set Key after an edit in the inspector
      // records exactly that.
      ParamKey key;
      key.m_frame = m_frame;

      if (!Animation::PackParamValue(*var, key.m_value))
      {
        TK_WRN("Dope sheet: %s is not a keyframable parameter.", trackId.c_str());
        return false;
      }

      key.m_type = var->GetType();
      KeyEditAction::SetParamKey(m_clip, trackId, key);

      m_endFrame         = glm::max(m_endFrame, m_frame);
      m_clip->m_duration = glm::max(m_clip->m_duration, m_endFrame / glm::max(1.0f, m_clip->m_fps));

      GetApp()->SetStatusMsg(g_statusSucceeded);
      TK_LOG("Dope sheet: parameter key set at frame %d for %s.", m_frame, trackId.c_str());
      return true;
    }

    void DopeSheetView::DeleteParamKey(const String& trackId, int frame)
    {
      if (m_clip == nullptr || !CanEdit() || !ParamTrackHasKey(trackId, frame))
      {
        return;
      }

      KeyEditAction::DeleteParamKey(m_clip, trackId, frame);
      TK_LOG("Dope sheet: parameter key deleted at frame %d for %s.", frame, trackId.c_str());
    }

    void DopeSheetView::ValidateSelection()
    {
      if (m_selection.empty())
      {
        return;
      }

      if (m_clip == nullptr)
      {
        ClearSelection();
        return;
      }

      // An undo, a delete action or a clip edit elsewhere may have taken a selected key away. Both
      // lists are walked backwards so an entry and its drag snapshot leave together: the drag reads
      // the snapshot by index, and a half pruned pair would move the wrong key.
      for (int i = (int) m_selection.size() - 1; i >= 0; i--)
      {
        const KeyRef& keyRef = m_selection[i];

        if (TrackHasKey(keyRef.m_track, keyRef.m_frame, keyRef.m_param))
        {
          continue;
        }

        m_selection.erase(m_selection.begin() + i);

        if (i < (int) m_dragFromFrames.size())
        {
          m_dragFromFrames.erase(m_dragFromFrames.begin() + i);
        }
      }

      if (m_selection.empty())
      {
        m_dragging = false;
        m_dragFromFrames.clear();
      }
    }

    int DopeSheetView::DragDelta(const std::vector<int>& fromFrames) const
    {
      if (m_dragFromFrame < 0 || m_dragToFrame < 0 || fromFrames.empty())
      {
        return 0;
      }

      int lowest = fromFrames[0];
      for (int frame : fromFrames)
      {
        lowest = glm::min(lowest, frame);
      }

      return glm::max(m_dragToFrame - m_dragFromFrame, -lowest);
    }

    void DopeSheetView::CommitKeyDrag()
    {
      if (!m_dragging)
      {
        return;
      }

      const int fromFrame = m_dragFromFrame;
      const int toFrame   = m_dragToFrame;

      // The keys are copied out before the drag is closed: ClearSelection drops the snapshot and the
      // selection is rebuilt from it further down. The frames are copied as well, because the member is
      // cleared below and every later step of the commit has to work from this copy.
      std::vector<KeyRef> movedKeys = m_selection;
      std::vector<int> fromFrames   = m_dragFromFrames;

      m_dragging = false;
      m_dragFromFrames.clear();

      if (fromFrame < 0 || toFrame < 0 || fromFrame == toFrame || movedKeys.empty() ||
          movedKeys.size() != fromFrames.size())
      {
        TK_WRN("Dope sheet: drag dropped, %d selected key(s), %d frame(s) snapshotted, %d -> %d.",
               (int) movedKeys.size(),
               (int) fromFrames.size(),
               fromFrame,
               toFrame);
        return;
      }

      // The anchor can be gone when the selection changed while the drag was running.
      const size_t anchor = m_dragAnchor < movedKeys.size() ? m_dragAnchor : 0;

      const int delta = DragDelta(fromFrames);

      if (delta == 0)
      {
        // The offset was clamped away: the group already sits on the lowest frame it may reach, or the
        // drag came back to where it started. Nothing is written, and no undo step is opened for it --
        // a per key move of zero frames pushes no action, so grouping would run on an empty stack.
        TK_LOG("Dope sheet: drag of %d key(s) left the frames untouched (%d -> %d).",
               (int) movedKeys.size(),
               fromFrame,
               toFrame);

        m_selection = movedKeys;
        return;
      }

      // One undo step for the whole group, so the group is opened before the first move lands.
      const bool groupEdits = movedKeys.size() > 1;
      if (groupEdits)
      {
        ActionManager::GetInstance()->BeginActionGroup();
      }

      // A key that moves towards another one would overwrite it before it has moved itself, so the
      // group is walked away from the direction of travel: descending frames for a move to the right,
      // ascending for a move to the left. Whatever sits inside the group therefore never gets
      // overwritten, and only keys outside it are replaced, which is the point of a move.
      std::vector<size_t> order(movedKeys.size());
      for (size_t i = 0; i < order.size(); i++)
      {
        order[i] = i;
      }

      std::sort(order.begin(),
                order.end(),
                [&fromFrames, delta](size_t left, size_t right) -> bool
                {
                  return delta > 0 ? fromFrames[left] > fromFrames[right] : fromFrames[left] < fromFrames[right];
                });

      int moved = 0;

      for (size_t indx : order)
      {
        KeyRef& keyRef   = movedKeys[indx];
        const int frame  = fromFrames[indx];
        const int target = frame + delta;

        if (!TrackHasKey(keyRef.m_track, frame, keyRef.m_param))
        {
          continue; // Undo took it away while the drag was running.
        }

        // A key that would land on the frame it already sits on pushes no action of its own, so it must
        // not be counted either: the count is what the undo group is built from.
        if (target == frame)
        {
          continue;
        }

        if (keyRef.m_param)
        {
          KeyEditAction::MoveParamKey(m_clip, keyRef.m_track, frame, target);
        }
        else
        {
          KeyEditAction::MoveKey(m_clip, keyRef.m_track, frame, target);
        }

        keyRef.m_frame = target;
        moved++;
      }

      // The group has to be closed even when one action landed, otherwise the grouping flag stays set
      // and the next unrelated edit of the editor would be folded into it. moved counts only the moves
      // that really pushed an action, so > 0 is the right test.
      if (groupEdits && moved > 0)
      {
        ActionManager::GetInstance()->GroupLastActions(moved);
      }

      // The moved keys stay selected, on the frames they landed on, so the group can be dragged again.
      m_selection.clear();
      for (const KeyRef& keyRef : movedKeys)
      {
        m_selection.push_back(keyRef);
      }

      if (moved == 0)
      {
        TK_WRN("Dope sheet: none of the %d selected key(s) could be moved by %d frame(s).",
               (int) movedKeys.size(),
               delta);
      }

      if (moved > 0)
      {
        // The track of the anchor key names the row the drag was started on, which is what makes a
        // bulk move of keys from several rows readable in the log.
        const String anchorTrack = movedKeys[anchor].m_track;

        TK_LOG("Dope sheet: %d key%s moved by %d frame%s in one undo step, starting on track %s.",
               moved,
               moved == 1 ? "" : "s",
               delta,
               delta == 1 || delta == -1 ? "" : "s",
               anchorTrack.c_str());
      }
    }

    void DopeSheetView::HandleSelectionShortcuts()
    {
      // The shortcuts are not driven through the lane buttons: a hidden button takes mouse ownership,
      // not the keyboard, so the key set is read directly instead. A text field is left alone, it
      // needs Escape and Ctrl + A for itself.
      ImGuiIO& io = ImGui::GetIO();

      if (m_clip == nullptr || io.WantTextInput)
      {
        return;
      }

      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
      {
        // Escape first cancels a drag that is running, which is what the sheet has always done with
        // it, and only then clears the selection.
        if (m_dragging)
        {
          m_dragging = false;
          m_dragFromFrames.clear();
        }
        else
        {
          ClearSelection();
        }
      }

      // Ctrl + A is read only while the sheet has the focus, so it cannot select all keys while the
      // viewport is the window the animator is working in.
      if (ImGui::IsKeyPressed(ImGuiKey_A, false) && io.KeyCtrl &&
          ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
      {
        SelectAllKeys();
      }
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
                     "Interpolation mode written with every new key, Smooth by default. Smooth blends "
                     "through the key, Flat eases into it, Stepped holds the value until the next key "
                     "and Linear leaves the curve as it is. An existing key is changed from the row "
                     "context menu.");

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

      // The sheet area is either the key lanes or a read only plot of the curves. The button names
      // the view it switches to, so what is on screen is never in question.
      ImGui::SameLine();
      if (ImGui::Button(m_curveView ? "Dope Sheet" : "Curves"))
      {
        m_curveView = !m_curveView;
      }
      UI::HelpMarker("DopeSheetCurveView",
                     "Alternates the sheet area between the key lanes and a read only plot of the "
                     "translation, rotation and scale curves of one track. The pose is sampled "
                     "through the engine's own interpolation, so a Stepped, Smooth or Flat key shows "
                     "in the shape of the curve. The plotted track is picked in the name column; the "
                     "curves cannot be edited here.");

      // What the clip currently addresses in the scene. Tracks that resolve to nothing are listed in
      // the sheet, they just never drive anything. Parameter tracks count when the id still names a
      // parameter of an entity in the scene.
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

        for (const auto& track : m_clip->m_paramKeys)
        {
          EntityPtr ntt = EntityForParamTrack(track.first);
          if (ntt != nullptr && m_clip->ResolveParamTrack(ntt, track.first) != nullptr)
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

      ImGui::SameLine();
      ImGui::TextDisabled("|  %d selected", (int) m_selection.size());

      UI::HelpMarker("DopeSheetKeySelection",
                     "Click a key to select it, Shift + click to add or remove one, Shift + drag in the "
                     "lane area to box select a range of rows and frames and add it to the selection. "
                     "Ctrl + drag is the same box, but it replaces the selection instead of adding to it. "
                     "Drag any selected key to move the whole selection in time, Delete removes every "
                     "selected key as one undo step and Ctrl + A selects all keys of the clip. Escape "
                     "drops the selection.");
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
        if (m_curveView)
        {
          ShowCurves(laneLeft, laneW);
        }
        else
        {
          ShowLanes(laneLeft, laneW);
        }
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

      // Rows: the transform tracks first, then the parameter tracks of the clip.
      const int transformRows = (int) m_clip->m_keys.size();
      const int paramRows     = (int) m_clip->m_paramKeys.size();
      const int rowCount      = transformRows + paramRows;
      const float contentH    = rowCount * g_rowHeight;
      const float maxScrollY  = glm::max(0.0f, contentH - viewHeight);

      auto rowIsParam = [transformRows](int row) -> bool { return row >= transformRows; };

      auto rowName = [this, transformRows](int row) -> const String&
      {
        if (row < transformRows)
        {
          return m_clip->m_keys[row].first;
        }

        return m_clip->m_paramKeys[row - transformRows].first;
      };

      // Plain wheel scrolls the rows, shift pans the timeline, ctrl zooms around the cursor. The
      // timeline half is shared with the curve view, the row scroll only exists here.
      if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MouseWheel != 0.0f && !io.KeyCtrl &&
          !io.KeyShift)
      {
        m_scrollY -= io.MouseWheel * g_rowHeight * g_scrollWheelRows;
      }

      HandleTimelineWheel(laneLeft);

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

      // Only one row may consume the press, drag and release of the gesture that is running. The rows
      // are drawn in one frame and each of them has its own hit area, so the two flags below are what
      // keeps a rubber band and a key drag apart.
      bool claimGesture = false;
      bool pressHitKey  = false;

      for (int row = firstRow; row < lastRow; row++)
      {
        const String& trackName = rowName(row);
        const bool paramRow     = rowIsParam(row);

        const KeyArray* keys        = paramRow ? nullptr : m_clip->m_keys.Find(trackName);
        const ParamKeyArray* pKeys  = paramRow ? m_clip->m_paramKeys.Find(trackName) : nullptr;
        const int keyCount          = paramRow ? (int) pKeys->size() : (int) keys->size();

        const float rowY = origin.y + row * g_rowHeight - m_scrollY;

        ImGui::PushID(row);

        // One hit area per row: it scrubs the playhead, selects a key and carries a key drag.
        ImGui::SetCursorScreenPos(ImVec2(origin.x, rowY));
        ImGui::InvisibleButton("##dopeSheetLane", ImVec2(avail.x, g_rowHeight));

        const bool rowHovered = ImGui::IsItemHovered();
        const bool rowActive  = ImGui::IsItemActive();

        // Only the active row, the one the press landed on, may steer the gesture that is running.
        // The rows are drawn in one frame and each of them reports the press, so the row that takes it
        // remembers that it did.
        const bool rowClaimedPress = rowActive && claimGesture;

        // A parameter track addresses an entity, component or material slot instead of a node.
        const EntityPtr ntt      = paramRow ? EntityForParamTrack(trackName) : EntityForTrack(trackName);
        const bool skinned       = paramRow ? false : IsSkinned(ntt);
        const bool resolvedParam = paramRow ? (ntt != nullptr && m_clip->ResolveParamTrack(ntt, trackName) != nullptr)
                                            : (ntt != nullptr);
        bool openCtx             = false;

        ImU32 background    = (row % 2 == 0) ? rowEven : rowOdd;
        if (rowHovered)
        {
          background = rowHover;
        }

        dl->AddRectFilled(ImVec2(laneLeft, rowY),
                          ImVec2(origin.x + avail.x, rowY + g_rowHeight),
                          background);

        // The first parameter row is separated from the transform rows above it.
        if (paramRow && row == transformRows && row > firstRow)
        {
          dl->AddLine(ImVec2(laneLeft, rowY), ImVec2(origin.x + avail.x, rowY), columnLine);
        }

        // Keys.
        const float keyY           = rowY + g_rowHeight * 0.5f;
        int hoveredKeyFrame        = -1;
        KeyInterp hoveredKeyInterp = KeyInterp::Linear;
        const ImU32 holdColor      = ImGui::GetColorU32(ImGuiCol_Text, 0.35f);

        // Both key kinds draw the same marker, they only differ in where the frame and the mode
        // come from: a transform key carries its own interpolation, a parameter key has no mode of
        // its own (its type decides how it blends) and draws as a plain diamond.
        auto drawKey = [&](int keyFrame, KeyInterp interp) -> void
        {
          const float keyX = FrameToX(keyFrame, laneLeft);

          if (keyX < laneLeft - g_keyRadius || keyX > laneLeft + laneWidth + g_keyRadius)
          {
            return;
          }

          if (rowHovered && glm::abs(io.MousePos.x - keyX) <= g_keyRadius &&
              glm::abs(io.MousePos.y - keyY) <= g_keyRadius)
          {
            hoveredKeyFrame  = keyFrame;
            hoveredKeyInterp = interp;
          }

          // Every key of the group is dragged away at once, so each of them is drawn as a ghost on the
          // frame it would land on, below.
          if (m_dragging && IsKeySelected(trackName, keyFrame, paramRow))
          {
            DrawKeyMarker(dl, ImVec2(keyX, keyY), g_keyRadius, interp, cursorColor, false);
            return;
          }

          const bool selected = IsKeySelected(trackName, keyFrame, paramRow);
          const bool current  = keyFrame == m_frame;

          DrawKeyMarker(dl,
                        ImVec2(keyX, keyY),
                        g_keyRadius,
                        interp,
                        (selected || current) ? cursorColor : keyColor,
                        true);

          if (selected)
          {
            DrawKeyMarker(dl,
                          ImVec2(keyX, keyY),
                          g_keyRadius,
                          interp,
                          ImGui::GetColorU32(ImGuiCol_Text),
                          false);
          }
        };

        if (paramRow)
        {
          for (const ParamKey& key : *pKeys)
          {
            drawKey(key.m_frame, KeyInterp::Smooth);
          }
        }
        else
        {
          for (size_t keyIndex = 0; keyIndex < keys->size(); keyIndex++)
          {
            const Key& key = (*keys)[keyIndex];

            // A held segment draws a bar to the next key, the classic "this does not move until
            // here" read. Which segment is held is the engine's rule, not a guess: either endpoint
            // being Stepped holds it, so the sheet cannot disagree with playback.
            if (keyIndex + 1 < keys->size() &&
                Interpolation::ResolveSegment(key.m_interp, (*keys)[keyIndex + 1].m_interp) == SegmentKind::Hold)
            {
              const float holdX = FrameToX((*keys)[keyIndex + 1].m_frame, laneLeft);
              dl->AddLine(ImVec2(FrameToX(key.m_frame, laneLeft), keyY),
                          ImVec2(glm::min(holdX, laneLeft + laneWidth), keyY),
                          holdColor,
                          2.0f);
            }

            drawKey(key.m_frame, key.m_interp);
          }
        }

        // Ghosts of the dragged keys, drawn on the frames they would land on. Every member of the
        // group gets one, on its own row, so a group move reads as a group.
        if (m_dragging && m_dragToFrame >= 0 && !m_dragFromFrames.empty())
        {
          const int delta = DragDelta(m_dragFromFrames);
          int ghostFrame  = -1;

          for (size_t indx = 0; indx < m_selection.size() && indx < m_dragFromFrames.size(); indx++)
          {
            const KeyRef& keyRef = m_selection[indx];
            if (keyRef.m_track != trackName || keyRef.m_param != paramRow)
            {
              continue;
            }

            ghostFrame = m_dragFromFrames[indx] + delta;

            const float ghostX = FrameToX(ghostFrame, laneLeft);
            const KeyInterp ghostInterp = indx == m_dragAnchor ? m_dragInterp : KeyInterp::Smooth;

            DrawKeyMarker(dl, ImVec2(ghostX, keyY), g_keyRadius, ghostInterp, cursorColor, true);
            DrawKeyMarker(dl,
                          ImVec2(ghostX, keyY),
                          g_keyRadius,
                          ghostInterp,
                          ImGui::GetColorU32(ImGuiCol_Text),
                          false);
          }

          // One frame label per row, next to the last ghost of the row: a label on every ghost would
          // sit on top of its neighbours once the group is dense.
          if (ghostFrame >= 0)
          {
            const String frameLabel = Format("%d", ghostFrame);
            dl->AddText(ImVec2(FrameToX(ghostFrame, laneLeft) + g_keyRadius + g_rubberBandPadding, rowY + 2.0f),
                        ImGui::GetColorU32(ImGuiCol_Text),
                        frameLabel.c_str());
          }
        }

        // Name column.
        const ImU32 nameColor = (ntt == nullptr) ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                                                 : ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddText(ImVec2(origin.x + 6.0f, rowY + (g_rowHeight - lineHeight) * 0.5f), nameColor, trackName.c_str());

        String info = Format("%d key%s", keyCount, keyCount == 1 ? "" : "s");
        if (paramRow)
        {
          // Param rows report the value the track holds at the playhead, and whether the id still
          // addresses something in the scene.
          Vec4 value;
          ParameterVariant::VariantType type;
          if (m_clip->GetParamValue(trackName, CurrentTime(), value, type))
          {
            info += "  = " + FormatParamValue(type, value);
          }

          if (!resolvedParam)
          {
            info += "  [no owner]";
          }
        }
        else if (skinned)
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
        // drag, and a key the running drag carries has left the row it is drawn on. Only a transform
        // key has a mode to report, a parameter key blends by its type.
        const bool keyMovingAway = m_dragging && IsKeySelected(trackName, hoveredKeyFrame, paramRow);
        if (hoveredKeyFrame >= 0 && !rowActive && !keyMovingAway)
        {
          if (paramRow)
          {
            ImGui::SetTooltip("%s\nframe %d  (%.3f s)",
                              trackName.c_str(),
                              hoveredKeyFrame,
                              hoveredKeyFrame / glm::max(1.0f, m_clip->m_fps));
          }
          else
          {
            ImGui::SetTooltip("%s\nframe %d  (%.3f s)\n%s",
                              trackName.c_str(),
                              hoveredKeyFrame,
                              hoveredKeyFrame / glm::max(1.0f, m_clip->m_fps),
                              InterpLabel(hoveredKeyInterp));
          }
        }

        if (rowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
          m_ctxTrack = trackName;
          m_ctxFrame = hoveredKeyFrame;
          m_ctxParam = paramRow;
          openCtx    = true;
        }

        // Left click on a key selects it, ctrl toggles it in the set, and a plain drag on a selected
        // key moves the whole selection in time. The press is not a click yet, so the drag only starts
        // following the mouse once it passes the drag threshold; a press that never moves collapses a
        // group to the key under the cursor when it is released.
        const bool pressClaimable = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !claimGesture;

        if (rowHovered && hoveredKeyFrame >= 0 && pressClaimable)
        {
          claimGesture = true;
          pressHitKey  = true;

          if (io.KeyShift)
          {
            // Shift + click toggles the key and never selects a group away, and it leaves the lane free
            // for the rubber band the same press may start.
            ToggleSelection(trackName, hoveredKeyFrame, paramRow);
          }
          else
          {
            if (!IsKeySelected(trackName, hoveredKeyFrame, paramRow))
            {
              SelectOnly(trackName, hoveredKeyFrame, paramRow);
            }

            // The grabbed key is the anchor: its frame drives the delta and its mode shapes the ghost.
            // The frames of the whole selection are snapshotted too, because the move is applied key by
            // key on release and the frame a key leaves from must not be read after an earlier move of
            // the same drag already changed the track.
            m_dragAnchor = 0;
            m_dragFromFrames.clear();
            m_dragFromFrames.reserve(m_selection.size());

            for (size_t i = 0; i < m_selection.size(); i++)
            {
              m_dragFromFrames.push_back(m_selection[i].m_frame);

              if (m_selection[i].m_track == trackName && m_selection[i].m_frame == hoveredKeyFrame &&
                  m_selection[i].m_param == paramRow)
              {
                m_dragAnchor = i;
              }
            }

            m_dragFromFrame = hoveredKeyFrame;
            m_dragToFrame   = hoveredKeyFrame;
            m_dragInterp    = hoveredKeyInterp;
            m_dragging      = true;
          }
        }
        else if (rowActive && pressClaimable && !m_dragging && !m_boxSelecting)
        {
          // Empty lane space: the selection goes and the press scrubs the playhead, unless ctrl turns
          // it into a rubber band below.
          claimGesture = true;
          ClearSelection();
        }

        // The row that took the press keeps following the mouse, even when it leaves the row. The
        // condition is the active row rather than the row the drag started on, because the start row
        // can scroll out of view, and then no row would be left to advance the ghost.
        if (m_dragging && rowActive)
        {
          m_dragToFrame = glm::clamp(XToFrame(io.MousePos.x, laneLeft), 0, m_endFrame);
        }

        // Plain drag on empty lane space scrubs. A rubber band swallows the mouse, so the playhead
        // stays where the drag started.
        if (rowActive && rowClaimedPress && !m_dragging && !m_boxSelecting)
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

      // Rubber band. Shift + drag in the lane area adds every key the box covers to the selection, and
      // Ctrl + drag is the same box that starts over instead. It cuts across rows, which the per row hit
      // areas cannot reach, and it is the only gesture that may run without a row of its own, so the
      // rows only report the press here.
      const ImVec2 laneMin(laneLeft, origin.y);
      const ImVec2 laneMax(laneLeft + laneWidth, origin.y + viewHeight);

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !pressHitKey && (io.KeyShift || io.KeyCtrl) &&
          ImGui::IsMouseHoveringRect(laneMin, laneMax))
      {
        m_boxSelecting = true;
        m_boxAdditive  = io.KeyShift;
        m_boxStart     = io.MousePos;
        // The rows are matched against the scroll the box was started with, so panning the rows while
        // the box is open does not slide the selection under it.
        m_boxScrollY   = m_scrollY;
      }

      if (m_boxSelecting)
      {
        const ImVec2 boxMin(glm::min(m_boxStart.x, io.MousePos.x), glm::min(m_boxStart.y, io.MousePos.y));
        const ImVec2 boxMax(glm::max(m_boxStart.x, io.MousePos.x), glm::max(m_boxStart.y, io.MousePos.y));

        dl->AddRectFilled(boxMin, boxMax, ImGui::GetColorU32(g_rubberBandFill));
        dl->AddRect(boxMin, boxMax, cursorColor, 0.0f, 0, g_rubberBandBorder);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
          // A box that never moved is a shift + click, which has already toggled its key.
          const bool movedBox = glm::abs(boxMax.x - boxMin.x) > FLT_EPSILON ||
                                glm::abs(boxMax.y - boxMin.y) > FLT_EPSILON;

          m_boxSelecting = false;

          if (movedBox)
          {
            if (!m_boxAdditive)
            {
              m_selection.clear();
            }

            const float framePad = g_keyRadius / glm::max(g_minPxPerFrame, m_pxPerFrame);

            for (int row = 0; row < rowCount; row++)
            {
              const String& trackName = rowName(row);
              const bool paramRow     = rowIsParam(row);

              // The row is matched against the frozen scroll, so the box and the row layout agree
              // even when the rows were scrolled while the box was open.
              const float rowTop = origin.y + row * g_rowHeight - m_boxScrollY;

              if (rowTop > boxMax.y || rowTop + g_rowHeight < boxMin.y)
              {
                continue;
              }

              // The horizontal test runs on frames instead of pixels, so a box edge that only touches
              // the tip of a diamond still takes it, the same slack the key hover uses.
              const float lowFrame  = (float) XToFrame(boxMin.x, laneLeft) - framePad;
              const float highFrame = (float) XToFrame(boxMax.x, laneLeft) + framePad;

              auto addKey = [&](int frame) -> void
              {
                const bool inside      = frame >= lowFrame && frame <= highFrame;
                const bool alreadyInIt = IsKeySelected(trackName, frame, paramRow);

                if (!inside || alreadyInIt)
                {
                  return;
                }

                KeyRef keyRef;
                keyRef.m_track = trackName;
                keyRef.m_frame = frame;
                keyRef.m_param = paramRow;
                m_selection.push_back(keyRef);
              };

              if (paramRow)
              {
                const ParamKeyArray* pKeys = m_clip->m_paramKeys.Find(trackName);
                if (pKeys != nullptr)
                {
                  for (const ParamKey& key : *pKeys)
                  {
                    addKey(key.m_frame);
                  }
                }
              }
              else
              {
                const KeyArray* keys = m_clip->m_keys.Find(trackName);
                if (keys != nullptr)
                {
                  for (const Key& key : *keys)
                  {
                    addKey(key.m_frame);
                  }
                }
              }
            }

            TK_LOG("Dope sheet: rubber band selected %d key%s.", (int) m_selection.size(),
                   m_selection.size() == 1 ? "" : "s");
          }
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

        const int ctxFrame   = m_ctxFrame;
        const bool ctxIsParam = m_ctxParam;

        ImGui::BeginDisabled(!CanEdit());
        if (ImGui::MenuItem("Delete Key", nullptr, false, ctxFrame >= 0))
        {
          ClearSelection();

          if (ctxIsParam)
          {
            KeyEditAction::DeleteParamKey(m_clip, m_ctxTrack, ctxFrame);
          }
          else
          {
            KeyEditAction::DeleteKey(m_clip, m_ctxTrack, ctxFrame);
          }
        }
        ImGui::EndDisabled();

        // Copy takes the key under the cursor. Paste drops the clipboard key at the playhead on the
        // row under the cursor, so a key can be sent to another entity without selecting anything.
        ImGui::BeginDisabled(!CanEdit());
        if (ImGui::MenuItem("Copy Key", nullptr, false, ctxFrame >= 0))
        {
          CopyKey(m_ctxTrack, ctxFrame, ctxIsParam);
        }

        if (ImGui::MenuItem("Paste Key", nullptr, false, m_keyClipboard.m_valid))
        {
          PasteKeyOn(m_ctxTrack, ctxIsParam);
        }
        ImGui::EndDisabled();

        // Interpolation of the key under the cursor. The menu stays open while the sheet is only
        // previewing, so the mode can still be read; the modes themselves are disabled then. A
        // parameter key carries no mode, its type decides how it blends, so the menu stays away.
        if (!ctxIsParam && ctxFrame >= 0 && ImGui::BeginMenu("Interpolation"))
        {
          const KeyInterp current = KeyInterpAt(m_ctxTrack, ctxFrame);

          ImGui::BeginDisabled(!CanEdit());
          for (KeyInterp interp : g_keyInterps)
          {
            if (ImGui::MenuItem(InterpLabel(interp), nullptr, interp == current))
            {
              SelectOnly(m_ctxTrack, ctxFrame, false);
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

          const bool removed = ctxIsParam ? m_clip->m_paramKeys.Erase(m_ctxTrack)
                                          : m_clip->m_keys.Erase(m_ctxTrack);
          if (removed)
          {
            m_clip->m_dirty = true;
            ClearSelection();
            ResolveTracks();
          }
        }
        ImGui::EndDisabled();

        ImGui::EndPopup();
      }
    }

    // Curve view
    //////////////////////////////////////////

    String DopeSheetView::CurveTrackName() const
    {
      if (m_clip == nullptr || m_clip->m_keys.empty())
      {
        return "";
      }

      // The picked track wins, then the track of the primary selected key when it is a transform
      // track, and the first track of the clip otherwise.
      if (m_clip->m_keys.Find(m_curveTrack) != nullptr)
      {
        return m_curveTrack;
      }

      const KeyRef* selected = PrimarySelection();

      if (selected != nullptr && !selected->m_param && m_clip->m_keys.Find(selected->m_track) != nullptr)
      {
        return selected->m_track;
      }

      return m_clip->m_keys[0].first;
    }

    void DopeSheetView::HandleTimelineWheel(float laneLeft)
    {
      ImGuiIO& io = ImGui::GetIO();
      if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) || io.MouseWheel == 0.0f)
      {
        return;
      }

      if (io.KeyCtrl)
      {
        const float anchorFrame = (io.MousePos.x - laneLeft + m_scrollX) / m_pxPerFrame;
        const float zoom        = io.MouseWheel > 0.0f ? g_zoomStep : 1.0f / g_zoomStep;

        m_pxPerFrame = glm::clamp(m_pxPerFrame * zoom, g_minPxPerFrame, g_maxPxPerFrame);
        m_scrollX    = anchorFrame * m_pxPerFrame - (io.MousePos.x - laneLeft);
      }
      else if (io.KeyShift)
      {
        m_scrollX -= io.MouseWheel * m_pxPerFrame * g_panWheelFrames;
      }
    }

    void DopeSheetView::ShowCurves(float laneLeft, float laneWidth)
    {
      ImDrawList* dl         = ImGui::GetWindowDrawList();
      ImGuiIO& io            = ImGui::GetIO();
      const ImVec2 origin    = ImGui::GetCursorScreenPos();
      const ImVec2 avail     = ImGui::GetContentRegionAvail();
      const float viewHeight = avail.y;
      const float bandRight  = laneLeft + laneWidth;

      HandleTimelineWheel(laneLeft);
      ClampScroll(laneWidth);

      // The two columns are clipped apart. A panned curve belongs to the lane area, it must not draw
      // over the name column, and the ruler keeps its ticks out of the column the same way, by
      // skipping the ones that land left of it.
      ImGui::PushClipRect(origin, ImVec2(laneLeft, origin.y + viewHeight), true);

      const ImU32 trackColumnTint = ImGui::GetColorU32(ImGuiCol_Text, g_trackColumnTint);
      const ImU32 columnLine      = ImGui::GetColorU32(ImGuiCol_Text, g_columnLineTint);
      const ImU32 bandFill        = ImGui::GetColorU32(ImGuiCol_FrameBg, 0.18f);
      const ImU32 bandEdge        = ImGui::GetColorU32(ImGuiCol_Text, 0.10f);
      const ImU32 zeroLine        = ImGui::GetColorU32(ImGuiCol_Text, 0.22f);
      const ImU32 trackPick       = ImGui::GetColorU32(ImGuiCol_Header, 0.55f);
      const ImU32 labelColor      = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const ImU32 cursorColor     = ImGui::GetColorU32(ImVec4(g_selectHighLightPrimaryColor));
      const ImU32 outOfRange      = ImGui::GetColorU32(g_outOfRangeVeil);

      dl->AddRectFilled(origin, ImVec2(laneLeft, origin.y + viewHeight), trackColumnTint);

      // The name column lists the transform tracks, the curve view plots one of them at a time, so
      // the column doubles as the picker.
      String plotted       = CurveTrackName();
      const int trackCount = (int) m_clip->m_keys.size();

      for (int row = 0; row < trackCount; row++)
      {
        const String& trackName = m_clip->m_keys[row].first;
        const float rowY        = origin.y + row * g_rowHeight;

        if (rowY > origin.y + viewHeight)
        {
          break;
        }

        ImGui::PushID(row);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, rowY));
        ImGui::InvisibleButton("##dopeSheetCurveTrack", ImVec2(m_nameColumnWidth, g_rowHeight));
        const bool trackHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
        {
          m_curveTrack = trackName;
          plotted      = trackName;
        }
        ImGui::PopID();

        const bool isPlotted = trackName == plotted;
        if (isPlotted)
        {
          dl->AddRectFilled(ImVec2(origin.x, rowY),
                            ImVec2(origin.x + m_nameColumnWidth, rowY + g_rowHeight),
                            trackPick);
        }
        else if (trackHovered)
        {
          dl->AddRectFilled(ImVec2(origin.x, rowY),
                            ImVec2(origin.x + m_nameColumnWidth, rowY + g_rowHeight),
                            ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.35f));
        }

        dl->AddText(ImVec2(origin.x + g_curveLabelInset,
                           rowY + (g_rowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                    isPlotted ? ImGui::GetColorU32(ImGuiCol_Text) : labelColor,
                    trackName.c_str());
      }

      ImGui::PopClipRect();

      // The divider sits on the boundary of the two clips, so it is drawn once the column clip is
      // gone and reads on top of both.
      dl->AddLine(ImVec2(laneLeft, origin.y), ImVec2(laneLeft, origin.y + viewHeight), columnLine);

      // Scrub area: the curve view has no keys to pick, so the lane area only moves the playhead.
      ImGui::SetCursorScreenPos(ImVec2(laneLeft, origin.y));
      ImGui::InvisibleButton("##dopeSheetCurveScrub", ImVec2(laneWidth, viewHeight));

      const bool areaHovered = ImGui::IsItemHovered();
      const bool areaActive  = ImGui::IsItemActive();
      if (areaActive)
      {
        SetFrame(XToFrame(io.MousePos.x, laneLeft), true);
      }

      const KeyArray* keys = m_clip->m_keys.Find(plotted);
      if (keys == nullptr || keys->size() < 2)
      {
        ImGui::PushClipRect(ImVec2(laneLeft, origin.y), ImVec2(bandRight, origin.y + viewHeight), true);
        dl->AddText(ImVec2(laneLeft + g_curveLabelInset, origin.y + g_curveLabelInset),
                    labelColor,
                    "Two keys are needed to plot a curve.");
        ImGui::PopClipRect();
        return;
      }

      // The keys are ascending, so the first and the last one bound the plot.
      const int firstFrame  = keys->front().m_frame;
      const int lastFrame   = keys->back().m_frame;
      const int sampleCount = lastFrame - firstFrame + 1;
      const float fps       = glm::max(1.0f, m_clip->m_fps);

      // Sampling the interpolated pose is exactly what the preview plays, so the plot shows what the
      // key modes do: Stepped holds the value, Smooth blends through the key, Flat eases into it.
      std::vector<Vec3> samples[g_curveBandCount];
      for (std::vector<Vec3>& channel : samples)
      {
        channel.resize(sampleCount);
      }

      for (int i = 0; i < sampleCount; i++)
      {
        const int frame = firstFrame + i;

        Vec3 pos(0.0f);
        Vec3 scl(1.0f);
        Quaternion rot(1.0f, 0.0f, 0.0f, 0.0f);
        SampleTrack(*keys, frame / fps, pos, rot, scl);

        samples[0][i] = pos;
        // Euler degrees: a rotation curve has to be readable and a quaternion component is not. The
        // readout is kept continuous along the track, see ContinuousEuler.
        samples[1][i] = ContinuousEuler(rot, i > 0 ? &samples[1][i - 1] : nullptr);
        samples[2][i] = scl;
      }

      const char* bandNames[g_curveBandCount] = {"Translation", "Rotation (deg)", "Scale"};
      const char* axisNames[3]                = {"x", "y", "z"};

      const float bandHeight =
          glm::max(20.0f, (viewHeight - (g_curveBandCount - 1) * g_curveBandGap) / g_curveBandCount);

      // The plot lives in the lane area only: a curve panned past the left edge stops at the name
      // column instead of running over the track names.
      ImGui::PushClipRect(ImVec2(laneLeft, origin.y), ImVec2(bandRight, origin.y + viewHeight), true);

      for (int band = 0; band < g_curveBandCount; band++)
      {
        const float bandTop    = origin.y + band * (bandHeight + g_curveBandGap);
        const float bandBottom = bandTop + bandHeight;

        // One value range per channel group: translation and scale are lengths, rotation is an angle,
        // so the groups do not share a scale. The three axes share the range inside a group, which is
        // what makes x / y / z comparable.
        Vec3 rangeMin = samples[band][0];
        Vec3 rangeMax = samples[band][0];
        for (const Vec3& sample : samples[band])
        {
          rangeMin = glm::min(rangeMin, sample);
          rangeMax = glm::max(rangeMax, sample);
        }

        float low  = glm::min(glm::min(rangeMin.x, rangeMin.y), rangeMin.z);
        float high = glm::max(glm::max(rangeMax.x, rangeMax.y), rangeMax.z);

        // A flat curve still gets a band to sit in, and the range is padded so a curve never touches
        // the edge of its band.
        if (high - low < g_curveMinRange)
        {
          const float mid = (high + low) * 0.5f;
          low             = mid - g_curveMinRange * 0.5f;
          high            = mid + g_curveMinRange * 0.5f;
        }

        const float pad = (high - low) * g_curveRangePad;
        low -= pad;
        high += pad;

        const float range = high - low;
        auto valueToY = [&](float value) -> float { return bandBottom - (value - low) / range * bandHeight; };

        dl->AddRectFilled(ImVec2(laneLeft, bandTop), ImVec2(bandRight, bandBottom), bandFill);
        dl->AddLine(ImVec2(laneLeft, bandTop), ImVec2(bandRight, bandTop), bandEdge);
        dl->AddLine(ImVec2(laneLeft, bandBottom), ImVec2(bandRight, bandBottom), bandEdge);

        if (low < 0.0f && high > 0.0f)
        {
          const float zeroY = valueToY(0.0f);
          dl->AddLine(ImVec2(laneLeft, zeroY), ImVec2(bandRight, zeroY), zeroLine);
        }

        for (int axis = 0; axis < 3; axis++)
        {
          std::vector<ImVec2> points;
          points.reserve(sampleCount);

          for (int i = 0; i < sampleCount; i++)
          {
            points.push_back(ImVec2(FrameToX(firstFrame + i, laneLeft), valueToY(samples[band][i][axis])));
          }

          dl->AddPolyline(points.data(), (int) points.size(), g_curveAxisColors[axis], 0, g_curveThickness);
        }

        const String rangeLabel = Format("%.3g .. %.3g", low, high);
        dl->AddText(ImVec2(laneLeft + g_curveLabelInset, bandTop + 2.0f), labelColor, bandNames[band]);

        const float rangeLabelWidth = ImGui::CalcTextSize(rangeLabel.c_str()).x;
        dl->AddText(ImVec2(bandRight - g_curveLabelInset - rangeLabelWidth, bandTop + 2.0f),
                    labelColor,
                    rangeLabel.c_str());

        // Axis legend, right aligned on the bottom edge of the band.
        float legendX = bandRight - g_curveLabelInset;
        for (int axis = 2; axis >= 0; axis--)
        {
          legendX -= ImGui::CalcTextSize(axisNames[axis]).x;
          dl->AddText(ImVec2(legendX, bandBottom - ImGui::GetTextLineHeight() - 2.0f),
                      g_curveAxisColors[axis],
                      axisNames[axis]);
          legendX -= g_curveLabelInset;
        }
      }

      // Frame grid and playhead, the same the lanes draw, so both views read the same.
      const float endX      = FrameToX(m_endFrame, laneLeft);
      const float playheadX = FrameToX(m_frame, laneLeft);

      if (endX < bandRight)
      {
        dl->AddRectFilled(ImVec2(glm::max(endX, laneLeft), origin.y),
                          ImVec2(bandRight, origin.y + viewHeight),
                          outOfRange);
      }

      if (endX > laneLeft && endX < bandRight)
      {
        dl->AddLine(ImVec2(endX, origin.y), ImVec2(endX, origin.y + viewHeight), cursorColor, 1.0f);
      }

      if (playheadX >= laneLeft && playheadX <= bandRight)
      {
        dl->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, origin.y + viewHeight), cursorColor, 1.0f);
      }

      ImGui::PopClipRect();

      // Hover readout: the sampled pose on the frame under the cursor, the curve form of the lane
      // tooltip.
      if (areaHovered && !areaActive)
      {
        const int frame = XToFrame(io.MousePos.x, laneLeft);
        if (frame >= firstFrame && frame <= lastFrame)
        {
          const int i = frame - firstFrame;
          ImGui::SetTooltip("frame %d\nT  %.3f  %.3f  %.3f\nR  %.1f  %.1f  %.1f\nS  %.3f  %.3f  %.3f",
                            frame,
                            samples[0][i].x,
                            samples[0][i].y,
                            samples[0][i].z,
                            samples[1][i].x,
                            samples[1][i].y,
                            samples[1][i].z,
                            samples[2][i].x,
                            samples[2][i].y,
                            samples[2][i].z);
        }
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

    DopeSheetView::ParamKeyState DopeSheetWindow::GetParamKeyState(const String& trackId)
    {
      if (m_view == nullptr)
      {
        return DopeSheetView::ParamKeyState::NoClip;
      }

      return m_view->GetParamKeyState(trackId);
    }

    bool DopeSheetWindow::SetParamKey(const String& trackId)
    {
      return m_view != nullptr && m_view->SetParamKey(trackId);
    }

    void DopeSheetWindow::DeleteParamKey(const String& trackId, int frame)
    {
      if (m_view != nullptr)
      {
        m_view->DeleteParamKey(trackId, frame);
      }
    }

    int DopeSheetWindow::GetFrame()
    {
      return m_view != nullptr ? m_view->GetFrame() : 0;
    }

    void DopeSheetWindow::Show()
    {
      ImGuiIO& io           = ImGui::GetIO();
      ImGui::SetNextWindowSize(Vec2(900.0f, 420.0f), ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowPos(Vec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver,
                              Vec2(0.5f, 0.5f));

      // The window name is what the layout file matches a window by, so it stays the plain name the
      // menu and App::GetDopeSheet() address the window with. A name carrying a generated id made
      // every session a first use: the size and the position above were applied again and the
      // window never came back where it was docked.
      if (ImGui::Begin(m_name.c_str(), &m_visible))
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

      // Copy and paste of the selected key. The sheet does not call ModShortCutSignals, so Ctrl+C and
      // Ctrl+V are free here and are not read as the transform mode shortcuts.
      if (ImGui::IsKeyPressed(ImGuiKey_C, false) && ImGui::IsKeyDown(ImGuiMod_Ctrl))
      {
        m_view->CopySelectedKey();
      }

      if (ImGui::IsKeyPressed(ImGuiKey_V, false) && ImGui::IsKeyDown(ImGuiMod_Ctrl))
      {
        m_view->PasteKey();
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

    XmlNode* DopeSheetWindow::SerializeImp(XmlDocument* doc, XmlNode* parent) const
    {
      XmlNode* wndNode = Window::SerializeImp(doc, parent);
      XmlNode* sheet   = CreateXmlNode(doc, "DopeSheetWindow", wndNode);

      // The clip the sheet edits, so the window opens on the animation it was left on. It is stored
      // relative to the resource root, the same way the editor stores the scene it was left on.
      if (m_view != nullptr && m_view->m_clip != nullptr)
      {
        const String clipFile = m_view->m_clip->GetFile();
        if (!clipFile.empty())
        {
          // GetRelativeResourcePath returns a different string on success: a clip that lives
          // outside the resource roots has no relative form and is not restored.
          String clipPath = GetRelativeResourcePath(clipFile);
          if (clipPath != clipFile)
          {
            WriteAttr(sheet, doc, "clip", clipPath);
          }
        }
      }

      return sheet;
    }

    XmlNode* DopeSheetWindow::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
    {
      // The settings of this window hang under the <Window> node that Window::DeSerializeImp
      // returns, while the node the caller passes in is the <Object> element wrapping it.
      XmlNode* wndNode = Window::DeSerializeImp(info, parent);
      XmlNode* sheet   = wndNode != nullptr ? wndNode->first_node("DopeSheetWindow") : nullptr;

      if (sheet != nullptr)
      {
        String clip;
        ReadAttr(sheet, "clip", clip);

        if (!clip.empty())
        {
          const String clipFile = AnimationPath(clip);
          if (CheckSystemFile(clipFile))
          {
            SetAnimation(GetAnimationManager()->Create<Animation>(clipFile));
          }
          else
          {
            TK_WRN("Dope sheet: the clip it was left on is missing (%s).", clip.c_str());
          }
        }
      }

      return nullptr;
    }

  } // namespace Editor
} // namespace ToolKit
