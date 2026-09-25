# Dope Sheet Editor -- Implementation Plan

> Working plan for the GDTK Dope Sheet (keyframe timeline) editor.
> Status: Phase 1 delivered and the 3.12 follow-up (node playback restore, key selection with drag
> and delete, undo/redo) is in. Phases 2-4 are the agreed follow-ups.
> Keep this file in sync while the work is in progress; delete it once the feature lands
> and the permanent description lives in `gdtk-overview.md`.

---

## 1. Goal

Give the editor a real dope sheet: place keys on entities by hand, capture movement with
auto key, and edit keys on a frame based timeline (select, bulk move in time, copy/paste).

Delivered in phases, each one usable on its own:

| Phase | Content | State |
|---|---|---|
| 1 | Frame timeline, Set Key (T/R/S), clip/track creation, Play/Pause/Stop, timeline scrubbing | implemented, plus the key editing of 3.12 |
| 2 | Auto key (capture gizmo / inspector movement) | later |
| 3 | Key selection, bulk move in time, copy/paste, delete, undo/redo | later |
| 4 | Skeleton/bone rows, curve view, runtime node playback, easing modes | later |

---

## 2. Facts the design is built on (verified in code)

1. **Key data model** -- `ToolKit/Resources/Animation.h`
   ```cpp
   struct Key { int m_frame; Vec3 m_position; Quaternion m_rotation; Vec3 m_scale; };
   typedef std::vector<Key> KeyArray;
   typedef OrderedStringMap<KeyArray> BoneKeyArrayMap;   // track name -> keys
   ```
   `Animation` carries `m_keys`, `m_fps`, `m_duration`, `m_rootKey`. A key is monolithic:
   one entry always holds position + rotation + scale, there is no per-channel key storage.
2. **Tracks are named after scene nodes / bones.** The importer names entities after their
   assimp node (`Utils/Import/import.cpp:1216`) and keys the animation channels by the same
   node name (`import.cpp:511-570`). So "track name == entity name" is already the engine's
   own convention.

### 2.1 Clarification: one track is one entity (no shared tracks)

This is the model this plan builds on, and it matches the intended design exactly:

* **A track is one entity's curve.** `Animation::m_keys` is a list of `String -> KeyArray`
  entries; each entry belongs to exactly one entity (its node). There is no track that carries
  several entities.
* **A track carries T + R + S together.** A `Key` stores position, rotation and scale in one
  entry -- there are no separate translation/rotation/scale curves like in Unity. The `[T][R][S]`
  toggles in the dope sheet are therefore a *write mask* for `Set Key` (which channels get
  updated), not a storage layout. Splitting the mask off still writes into the single track.
* **A clip (the `Animation` resource / `.anim` file) holds N tracks, one per animated entity.**
  Five animated entities in a scene = one clip with five tracks. Per entity one track, never
  more, never shared.
* **The only place names matter is the track -> entity link**, because the engine keys tracks by
  `String`, not by an object reference (see 2.1.1).

#### 2.1.1 Why the link is a name and not an ObjectId
Storing `ObjectId` in the track would tie the `.anim` file to one scene instance: ids are
generated at runtime (`ObjectRegistry::GenerateId()`) and recycled, so a saved-and-reloaded
scene, a duplicated entity or a prefab instance would all break the link. A name survives all of
those, and it is what the importer already writes. The cost is one edge case: the engine does not
enforce unique entity names, so two entities both called `Cube` make the track name ambiguous.
Resolution rule in this plan (deterministic, no user visible change in the normal case): scene
order decides, the first unclaimed matching entity gets the existing track, and a second entity
with the same name gets its own track with a numeric suffix (`Cube_1`) when a key is first set on
it. Nothing else in the design depends on names.

3. **Sampling is shared.** `Animation::GetNearestKeys(keys, key1, key2, ratio, t)` +
   `Interpolate` / `glm::slerp` is exactly how the engine samples curves
   (`Animation.cpp:270`, `Animation.cpp:36-67`). The dope sheet reuses it so preview matches
   playback. `GetNearestKeys` needs a **non-empty, frame-ascending** `KeyArray`
   (it asserts on empty and binary-searches by walking pairs) -- both invariants must be kept
   by every edit the dope sheet makes.
4. **Multi-track node animation has no runtime path today.** `Animation::GetPose(Node*, float)`
   uses `m_keys.begin()->second` only -- the first track (`Animation.cpp:45`), and
   `AnimationPlayer::Update` applies poses to *skeletons* only (anim data texture) plus root
   motion (`Animation.cpp:522-575`). Nothing in the engine calls `Entity::SetPose` for plain
   entities. Consequence: the dope sheet must apply the preview pose itself, and a
   multi-entity clip is an editor-side construct until the engine resolves tracks by name
   (see Phase 4 / open decision).
5. **Editor state today**: `Editor/UI/View/AnimationView.cpp` shows a read-only key table per
   animation plus the root-key checkbox. There is no key authoring UI, no timeline widget and
   no undoable key edit anywhere in the editor.
6. **Window plumbing**: persistent windows are `TKDeclareClass` Window subclasses created on
   demand by `App::CreateOrRetrieveWindow<T>(name)` and listed in `UI::ShowMenuWindows()`
   (`Editor/UI/UI.cpp:838-947`); they are serialized into `Config/Editor.settings` and restored
   through `App::DeserializeWindows` via `ObjectFactory` (`App.cpp:1758`), so the class must be
   registered with `TKDeclareClass` / `TKDefineClass`. Transport icons already exist:
   `UI::m_playIcon`, `UI::m_pauseIcon`, `UI::m_stopIcon`, used through
   `EditorImGuiTextureCache::Acquire()` + `UI::ImageButtonDecorless()`.
7. **Project resource folder**: `AnimationPath(file)` resolves to `<project>/Resources/Meshes`
   (`ToolKit/Source/ToolKit.cpp:639`), and a workspace project always creates
   `Materials, Meshes, Scenes, Textures` (`Modules/Workspace/Source/Workspace.cpp:184`), so a
   new `.anim` file has an existing directory to land in.
8. **Shutdown / lifetime contract** (`AGENTS.md`): state that holds engine objects must be a
   member of an object destroyed in `App::Destroy`/`DeleteWindows` (the dope sheet window is a
   member of `App::m_windows`) or have an explicit release point called from `App::Destroy`.
   The dope sheet therefore keeps its state in members, not in file-scope statics.
9. **No test harness exists** in the repo for editor code, so Phase 1 verification is
   "clean build + manual checklist in the editor".

### 2.2 Can a clip hold more than one entity today?

**As data: yes, and it already does.** `Animation::m_keys` is an ordered list of named tracks
with no limit on the count, and the importer writes one track per animated node (plus a T-pose
track for every bone that has no channel, `import.cpp:573-601`). Any imported skeletal clip is
therefore a multi-track clip today -- it just happens that its tracks are named after *bones*
rather than after scene entities. `AnimationView` already lists all of them.

**As playback: it depends on what the tracks are named after.**

| Track kind | Who applies it | Works with several tracks? |
|---|---|---|
| Bone track (skinned mesh) | `Animation::GetPose(SkeletonComponentPtr, time)` -- loops `skeleton->m_map->m_boneMap` and looks each bone's track up **by name** (`Animation.cpp:83-118`), then `AnimationPlayer` feeds the anim data texture for GPU skinning | Yes, name-keyed and per bone |
| Entity/node track (non-skinned) | `Animation::GetPose(Node*, time)` -- takes `m_keys.begin()->second`, the **first** track, names are never consulted (`Animation.cpp:45`); nothing in the engine calls `Entity::SetPose` at all | No: only the first track is reachable, and nobody applies it |
| Root motion track | `AnimationPlayer::ApplyRootMotion` -- looks the track up **by name** via `anim->m_rootKey` (`Animation.cpp:597`) | Yes, one designated track |

So the answer splits cleanly:

* **Data model / file format**: a clip with several entity tracks is valid and nothing has to be
  invented for it; it is the same shape the importer already writes for bones.
* **Engine playback of node tracks**: not supported today. A three-entity node clip cannot be
  played by the engine -- not in the editor, not in a game -- without code that walks the tracks
  itself. That is the gap described in Section 7, and it does not change the 1 track = 1 entity
  model; it only decides *which* track a given node receives.
* **Editor**: the dope sheet is the code that walks the tracks (track -> entity by name), so the
  editor side works as soon as Phase 1 lands.

---

## 3. Phase 1 -- foundations

### 3.1 New files

| File | Content |
|---|---|
| `Editor/UI/View/DopeSheetView.h` | `DopeSheetView` (the widget) + `DopeSheetWindow` (persistent window) |
| `Editor/UI/View/DopeSheetView.cpp` | implementation |

Follows the existing `AnimationView.h` / `AnimationWindow` layout (view + window in one pair of
files). `Editor/CMakeLists.txt` uses `file(GLOB_RECURSE ...)`, so the files are picked up by a
CMake re-configure; no build file edit is needed.

### 3.2 Touched files

| File | Change |
|---|---|
| `Editor/Source/EditorTypes.h` | `DopeSheetViewPtr` / `DopeSheetWindowPtr` typedefs, `g_dopeSheetStr("Dope Sheet")` |
| `Editor/UI/UI.cpp` | `#include "DopeSheetView.h"`, `ShowPersistentWindow<DopeSheetWindow>(g_dopeSheetStr)` in `ShowMenuWindows()` |
| `Editor/UI/UI.h` | only if a new icon is needed (none planned in Phase 1) |
| `Editor/UI/View/AnimationView.cpp` | optional: "Open in Dope Sheet" button that focuses the window on the shown clip |
| `gdtk-overview.md` | Section 9.3 window list + Section 14 file lookup |

No engine (`ToolKit/`) change in Phase 1.

### 3.3 Window layout

```
+--------------------------------------------------------------------------------+
| [dropzone] Door.anim   fps[30]  End[120]f (4.000s) [Save] [New Clip]           |
| [|<][<][play/pause][stop][>][>|]  Frame[  0] Speed[1.0] [loop]                 |
| Set Key: [T][R][S]  [Set Key (K)]   [Fit]  [Auto Key: phase 2]                  |
+---------------------+----------------------------------------------------------+
| Track               | 0    10    20    30    40    50    60    70   ruler      |
| Door_Root    (3)    | . . . .<> . . . . . . .<> . . . . . . .<> . . . .         |
| Door_Handle  (2)    | . . . . . . . . . .<> . . . . . . . . .<> . .             |
| Light_A      (0)    | (no keys -- warning: no entity matches)                   |
+---------------------+----------------------------------------------------------+
```

* Left column: track names (fixed width, resizable), key count, and a match indicator
  (`no entity` when no scene entity carries that name).
* Right area: ruler + key rows, drawn with `ImDrawList`; the horizontal transform
  (`m_scrollX`, `m_pxPerFrame`) is owned by the view, vertical scroll comes from the child
  window so names and rows stay aligned.
* Row height and the ruler strip height are named constants (no magic numbers).

### 3.4 Clip slot (the "known animation track" the keys are written into)

* `AnimationPtr m_clip` -- held by the view, resolved from the resource manager so it is the
  same instance other views/entities use (`GetAnimationManager()->Create<Animation>(path)`).
* Header widgets:
  * Drop zone (reuse `View::DropZone`, same pattern as the animation dropzone in
    `ComponentView.cpp:277-353`) accepting only `Animation` files.
  * `New Clip` -> `StringInputWindow` for the name (same widget `UI::ShowMenuFile` uses),
    then: path = `AnimationPath(name + ".anim")`; refuse if the file already exists; create
    `MakeNewPtr<Animation>()`, `SetFile(path)`, `fps = 30`, `duration = 60/fps`,
    `GetAnimationManager()->Manage(anim)`, `Save(false)`, refresh the asset browsers
    (`FolderWindow::UpdateContent()`), bind it to the view.
  * `Save` -> `m_clip->Save(false)` (marks clean, writes base64 key tracks).
* On scene change (`GetSceneManager()->GetCurrentScene()` id differs from the cached one):
  stop playback, drop the preview snapshots, keep the clip.
* While `GetApp()->m_gameMod != GameMod::Stop` (PIE running) the preview applier and Set Key
  are disabled with an inline note, so the dope sheet never fights the game.

### 3.5 Timeline widget

* Units: **frames**. `m_frame` (int) is the playhead; `m_time = m_frame / fps` is what gets
  sampled. Scrub snaps to whole frames; playback advances continuously and interpolates.
* Range: `[0, m_endFrame]`, `m_endFrame` is a drag-int field; editing it writes
  `clip->m_duration = m_endFrame / fps`. Loading a clip initializes
  `m_endFrame = round(duration * fps)` (fallback: last key frame, else 60).
* Ruler: adaptive tick step chosen from `m_pxPerFrame` so labels never overlap; every 5th/10th
  tick is labelled with its frame number; the playhead is drawn as a vertical line over the
  whole sheet.
* Scrub: left-drag anywhere on the ruler or on empty timeline space sets the frame
  (`ImGui::InvisibleButton` + `IsItemActive()/IsItemHovered()` inside a `PushClipRect`).
* Zoom: `Ctrl+wheel` zooms around the frame under the cursor (clamped px/frame);
  plain `wheel` pans horizontally; `Fit` recomputes px/frame for the visible width.
  Horizontal panning is my own `m_scrollX` (pixels), so no ImGui horizontal scrollbar is used.
* Snap toggle (`Snap` checkbox, default on) keeps later key dragging on whole frames.

### 3.6 Track rows

* One row per `clip->m_keys` entry, drawn in the map's insertion order (matches the engine and
  the importer; the rig order should not shuffle while editing).
* Each row shows: name, key count, matched-entity name or a `no entity` warning.
* Keys are drawn as diamonds at `frame -> x`; hovered key shows a tooltip
  (`frame N, t=0.033s`) and the playhead frame is highlighted by marking the key whose frame
  equals `m_frame`.
* Per-channel filtering (T/R/S sub-rows) is **not** in Phase 1; the T/R/S mask below decides
  which channels `Set Key` writes.

### 3.7 Transport

State machine `enum class PlayState { Stopped, Playing, Paused }` owned by the view, advanced
inside `Show()` from `ImGui::GetIO().DeltaTime` (the window is drawn once per frame, so no
engine update hook is needed):

| Action | Behavior |
|---|---|
| Play | `Stopped`/`Paused` -> `Playing`; on entering from `Stopped` snapshot the base transforms (used by Stop to put them back). Each frame: `m_time += dt * speed`; wrap into `[0, endTime]` while `Loop` is on, else clamp at the end and go to `Paused`. Pose is applied every frame (interpolated). |
| Pause | `Playing` -> `Paused`. The last applied pose stays on the entities; **no** further pose application, so the user can move the object with the gizmo and press Set Key. |
| Stop | `Stopped`; the base transforms snapshotted when the preview session started are restored (the same behavior as the root-motion preview in `ComponentView`), snapshot cleared, `m_frame = 0`. |

Note on Stop: after Stop the entities show the pose they had before previewing, which is not the
frame 0 pose of the clip. Press Play or scrub once to apply the clip pose again before using
Set Key, otherwise Set Key would record the restored (pre-preview) transform.
| `|<` `>` `>|` `<` | first / previous / next / last frame, snapped, pose applied. |

Applied pose is written to `m_node->SetLocalTransforms(pos, rot, scale)` (local space, the same
thing `GetPose` does), so what the viewport shows is what the clip stores.

### 3.8 Set Key

* Operates on the current selection (`EditorScene::GetSelectedEntities`); with several entities
  selected each gets its own track, so "key everything that moves" works in one click.
* Track resolution (one shared helper, used by Set Key, the preview applier and the row
  matcher):
  1. Walk the scene entities in scene order; a track claims the first unclaimed entity whose
     name equals the track name; an entity claims the first track whose name equals it.
  2. Set Key uses the track resolved to the selected entity; if there is none yet, a new track
     is created. New track name = entity name, disambiguated with a numeric suffix only when a
     name is already taken by a track bound to another entity.
  3. This is what "keys build a known animation track" means: the first Set Key on an entity
     inserts the track into the clip.
* Frame = `m_frame`; key values = the entity node's current local T/R/S, restricted by the
  T/R/S mask. Masked-off channels keep: the value already stored at that frame, else the value
  sampled from the existing curve at that frame, else the node's current value (no keys yet).
* Insert keeps `KeyArray` sorted by frame (`std::lower_bound`); an existing key at the same
  frame is updated in place, never duplicated.
* Side effects: `clip->m_dirty = true`, `clip->m_duration = max(duration, frame / fps)`,
  `m_endFrame` grows with it, status message via `GetApp()->SetStatusMsg()`.
* Shortcut `K` (and a button) through the window's own `DispatchSignals()` override;
  `Window::ModShortCutSignals()` is deliberately not called from this window (it would steal
  S/R/G/C/X as transform modes while the mouse hovers the sheet).

### 3.9 Preview applier

```
ApplyPoseAt(float time):
  for each (trackName, keys) in clip->m_keys:
    if keys.empty(): continue                      // GetNearestKeys asserts on empty
    ntt = ResolveEntity(trackName)                 // nullptr -> skip, row shows "no entity"
    if ntt is skinned: skip                        // skeleton/bone rows are Phase 4
    GetNearestKeys(keys, k1, k2, ratio, time)
    pos = Interpolate(...); rot = slerp(...); scl = Interpolate(...)
    ntt->m_node->SetLocalTransforms(pos, rot, scl)
```

* Called while `Playing`, on every scrub/step, and after Set Key.
* `Stop` restores the local transforms snapshotted when the preview session started
  (`ObjectId -> Mat4`, taken on the first Play from `Stopped`), then clears the snapshot. This is
  the same safety net `ComponentView` uses for root-motion preview, and it is the way back if
  scrubbing is stopped mid-clip. A scrubbing-only session (never pressed Play) also takes the
  snapshot on its first scrub, so Stop can always undo it.
* Scrubbing overwrites entity transforms; this is a dope sheet's normal behavior, and it is why
  Stop restores them. Scene entities keep whatever the playhead left them at until Stop.
  (Decision taken: Stop restores, there is no separate Revert button.)

### 3.10 Phase 1 verification

1. Re-configure + build (new files are GLOBbed, so CMake must re-run):
   `python BuildScripts/build_gdtk.py --configs Debug`, or the already configured generator:
   `cmake --build Intermediate --config Debug --target Editor`.
   Acceptance: no new warnings, `BinDebug/Editor.exe` rebuilt.
2. Manual checklist (run by the user, or by me if a GUI run is possible):
   * Windows menu -> Dope Sheet opens and docks; layout survives an editor restart.
   * New Clip -> file appears in the asset browser under `Resources/Meshes`.
   * Select an entity, scrub to frame 20, move it, `Set Key` -> a row appears with one diamond
     at frame 20; a second Set Key at frame 40 makes two; scrubbing between them interpolates.
   * Play loops over `[0, End]`; Pause freezes; Stop restores the pre-preview transforms and
     returns the playhead to frame 0.
   * Save -> reload the clip (double-click the .anim, or reopen the project) and the keys are
     still there; `AnimationView` shows the same track names and key counts.
   * Keying a selection of 3 entities creates 3 tracks and preview drives all of them.
3. `gdtk-overview.md` updated in the same commit (Section 9.3 window list, Section 14 lookup).

### 3.11 Phase 1 as implemented

Files:

| File | Content |
|---|---|
| `Editor/UI/View/DopeSheetView.h` / `.cpp` | `DopeSheetView` + `DopeSheetWindow` (new) |
| `Editor/Source/EditorTypes.h` | `DopeSheetViewPtr` / `DopeSheetWindowPtr` typedefs, `g_dopeSheetStr` |
| `Editor/UI/UI.cpp` | `ShowPersistentWindow<DopeSheetWindow>(g_dopeSheetStr)` in `ShowMenuWindows()` |
| `Editor/UI/View/AnimationView.cpp` | `Open in Dope Sheet` button binds the shown clip to the sheet window |
| `gdtk-overview.md` | Section 9.3 persistent window note + Section 9.7 Dope Sheet editor + Section 14 lookup |

Delivered behavior:

* Clip slot: drop zone for an `.anim`, `New Clip` (creates under `Resources/Meshes` via
  `AnimationPath`, registers with `AnimationManager::Manage`, refreshes the asset browsers),
  `fps` / `end` fields, `Save`, unsaved marker. A press always makes a clip and binds it: a taken
  name goes through `CreateIncrementalFileFullPath()` with an empty postfix (`NewAnimation(1).anim`),
  and a replaced clip with unsaved keys is reported in the console.
* Transport: first / previous / play-pause / stop / next / last, frame field, speed, `Loop`,
  `Snap`, seconds readout. Shortcuts (window focused, no text input active): `Space`, `K`,
  arrows, `Home`, `End`. The window deliberately does not call `Window::ModShortCutSignals()`.
* Timeline: ruler with adaptive ticks and labels, draggable playhead, drag-to-scrub on any lane,
  `Ctrl+wheel` zoom around the cursor, `Shift+wheel` pan, wheel scrolls rows, `Fit`, draggable
  name column splitter, out-of-range shading past `End`, frame grid + playhead over the rows.
* Rows: one per track, name / key count / `[no entity]` / `[skinned]` markers, keys as diamonds
  (the key at the playhead is highlighted), key tooltip with frame and seconds, right-click
  context menu with `Delete Key` / `Delete Track`.
* `Set Key`: writes every selected entity's local transform at the playhead, creates the track on
  first use, honors the T/R/S mask, keeps keys frame-sorted, grows `End`/duration, reports through
  the status bar. Skinned entities are skipped with a warning.
* Preview: `ApplyPoseAt` drives matched non-skinned entity nodes; Play/Pause/Stop semantics as in
  3.7; Stop (and closing the window, and swapping the clip) restores the pre-preview transforms.

Known Phase 1 limits, intentional: no key selection / bulk move / copy-paste, no auto key, no
skeleton rows, no undo of key edits, one sheet instance at a time, and clicking a row's name column
scrubs like the rest of the row. The first two of those were addressed by 3.12.

### 3.12 Follow-up pass: node playback restore and key editing

Done after the user tested Phase 1 and asked for the node playback path back plus basic key
editing.

Engine (this is the "runtime node animation" item that used to sit in Phase 4, restored early
because a refactor had dropped it):

* `AnimationPlayer::Update` poses non skinned entity nodes again: for a record whose entity has no
  skinned mesh, `ntt->SetPose(record->m_animation, record->m_currentTime)` runs every frame, so a
  clip that reaches an entity through an `AnimControllerComponent` moves it without plugin code.
  Records that request root motion are skipped there -- they stay under root motion control, which
  accumulates deltas on the node instead of setting it. Node tracks are not pose blended: when two
  records overlap during a fade, the one played last owns the node.
* `Animation::GetPose(Node*, time, keyName)` resolves the track by name and falls back to the first
  track, so a multi entity clip drives each node from its own curve while single curve clips keep
  their old behavior. `Entity::SetPose` passes the entity name, which the importer already uses as
  the track name.
* The entity loop no longer dereferences a missing `MeshComponent` (an entity without a mesh
  component and a record used to be a null dereference).
* Note for prefab instances: the runtime resolves the track by name, so two entities that share a
  name both play the same track. The sheet gives the second entity a suffixed track (`Cube_1`),
  which only the editor preview uses -- an open point if prefab instances should share one curve.

Sheet:

* `Animation::SetKey(trackName, frame, key)` (engine) is the single place that inserts, replaces or
  removes a key, so a track is always ascending by frame -- what `GetNearestKeys` assumes.
* `KeyEditAction` (`Editor/Source/Action.h`) makes key edits undoable: it records what sat on the
  source and target frames before and after the edit, so insert, update, delete and a move that
  replaced another key all replay in both directions. A `Set Key` press over several entities is
  grouped into one undo step (`BeginActionGroup` / `GroupLastActions`).
* Single key selection (click a key), drag in time (the key follows the mouse as a ghost, `Esc`
  cancels), `Delete` / `Backspace` removes the selection, and the row context menu deletes through
  the same action. Dragging is clamped to `[0, End]`, so it never extends the clip range.
* `Delete Track` is still not undoable; it drops every key of the track.

Still open from Phase 3: multi key selection, box select, copy/paste, and bulk moves.

---

## 4. Phase 2 -- auto key

> Status: **on hold.** Phase 2 is not to be started until the user has tested Phase 1 in the
> editor and given feedback (explicit request).

* `Auto Key` toggle in the header (persisted in the view, off by default).
* Each frame, for every selected entity that has a track (or gains one on first change):
  compare the node's current local T/R/S against the values the view last applied or last saw.
  If a channel differs, insert/update a key at the current frame for that channel only:
  the differing channel takes the node's value, the others keep the curve's value at that frame
  (sampled), so a rotation drag never freezes a translation curve.
* Never captures while `Playing` (playback writes the poses itself); captures only user edits,
  detected as "current transform differs from what the view applied".
* Options: `key on every change` vs `key when the gizmo drag ends` (default: on drag end, so a
  drag produces one key instead of one per frame). Requires watching
  `ImGui::IsMouseDown(ImGuiMouseButton_Left)` around the viewport, or polling until the
  transform stops changing (simpler, no coupling to the gizmo code).

## 5. Phase 3 -- selection, bulk move, copy/paste

* Selection model in the view: `std::set<std::pair<String, int>> m_selectedKeys` (track name +
  frame). Click selects, `Ctrl+click` toggles, box-drag on the key area selects a range,
  `Shift+click` selects a row range, `Ctrl+A` selects everything in the visible rows,
  `Delete` removes the selection (undoable).
* Bulk move: drag a selected key horizontally; every selected key moves by the same frame delta,
  clamped so no key goes below frame 0 and no two keys in a track collide (collision policy:
  the moved key overwrites the target, like most dope sheets). Snapped to whole frames when
  `Snap` is on.
* Copy/paste: internal clipboard of `(trackName, Key)` pairs; copy `Ctrl+C`, paste `Ctrl+V`
  places keys at the playhead, preserving relative frame offsets clamped at 0; pasting into a
  track that does not exist creates it.
* Undo/redo: a `KeyEditAction : Action` (`Editor/Source/Action.h`) captures the source and target
  frames of the edit and replays it in both directions. Shipped early, see 3.12 -- what is left
  here is extending it from one key to a whole selection (one action per completed gesture).

## 6. Phase 4 -- later work

* Skeleton/bone rows: bone names come from `SkeletonComponent::m_map->m_boneMap`; the preview
  must drive `DynamicBone::node` instead of the entity node. Needs a row tree per entity.
* Runtime node animation: **done early, see 3.12** -- `AnimationPlayer::Update` poses non skinned
  entity nodes and `Animation::GetPose(Node*, time, keyName)` resolves the track by name.
  What is left here is pose blending for node tracks (fades currently let the last record own the
  node) and a decision on whether prefab instances should share one track.
* Curve view / easing: `Key` has no tangent or interpolation mode today (linear + slerp only);
  adding modes is an engine + serialization change.
* Root motion: the `m_rootKey` checkbox stays in `AnimationView`; the dope sheet only needs to
  show which track is the root key.

---

## 7. Decisions

1. **Stop semantics** -- decided: Stop restores the pre-preview transforms and returns the
   playhead to frame 0 (no separate Revert button). See 3.7 / 3.9.
2. **Clip/track model** -- decided: one `.anim` clip, one track per entity, track named after the
   entity (the importer's own convention).
3. **Runtime correctness** -- open. Phase 1 is editor-preview only, the engine is untouched; the
   name-based track lookup described in "Why the engine change is needed" below is a separate
   small step (Phase 4 by default).

### Why the engine change is needed for runtime playback

This is **not** a data model change: one track still belongs to one entity (see 2.1). It is only
about *which* of a clip's tracks gets applied to a given node at play time.

Two facts in today's engine decide this:

1. `Animation::GetPose(Node* node, float time)` (`ToolKit/Resources/Animation.cpp:36-67`) samples
   **only the first track** of the clip: `std::vector<Key>& keys = m_keys.begin()->second;` and
   writes the result to the node it is handed. A clip with three tracks
   (`Door_Root`, `Door_Handle`, `Light_A`) therefore hands every caller the `Door_Root` curve.
2. `AnimationPlayer::Update` (`Animation.cpp:411-575`) never poses a plain entity node. It fills
   `SkeletonComponent::m_animData` for skinned meshes (the GPU skinning path) and applies root
   motion. So a game that plays a node clip through `AnimControllerComponent` gets nothing
   moving unless its own plugin code poses the nodes.

Consequence: in the editor the dope sheet applies each track to the matching entity itself, so
Phase 1 works with the engine as it is. In a shipped game, `entity->SetPose(clip, t)` on three
entities would drive all three from the first track, which is wrong for a multi-entity clip. A
game could work around it by sampling the tracks itself, but that is engine work leaking into
user code.

The small fix (about 15 lines, Phase 4 candidate):

```cpp
// Animation.h -- additive overload, existing callers keep working
void GetPose(Node* node, float time, const String& keyName = "");

// Animation.cpp
const KeyArray* keys = keyName.empty() ? nullptr : m_keys.Find(keyName);
if (keys == nullptr)
{
  keys = &m_keys.begin()->second; // today's behavior when no name matches
}
```

`Entity::SetPose` then passes its own name (`GetNameVal()`), which for imported assets is the
same name the importer wrote the track with (`Utils/Import/import.cpp:1216` and `:511-570`).
The fallback means every existing clip behaves exactly as before; the only behavior change is
that an entity whose name matches a track name is driven by that track instead of the first one,
which is what a name-keyed animation is supposed to do. A full runtime node path would also need
`AnimationPlayer::Update` to pose node tracks (so `AnimControllerComponent` works without any
plugin code); that part changes behavior for existing projects, so it stays a separate decision.

## 8. Risks / notes

* `GetNearestKeys` asserts on an empty `KeyArray` and assumes ascending frames -- every write
  path (Set Key, paste, move, delete) must hold both invariants; deleting the last key of a
  track is allowed only if the applier skips empty tracks (it does) and
  `AnimationPlayer::CreateAnimationDataTexture` is not reached for that clip.
* The dope sheet edits the shared `Animation` instance from the resource manager, so an edit is
  visible to every entity that plays that clip; `SaveAllResources` also writes it.
* Scrubbing overwrites scene entity transforms: this is intended, `Stop` is the escape hatch,
  and saving the scene while scrubbed records the current pose.
* One persistent window instance named `Dope Sheet` (no multi-instance editing of two clips
  side by side in Phase 1).
