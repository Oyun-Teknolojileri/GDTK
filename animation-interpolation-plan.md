# Animation Interpolation (Node Tracks) -- Implementation Plan

> Working plan for smooth key interpolation on **node / entity tracks**.
> Companion to `dope-sheet-plan.md` (its phase 4 "Curve view / easing" item).
> Status: all review decisions closed (see 2, 2.3 and 10), work not started.
> Scope: **CPU node tracks only.** The skeleton path and the GPU animation data texture are
> explicitly out of scope (see 9).
> Keep this file in sync while the work is in progress; fold the result into
> `gdtk-overview.md` section 6.3.1 once it lands.

---

## 1. Goal

Today a track is a polyline: two neighbouring keys are joined by a straight line
(`Interpolate` for position / scale, `glm::slerp` for rotation). Velocity therefore jumps at
every interior key -- the motion "snaps" from one segment to the next, which reads as a hard
tick in the viewport.

The goal is the standard fix: give an interior key a **tangent**, so the segment before it and
the segment after it leave / arrive with the same velocity and the motion blends through the
key instead of breaking at it.

Delivered in phases, each one usable on its own:

| Phase | Content | State |
|---|---|---|
| A | Engine: per-key interpolation mode (`Stepped` / `Linear` / `Smooth` / `Flat`), auto clamped tangents, one shared sampler, file compatibility | agreed, not started |
| B | Editor preview delegates to the engine sampler (preview == runtime) | agreed, not started |
| C | Dope sheet: per-key mode edit (undoable, new keys stay `Linear`), key shape + hold bar, "Smooth All Keys" | agreed, not started |
| D | Free (hand edited) tangents + curve view, loop seam tangents | later |

## 2. Decisions

| # | Question | Decision |
|---|---|---|
| 1 | Scope | Node / entity tracks on the CPU path. No GPU work, no skeleton path. |
| 2 | Rotation | `slerp` with an eased parameter (no `squad`, no quaternion tangents). |
| 3 | Existing clips | Must look **exactly** as they do today. Default mode is `Linear`, and today's files keep loading. |
| 4 | Stepped keys | Needed. It is a third explicit mode. |
| 5 | Overshoot | Clamped (auto clamped, no overshoot) is the default; free / overshoot capable tangents arrive in phase D. |
| 6 | UI | Per-key mode selector first; draggable handles later. |
| 7 | Default mode | `Linear` **everywhere**: legacy files, importer output, and newly created keys. A key becomes `Smooth`, `Stepped` or `Flat` only when the user asks for it (7.1). |
| 8 | `Flat` mode | A fourth mode: zero velocity at that key, usable on any key, not only the clip ends (2.3). |
| 9 | Ease in / out | **No separate feature.** A soft start / stop is just the first / last key set to `Flat`. No clip level flag, no button, no per-segment easing preset library (2.3). |

### 2.1 Answer to the "is stepped the default?" question

No. **Today nothing is stepped, everything is linear.** `GetNearestKeys` hands out two keys plus
a ratio and the sampler draws a straight line between them, so a key that the user never touched
already interpolates smoothly (linearly). Stepped means *hold the previous key's value until the
next key's frame and then snap* -- a third behaviour that has to be asked for explicitly, which
is why it becomes a mode.

### 2.2 Derived decisions (needed to make 1-9 concrete)

1. **The mode lives on the key, but the shape of a segment is decided by both of its endpoints:**
   - `Stepped` on either endpoint wins outright (hold, 5.2),
   - else `Linear` on either endpoint means the segment is straight,
   - else both endpoints are `Smooth` / `Flat` and the segment is a cubic, where `Flat` pins that
     endpoint's tangent to zero and `Smooth` derives it from the neighbours (5.3).
   Two sided on purpose: with one tangent per key (5.3) `Flat` is inherently two sided, and a
   single sided ("the left key rules") rule would let a `Flat` key sit next to a `Linear` one and
   produce exactly the velocity jump it exists to remove. The mode still rides inside `Key`, so the
   editor's existing `KeyEditAction` snapshot / restore covers a mode edit with no new action class
   (see 6).
2. **One sampler, one source of truth.** The engine gets a public track sampler; the runtime node
   path, root motion and the dope sheet preview all call it. The preview must not keep its own
   copy of the math, or the sheet and playback drift apart.
3. **The math goes into a header-only, engine light unit** so it can be verified numerically with
   a single `g++` command, without a window, a backend or a scene (see 8).
4. **Naming:** `Stepped` / `Linear` / `Smooth` / `Flat`. UI labels: `Stepped`, `Linear`,
   `Smooth (Auto)`, `Flat`.

### 2.3 `Flat` semantics

`Flat` means **zero velocity at that key, on both sides**. Which side is felt depends on where the
key sits:

| Key | Left side | Right side | Felt as |
|---|---|---|---|
| First key of a clip | nothing before it | accelerates from rest | soft start |
| Last key of a clip | decelerates to rest | nothing after it | soft stop |
| Interior key | decelerates to rest | accelerates from rest | a brief settle / hold |

For a two key clip this gives the classic shapes: `Flat` + `Smooth` = `f(u) = u*u*(2-u)` (soft
start, arrives at speed), `Flat` + `Flat` = `f(u) = 3u*u - 2u*u*u`, i.e. smoothstep.

An interior `Flat` key is a *pause*, not a break: the velocity is continuous, it merely passes
through zero, and the position still reaches the key's value exactly on its frame. On a key that
is already a local extremum the auto rule has produced a zero tangent anyway, so `Flat` changes
nothing there; it is only visible on monotone passes.

At the clip ends `Flat` is mathematically identical to the "ease in / ease out" of other tools,
which is why no separate mechanism exists for those (decision 9). The difference is only scope:
`Flat` works on any key, an ease flag would only ever touch the two ends.

**Not expressible in this model:** a *broken* tangent (zero on one side, fast on the other, the
impact / anticipation pattern). One key carries one tangent (5.3). That needs phase D's `Free`
mode with separate in / out tangents.

## 3. Facts verified in code

| Fact | Where |
|---|---|
| `Key` = `{frame, position, rotation, scale}`, no tangent, no mode | `ToolKit/Resources/Animation.h:28` |
| Node sampling is `Interpolate` + `glm::slerp` on exactly two keys | `ToolKit/Resources/Animation.cpp:71-73` |
| `GetNearestKeys` returns the bracketing pair plus a ratio | `ToolKit/Resources/Animation.cpp:327-379` |
| Missing time before / after the key range clamps to the ends (ratio 0 / 1, no extrapolation) | `ToolKit/Resources/Animation.cpp:345-362` |
| Runtime node path: `AnimationPlayer::Update` -> `Entity::SetPose` -> `GetPose(Node*)` | `Animation.cpp:631`, `Entity.cpp:84-107` |
| Root motion samples through its own inline lambda with the same two key lerp | `ToolKit/Resources/Animation.cpp:672-688` |
| Skeleton sampling is a separate code path (out of scope, stays linear) | `ToolKit/Resources/Animation.cpp:78-127` |
| The editor preview has its **own** copy of the same two key lerp | `Editor/UI/View/DopeSheetView.cpp:393-419` |
| Sheet creates keys with all three channels in one `Key` | `Editor/UI/View/DopeSheetView.cpp:637-644` |
| Undo state stores whole `Key` values, so a mode edit is undoable for free | `Editor/Source/Action.h:136-141`, `Action.cpp:194-265` |
| Keys are serialized as base64 of the raw `Key` array | `ToolKit/Resources/Animation.cpp:211-214`, `:251-258` |
| Real files on disk use that layout: `KeyCount="36"`, 1584 decoded bytes = **44 bytes per key** | `BinDebug/Temp/Meshes/Character/Linear-Patrol/recorded_clip.anim` (measured) |
| The importer writes keys with no mode field | `Utils/Import/import.cpp:500`, `:591` |
| Nothing else assumes `sizeof(Key)` | only `Animation.cpp:214` |

## 4. Data model

### 4.1 The mode field

```cpp
// ToolKit/Resources/Animation.h, above struct Key
/**
 * How a key takes part in the interpolation of the segments around it.
 */
enum class KeyInterp : uint8
{
  Stepped = 0, //!< Hold this key's value until the next key, then snap.
  Linear  = 1, //!< Straight line / slerp: the pre 1.x behaviour.
  Smooth  = 2, //!< Cubic tangent derived from the neighbouring keys.
  Flat    = 3  //!< Cubic with a zero tangent at this key (a settle / soft end).
};

struct Key
{
  int m_frame = 0;       //!< Order / Frame of the key.
  Vec3 m_position;       //!< Position of the transform.
  Quaternion m_rotation; //!< Rotation of the transform.
  Vec3 m_scale;          //!< Scale of the transform.

  /**
   * Interpolation of the segment from this key to the next one. Appended after the
   * existing members on purpose, see 4.2.
   */
  KeyInterp m_interp = KeyInterp::Linear;
};
```

`Linear` is the default, so every existing construction site (importer, XML reader, editor code
that fills a `Key` field by field) keeps today's behaviour without being touched. The dope sheet
follows the same rule for the keys it creates (decision 7, see 7.1), so no key becomes `Smooth`,
`Stepped` or `Flat` anywhere unless a user says so.

`Key` stays trivially copyable, which the serializer and the physics of `std::vector<Key>` rely
on.

### 4.2 File compatibility (the important part)

The base64 blob is a raw struct dump: `keyCount` times `sizeof(Key)`. Appending a field changes
that stride from 44 to 48 bytes, and a legacy file read with the new struct would be read at the
wrong offsets -- silently, as garbage.

The reader therefore stops assuming the stride and **derives it from the file**:

```cpp
// b64tobin() writes, so the size has to come from the string itself:
// 4 base64 digits decode to 3 bytes, minus one byte per trailing '='.
const char* b64      = b64Node->value();
const size_t b64Len  = strlen(b64);
size_t padding       = 0;
if (b64Len > 0 && b64[b64Len - 1] == '=')
{
  padding            = (b64Len > 1 && b64[b64Len - 2] == '=') ? 2 : 1;
}
const size_t decoded = (b64Len / 4) * 3 - padding;
const size_t stride  = keyCount > 0 ? decoded / keyCount : 0; // 44 for a legacy file, sizeof(Key) now

if (keyCount == 0 || decoded % keyCount != 0 || stride > sizeof(Key))
{
  TK_ERR("Animation track \"%s\" holds a key block this build cannot read.", boneName.c_str());
  continue; // leave the track empty instead of reading garbage
}

std::vector<char> raw(decoded);
b64tobin(raw.data(), b64);

keys->resize(keyCount); // value initialises, so m_interp is Linear
for (uint i = 0; i < keyCount; ++i)
{
  // Copy only the bytes the file actually holds; the rest keep the struct defaults.
  memcpy(&(*keys)[i], raw.data() + i * stride, stride);
}
```

This works because of one rule, which has to be written down next to the struct:

> **New `Key` members are appended at the end (`m_frame, m_position, m_rotation, m_scale`
> prefix is frozen). The existing members are never reordered, retyped or removed.**

With that rule the byte prefix of an old record maps onto the new struct unchanged, and every
appended field keeps its default. A future phase D that adds tangent members gets the same
compatibility for free, with no version counter to maintain.

The XML branch (`SerializeImp` / `DeSerializeImp` when `KeyCount` is absent) gets an optional
`interp` attribute; a missing attribute means `Linear`.

### 4.3 What stays identical

- The `std::vector<Key>` layout of the prefix, `m_frame` semantics, sorted-by-frame invariant.
- `Animation::SetKey`, `CopyTo`, `m_fps`, `m_duration`, `m_rootKey`: unchanged.
- The `Linear` evaluation path: same `Interpolate` / `glm::slerp` calls with the same ratio value,
  so a clip made of `Linear` keys produces the same numbers as before bit for bit. This is the
  regression test for decision 3.

## 5. Sampling

### 5.1 Shared sampler

```cpp
// ToolKit/Resources/Animation.h
/**
 * Samples one track at the given time, honouring each key's interpolation mode.
 * Used by the runtime node path, root motion and the editor preview.
 */
bool SampleTrack(const KeyArray& keys, float time, Vec3& pos, Quaternion& rot, Vec3& scale) const;
```

It calls `GetNearestKeys` (unchanged and still public, the skeleton path keeps using it), then
evaluates the bracketing segment according to the mode. `false` when the track is empty or the
indices do not resolve, which is what the editor's private `SampleTrack` already returns.

Notation below: keys `k0..kn-1`, `t_i = frame_i / fps`, segment `i` = `[t_i, t_i+1]`,
`d_i = t_i+1 - t_i > 0` (guaranteed: `SetKey` keeps frames unique and ascending), local parameter
`tau = (t - t_i) / d_i`, values `p` / `q` / `s`.

### 5.2 Segment rule

The shape of the segment between `k_i` and `k_i+1` comes from **both** of its endpoints, the hard
modes winning over the curve modes:

| Endpoints | Segment |
|---|---|
| `Stepped` on either side | hold `k_i`'s values. `tau >= 1` yields `k_i+1`'s values exactly |
| else `Linear` on either side | today's `Interpolate` / `glm::slerp` with `tau` |
| else (both `Smooth` / `Flat`) | cubic Hermite (position, scale) and eased `slerp` (rotation), 5.3 / 5.4. `Flat` pins that side's tangent to 0, `Smooth` derives it |

`tau >= 1` returns the second key's exact values for every mode. (`GetNearestKeys` reports the
earlier segment when the time lands exactly on a key, which for `Stepped` would otherwise hold a
frame too long.)

Two consequences worth stating: a legacy clip is all `Linear` and therefore draws exactly the same
lines it draws today (decision 3), and a `Flat` key next to a `Linear` neighbour does nothing
rather than producing the velocity jump it exists to remove (2.2 item 1).

### 5.3 Position and scale: cubic Hermite with auto tangents

```cpp
h00 =  2*tau^3 - 3*tau^2 + 1;
h10 =      tau^3 - 2*tau^2 + tau;
h01 = -2*tau^3 + 3*tau^2;
h11 =      tau^3 -     tau^2;

p(tau) = h00*p_i + h10*d_i*T_i + h01*p_i+1 + h11*d_i*T_i+1;   // per component
```

`T_i` is a single tangent vector per key used as the **out** tangent in segment `i` and as the
**in** tangent in segment `i-1`. Because both segments then share `T_i`, the velocity is
continuous at `k_i` by construction -- that is the whole point of the feature.

Auto tangent (interior key, per component):

```
a = (p_i   - p_i-1) / d_i-1      // incoming secant, per second
b = (p_i+1 - p_i  ) / d_i        // outgoing secant, per second

a*b <= 0  -> T = 0               // local extremum or flat: flatten (Maya Auto / Blender Auto
                                 // Clamped / Unity Clamped Auto behaviour, kills the overshoot)
else          T = (a + b) / 2
```

Ends have one neighbour only, so they take that segment's secant (`T = b` for the first key,
`T = a` for the last one): the clip starts and ends at the segment's own linear speed, which is
what the packages do for a single sided derivative. A `Flat` key overrides this with `T = 0`
regardless of how many neighbours it has, which is what makes a soft start / stop possible at all
(2.3) -- `Smooth` on an end key cannot produce one.

Clamping (the exact form of "no overshoot"): a cubic whose two end slopes are both inside
`[0, 3]`, measured in units of the segment's secant, cannot leave the value range of its endpoints.
So for interior key `i`, per component, with the two neighbouring secants `a` (incoming) and `b`
(outgoing):

```
a*b <= 0                        -> T = 0            // flattened, as above
else       T = (a + b) / 2
           |T| <= 3 * min(|a|, |b|)                 // the monotonicity limit
```

**The limit is applied to the key, not to the segment.** That matters: a key carries one tangent and
it has to be valid for the segment on either side of it, so the cap comes from the smaller of the two
secants. Limiting a segment's two tangents independently (the textbook per-interval form of the
Fritsch-Carlson filter, which scales the pair of a segment) would let one side clamp while the other
does not, and the velocity would jump at the key -- exactly what this feature exists to remove. The
check program covers this with an uneven key spacing case.

The limit is applied **per component**, like the DCC packages do per channel: a key that peaks on
X while Y keeps rising flattens X only. (Rejected alternative: one shared scale factor across the
three components, which would freeze an axis that is still moving.)

### 5.4 Rotation: slerp with an angular speed matched parameter

The parameter of the slerp is eased instead of linear, and the easing is derived from the
**angular** budget of the neighbouring segments, so the angular velocity is continuous at the key
without touching quaternion tangents:

```
angle_i = angle(q_i, q_i+1)          // hemisphere aligned first: if dot(q_i, q_i+1) < 0, use -q_i+1
                                     // (the same convention glm::slerp applies internally)
w_i     = angle_i / d_i              // this segment's linear angular speed, rad/s
W_i     = (w_i-1 + w_i) / 2          // the key's auto angular speed (single neighbour at the ends)
W_i     = min(W_i, 3 * min(w_i-1, w_i))   // the same per key limit as the position tangents

segment i:  a = W_i   / w_i          // de/dtau at tau = 0
            b = W_i+1 / w_i          // de/dtau at tau = 1
            k_i   is Flat -> a = 0   // leaves from rest
            k_i+1 is Flat -> b = 0   // arrives at rest
            a = clamp(a, 0, 3); b = clamp(b, 0, 3)   // keeps e monotone inside [0, 1]

e(tau) = h10*a + h01 + h11*b;
rot    = slerp(q_i, q_i+1, e(tau));
```

The speed limit applies to `W_i`, so both sides of the key read the same speed and the angular
velocity is continuous there. Clamping each segment's slope on its own (the obvious first
implementation) makes the two sides disagree whenever the segments have very different speed, and
that is a visible rotation snap.

Properties:

- Uniform case (`w_i-1 == w_i == w_i+1`): `a = b = 1`, `e(tau) = tau`, i.e. plain slerp. The
  segment speeds come out of an `acos` of a dot product, so "uniform" can still differ by an ULP and
  the parameter is then `tau` plus a rounding step; the result matches plain slerp to within float
  rounding rather than bit for bit (legacy clips are all `Linear` and do match bit for bit).
- Slow approach (`w_i-1 < w_i`): `a < 1`, the parameter lags behind the diagonal and catches up;
  a fast exit (`w_i+1 > w_i`) does the same at the far end (`b > 1`). That is exactly the "blend
  through the key" this feature is for.
- At the ends `W` is the single segment's own speed, so `a = 1`: the clip starts (and stops) at its
  linear angular speed, matching 5.3.
- `Flat` on an endpoint forces the matching slope to zero, so the rotation comes to rest / leaves
  from rest at the same key the position does. Without this the position would ease while the
  rotation snapped into speed.
- `e` stays inside `[0, 1]`, so rotation never overshoots past the next key: the clamped policy of
  decision 5 holds for rotation too.
- Angular velocity is continuous at the key by construction: both sides target `W_i`.

Position and rotation easing do not have to agree at a key (the position tangent comes from
linear velocity, the rotation parameter from angular velocity). That is the same situation as a
DCC animating X/Y/Z and rotX/rotY/rotZ as separate curves, and it is the reason rotation does not
need quaternion tangents to look smooth.

### 5.5 Boundaries

- Before the first / after the last key: `GetNearestKeys` clamps to a segment with ratio 0 or 1.
  Every mode yields the corresponding end key's exact values, so there is no extrapolation.
- Loop seam: a looping clip still clamps at both ends, so the wrap can show a small hitch. Making
  the end tangents wrap (`k0` sees `kn-1` as its neighbour and the other way round) needs to know
  whether the record loops, which the `Animation` resource does not know today -- phase D, behind
  a clip level flag.

### 5.6 Stepped semantics

`Stepped` holds the key's values for the whole segment and the next key's values appear exactly on
the next key's frame. Position, rotation and scale all hold together (a `Key` is monolithic, see
`dope-sheet-plan.md` 2.1). This is the "deliberate hard stop" mode: impact frames, stop motion.

### 5.7 Cost

Auto tangents need the bracket plus one neighbour on each side for the two tangents, so a `Smooth`
sample touches 4 keys (`Flat` and `Linear` endpoints need no neighbour lookup at all), does a handful of divisions and (for rotation) two `angle` calls. Node
tracks are sampled once per animated entity per frame, so this is noise next to the pose write
itself. **No caching in phase A.** If a profile ever asks for it, cache per `(track, key index)`
behind a dirty flag, not globally.

## 6. Touch list

| File | Change |
|---|---|
| `ToolKit/Resources/KeyInterpolation.h` | **new**, header only: `KeyInterp`, the auto tangent + `Flat` override + clamp + Hermite + eased parameter math on glm types. No engine includes, so it can be compiled standalone (8) |
| `ToolKit/Resources/Animation.h` | `KeyInterp` + `Key::m_interp` (appended last), `SampleTrack` declaration, doc comments |
| `ToolKit/Resources/Animation.cpp` | `SampleTrack` implementation; `GetPose(Node*, time, keyName)` (38) routes through it; root motion lambda (672-688) routes through it; `SerializeImp` (211-214) writes the mode with the raw keys; `DeSerializeImp` (251-258) gets the stride based expander of 4.2; XML branch reads the optional `interp` attribute |
| `ToolKit/Resources/Animation.cpp:78-127` | **untouched** (skeleton path stays linear, decision 1) |
| `Editor/UI/View/DopeSheetView.cpp:393-419` | `SampleTrack` body becomes a delegate to `m_clip->SampleTrack(...)`, its own lerp is deleted |
| `Editor/UI/View/DopeSheetView.cpp:637-644` | `SetKeyOnSelection` writes `key.m_interp = m_newKeyInterp` (`Linear` unless the animator changed the combo) |
| `Editor/UI/View/DopeSheetView.cpp` (`ShowLanes`) | key shape per mode (4) + hold bar for `Stepped` |
| `Editor/UI/View/DopeSheetView.cpp` (`##dopeSheetRowCtx`, 1490-1523) | `Interpolation` submenu: `Stepped` / `Linear` / `Smooth (Auto)` / `Flat` |
| `Editor/UI/View/DopeSheetView.cpp` (`ShowKeyTools`) | `New key` combo (4 modes, default `Linear`) + `Smooth All Keys` (one undo group) |
| `Editor/UI/View/DopeSheetView.h` | `KeyInterp m_newKeyInterp = KeyInterp::Linear;` (session preference, 7.1), helper declarations |
| `Editor/Source/Action.h` / `.cpp` | optional convenience `KeyEditAction::SetInterp(clip, track, frame, KeyInterp)` -- read the key, change the mode, hand it to the existing `SetKey`. No new action class, `FrameState` already carries the whole `Key` |
| `Utils/Import/import.cpp:500`, `:591` | **unchanged**: the importer's keys keep the `Linear` default, so imported clips look exactly as they do today (they are baked per frame anyway, so tangent shaping would be invisible) |
| `dope-sheet-plan.md:438` | cross reference this file (done) |
| `gdtk-overview.md:428-457` | update when phase A lands: the `Key` description and the sampling paragraph |

## 7. Dope sheet UI (phase C)

1. **New keys are `Linear`** (decision 7: the default never changes on its own). A key becomes
   `Smooth`, `Stepped` or `Flat` through the per-key edit, or the animator opts into a smooth
   authoring session with the "new key" combo in the key tools row (`New key: Stepped / Linear /
   Smooth / Flat`, defaulting to `Linear`). The combo is a session preference only; it writes
   nothing until the next `Set Key`.
   *Consequence to keep in mind:* a hard mode on either endpoint wins (5.2), so a `Linear` key
   dropped into the middle of a `Smooth` curve makes **both** segments around it straight -- the
   key becomes an anchor on its left and on its right. Setting that key to `Smooth` restores both,
   which makes it the one click fix, and the combo is how to avoid it while keying a smooth
   sequence in the first place.
2. **Per-key edit** through the existing row context menu: `Interpolation` submenu with the four
   modes applying to the selected key, through `KeyEditAction` so it is one undo step. Multi key
   edit comes with the bulk selection phase, not before.
3. **Visual language** in the lanes: `Smooth` keeps the diamond, `Linear` a flat topped marker,
   `Stepped` a square, `Flat` a diamond with a horizontal bar through it (a "no speed here" mark),
   and a `Stepped` key draws a horizontal hold bar to the next key (the classic dope sheet read for
   "this does not move until here"). Colour stays what it is.
4. **`Smooth All Keys`** button: sets every key of every track to `Smooth` in one undoable group.
   With decision 7 (new keys are `Linear`) this is the main way to smooth a clip that was authored
   before this feature, or keyed without the smooth combo on.
5. **No curve view / no draggable handles** in this phase. The view stays a sheet; a curve editor
   with handles is part of phase D and needs the tangent members that phase D adds.

## 8. Verification (phase A gate)

The repo has no test target (`add_test` / gtest / catch2 do not exist), and `ToolKit/**/*.cpp` is
globbed recursively into the library, so a test file inside `ToolKit/` would be linked into the
engine. Verification is therefore a **throwaway standalone check program**, written outside the
tree, compiled by hand against the header-only math unit and deleted once the phase is verified
(decision: no test target, no committed test file):

```bash
g++ -std=c++17 -I ToolKit/Source -I <vendored glm include> /tmp/key_interp_check.cpp -o /tmp/key_interp_check
/tmp/key_interp_check
```

For the serialization rows the same file can be compiled against the engine instead (link
`Bin<Config>/libToolKit<d>.so`) when the stride expander needs a real file; the samples under
`BinDebug/Temp/Meshes/` are 44 byte legacy blobs and are the natural input for that check.

The program builds small `KeyArray`s in memory and asserts, with no engine and no window:

| Property | Assertion |
|---|---|
| Decision 3, old clips | A track of `Linear` keys sampled at 100 sub frame times matches the old `Interpolate` / `glm::slerp` result exactly (same floats) |
| C1 at an interior key | Finite difference of the sampled position just before and just after a `Smooth` key agrees within 1e-3 |
| C1 with uneven spacing | Same test on a track whose segment lengths differ by 25x, where a per segment limit would break down and a per key one holds |
| Rotation C1 | The same finite difference on the angular speed of a `Smooth` rotation track, and a uniform one reproduces plain slerp within 1e-6 |
| C1 for rotation | Same, on the angular distance between consecutive samples |
| No overshoot | The sampled position never leaves `[min(p_i, p_i+1), max(...)]` per component on a `Smooth` segment |
| Local extremum | A key that peaks on one component gets a zero tangent on that component (flat) while the other components keep the averaged tangent |
| Stepped | Every sample before the next key equals the first key's values, and the value on the next key's frame equals that key's values |
| Flat, soft start | First key `Flat` + second key `Smooth` gives `f(u) = u*u*(2-u)`: the sampled speed at `u = 0` is 0 and the value reaches the second key exactly |
| Flat, soft stop | Last key `Flat`: the sampled speed at the end is 0, approaching it monotonically |
| Flat, both ends | Two keys, both `Flat`, gives `f(u) = 3u*u - 2u*u*u` (smoothstep) within 1e-6 |
| Flat, interior | An interior `Flat` key with `Smooth` neighbours has velocity 0 on both sides (finite difference) and still hits the key's exact value on its frame |
| Flat next to Linear | `Flat` on one endpoint, `Linear` on the other: the segment is identical to the `Linear` line (hard modes win, no velocity jump) |
| Uneven spacing | Keys 10 frames apart next to keys 2 frames apart still produce a monotone parameter (`e` inside `[0, 1]`) |
| Endpoints | `tau` 0 and 1 reproduce the end keys exactly for every mode |
| Serialization | A legacy 44 byte stride blob decodes into the same values with `Linear` modes; a round trip through `SerializeImp` / `DeSerializeImp` preserves the modes |

Then, in the editor (manual): open a clip, set a middle key to `Smooth`, scrub across it, and
confirm the preview shows the same blend the runtime plays (phase B is what makes that true).

## 9. Non goals and later phases

**Not in this work:**

- The skeleton / bone path and `Animation::GetPose(SkeletonComponentPtr)` (a `Key` mode is
  ignored there; the dope sheet already refuses to key skinned tracks).
- The animation data texture and `skinning.shader` (GPU skinning keeps its two matrix `mix`).
- `squad` / exact geodesic continuity, quaternion tangents, per-channel key storage.
- Splitting a `Key` into separate T/R/S curves.
- Root motion *behaviour*: it only inherits the smooth sampler. Note that a cubic preserves the
  end values, so the displacement accumulated over a finished segment is unchanged; only its
  distribution inside the segment changes.

**Phase D (later, same mechanism extends):**

1. `Free` mode + `m_inTangent` / `m_outTangent` members: draggable handles in a curve view, and the
   only way to get a *broken* tangent (zero on one side, fast on the other, 2.3). The serialization
   rule of 4.2 already covers the new members.
2. Loop seam tangents behind a clip level flag (`k0` wrapping to `kn-1`).

**Explicitly not planned:** a separate ease in / out mechanism (clip flag, button, preset) and a
per-segment shape preset library (`Sine`, `Quad`, `Back`, `Elastic`). A soft start or stop is the
end key set to `Flat`, and that is the whole feature (decisions 8 and 9).

## 10. Settled during review

1. **Check program:** throwaway under `/tmp`, not committed, deleted once phase A is verified.
   No test target, no `ToolKit/Tests/` (see 8).
2. **Default mode:** `Linear` everywhere, including newly created keys. `Smooth` / `Stepped` /
   `Flat` are opt-in per key (or through the sheet's session combo / `Smooth All Keys`). See
   decision 7 and 7.1.
3. **Modes:** four of them, `Stepped` / `Linear` / `Smooth` / `Flat`, with the two sided segment
   rule of 5.2. `Flat` is a per key property, not a clip property (decision 8).
4. **Ease in / out:** no separate feature. `Flat` on the first / last key *is* the soft start and
   stop, and it is mathematically identical to what an ease flag would do (2.3), so the clip level
   flag, the button and the preset library all stay out (decision 9).
5. **Clip ends under `Smooth`:** the first and last key keep the single neighbour's secant (5.3),
   so `Smooth` alone never eases the ends -- which is exactly why `Flat` exists.
