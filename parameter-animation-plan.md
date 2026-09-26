# Parameter Animation Plan (dope sheet, beyond node transforms)

> Working plan for keying `ParameterVariant` values (float / int / uint / bool / vec2 / vec3 / vec4)
> from the dope sheet, next to the node transform tracks that already work.
>
> Status: design agreed with the user, not started. Slices below are ordered, each one usable on its
> own. This is a work item of its own, not a numbered phase of `dope-sheet-plan.md`.

---

## 1. Goal

Any parameter that a `ParameterVariant` carries -- entity parameters, component parameters and the
material parameters a `MaterialComponent` slot points at -- can be keyed on the dope sheet timeline,
previewed in the editor and played back at runtime, with the same authoring rules the transform
tracks already follow (Set Key, diamonds, drag, delete, undo).

## 2. Decisions taken

| Decision | Choice |
|---|---|
| Interpolation | Driven by the variant type, no second hint flag. Float / Vec2 / Vec3 / Vec4 lerp; Bool / Byte / Ubyte / Int / UInt step (hold the previous key). |
| Opt in | A single `UIHint::animatable` flag, default off. Types that can not be keyed (resource pointers, callbacks, record maps, Mat3 / Mat4, ObjectId, MultiChoice) never get a diamond, flagged or not. `ObjectId` is an identity and a `MultiChoice` is a named state, neither is a curve; both can be added later if a real use shows up. |
| Which parameters first | `Light` (Color, Intensity, ShadowBias, BleedingReduction), `Camera` (Fov, NearClip, FarClip, OrthographicScale), `EnvironmentComponent` (Intensity, Fade, Size, CaptureFar), `Entity` (Visible), `Material` (Color, EmissiveColor, Alpha, Metallic, Roughness). |
| Texture | Out of scope. `Texture` has no `TKDeclareParam` at all and its settings (`TextureSettings`: wrap / filter / format / mipmaps) are GPU configuration, not animation targets. Revisit only if a specific field is asked for. |
| Material addressing | Through the owning slot, never through the resource file: `Entity.MaterialComponent.<index>.<param>`, index always written (single material included). |
| Shared materials | Animating a material a slot shares with other meshes is allowed and expected to affect them; the editor logs a warning naming the share count and keys it anyway. No "make unique" flow. |
| `Object.Param` | An entity is an Object, so `Entity.Param` is this case. A parameter block on an object the scene can not reach (a standalone resource) is not addressed by the sheet for now: materials go through their slots, other resources are out of scope. |
| Key state source | The clip. There is no registry and no global store: the diamond asks the dope sheet for the track in the clip it has open. |

## 3. Track id grammar

```
<entity>.<param>                          Entity.Param
<entity>.<componentClass>.<param>          Entity.Component.Param
<entity>.MaterialComponent.<index>.<param> material slot of the entity
```

Examples:

| Track id | Meaning |
|---|---|
| `Sun.Intensity` | `DirectionalLight` is an `Entity`, its Intensity is an entity parameter |
| `MainCamera.Fov` | camera parameter (Camera is an entity subclass) |
| `Door.MeshComponent.CastShadow` | component parameter (step, bool) |
| `Sky.EnvironmentComponent.Intensity` | component parameter (lerp, float) |
| `Door.MaterialComponent.0.Color` | first material slot of Door (lerp, Vec3) |
| `Door.MaterialComponent.2.Alpha` | third slot (a mesh with three submeshes) |

### 3.1 Resolution

Names may contain dots (entity names are free text), so a track id is resolved by validating
candidates rather than splitting blind:

1. Walk the dot positions left to right; the first candidate owner is the text before the first dot.
2. For each candidate: resolve the owner (an entity by name, or another object by its own rules), then
   parse the remainder as `<param>` (the owner's own parameter block) or
   `<componentClass>.<param>` (a component of that entity) or
   `MaterialComponent.<index>.<param>` (a material slot).
3. The first candidate whose remainder parses against what really exists wins.
4. Nothing validates: the row shows `[no owner]`, the sheet refuses to key it, and the pose applier
   skips it.

The same rule serves the editor and the runtime, so a clip behaves the same in both.

## 4. Engine

### 4.1 Data

```cpp
// Animation: a second track space next to m_keys (transform tracks stay untouched).
struct ParamKey
{
  int m_frame = 0;
  ParameterVariant::VariantType m_type;   // the key carries its own type
  Vec4 m_value;                           // holds float / int / uint / bool / vec2 / vec3 / vec4
};

typedef std::vector<ParamKey> ParamKeyArray;
typedef OrderedStringMap<ParamKeyArray> ParamKeyArrayMap;   // track id -> keys
```

* Keys stay ascending by frame, like transform keys: `Animation::SetParamKey(track, frame, key)`
  mirrors `SetKey()` (the one entry point that inserts, replaces or removes).
* Sampling: `Animation::GetParamValue(track, time, type, Vec4& out)` -- lerp for float / vec2 / vec3 /
  vec4, step for the rest. No interpolation across a type change. A step type holds the key at or
  before the sampled time, and **past the last key it holds the last key**: holding the previous one
  there leaves a bool track stuck one key behind, so a flag looks like it is not animated at all.

### 4.2 Serialization

Param tracks are written as `<param>` nodes next to the existing `<node>` tracks, base64 like the
transform keys with a `Type` attribute and a `KeyCount`. Old clips load unchanged (no `<param>`
nodes), a clip with param tracks still loads in an older build because the extra nodes are ignored.

### 4.3 Hints

```cpp
struct UIHint
{
  bool isColor = false;
  ...
  bool animatable = false;   // new: this parameter may be keyed
};
```

Type decides the interpolation, so there is no `interpolatable` flag.

### 4.4 Applying values

* `Animation::ApplyParamTracks(EntityPtr entity, float time)` resolves every `<entity>.` track and
  writes the sampled value **through the `ParameterVariant`** (`*var = value`), which is what fires
  `m_onValueChangedFn` and updates material caches / light buffers. Writing the raw variant storage
  would skip all of that.
* `AnimationPlayer::Update` calls it per record, next to the node pose, so a clip that reaches an
  entity through an `AnimControllerComponent` animates its parameters too.
* `Entity::SetPose` calls it as well, so plugin code that poses an entity by hand gets parameters.
* A shared material is written once per slot track; if two tracks address the same material instance
  the later application wins. The editor warns at authoring time; the runtime stays simple.

## 5. Editor

### 5.1 The diamond (inspector)

Each animatable parameter row gets a diamond drawn from `CustomDataView::ShowVariant`, with three
states read from the dope sheet's clip:

| State | Look | Meaning |
|---|---|---|
| no track, or the track has no keys | hollow, dim | not animated by this clip |
| track with keys, none on the playhead frame | filled | animated, playhead sits between keys |
| key exactly on the playhead frame | filled + outline | a key sits here |

* Click: no key / between keys -> Set Key at the playhead (undoable through the same key action).
  Key present -> context menu (`Delete Key`, `Show in Dope Sheet`). Right click always opens the menu.
* No clip bound in the sheet: no diamond, and a status message explains what to do. Same rule the
  viewport `K` shortcut uses today.
* The inspector asks the sheet (`App::GetDopeSheet()`), which owns the clip; there is no separate
  key registry to keep in sync.

### 5.2 One diamond, widgets stay where they are

The diamond is a small shared widget, `CustomDataView::ShowKeyDiamond()` (state lookup, click and
context menu inside it), so the key behavior exists once:

* `CustomDataView::ShowVariant` calls it for every variant whose hint says `animatable`, which covers
  the entity and component panels for free.
* `MaterialView` keeps its own hand written rows (`UI::SRGBColorEdit3` for the diffuse color, the HDR
  emissive color, `DragFloat` for alpha / metallic / roughness, the texture and shader drop zones) and
  calls the same `ShowKeyDiamond()` next to each animatable row.

`ShowVariant`'s color handling is **not** changed: it keeps drawing `isColor` variants with a plain
`ColorEdit`, so no other panel (light color, sky colors) changes behavior. That means material colors
keep being edited through `UI::SRGBColorEdit3` in `MaterialView`, which is exactly where they are
edited today. The point of this shape is that the diamond, not the widget, is what gets shared.

Texture and shader slot rows stay custom as well: they are resource pointers, they can not be keyed,
and their per slot clear button and thumbnails are not something `ShowVariant` draws.

### 5.3 Owner context

The diamond needs to know which object a row belongs to in order to build the track id, and only the
panel that draws the row knows that. So the panel pushes a prefix around the block of rows it draws
and the diamond composes `prefix + "." + var->m_name`:

| Panel | Prefix pushed | Row diamond |
|---|---|---|
| `EntityView` (entity parameters) | `<entityName>` | `Door.Visible` |
| `ComponentView` (component parameters) | `<entityName>.<componentClass>` | `Door.MeshComponent.CastShadow` |
| `MaterialView` opened from `PropInspectorWindow` | `<entityName>.MaterialComponent.<materialIndex>` | `Door.MaterialComponent.0.Alpha` |
| `MaterialView` opened from the asset browser | none | no diamond, tooltip explains why |

`MaterialView` currently receives only the material list (`SetMaterials`), so it needs the owner as
well: `PropInspectorWindow` already has both the selected entity and the `MaterialComponent` it takes
the list from, and passes them along. The material index is the row's position in that list, which is
the same index the track id uses.

No clip is bound in the sheet: no diamonds anywhere, plus one status message explaining what to do.
Same rule the viewport `K` shortcut already follows.

### 5.4 Dope sheet rows

* Param tracks appear as rows next to transform tracks; the row label shows the short form (for a
  single slot material the index is hidden on screen while the clip keeps it).
* `Set Key` keeps meaning "the transform": a parameter is keyed from its own row or from its diamond,
  not by the sheet wide button. A later slice can add "key every animated parameter of the selection"
  if that turns out to be useful.
* Key drag, delete and undo work on param keys as well: the existing two frame before/after action is
  generalized from `KeyArray` to a param key array (the model is the same, the payload differs).
* The row context menu gains `Delete Parameter Track`.
* Editing the value at the playhead from the sheet is a later nicety; the inspector already does it.

### 5.5 Preview applies parameters, not just poses

The sheet's preview is the only thing that puts a clip on the scene in the editor, and a parameter
track is not a transform, so `ApplyPoseAt()` applies the parameter tracks as well
(`ApplyParamTracksAt()`, one call per addressed entity, the engine does the resolving and writing).
The preview session snapshots the parameter values it overwrites (`m_baseParams`, packed like a key)
and `RestorePreviewState()` puts them back through the variant on Stop, exactly like the transforms.

Writing through the variant matters here and is not interchangeable with the other two write paths in
the code base:

| Write path | Fires `m_onValueChangedFn` |
|---|---|
| `var = value` (typed `operator=`) | yes, this is what the animation code uses |
| `var.SetValue(value)` | no, it assigns the variant storage directly |
| `var.GetVar<T>() = value` | no, it writes through the reference |

The first is what keeps a light's cache and a material's GPU data in sync (`Light::ParameterEventConstructor`
registers `InvalidateCacheItem()` on its parameters). `Cone::Generate` uses the third on purpose, to
avoid regenerating the mesh on every Set; that is the documented exception.

## 6. Slices

| Slice | Content | Notes |
|---|---|---|
| 1 | Engine: `UIHint::animatable`, `ParamKey` + storage + `SetParamKey`, sampling, serialization, `ApplyParamTracks`, player + `SetPose` wiring. Mark the first parameter set animatable. | **Done.** Engine only, nothing visible in the editor yet. Verified with a clean Debug build of ToolKit and Editor. |
| 2 | Editor: the shared key diamond with its three states, click / menu behavior, owner prefix plumbing, `KeyEditAction` generalized to param keys. | **Done.** `CustomDataView::ShowKeyDiamond` + owner prefix stack, `ShowVariant` calls it for animatable variants, `EntityView` / `ComponentView` / `MaterialView` push their prefixes, `KeyEditAction` gained the param key factories. Verified with a clean Debug build. The sheet side of the key state (`GetParamKeyState`, `SetParamKey`, `DeleteParamKey`) is in as well. |
| 3 | Sheet: param track rows, resolution and `[no owner]` reporting, drag / delete / undo on param keys, value readout in the row. | **Done.** Rows follow the transform tracks (separated by a rule), the info column shows the sampled value at the playhead plus `[no owner]` when the id no longer resolves, key select / drag / delete / context menu and undo work on param keys through the same action. Verified with a clean Debug build. |
| 4 | Later: per channel sub rows (x / y / z) with channel colors, parameter track blending during a clip fade, the shared material warning, "key every animated parameter" shortcut. | Each one optional. |

## 7. Verification

* Slice 1: a light with keyed Intensity/Color fades over the timeline in a game session through
  `AnimControllerComponent`; a clip with param tracks round trips through save/load; an old clip
  loads and plays exactly as before.
* Slice 2: the diamond shows the three states, Set Key / Delete Key work and undo both, a shared
  material logs the warning, the standalone material window shows no diamond.
* Slice 3: param rows resolve (including a dotted entity name and a material slot index), drag moves
  a param key and undo restores it, `[no owner]` shows for a track whose entity is gone.
* Step types: a `Visible` track with true at frame 0 and false at frame 30 hides the entity at 30 and
  keeps it hidden past the end of the track, and going back to frame 0 shows it again. Two things can
  make a bool key look dead while it is not: a track with a single key (nothing to step to) and
  `Entity::IsVisible`, which also consults the prefab root, so a hidden prefab root masks the flag of
  an instance.

## 8. Open points

1. Parameter rows in the sheet: is a value readout (and later inline editing) wanted in slice 3, or
   only keys.
2. Material slot ownership: `MaterialView` needs the owning entity and component passed in from
   `PropInspectorWindow` (slice 2). The standalone material window keeps its current look and simply
   has no diamonds.
3. Where the shared material warning is produced: the count of slots in the scene that use a material
   is an editor side scan, done once per Set Key press, not a runtime concern.
