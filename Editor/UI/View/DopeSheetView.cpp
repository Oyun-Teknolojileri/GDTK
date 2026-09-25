/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "DopeSheetView.h"

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

      ShowClipHeader();
      ShowTransport();
      ShowKeyTools();

      ImGui::Separator();
      ShowSheet();
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
      if (m_clip == nullptr || keys.empty())
      {
        return false;
      }

      int key1    = -1;
      int key2    = -1;
      float ratio = 0.0f;

      // Same sampler the engine plays clips with, so the preview matches playback.
      m_clip->GetNearestKeys(keys, key1, key2, ratio, time);

      if (key1 < 0 || key2 < 0 || key1 >= (int) keys.size() || key2 >= (int) keys.size())
      {
        return false;
      }

      const Key& k1 = keys[key1];
      const Key& k2 = keys[key2];

      pos   = Interpolate(k1.m_position, k2.m_position, ratio);
      rot   = glm::slerp(k1.m_rotation, k2.m_rotation, ratio);
      scale = Interpolate(k1.m_scale, k2.m_scale, ratio);
      return true;
    }

    void DopeSheetView::InsertKey(KeyArray& keys, const Key& key)
    {
      // Keys stay ascending by frame: Animation::GetNearestKeys walks the array in order and the
      // engine's anim data texture path indexes it by keyframe.
      auto it = std::lower_bound(keys.begin(),
                                 keys.end(),
                                 key.m_frame,
                                 [](const Key& k, int frame) -> bool { return k.m_frame < frame; });

      if (it != keys.end() && it->m_frame == key.m_frame)
      {
        *it = key;
        return;
      }

      keys.insert(it, key);
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

      int keyed          = 0;
      int skippedSkinned = 0;

      for (EntityPtr ntt : selection)
      {
        if (IsSkinned(ntt))
        {
          skippedSkinned++;
          continue;
        }

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

        InsertKey(*keys, key);
        keyed++;
      }

      if (keyed > 0)
      {
        m_clip->m_dirty    = true;
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
      const float frames = glm::max(1.0f, (float) m_endFrame);
      m_pxPerFrame       = glm::clamp(laneWidth / frames, g_minPxPerFrame, g_maxPxPerFrame);
      m_scrollX          = 0.0f;
    }

    void DopeSheetView::CreateClip(const String& name)
    {
      if (name.empty() || m_clip != nullptr)
      {
        return;
      }

      // Animations live next to meshes in this engine, AnimationPath() resolves the Meshes folder
      // of the project's resource tree (a workspace project always creates it).
      const String clipName = name.size() > ANIM.size() && name.compare(name.size() - ANIM.size(), ANIM.size(), ANIM) == 0
                                  ? name.substr(0, name.size() - ANIM.size())
                                  : name;
      const String path     = AnimationPath(clipName + ANIM);

      if (CheckFile(path))
      {
        GetApp()->SetStatusMsg(g_statusFailed);
        TK_ERR("Animation already exists: %s", GetRelativeResourcePath(path).c_str());
        return;
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
      ImGui::BeginDisabled(!CanEdit() || m_clip != nullptr);
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
      const bool playing = m_playState == PlayState::Playing;
      if (UI::ImageButtonDecorless(EditorImGuiTextureCache::Acquire(playing ? UI::m_pauseIcon : UI::m_playIcon),
                                   Vec2(24.0f, 24.0f)))
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
      if (UI::ImageButtonDecorless(EditorImGuiTextureCache::Acquire(UI::m_stopIcon), Vec2(24.0f, 24.0f)))
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

      ImGui::SameLine();
      ImGui::Checkbox("Snap", &m_snapToFrames);
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

      const ImU32 barColor   = ImGui::GetColorU32(ImGuiCol_MenuBarBg);
      const ImU32 tickColor  = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const ImU32 labelColor = ImGui::GetColorU32(ImGuiCol_Text);
      const ImU32 outOfRange = ImGui::GetColorU32(ImGuiCol_WindowBg, 0.65f);
      const ImU32 cursorColor = ImGui::GetColorU32(ImVec4(g_selectHighLightPrimaryColor));

      dl->AddRectFilled(origin, ImVec2(origin.x + size.x, rulerBottom), barColor);

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

      const ImU32 nameBg   = ImGui::GetColorU32(ImGuiCol_MenuBarBg);
      const ImU32 rowEven  = ImGui::GetColorU32(ImGuiCol_FrameBg, 0.28f);
      const ImU32 rowOdd   = ImGui::GetColorU32(ImGuiCol_FrameBg, 0.14f);
      const ImU32 rowHover = ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.35f);
      const ImU32 keyColor = ImGui::GetColorU32(ImGuiCol_Text);
      const ImU32 cursorColor = ImGui::GetColorU32(ImVec4(g_selectHighLightPrimaryColor));

      dl->AddRectFilled(origin, ImVec2(laneLeft, origin.y + viewHeight), nameBg);

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

        dl->AddRectFilled(ImVec2(origin.x, rowY), ImVec2(origin.x + avail.x, rowY + g_rowHeight), background);

        // Keys.
        const float keyY      = rowY + g_rowHeight * 0.5f;
        int hoveredKeyFrame   = -1;

        for (const Key& key : keys)
        {
          const float keyX = FrameToX(key.m_frame, laneLeft);
          if (keyX < laneLeft - g_keyRadius || keyX > laneLeft + laneWidth + g_keyRadius)
          {
            continue;
          }

          const bool current = key.m_frame == m_frame;
          dl->AddQuadFilled(ImVec2(keyX, keyY - g_keyRadius),
                            ImVec2(keyX + g_keyRadius, keyY),
                            ImVec2(keyX, keyY + g_keyRadius),
                            ImVec2(keyX - g_keyRadius, keyY),
                            current ? cursorColor : keyColor);

          if (rowHovered && glm::abs(io.MousePos.x - keyX) <= g_keyRadius &&
              glm::abs(io.MousePos.y - keyY) <= g_keyRadius)
          {
            hoveredKeyFrame = key.m_frame;
          }
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
          ImGui::SetTooltip("%s\nframe %d  (%.3f s)",
                            trackName.c_str(),
                            hoveredKeyFrame,
                            hoveredKeyFrame / glm::max(1.0f, m_clip->m_fps));
        }

        if (rowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
          m_ctxTrack = trackName;
          m_ctxFrame = hoveredKeyFrame;
          openCtx    = true;
        }

        if (rowActive)
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
          if (KeyArray* keys = m_clip->m_keys.Find(m_ctxTrack))
          {
            erase_if(*keys, [ctxFrame](const Key& key) -> bool { return key.m_frame == ctxFrame; });
            m_clip->m_dirty = true;
            ResolveTracks();
          }
        }

        if (ImGui::MenuItem("Delete Track"))
        {
          // Stop first: the preview snapshot of the track that is going away must be restored
          // before its keys are gone.
          Stop();

          if (m_clip->m_keys.Erase(m_ctxTrack))
          {
            m_clip->m_dirty = true;
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
