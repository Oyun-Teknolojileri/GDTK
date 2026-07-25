# GDTK Render Instancing Refactor - Roadmap

> **Status:** Draft v9 (design phase, pending review).
> **Targets:** Native mobile (GLES 3.2, see `ToolKit/Render/OpenGL/TKOpenGL.h` -> `GLES3/gl32.h`) and WebGL 2.0; Vulkan remains a first-class desktop/backend target.
> **Related docs:** `AGENTS.md` (coding standards, Pass/PassRequirements conventions, overview-sync rules), `gdtk-overview.md` (architecture), Section 4 (Rendering Subsystem).
> **Goal:** Collapse per-object draw calls and per-object state changes into a small number of instanced draws, without rewriting the renderer.

### Changelog

- **v9** - Light store redesign (correctness for instancing). The engine's **LRU-based light cache UBO** (`PointLightCache`/`SpotLightCache`, capacity 32) is incompatible with the instanced path: per-instance light indices are stored in instance data and must remain valid for the whole frame, but LRU evicts mid-frame. -> Replaced by a **persistent, frame-stable light data buffer** (id-indexed, no eviction, generation-dirty via `Light.gen`). Per-instance light-index lists point into it by stable id. Fixes the v8 §1.3 assumption ("reused directly") that was wrong. Also removes the 32-capacity UBO limit (texture/SSBO holds thousands). Lights do NOT need generational handles (per-instance index lists are frame-local, so no cross-frame staleness) - only frame-stability + generation-dirty update.
- **v8** - Seventh external review (~9.7-9.8/10). Execution-detail tightening (not architecture): (1) **Handle generation vs resource generation separated** - `RenderObjectHandle.generation` = table-slot lifetime (stale-handle detection); the CPU-side `RenderObjectEntry` caches the resource gens it last validated against (Material/Env/Skeleton). (2) **Resource param changes update table/texture rows only, never instance rows** (a material color tweak -> one material-table upload; `RenderObject` + instances untouched) - fixes a §2.5 path that would have caused mass instance uploads on a color tweak. (3) `static_assert(alignof(InstanceRecord) >= 16)` added (`sizeof%16` alone does not guarantee alignment for SSBO/Vulkan). (4) **`InstanceTransport` backend-capability enum** (`TextureFetch` / `SSBO` / `VertexBuffer`) + Phase 1 VTF-vs-vertex-attribute A/B metrics -> data-driven fallback, no renderer rewrite. (5) **Batch fragmentation score** formula `(actual-ideal)/max(ideal,1)` (0 = perfect). (6) `RenderItemCount` metric (sort is O(N log N)). (7) Sort-identity-vs-storage-identity noted (content-hash sort key as a Phase 4+ option). (8) `RenderItem` as the scene->renderer seam (optionally pre-wired in the legacy path).
- **v7** - Sixth external review (~9.6/10, "implementation-ready"). Pre-Phase-2 consistency fixes (not architecture): (1) **`RenderObjectHandle.generation` -> `uint64`** (matches resource gen width). (2) **RenderObject identity is immutable** - a material/env/skeleton *swap* creates a new RenderObject + handle; a *parameter* change bumps the resource gen (RenderObject unchanged). (3) RenderObject identity clarified as (material+env+skeleton), **excluding mesh** (mesh lives in the `BatchKey`). (4) Phase 2.5 image-diff uses **tolerances** (RMS < 1/255, max channel <= 2), not "zero pixels". (5) Phase 1 CPU metric split: `BatchBuildCPUTime` / `RenderItemSortCPUTime` / `InstanceUploadCPUTime`. (6) **Phase 2.75 - single-batch instancing** (1000 identical, `drawElementsInstanced(1000)`, no sort) - isolates instanced-draw mechanics before batching. (7) InstanceRecord mirroring: explicit `static_assert(sizeof % 16 == 0)` + `offsetof(model)==0`. (8) Phase 3 instance indices documented as transient frame-local (stable identity arrives with Phase 4 page allocator). (9) Light offset+count inline (vs table lookup) noted as a Phase 2b/3 fragment-heavy optimization.
- **v6** - Fifth external review (second reviewer, ~9.7/10) + first reviewer's open-decision resolutions folded in. Architecture declared implementation-ready; remaining items are implementation-discipline. (1) **`RenderObjectHandle {index, generation}`** + ownership invariant: `Entity -> RenderComponent -> RenderObjectHandle`; `BatchBuilder` never creates/destroys RenderObjects (documented invariant). (2) **RenderObject identity = shader-visible only** - debug/editor/CPU metadata never enters identity (anti-fragmentation rule). (3) **`InstanceDataBuffer` API byte-oriented** (`offset + size + data`), no "instance row" leak. (4) **`baseInstance` designed day-one** (unlocks Phase 8 indirect). (5) **Phase 2.5 - shader transport validation** (full-scene `instanceCount=1` + image-diff vs legacy) - isolates transport correctness before batching. (6) **Batch fragmentation metric** (`actual/ideal`) in Phase 1. (7) **Single-buffer = production baseline** (Phase 6 strictly profiling-gated). (8) **Light list = interleaved fixed-stride for v1** behind a `LoadLightList` abstraction (split/forward+ layout is a drop-in swap later). (9) Instrumentation overlay-vs-logged split + `FrameStatType` naming. (10) Per-batch-contiguous allocator for v1 (page allocator behind Phase 4 gate). (11) Tiered mobile profiles (`TK_MOBILE_LOW` / `TK_MOBILE_HIGH`) calibrated from Phase 1. (12) Graceful fallback: instance-budget overflow -> excess instances to legacy per-draw (no OOM). AGENTS.md additions queued.
- **v5** - Fourth external review. (1) **Double-buffer correctness rule:** dirty rows enter a pending-write set and are uploaded to the current write-buffer each frame until BOTH buffers hold the new value (carried across one swap); the read-buffer is never written during its read frame - fixes an every-other-frame flicker for dirty-then-static instances. (2) **Profile-driven instance-texture sizing** (from a max-instance budget x STRIDE, not a fixed 2048x2048; ~1.25 MB for ~16k mobile instances vs the previous 64 MB). (3) **`RenderObject` lifecycle clarified:** persistent stable handles held by the source entity, generation-tracked, NOT rebuilt via per-frame hash (preserves "sort, don't hash" on the RenderObject side too); env stays on `RenderObject`, reassigned only on env-boundary crossing (rare, dependency-graph detected). (4) **WebGL2 vertex-stage texelFetch (VTF) bandwidth** added to the risk table + an A/B GPU-time metric in Phase 1. (5) **Dead deferred render partitions** (`RenderData` partitions 1-2, confirmed unused) scheduled for removal in Phase 0. (6) **WebGL2 RGBA32F** confirmed core-supported for creation + nearest (texelFetch) sampling - no extension needed.
- **v4** - Third external review (9.8/10, "no serious architectural problem"). Final polish: (1) `renderObjectIndex` removed from `InstanceRecord` (it is batch-uniform, already in the `BatchKey`) - the record is now self-consistent; `RenderObject` is accessed via `LoadRenderObject()` (backend-impl-detail: per-draw uniform/UBO today, buffer/SSBO read as it grows). (2) Generations are `uint64` (CPU-side validation only). (3) `BatchKey` is `alignas(32)` for SIMD compare. (4) Added "Future extensions" subsection (compressed transforms; material/pipeline-system refactor).
- **v3** - Second external review incorporated. Key changes: (1) **`RenderObject` / `InstanceRecord` split** - shared (batch-uniform) handles hoisted into a `RenderObject` (the stable extension point for future scene features); `InstanceRecord` keeps only per-instance-varying data. (2) **Resource-side generation / dependency graph** replaces object-side dirty flags. (3) **Fixed-stride light list.** (4) **Sort-based batching** (memcmp key, no hashing) with **`RenderItem`** output. (5) **Instrumentation phase** added as a prerequisite (Phase 1). (6) **Native multi-draw-indirect** fast-path (Phase 8). (7) Per-instance **inverse-matrix heuristic.** (8) Explicit **table lifetimes.** Phase numbers shifted (instrumentation is now Phase 1).
- **v2** - First external review: lean `InstanceRecord`, `LoadInstance`, RenderState in batch key, generation-based validation, separate `BatchBuilder`.
- **v1** - Initial grounded draft.

---

## 0. Summary & Core Reframe

**This is not a new renderer.** The engine already computes everything an instanced pipeline needs. The refactor changes the **transport** of that data and the **draw granularity**:

- **From:** per draw call -> one `Renderer::Render(job)` per `RenderJob` -> one `BindPipeline` + one per-draw UBO upload (`FeedUniforms`) + one `Draw`.
- **To:** per batch -> one `drawElementsInstanced` per batch (keyed by mesh + shader-variant + texture-set + render-state + render-object), reading per-instance records from an instance data buffer indexed by `gl_InstanceID`.

The per-instance data the new pipeline needs is **already computed** in `PerDrawUboLayout` (`ToolKit/Render/Renderer.h:151-182`), bound per-draw today at UBO slot 2. We reuse the **computation, not the layout** ("reuse the computation, not the layout" is this roadmap's one-line summary): a lean **`InstanceRecord`** + shared **`RenderObject`** are populated from `PerDrawUboLayout`/`RenderJob` at batch-build time, and heavy shared data is externalized into global tables.

**Headline outcome:** draw calls go from `N_objects` to `N_batches` (e.g. 1000 objects across 20 mesh+material combos -> 20 draws, or one multi-draw-indirect call on native). Per-object UBO upload and per-object state changes are eliminated for the static opaque PBR path. Per-instance data shrinks from ~1100 bytes (`PerDrawUboLayout`) to ~80 bytes (`InstanceRecord`).

---

## 1. Current Engine State (what we build on)

The refactor reuses existing machinery rather than rebuilding it. This table maps the design to the code that already exists.

| Capability | Already in engine? | Where | What is actually new |
|---|---|---|---|
| Per-instance data SOURCE (matrices, material, light indices, anim params) | YES | `PerDrawUboLayout` `Renderer.h:151-182` (per-draw UBO slot 2) | `InstanceRecord` + `RenderObject` extracted from it at batch-build time (2.1) |
| Per-object CPU light assignment + active index lists | YES | `RenderJobProcessor::AssignLight` (`Pass.h:220`), `activePointLightIndices`/`activeSpotLightIndices` in `PerDrawUboLayout` | Fixed-stride light-index table (2.1) |
| Spatial acceleration (BVH) | YES | `Scene::m_aabbTree`, `ToolKit/Source/AABBTree.h` (dynamic, proxy-based) | Add light-influence region query (sphere/AABB); do NOT build a new BVH |
| Tiered layered shadow atlas | YES | `ShadowAtlas` `ToolKit/Render/ShadowAtlas.h` (2 layers, Half/Quarter/Eighth, atomic batch alloc) | Shadow caching + hysteresis (2.4) |
| GPU-side skinning via texture | YES | `Renderer.cpp:237-276` (`updateAndBindSkinningTextures`), `AnimationPlayer::CreateAnimationDataTexture` `Animation.cpp:609-688`, `Resources/Engine/Shaders/skinning.shader` | Natural fallback; (optional) skinned instancing (Phase 7) |
| Runtime-branch PBR "ubershader" | YES | `Resources/Engine/Shaders/defaultFragment.shader` (texture presence via UBO flags: lines 50, 60, 70, 113) | None - existing branches work with per-instance material data |
| Declarative pass binding | YES (migration effectively complete) | `PassRequirements` / `ApplyRequirements` `ToolKit/Render/Pass.h` | Coordinate so the instanced batch draw flows through `ApplyRequirements` |
| Frame statistics + GPU timer queries | YES | `Stats::IncrementStat(FrameStatType::DrawCall)` `Renderer.cpp:355`, `StartTimerQuery`/`EndTimerQuery`/`GetElapsedTime` | Extend to the full metric set (Phase 1) |
| Texture arrays (RHI plumbing) | YES | `GL_TEXTURE_2D_ARRAY` `GLBackend.cpp:75,976`, `Tex2DArray` `Shader.h:40` | DEFERRED to Phase 9 (see 2.6) |
| Instanced draws / MDI | NO | `DrawDesc.instanceCount` defaults to 1 (`IGraphicsBackend.h:63`) | `drawElementsInstanced` (Phase 3) + `glMultiDrawElementsIndirect` native (Phase 8) |
| Instance data buffer (texture/SSBO) | NO | - | New `InstanceDataBuffer` RHI abstraction (Section 5) |
| Material / env-volume / light-list tables | PARTIAL | `MaterialCacheItem` (`Material.h:26-43`), `PointLightCache`/`SpotLightCache` UBOs (slots 4/5), `EnvironmentComponent` | Pack into instance-indexed tables with lifetimes (2.1) |
| Cache validation | PARTIAL | `AABBTree::Invalidate`, `Entity::m_aabbTreeNodeProxy` | Resource-side generation / dependency graph (2.5) |
| Shadow caching / hysteresis | NO | - | New (atop existing `ShadowAtlas`, generation-based) |
| Double buffering of instance data | NO | - | New, optional (Phase 6) |

### 1.1 Forward pass draw loop (the surface an instanced rewrite replaces)

`ForwardRenderPass::Render()` (`ToolKit/Render/RenderPass/ForwardPass.cpp:47-77`) applies pass-level requirements once, then calls `RenderOpaque` then `RenderTranslucent`.

- **Opaque path** (`ForwardPass.cpp:129-156`): builds two `GpuProgram`s up front by flipping the `DrawAlphaMasked` define on the shared fragment shader, then runs `RenderOpaqueHelper` twice - once over the forward-opaque partition, once over forward-alpha-masked.
- **`RenderOpaqueHelper`** (`ForwardPass.cpp:224-248`) - the per-job loop to replace:

```cpp
renderer->SetPassState(m_opaquePassState);
for (RenderJobItr job = begin; job != end; job++)
{
  if (job->Material->IsShaderMaterial())
    renderer->RenderWithProgramFromMaterial(*job);
  else {
    renderer->BindProgram(defaultGpuProgram);
    renderer->Render(*job);          // <-- one draw per job
  }
}
```

- Each iteration funnels into `Renderer::Render(const RenderJob&)` (`Renderer.cpp:232-356`): `SetTransforms` (CPU only), `SetLights(job.lights)`, compose+bind `RenderState`, `BindPipeline`, `updateAndBindSkinningTextures`, `SetMaterial`, `SetDataTextures`, `FeedUniforms` (one per-draw UBO upload), `Draw`. `Render(const RenderJobArray&)` (`Renderer.cpp:381-389`) is just `for (job : jobs) Render(job)`.
- **RenderState is composed per-job** (`Renderer.cpp:290-319`) from `m_passiveState` + material active bits (`cullMode`, `blendFunction`, `drawType`, `alphaMaskThreshold`, `lineWidth`). This is why RenderState must be part of the batch key (2.2).

### 1.2 Per-draw UBO (slot 2) - migration is complete

The per-draw UBO is bound at **slot `ReservedUniformBufferSlots::PerDrawData == 2`** (`ToolKit/Render/RHI.h:50-62`), NOT slot 6. The "active migration" docstring is stale: every per-draw value already flows through this UBO; there are no remaining `SetUniform`/`glUniform*` calls for per-draw data in `Render(job)`.

- `Renderer::FeedUniforms` (`Renderer.cpp:1088-1153`) populates `PerDrawUboLayout`, then `Invalidate()` + `Map()` + `m_backend->SubmitPerDrawData(...)`.
- GL: `SubmitPerDrawData` is a no-op (the UBO is already bound via `glBindBufferBase`).
- Vulkan: `memcpy` into a per-frame dynamic-offset ring (`AllocatePerDrawSlot`), offset fed as `pDynamicOffsets[0]`.

**Stale comments to fix (Phase 0):** `Renderer.h:141` (`// PerDrawGpuBuffer (slot 6)`), `Renderer.h:267` (per-draw UBO), `GLBackend.cpp:466` (`// slot 6, 'PerDrawData' UBO`).

### 1.3 Light binding model (important for what stays global vs per-instance)

- **Directional lights:** bound ONCE per pass into the global `directionalLightBuffer` (slot 3). NOT per-instance.
- **Point/spot light DATA:** today in an **LRU-based cache UBO** (`PointLightCache` slot 4, `SpotLightCache` slot 5, capacity 32). The LRU is fine for the per-draw path (indices resolved fresh at draw time) but **incompatible with instancing**: per-instance indices are stored in instance data and must stay valid for the whole frame, while LRU evicts mid-frame. -> The instanced path uses a **persistent, frame-stable light data buffer** (id-indexed, no eviction, generation-dirty) instead (2.1).
- **Point/spot active INDICES per object:** today written into the per-draw UBO. These move into the fixed-stride light-index table (2.1).

So per-instance light data = the active index lists only. The light data itself stays global.

### 1.4 RHI limits (`ToolKit/Render/RHI.h`)

| Constant | Current value | Cache capacity |
|---|---|---|
| `MaxDirectionalLightPerObject` | 8 | `DirectionalLightCacheItemCount = 12` |
| `MaxPointLightPerObject` | 24 | `PointLightCacheItemCount = 32` |
| `MaxSpotLightPerObject` | 24 | `SpotLightCacheItemCount = 32` |
| `TextureSlotCount` | 32 | - |
| `ShadowAtlasSlot` | 8 | - |

These are desktop-oriented. The mobile profile lowers the per-object caps (Section 2.3), which shrinks the fixed-stride light list.

---

## 2. Architecture

### 2.1 Instance data: `RenderObject` (shared) + lean `InstanceRecord` (per-instance) + global tables

> **Design principle:** separate what is **shared** (per-batch) from what **varies** (per-instance). The shared bundle is the stable extension point for future scene features; the per-instance record stays minimal and hot.

Two records:

**`InstanceRecord`** - per-instance, the hot per-vertex fetch. Only fields that actually vary per object:

```cpp
// Lean per-instance record. Populated from PerDrawUboLayout / RenderJob.
// renderObjectIndex is NOT here: it is batch-uniform (in the BatchKey), so all
// instances in a batch share one RenderObject (accessed via LoadRenderObject()).
struct InstanceRecord
{
  Mat4  model;          // world matrix (always per-instance)
  uint  lightListIndex; // -> fixed-stride light-index table (position-dependent)
  uint  animKeyIndex;   // -> animation keyframe params (skinned; 0 = none)
  uint  flags;          // bit0: uniformScale, bit1: isSkinned, bit2: envOverride, ...
  uint  _pad;           // texel alignment
};
// 64 (Mat4) + 16 = 80 bytes -> 5 RGBA32F texels.
// Future optimization: compressed transforms (quat + translation + scale ~= 40 B),
// only if profiling shows the matrix is the instance bottleneck (see 2.7).
```

**`RenderObject`** - shared bundle of `(material + env + skeleton + future probes/decals/GI)` - **mesh is NOT part of it** (mesh lives in the `BatchKey`, so two meshes with the same material+env+skeleton share one `RenderObject`). Referenced by `renderObjectId` in the `BatchKey`. Accessed in-shader via **`LoadRenderObject()`**, a backend-implementation-detail fetch (per-draw uniform/UBO today; promoted to a buffer/SSBO read as `RenderObject` grows with future probes/decals/GI). The shader never knows the mechanism. The extension point:

```cpp
// Shared bundle. One per distinct (material + env + skeleton) bundle (mesh excluded - lives in BatchKey).
struct RenderObject
{
  uint  materialIndex;            // -> material table (MaterialCacheItem::Data)
  uint  envVolumeIndex;           // -> env-volume table (primary)
  uint  secondaryEnvVolumeIndex;  // -> env-volume table (IBL blend)
  uint  skeletonIndex;            // -> skinning pose texture (Phase 7 skinned)
  // Future scene-aware bindings attach HERE, not to InstanceRecord:
  //   reflectionProbeIndex, lightProbeIndex, decalSetIndex, giVolumeIndex,
  //   fogVolumeIndex, clipPlaneIndex, ...
};
// ~16 bytes.
```

**`RenderObjectHandle` (generational, stale-safe):**

```cpp
struct RenderObjectHandle { uint32 index = 0; uint64 generation = 0; };  // generation = TABLE-SLOT LIFETIME (stale-handle detection after recycle). NOT a resource generation.
```

**Handle generation != resource generation.** The handle's `generation` protects against a recycled slot; resource generations (2.5) detect changed Material/Light/Env/Skeleton data. The CPU-side table row pairs the shader-visible identity with the cached resource gens it last validated against:

```cpp
struct RenderObjectEntry {       // CPU-side table row (only `shaderData` reaches the GPU)
  RenderObject shaderData;        // -> GPU (materialIndex/envVolumeIndex/skeletonIndex)
  uint64 materialGeneration;      // cached gen last synced (CPU-only validation)
  uint64 environmentGeneration;
  uint64 skeletonGeneration;
};
```

**Ownership invariant (AGENTS.md):** `Entity -> RenderComponent -> RenderObjectHandle`. The handle is owned by the source entity's render component; the `RenderObject` table row is created/destroyed only through that ownership path. **`BatchBuilder` never creates or destroys RenderObjects** - it only maps `RenderComponent -> RenderObjectHandle -> RenderItem`. This keeps the frame-local batcher from becoming a hidden resource manager when probes/decals/GI/material-variants are added later.

**Identity rule (AGENTS.md - anti-fragmentation):** `RenderObject` identity (= what goes into `renderObjectId` / the batch key) is **shader-visible data only** (material, env, skeleton, probes/decals/GI). Debug names, editor selection flags, CPU metadata, `debugMaterialOverride`, etc. must NEVER enter `RenderObject` identity or the `BatchKey` - they fragment batches silently.

**Identity = (material + env + skeleton + probes/decals/GI), EXCLUDING mesh** - mesh lives in the `BatchKey` directly, so two meshes can share a `RenderObject` (same material+env).

**Immutability rule:** `RenderObject` identity is immutable. A *swap* of an identity field (entity switches material/env/skeleton, or crosses an env boundary) creates a **new** `RenderObject` + new handle - the old one is never mutated in place. A *parameter* change (e.g. material color tweak) bumps the **resource** generation (2.5); the `RenderObject` stays the same. This avoids the "BatchKey says it belongs here but it changed underneath" invalidation class.

**Why the split (v2 mistake corrected):** in v2 `materialIndex`/`envVolumeIndex`/`animIndex` sat per-instance even though they are uniform across a batch (material is in the batch key). v3 hoists the shared handles into `RenderObject` (exposed via `LoadRenderObject()`, uniform today - zero extra per-instance fetch), leaving `InstanceRecord` with only per-instance-varying data. Adding decals/probes/GI volumes later grows `RenderObject`, never `InstanceRecord`.

**`RenderObject` lifecycle (stable handles, no per-frame hash):** a `RenderObject` is a persistent handle held by the source entity/drawable, created once per distinct **(material + env + skeleton)** bundle (mesh excluded) and generation-tracked. `BatchBuilder` reads the entity's handle directly - there is no per-frame hash/lookup to resolve `(material + env + skeleton) -> renderObjectId` (this preserves the "sort, don't hash" principle on the RenderObject side too).

**Env volume placement:** `envVolumeIndex` lives on `RenderObject`, so a batch is env-coherent by construction (`renderObjectId` is in the `BatchKey`). Env is the one spatially-dynamic component: when an entity crosses an env-volume boundary, the dependency graph bumps `envGen` and the entity's `RenderObject` handle is reassigned/rebuilt - rare, so cheaper than a per-instance env fetch for the common case. The `envOverride` flag remains as the escape hatch for pathological batch-spanning-boundary cases.

**Derived, not stored:**

- `inverseModel`, normal matrix, `modelWithoutTranslate`, IBL rotations - derived in shader or via the inverse heuristic below.
- **Inverse-matrix heuristic (per-instance, not per-vertex):** all vertices of an instance share one normal matrix, so never recompute it per-vertex. At batch-build, decide per instance:
  - uniform scale (`flags & uniformScale`) -> shader uses `mat3(model)` directly (cheap).
  - non-uniform scale -> CPU precomputes the 3x3 inverse-transpose once, stored in a small per-instance transform extension.
  - (Dynamic vs static does not change this - a moving object's matrix changes per frame either way; the decision is about scale type.)

**Global tables** (generation tracked on the resource, see 2.5):

| Table | Contents | Source | Lifetime |
|---|---|---|---|
| Material table | `MaterialCacheItem::Data` (4 `Vec4`) | `Material.h:26-43`, `GetCacheItem()` | **persistent** (dirty-update on material change) |
| Env-volume table | active `EnvironmentComponent`s (11 `Vec4` each) | `RenderJob::EnvironmentVolume`, `AssignEnvironment` | **semi-persistent** (on env transform change) |
| Light data buffer | all lights' params (pos/color/range/direction), id-indexed | `PointLight`/`SpotLight`; **replaces the LRU `PointLightCache`/`SpotLightCache`** | **persistent** (generation-dirty; no eviction) |
| Light-index table | fixed-stride per-instance point/spot index lists | `PerDrawUboLayout` arrays | **frame-local** |
| Animation key table | per-instance keyframe/blend params (skinned) | `PerDrawUboLayout` anim | **frame-local** |
| Skinning pose texture | pre-baked bone matrices | `AnimationPlayer::CreateAnimationDataTexture` `Animation.cpp:609-688` | **persistent** (on anim data change) |
| RenderObject table | shared bundles (above) | batch-build | **semi-persistent** (on bundle change) |

The light data itself (point/spot) moves into the **persistent light data buffer** (id-indexed, generation-dirty, no eviction - replaces the LRU `PointLightCache`/`SpotLightCache`); the per-instance index lists point into it by stable id.

**Fixed-stride light list (v6 decision: interleaved for v1):** each instance reserves `MaxPointLightPerObject + MaxSpotLightPerObject` slots in the light-index table (mobile 6+6 = 48 bytes, desktop 24+24), interleaved point-then-spot. Accessed via a **`LoadLightList(idx, ...)`** abstraction (same hiding philosophy as `LoadInstance`) so a future split layout (separate point/spot offsets + packed arrays, forward+ aligned) is a drop-in swap with zero shader change. Interleaved chosen for v1 for simplicity + measure-first discipline; the abstraction makes the later swap cheap. Mobile stride is small enough to inline into `InstanceRecord` if desired; desktop stays in a separate fixed-stride table to avoid bloating the hot record. **Fragment-heavy optimization (Phase 2b/3):** inline `lightOffset` + `lightCounts` into `InstanceRecord` (plumbed as flat varyings to the fragment shader) instead of a `lightListIndex` -> table lookup, so per-fragment light setup costs zero fetch; the `LoadLightList(offset, counts, ...)` signature stays compatible.

**Fetch locality (why the split helps):** the small fixed `InstanceRecord` is fetched once per vertex (model + indices in a tight cache footprint). Variable data (light list) is fetched only in the fragment shader. Upload bandwidth for dirty instances drops ~9x vs the raw `PerDrawUboLayout`.

### 2.2 Batching (sort-based, memcmp key, `RenderItem` output)

**`BatchBuilder`** produces a flat array of **`RenderItem`**s. Each `RenderItem` carries a small POD **`BatchKey`** (~32 bytes):

```cpp
struct alignas(32) BatchKey    // 32-byte aligned for SIMD memcmp / compare
{
  uint32 meshId;
  uint32 vertVariantId;
  uint32 fragVariantId;
  uint32 textureSetId;   // packed material-texture-set identity
  uint32 renderStateSig; // packed cull/blend/depth/stencil/primitive bits
  uint32 renderObjectId; // -> RenderObject table (handle index; generation is validation-only, not identity)
  uint32 _pad;
};  // 32 bytes - sortable with memcmp / struct operator<.

struct RenderItem
{
  BatchKey key;
  uint32   instanceIndex;   // -> InstanceRecord row
  // ...draw params; maps to VkDrawIndexedIndirectCommand (Phase 8)
};
```

- **Sort, don't hash:** sort the `RenderItem` array by `BatchKey` (memcmp / struct `<`); contiguous runs of equal keys = batches. No hash table, no collisions, cache-friendly linear scan.
- **One draw per batch:** `drawElementsInstanced(count = run length)`, with the batch's `RenderObject` exposed via `LoadRenderObject()`.
- **Key includes:**
  - `textureSetId` - material textures are bound once per batch.
  - `renderStateSig` - cull/blend/depth/stencil/primitive differ -> separate batch (correctness).
  - `renderObjectId` - the env/material/skeleton bundle (the `RenderObject`; mesh is the separate `meshId` field).
  - **Sort identity vs storage identity:** `renderObjectId` (a table index) is the v1 sort key - it faithfully groups equivalent instances within a frame, and the immutability rule keeps indices stable across frames (rare recycling). If profiling shows inter-frame batch-order churn, swap the sort key for a content-derived `renderObjectSortKey` (hash of the shader-visible identity); storage lookup still uses `RenderObjectHandle.index`. Sort identity != storage identity.
- Within a batch, `material.*InUse` flags are uniform -> existing `defaultFragment.shader` branches (lines 50, 60, 70, 113) stay uniform -> no divergent branching.
- The `RenderItem` array maps directly to `VkDrawIndexedIndirectCommand` for the GPU-driven / multi-draw-indirect future (Phase 8). It is the common output of instanced / indirect / mesh-shader / visibility-buffer pipelines.

**The instanced batches map onto the existing partition split:** the `DrawAlphaMasked` define flip (`ForwardPass.cpp:140,150,171`) becomes a per-batch program variant, exactly as today.

**Legacy (non-instanced) path - retained:**

- **Translucent** (forward-translucent partition): needs per-fragment depth sort; cannot naively batch. Stays per-draw.
- **Shader materials** (`Material->IsShaderMaterial()`): own program per material. Stays per-draw.
- **Skinned meshes:** GPU skinning keyed by `(skeletonId, animationId)`. Stays per-draw until Phase 7.

**Draw type mapping for the instanced path:**

| RenderData partition (`Pass.h:166-189`) | Instanced path? |
|---|---|
| 0 Culled | not drawn |
| 1 Deferred Opaque / 2 Deferred Alpha-Masked | DEAD - scheduled for removal (Phase 0); not instanced |
| 3 Forward Opaque | YES - instanced |
| 4 Forward Alpha-Masked | YES - instanced (`DrawAlphaMasked=1` variant) |
| 5 Forward Translucent | NO - legacy per-draw |

### 2.3 CPU light assignment (reuse, do not rebuild)

- `RenderJobProcessor::AssignLight` already assigns per-object lights and produces the active index lists. Reuse as-is; write into the fixed-stride light-index table.
- Reuse `Scene::m_aabbTree` for light-vs-object intersection instead of a new BVH. Add a region query (sphere/AABB) to `AABBTree` (it already has `Traverse`, `Invalidate`, `Rebuild`). On light move, query affected objects -> bump their consumed-light generation (2.5) + the light's generation (2.4).
- **Mobile profile:** gate lower per-object caps on a render profile (e.g. `TK_GL_ES_3_0` -> mobile). Proposal: 4 directional / 6 point / 6 spot per object on mobile (current 8/24/24 is desktop-oriented). Because these constants are `constexpr`, profile selection is compile-time. **The fixed-stride light list / table widths must be derived from the active profile**, not hardcoded.

### 2.4 Shadow atlas (generation-based, resource-side)

`ShadowAtlas` (`ShadowAtlas.h`) already implements a 2-layer, Half/Quarter/Eighth tiered atlas with atomic batch allocation and a single FBO + `glFramebufferTextureLayer`. Do not rebuild.

**New work:**

- **Resource-side generation:** each `Light` carries `m_gen` (bumps on transform/color/range). Each cached slot stores the generations it was rendered with - `LightGen` + `GeometryGen` (region) + `ViewGen` (tier). Re-render only when any advances.
  - `GeometryGen` bumps via the AABBTree region query when a shadow-caster moves in the light's influence.
  - `ViewGen` bumps on camera-driven tier change.
- **Hysteresis:** tolerance margin on the tier-selection distance check so a light near a boundary does not thrash between resolutions every frame (promote at distance D, demote only beyond D + margin).
- **No double buffering for the atlas:** reading and writing different slots of the same array texture in one frame is fine; the hardware handles it.

### 2.5 Cache validation (resource-side dependency graph)

Generations live on **resources**, not objects. Consumers cache the generation of each resource they last consumed. This is automatic propagation: the generation is bumped inside the resource's mutator, so a missed setter cannot silently produce stale data. Generations are `uint64` (CPU-side validation only - the shader never sees them) to make wrap-around a non-issue in long-lived editor sessions.

| Resource | Generation bumps on | Affected consumers (auto-propagated) |
|---|---|---|
| `Material` | any param change | **material table row only** (one upload); `RenderObject` + instances untouched |
| `Light` | transform/color/range | **light data buffer slot** (one upload); transform ALSO triggers AABBTree reassignment of affected instances' light-index rows + the light's shadow slot |
| `EnvironmentComponent` | data/transform change | **env table row only**; boundary crossing -> RenderObject reassignment (immutability), not instance content |
| `Skeleton`/`Animation` | anim data | **skinning pose texture only**; per-instance `animKeyIndex` advances with anim time (normal per-frame update, not a resource invalidation) |

- Each `RenderObjectEntry` caches the resource gens it consumed; each instance row caches its per-instance gens (transform/lightList/animKey). Rebind a table/texture row when its resource gen advances; upload an instance row only when a per-instance gen advances AND the instance is visible.
- **Resource param changes update TABLE/TEXTURE rows only, never instance rows.** A material color tweak, env-volume move, or anim-data change rebinds one shared row read by all consuming batches - it does NOT invalidate `InstanceRecord` rows (model/lightList/animKey are unaffected). This is the core benefit of the `RenderObject` split. (Light moves are the exception: they change per-object light *assignment*, so affected instances' light-index rows do update.)
- **Upload rule:** upload only `(visible AND any-consumed-gen-advanced)` rows + dirty table rows. Newly-visible rows are fully stale (full write). Static visible rows are never re-uploaded after their first write.
- **Double-buffer write rule (Phase 6):** a dirty row is uploaded to the current write-buffer AND enters a pending-write set; it is re-uploaded to the (now-current) write-buffer next frame until BOTH buffers hold the new value. Required because a dirty-then-static instance would otherwise leave the other buffer stale and flicker every swap. Never write the read-buffer during its read frame.
- **Threshold (small vs full upload):** if `dirtyCount < X`, upload rows individually (grouped by page/region); if `dirtyCount >= X`, re-upload the whole used region (avoids many small `texSubImage2D` calls).

### 2.6 Texture arrays (DEFERRED - Phase 9)

**Decision: defer.** Rationale:

- Texture arrays would remove `textureSetId` from the batch key (collapsing more batches), but only help the **same mesh + different texture** content pattern (billboards, cards, decals, crowd clothing variation, unique-sign buildings).
- Typical mobile instancing wins (foliage, repeated props, identical units) already batch perfectly, because those objects share textures too. Arrays add nothing there.
- Cost is a permanent packing pipeline: uniform layer size + format (resize/pad to power-of-two buckets), shared wrap mode per array sampler (tiling `GL_REPEAT` vs clamped must be separate arrays), layer-index management, memory overhead.
- On mobile tiler GPUs (Mali/Adreno), once per-object calls are gone, the remaining per-batch texture binds are cheap relative to fragment bandwidth.

**Revisit trigger (Phase 9, profiling-gated):** add texture arrays ONLY if (a) profiling shows batch count / texture binds as the bottleneck, AND (b) the target content is "same mesh, many unique textures."

**When implemented:** use `sampler2DArray` (NOT a 2D UV atlas - arrays preserve tiling/repeat and avoid mip bleeding), bucket by size + wrap mode, store per-instance texture indices. RHI plumbing already exists (`GL_TEXTURE_2D_ARRAY` in `GLBackend.cpp`, `Tex2DArray` in `Shader.h`).

### 2.7 Future extensions (out of scope, noted for awareness)

- **Compressed transforms:** `Mat4 model` (64 B) could become quat + translation + scale (~40 B), with optional quantization for extreme mobile. Only if profiling shows the matrix is the instance bottleneck.
- **Material / pipeline system:** as the renderer grows, the next major complexity will be a unified `MaterialCompiler` + shader-variant + pipeline-cache layer (today the engine uses a runtime-branch PBR ubershader + per-pass defines). This roadmap assumes the current material system; a material-system refactor is a separate future track.

---

## 3. Frame Loop

```
CPU (frame N):
  1. Frustum cull                 -> visible instances (reuse)
  2. BatchBuilder:
       a. for each visible instance, write a RenderItem
          (BatchKey = mesh + shader-variant + texture-set + render-state + renderObject)
       b. extract InstanceRecord (model, indices, flags) from PerDrawUboLayout/RenderJob
       c. refresh global tables (material/env/light-data/light-list/anim/RenderObject) per resource gens
  3. Sort RenderItems by BatchKey  -> contiguous runs = batches
  4. Allocate/free instance rows   -> newly visible = new row (all gens stale)
                                      culled-out = free row
  5. Generation check              -> visible AND any consumed resource gen advanced
  6. Upload dirty rows + tables    -> write-buffer (B): texSubImage2D (GL/WebGL) / SSBO (Vulkan)

GPU (frame N):
  7. Shadow pass                   -> re-render only slots whose gen advanced
  8. Forward pass:
       For each batch (sorted RenderItem run):
         - ApplyRequirements       -> program, framebuffer, pass RenderState
         - bind material textures (diffuse/normal/ORM) once
         - set RenderObject (LoadRenderObject source) + bind s_instanceData + table bindings
         - drawElementsInstanced(count = run length)
       Legacy per-draw for: translucent, shader materials, skinned
  9. Swap read/write buffers       -> next frame writes A, reads B (Phase 6 only)
```

**Mental model:** static instance data is written once; only `(visible AND gen-stale)` rows are re-uploaded. Draw calls go from `N_objects` to `N_batches` (or one MDI call on native). Per-object UBO upload and per-object state changes are eliminated on the instanced path. What remains: per-batch binds (program + material textures + RenderObject + one instanced draw) and per-frame global binds.

---

## 4. Phasing

Incremental. Each phase is independently shippable and measurable. The legacy per-draw path is always retained as the fallback, so a broken phase never blocks shipping. **Phase 1 (Instrumentation) is a prerequisite** - without it, no later phase's win is measurable.

### Phase 0 - Documentation sync + dead-code cleanup (prerequisite)

- Fix `gdtk-overview.md` Section 4.5 UBO slot table (per-draw is slot 2, not 6; custom slots start at 7).
- Fix stale comments: `Renderer.h:141`, `Renderer.h:267`, `GLBackend.cpp:466`.
- Remove dead deferred render partitions (`RenderData` partitions 1-2, deferred opaque / alpha-masked) + simplify `RenderData` / `RenderJobProcessor` (confirmed unused).
- **Success:** overview matches `RHI.h:50-62`; `RenderData` carries only the forward partitions.

### Phase 1 - Instrumentation (prerequisite)

- Extend the existing `Stats` system + GPU timer queries (`StartTimerQuery`/`EndTimerQuery`). Split by surface:
  - **Overlay (live):** draw calls, instanced draw calls, batch count, average batch size, **batch fragmentation score** (`(actual_batches - ideal_batches) / max(ideal_batches, 1)`; 0 = perfect; ideal = distinct mesh+material+state combos), **render-item count**, frame time, shadow redraw count, **CPU build/sort/upload time** (a 500k-item scene can make the O(N log N) sort dominate).
  - **Logged/exportable (regression tracking):** uploaded bytes history, instanced-vs-legacy A/B GPU time series, **`GPUTime_TextureFetch` vs `GPUTime_VertexAttribute`** (drives the `InstanceTransport` fallback decision), culled/visible object counts.
- `FrameStatType` naming follows the existing `DrawCall` pattern: `InstancedDrawCall`, `BatchCount`, `AvgBatchSize`, `BatchFragmentationScore`, `RenderItemCount`, `UploadedBytes`, `ShadowRedrawCount`, `BatchBuildCPUTime`, `RenderItemSortCPUTime`, `InstanceUploadCPUTime`.
- **Success:** every later phase's success criterion is expressed as a measured number (baseline captured here). This is the only way to validate Phase 3's draw-call reduction, Phase 4's upload reduction, Phase 5's zero-shadow-redraw.

### Phase 2 - Instance data transport + `InstanceRecord` + `RenderObject` + `LoadInstance`

- **2a - Transport proof:** add the byte-oriented `InstanceDataBuffer` RHI abstraction (Section 5) + `LoadInstance(id)` shader abstraction (with `baseInstance` designed in). Introduce `InstanceRecord` + `RenderObject`; in 2a they may initially mirror `PerDrawUboLayout` 1:1 to prove the transport. Render a SINGLE instance through the new path, verify pixel-identical vs the current UBO path (`instanceCount = 1`, no batching).
- **2b - Lean split + global tables:** switch to the v3 split (2.1) - `RenderObject` (shared, via `LoadRenderObject`) + lean `InstanceRecord`; build the material / env-volume / light-index / animation / RenderObject tables; apply the inverse-matrix heuristic. Measure per-instance byte count and upload bandwidth (Phase 1 metrics).
- **Success (2a):** one object renders identically through the data-buffer path. **Success (2b):** per-instance data ~80 bytes; visual parity; upload bytes < baseline. **Budget overflow (graceful degrade):** if visible instances exceed the per-profile texture budget (hard ceiling, capped below OOM risk), the excess fall back to the legacy per-draw path - never crash/OOM. (The retained legacy path is exactly what makes this cheap.)
- **Risk:** WebGL2 vertex-stage texelFetch (VTF) bandwidth (Section 7); mitigate via lean `InstanceRecord` + large batches. RGBA32F creation + nearest sampling is core-supported on WebGL2 (no extension). Native GLES 3.2 may use SSBO directly via the same `LoadInstance` interface.

### Phase 2.5 - Shader transport validation (gate before batching)

- Render the **full scene** through the new path with `instanceCount = 1` (no batching) AND through the legacy per-draw path, then **image-diff** the two framebuffers.
- Correct = no *structural* artifacts, within tolerance (FP precision, derivatives, Mali rounding, NaN differ across GPUs): **RMS error < 1/255 AND max channel difference <= 2**. Not "zero pixels" - that rejects correct implementations.
- Why a dedicated phase: once batching starts (Phase 3), a wrong instance/table/RenderObject/key index manifests as a scrambled image with many possible causes. Isolate transport correctness first.
- **Success (measured):** image-diff within tolerance (no structural artifacts) across a representative scene set.

### Phase 2.75 - Single-batch instancing (isolate the instanced draw)

- 1000 **identical** objects sharing one `RenderObject`; the instance buffer holds 1000 rows; one `drawElementsInstanced(1000)`. **No `BatchBuilder`, no sorting, no keys.**
- Validates instance-ID indexing across many instances, per-vertex instance fetch, and buffer indexing at scale - without the batching complexity stacked on top.
- **Success (measured):** 1000 objects render correctly (within image-diff tolerance) in a single instanced draw; draw-call count = 1.

### Phase 3 - `BatchBuilder` + sort-based batching + instanced draws (the headline win)

- **`RenderItem` as the seam (optional pre-step):** introduce `RenderItem` as the legacy renderer's draw unit too (`RenderJob -> RenderItem -> Renderer::Draw`), so the instanced path is just `RenderItem[] -> Batch -> drawElementsInstanced`. Smaller Phase 3 diff, easier rollback, and the renderer stops knowing about scene traversal - the natural shape for indirect/render-graph. Pre-wiring this in legacy first makes Phase 3 a pure "group RenderItems" change.
- Separate **`BatchBuilder`** component: input = visible render jobs, output = sorted `RenderItem` array consumed by the Renderer. The Renderer no longer builds batches - this decoupling enables future multithreaded recording, indirect draw, and Vulkan secondary command buffers.
- Sort by `BatchKey` (memcmp); contiguous runs = batches; one `drawElementsInstanced` per run.
- Translucent / shader materials / skinned stay on legacy per-draw path.
- **First milestone (prove the big win, nothing else):** 1000 identical cubes -> `1000 draws` before, `1 drawElementsInstanced` after. Land this before any incremental-upload work.
- **Allocator (v1):** per-batch-contiguous instance rows (natural since `BatchBuilder` already groups by batch). **Instance indices are transient frame-local IDs** (reassigned each frame for write-locality) - NOT stable identity; stable identity arrives with the Phase 4 page allocator. The page/chunk allocator is deferred behind Phase 4's measured gate (only if small dirty-row uploads dominate).
- **Success (measured):** draw-call count drops from `N_objects` to `N_batches` for static opaque PBR; visual parity; average batch size + batch fragmentation score reported by Phase 1 metrics.
- **Risk:** batch granularity must include texture-set, render-state, AND renderObject, or pipelines/branches diverge.
- **Scope guard:** the `RenderItem` array is a thin command stream, NOT a full render graph. GPU-driven / render-graph is a separate future track.

### Phase 4 - Generation-based incremental uploads + allocator

- Resource-side generation / dependency graph (2.5).
- Upload only `(visible AND gen-stale)` rows + dirty table rows.
- Threshold: small dirty set -> per-row upload (grouped by region); large dirty set -> whole-region re-upload.
- **Allocator:** page/chunk allocator for instance rows (locality for upload, L2 cache, future streaming). Stable handle = (page, offset); dirty uploads grouped per page. Resolves the stable-index (gen tracking) vs compact-index (locality) tension.
- **Success (measured):** a static scene with a few moving objects uploads near-zero bytes/frame (Phase 1 "uploaded bytes" metric).

### Phase 5 - Shadow caching + hysteresis (atop existing `ShadowAtlas`)

- Resource-side slot validation (`LightGen` / `GeometryGen` / `ViewGen`, 2.4).
- Hysteresis margin on tier transitions.
- **Success (measured):** a scene with static shadows reports zero shadow redraws (Phase 1 metric).

### Phase 6 - Double buffering (optional, profiling-gated)

- **Single-buffer is the production baseline.** Enable double-buffering ONLY if Phase 1 metrics prove CPU uploads are stalling the GPU (mobile/WebGL: the extra memory + copy + sync complexity may cost more than it saves).
- Ping-pong two instance buffers (+ tables); render frame N from buffer A while CPU writes buffer B.
- **Pending-write-across-swap rule (correctness):** every dirty row (including a first allocation) is uploaded to the current write-buffer and enters a pending set; it stays pending and is re-uploaded to the (now-current) write-buffer next frame until BOTH buffers hold the new value. Without this, a dirty-then-static instance leaves the other buffer stale and flickers every swap. Never write the read-buffer during its read frame (that would reintroduce the stall the double-buffer exists to avoid). See 2.5.
- **Note:** introduces 1 frame of latency. Revisit for camera-locked objects.
- **Success (measured):** no `texSubImage2D`-induced stalls under high dirty churn (GPU frame time stable).

### Phase 7 - Skinned instancing (optional)

- Batch skinned instances sharing skeleton + animation (same `s_skinningPose` texture).
- Per-instance keyframe params move into the animation key table (indexed by `animKeyIndex`); `skeletonIndex` on `RenderObject`.
- **Success (measured):** crowds of identical animated characters batch (batch count down, draw calls down).

### Phase 8 - Native multi-draw-indirect fast-path (optional, native-only)

- Native GLES 3.2 + Vulkan: convert the sorted `RenderItem` array to `glMultiDrawElementsIndirect` / `vkCmdDrawIndexedIndirect` args -> one (or few) indirect calls submit all batches. Use persistent-mapped SSBOs for the instance data + draw args.
- **WebGL 2 has no indirect draws** -> stays on per-batch `drawElementsInstanced` (Phase 3). WebGL is the lowest-common-denominator backend.
- **Success (measured):** draw-call count near-constant regardless of batch count on native (driver counter / Phase 1 metric).

### Phase 9 - Texture arrays (optional, profiling-gated)

- See 2.6. Only if content is "same mesh, many unique textures" and batch count is the bottleneck.

### Vulkan parity

Threaded through Phase 2 (the `LoadInstance` interface lets Vulkan use SSBO directly while GL/WebGL uses a texture). Every phase's definition of done includes "Vulkan renders identically."

---

## 5. RHI / Backend Abstraction

Add a logical instance-data concept to `IGraphicsBackend` so the per-instance transport is backend-agnostic. **Shaders never see the transport** - they call `LoadInstance(id)` / `LoadRenderObject(idx)` and access fields; the fetch mechanism lives in a backend-specific include.

```cpp
// Sketch - exact names TBD, must follow AGENTS.md style. Byte-oriented: the API
// never leaks "instance rows" - backends decide whether bytes map to texels (GL)
// or a buffer copy (Vulkan).
struct InstanceUpload { uint64 offset; uint64 size; const void* data; };

class IGraphicsBackend {
  // Create/resize an instance data buffer holding up to maxInstances records of stride bytes.
  InstanceDataBufferHandle CreateInstanceDataBuffer(uint maxInstances, uint stride);
  void DestroyInstanceDataBuffer(InstanceDataBufferHandle);
  // Upload a byte range (only gen-stale rows). GL -> texSubImage2D; Vulkan -> vkCmdCopyBuffer.
  void UpdateInstanceData(InstanceDataBufferHandle, const InstanceUpload& upload);
  // Bind for the upcoming instanced draw (baseInstance is mandatory - see indexing below).
  void BindInstanceData(InstanceDataBufferHandle, uint baseInstance);
};
```

Backend mapping (forward-looking):

| Backend | `InstanceDataBuffer` backing | `LoadInstance` fetch |
|---|---|---|
| GL / WebGL 2 | `RGBA32F` `GL_TEXTURE_2D` | `texelFetch` |
| Native GLES 3.2 | SSBO (available - preferred) | SSBO read |
| Vulkan | SSBO | SSBO read |
| (future D3D12) | StructuredBuffer | SRV read |
| (future Metal) | Buffer | buffer read |

- `LoadInstance` hides the fetch; switching texture <-> SSBO changes only the backend include, never the shader body. WebGPU / Metal can be added without touching shaders.
- `RenderObject` is accessed via `LoadRenderObject()` - a backend-impl-detail fetch (per-draw uniform/UBO today; buffer/SSBO read as it grows). NOT fetched per-instance in the common case. Future scene-aware bindings (probes/decals/GI) added to `RenderObject` are therefore free at the per-instance level.
- `LoadLightList(idx, ...)` similarly hides the light-index layout (interleaved fixed-stride today; split/forward+ layout is a drop-in swap).
- The **light data buffer** (light params, id-indexed) is a separate persistent buffer (texture on GL/WebGL, SSBO on native/Vulkan) fetched via `LoadLight(id)`; `LoadLightList` returns ids that `LoadLight` resolves. **No LRU eviction** - indices are frame-stable for the instanced draw.
- **`InstanceTransport` (backend capability):** `{ TextureFetch (GL/WebGL), SSBO (native GLES 3.2 + Vulkan), VertexBuffer (fallback) }`, selected per backend; `LoadInstance` hides it. If Phase 1 metrics show VTF underperforms on a target device, swap to `VertexBuffer` - no renderer or shader rewrite.

**Indexing (baseInstance designed day-one):** the instance id is `gl_InstanceID + baseInstance`. Today `baseInstance` is a per-draw uniform (`u_instanceBase`); in Phase 8 it becomes the indirect draw's `firstInstance` directly - designed in now so MDI is trivial later. (Desktop GL's `gl_InstanceID`-excludes-base gotcha is absorbed inside `LoadInstance`; GLSL ES 3.00 provides `gl_InstanceID`, Vulkan uses `gl_InstanceIndex`.) No per-instance vertex attribute required -> zero per-instance vertex bandwidth.

**Sampler/binding slot:** reserve a free slot for `s_instanceData` (and table bindings). In-use sampler slots: 0 (`s_diffuseColor`), 1 (`s_emissiveColor`), 2 (`s_skinningPose`), 3 (`s_blendWeights`), 4 (`s_metallicRoughness`), 8 (`s_shadowAtlas`), 9 (`s_normalMap`), plus IBL/AO. A high slot (e.g. 10) or a reserved block is fine (`TextureSlotCount = 32`).

**Layout (concrete, when texture-backed):**

- `STRIDE = ceil(sizeof(InstanceRecord) / 16)` RGBA32F texels per instance.
- **Profile-driven sizing (not a fixed 2048x2048):** size the texture from a per-profile max-instance budget x `STRIDE`. Mobile ~16k instances x 5 texels = ~80k texels -> a ~256x512 (or similar) texture ~= 1.25 MB (the old fixed 2048x2048 = 64 MB was ~50x over-provisioned for mobile). Desktop uses a larger budget. Grow geometrically on demand (reallocate + copy) if the budget is exceeded.
- `WIDTH` chosen as a divisor that packs `WIDTH / STRIDE` instances per row, stacked vertically; stay below `gl_MaxTextureSize`.

**Mirroring rule (add to `AGENTS.md`):** the C++ `InstanceRecord` / `RenderObject` / table rows and the GLSL `LoadInstance` layouts must agree on field order/offset. Concrete asserts (GLSL texel layouts are unforgiving - a future compiler padding change would be catastrophic):

```cpp
static_assert(alignof(InstanceRecord) >= 16);        // SSBO/Vulkan-safe alignment (sizeof%16 alone is NOT enough)
static_assert(sizeof(InstanceRecord) % 16 == 0);    // texel-aligned stride
static_assert(offsetof(InstanceRecord, model) == 0); // first field is the matrix
static_assert(sizeof(InstanceRecord) == STRIDE * 16);// matches shader-declared STRIDE
```

(This replaces the per-draw std140 mirror for the instanced path; `PerDrawUboLayout`'s std140 mirror stays for the legacy per-draw path.)

---

## 6. Shader Changes

- **Shared include `instanceDataInc.shader`:** declares `InstanceRecord` + `RenderObject` layouts, the `LoadInstance(uint id)` function (backend-specific fetch: `texelFetch` on GL/WebGL, SSBO read on native/Vulkan), and table accessors (`LoadMaterial(idx)`, `LoadEnvVolume(idx)`, `LoadLightList(idx, count)`, `LoadAnim(idx)`). `LoadRenderObject()` is the same abstraction as `LoadInstance` (per-draw uniform/UBO today, buffer read later - the shader does not know).
- **`defaultVertex.shader`:** when instanced, `InstanceRecord r = LoadInstance(gl_InstanceID + u_instanceBase);` use `r.model`; derive inverse/normal matrix via the uniformScale flag; read `RenderObject ro = LoadRenderObject();` (the batch's RenderObject) for env/material indices.
- **`defaultFragment.shader`:** fetch `MaterialCacheItem::Data` via `LoadMaterial(ro.materialIndex)`, light indices via `LoadLightList(r.lightListIndex, ...)`, env volumes via `LoadEnvVolume(ro.envVolumeIndex)`. Existing `material.*InUse` branches unchanged (uniform within a batch).
- **`skinning.shader`:** unchanged for now; Phase 7 reads per-instance keyframe via `LoadAnim(r.animKeyIndex)` and the shared `s_skinningPose` texture from `ro.skeletonIndex`.
- **`perDrawDataInc.shader`:** the existing std140 mirror stays for the legacy per-draw path.

---

## 7. Platform Capabilities & Risks

| Concern | Status / action |
|---|---|
| `RGBA32F` texture + `texelFetch` (nearest) on GLES 3.2 native | Supported; native may prefer SSBO (same `LoadInstance` interface). Verify on lowest target device. |
| `RGBA32F` on WebGL 2 | **Core-supported:** creating an RGBA32F texture and sampling it with NEAREST (`texelFetch`) needs no extension. (`EXT_color_buffer_float` is only required to render *to* float, which this path does not do.) |
| Native GLES 3.2 SSBO / MDI | Available (`gl32.h`). `LoadInstance` -> SSBO; Phase 8 MDI. WebGL has neither -> stays texture + per-batch instanced. |
| WebGL2 vertex-stage texelFetch (VTF) bandwidth | `LoadInstance` does a per-vertex texture fetch on WebGL2 (native uses SSBO). Monitor via the Phase 1 A/B GPU-time metric; mitigate via lean `InstanceRecord` + large batches. `LoadInstance` makes a vertex-attribute fallback path a drop-in swap if VTF underperforms on a target device. |
| `gl_InstanceID` (GLSL ES 3.00) | Available. Vulkan uses `gl_InstanceIndex`. |
| Max texture size | Stay below `gl_MaxTextureSize`; size from the instance budget (5.x), not a fixed 2048x2048. |
| Divergent branching | Prevented by `textureSetId` in the batch key. |
| RenderState divergence | Prevented by `renderStateSig` in the batch key. |
| Env divergence within a batch | Prevented by `renderObjectId` in the key; or per-instance `envOverride`. |
| Per-vertex inverse redundancy | Avoided by per-instance inverse heuristic (2.1). |
| Small `texSubImage2D` churn | Phase 4 threshold + whole-region fallback. |
| Double-buffer latency | Phase 6 only; 1-frame lag, documented. |

---

## 8. Documentation Debt (Phase 0)

- `gdtk-overview.md` Section 4.5 UBO slot table is stale (per-draw shown as slot 6; actual slot 2, `RHI.h:50-62`).
- Three stale "slot 6" comments: `Renderer.h:141`, `Renderer.h:267`, `GLBackend.cpp:466`.
- `PerDrawUboLayout` "active migration" docstring is stale (migration is complete).
- **AGENTS.md additions (Phase 0):** backend-selection rule (texture vs SSBO per backend); RenderObject ownership invariant (`Entity -> RenderComponent -> RenderObjectHandle`; `BatchBuilder` never creates/destroys); RenderObject identity-pollution rule (shader-visible only); `InstanceRecord.flags` bit table added to the mirroring rule.

---

## 9. Decisions (resolved; numbers calibrated from Phase 1 data)

Architecture is locked. Remaining items are number-calibration, not design.

1. **Backend abstraction:** `InstanceDataBuffer` + `LoadInstance`/`LoadRenderObject`/`LoadLightList` on `IGraphicsBackend` (GL/WebGL = texture; native GLES 3.2 + Vulkan = SSBO). **DECIDED.** Backend-selection rule to be documented in `AGENTS.md` (Phase 0).
2. **Mobile light caps:** **tiered** - `TK_MOBILE_LOW` (~4 dir / 4 point / 4 spot) and `TK_MOBILE_HIGH` (~6 / 8 / 8); desktop 8/24/24. **Exact numbers calibrated from Phase 1** (typical active lights per object in real scenes), not gut-feeling.
3. **Hybrid scope:** instanced path for static opaque PBR only; translucent / shader materials / skinned remain legacy. **DECIDED.**
4. **Deferred partitions:** **RESOLVED** - dead code, removed in Phase 0.
5. **RenderObject / InstanceRecord:** `RenderObjectHandle {index, generation}` entity-owned; `renderObjectIndex` batch-uniform (in `BatchKey`); env on `RenderObject`, reassigned on boundary crossing; identity = shader-visible only; `flags` bit table (bit0 uniformScale, bit1 isSkinned, bit2 envOverride, ...) to be added to `AGENTS.md` mirroring rule. **Remaining:** concretize exact fields + table layouts before Phase 2b; usage-frequency audit of `PerDrawUboLayout` (rare fields, e.g. `secondaryEnvVolumeIndex`, become optional extensions, not mandatory fetch).
6. **Allocator:** **per-batch-contiguous for v1**; page/chunk allocator behind Phase 4's measured gate (page size ~64-128 instances, cache-line/tile aligned, if adopted).
7. **Instrumentation:** overlay vs logged split; `FrameStatType` naming follows the existing `DrawCall` pattern (canonical list in Phase 1 - includes `BatchFragmentationScore`, `RenderItemCount`, and the CPU build/sort/upload times). **DECIDED.**
8. **Instance-texture sizing:** per-profile budget x STRIDE (~16-24k mobile); hard ceiling at desktop budget; **grow-on-demand = geometric 2x, but on overflow fall back to legacy per-draw (no OOM)**. Mobile starting budget confirmed from Phase 1.
9. **Light list layout:** **interleaved fixed-stride for v1** behind `LoadLightList` (split/forward+ layout is a drop-in swap later). **DECIDED.**
10. **Double buffering:** **single-buffer is the production baseline**; Phase 6 enabled only on profiling proof. **DECIDED.**

---

## 10. Glossary

- **InstanceRecord:** lean per-instance record (~80 bytes): model, lightListIndex, animKeyIndex, flags (`renderObjectIndex` is batch-uniform, not here). Populated from `PerDrawUboLayout`/`RenderJob`.
- **RenderObject:** shared bundle (material/env/skeleton indices + future probes/decals/GI; **mesh excluded** - lives in the `BatchKey`), accessed via `LoadRenderObject()`. The stable extension point. One per distinct (material + env + skeleton) bundle; shared across meshes with the same bundle.
- **RenderObjectHandle:** `{index, generation}` - the generational handle owned by the source entity's render component; `generation` = table-slot lifetime (stale-handle detection), distinct from resource generations.
- **RenderObjectEntry:** CPU-side table row = `RenderObject shaderData` (-> GPU) + cached resource gens (Material/Env/Skeleton) for validation.
- **Ownership invariant:** `Entity -> RenderComponent -> RenderObjectHandle`; `BatchBuilder` never creates/destroys RenderObjects.
- **LoadLightList(idx, ...):** shader abstraction over the per-instance light-index list (interleaved fixed-stride today; split/forward+ layout is a drop-in swap).
- **RenderItem:** one draw-unit emitted by `BatchBuilder`, carrying a `BatchKey` + instance index. Sortable; maps to indirect draw args (Phase 8).
- **BatchKey:** 32-byte POD (mesh + shader-variant + texture-set + render-state + renderObject). Sort key for batching (memcmp).
- **BatchBuilder:** component that turns visible render jobs into a sorted `RenderItem` array consumed by the Renderer.
- **Batch fragmentation score:** `(actual_batches - ideal_batches) / max(ideal_batches, 1)` - 0 = perfect, positive = fragmentation (ideal = distinct mesh+material+state combos). High score signals RenderObject identity pollution, overly-unique materials, or state divergence.
- **Global tables:** material / env-volume / **light-data** / light-index / animation / RenderObject tables, each with a declared lifetime (persistent / semi-persistent / frame-local).
- **LoadInstance / LoadRenderObject:** shader abstractions that fetch records by index/handle; hide the transport (texture vs SSBO vs uniform). `LoadInstance` is per-instance; `LoadRenderObject` is per-batch.
- **Resource-side generation (dependency graph):** each resource (Material/Light/EnvVolume/Skeleton) carries a `uint64` generation bumped in its mutator; consumers validate against cached gen -> automatic, robust invalidation.
- **Pending-write set (double-buffer):** dirty rows carried across one swap until both buffers hold the new value (Phase 6 correctness rule).
- **PerDrawUboLayout** (`Renderer.h:151-182`): existing per-draw record (~1100 bytes), bound per-draw at UBO slot 2. The **source** for `InstanceRecord`/`RenderObject`, not the target schema. Stays for the legacy per-draw path.
- **RenderJob** (`Pass.h:135-151`): one draw call's worth of data today.
- **RenderData** (`Pass.h:166-189`): partitioned job list (culled / forward opaque / alpha-masked / translucent; deferred partitions 1-2 are dead, removed in Phase 0).
- **RenderJobProcessor** (`Pass.h:191-253`): builds jobs, separates partitions, assigns lights/environments, sorts.
- **PassRequirements / ApplyRequirements** (`Pass.h:37-71`): declarative per-draw binding; 7-step deterministic bind.
- **ShadowAtlas** (`ShadowAtlas.h`): 2-layer tiered shadow array texture.
- **AABBTree** (`ToolKit/Source/AABBTree.h`): dynamic spatial structure on `Scene`, reused for light intersection.
- **MDI:** multi-draw-indirect (`glMultiDrawElementsIndirect`); native-only fast-path (Phase 8).

---

## 11. References (file index)

- Forward pass: `ToolKit/Render/RenderPass/ForwardPass.cpp` (`Render` 47-77, opaque 129-156, `RenderOpaqueHelper` 224-248, translucent 158-222).
- Per-job draw: `ToolKit/Render/Renderer.cpp` (`Render(job)` 232-356, `Render(jobs)` 381-389, `FeedUniforms` 1088-1153, RenderState compose 290-319, skinning lambda 237-276, DrawCall stat 355).
- Per-instance SOURCE: `ToolKit/Render/Renderer.h:151-182` (`PerDrawUboLayout`).
- Pass / RenderJob / RenderData / RenderJobProcessor: `ToolKit/Render/Pass.h`.
- RHI slots + limits: `ToolKit/Render/RHI.h:17-62`.
- Shadow atlas: `ToolKit/Render/ShadowAtlas.h`.
- Spatial accel: `ToolKit/Source/AABBTree.h`, `ToolKit/Resources/Scene.h:315`.
- Skinning data: `ToolKit/Resources/Animation.cpp:609-688`, `Resources/Engine/Shaders/skinning.shader`.
- PBR ubershader: `Resources/Engine/Shaders/defaultFragment.shader` (flags at 50, 60, 70, 113).
- Material data: `ToolKit/Resources/Material.h:26-43` (`MaterialCacheItem::Data`), `:117-134`.
- Program cache: `ToolKit/Render/GpuProgram.h:111`.
- GLES target: `ToolKit/Render/OpenGL/TKOpenGL.h:13` (`GLES3/gl32.h`).
