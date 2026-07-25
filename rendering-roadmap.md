# GDTK Render Instancing Refactor - Roadmap

> **Status:** Draft v3 (design phase, pending review).
> **Targets:** Native mobile (GLES 3.2, see `ToolKit/Render/OpenGL/TKOpenGL.h` -> `GLES3/gl32.h`) and WebGL 2.0; Vulkan remains a first-class desktop/backend target.
> **Related docs:** `AGENTS.md` (coding standards, Pass/PassRequirements conventions, overview-sync rules), `gdtk-overview.md` (architecture), Section 4 (Rendering Subsystem).
> **Goal:** Collapse per-object draw calls and per-object state changes into a small number of instanced draws, without rewriting the renderer.

### Changelog

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
- **Point/spot light DATA:** lives in global cache UBOs (`PointLightCache` slot 4, `SpotLightCache` slot 5), capacity `PointLightCacheItemCount=32`, `SpotLightCacheItemCount=32` (`RHI.h`). Already global - reused directly.
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
struct InstanceRecord
{
  Mat4  model;             // world matrix (always per-instance)
  uint  renderObjectIndex; // -> RenderObject table (the shared bundle)
  uint  lightListIndex;    // -> fixed-stride light-index table (position-dependent)
  uint  animKeyIndex;      // -> animation keyframe params (skinned; 0 = none)
  uint  flags;             // bit0: uniformScale, bit1: isSkinned, bit2: envOverride, ...
};
// ~64 (Mat4) + 16 = 80 bytes -> ~5 RGBA32F texels.
```

**`RenderObject`** - shared across all instances of a mesh+material+env+anim bundle. **Bound per-batch (uniform), NOT fetched per-instance.** The extension point:

```cpp
// Shared bundle. One per distinct (mesh + material + env + anim) bundle.
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

**Why the split (v2 mistake corrected):** in v2 `materialIndex`/`envVolumeIndex`/`animIndex` sat per-instance even though they are uniform across a batch (material is in the batch key). v3 hoists the shared handles into `RenderObject` (bound once per draw, zero extra per-instance fetch), leaving `InstanceRecord` with only per-instance-varying data. Adding decals/probes/GI volumes later grows `RenderObject`, never `InstanceRecord`.

**Env volume placement:** `envVolumeIndex` lives on `RenderObject`, so a batch must be env-coherent (all instances in the same primary env volume). If objects in one batch span an env boundary, either split the batch by env or use the per-instance `envOverride` flag (fetch env per-instance for that batch).

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
| Light-index table | fixed-stride per-instance point/spot index lists | `PerDrawUboLayout` arrays | **frame-local** |
| Animation key table | per-instance keyframe/blend params (skinned) | `PerDrawUboLayout` anim | **frame-local** |
| Skinning pose texture | pre-baked bone matrices | `AnimationPlayer::CreateAnimationDataTexture` `Animation.cpp:609-688` | **persistent** (on anim data change) |
| RenderObject table | shared bundles (above) | batch-build | **semi-persistent** (on bundle change) |

The light data itself (point/spot) stays in the existing global cache UBOs (slots 4/5); only the per-instance index lists move.

**Fixed-stride light list (v3):** each instance reserves `MaxPointLightPerObject + MaxSpotLightPerObject` slots in the light-index table (mobile 6+6 = 48 bytes, desktop 24+24). Fixed stride -> predictable fetch, contiguous locality, early-out via count. Mobile stride is small enough to inline into `InstanceRecord` if desired; desktop stays in a separate fixed-stride table to avoid bloating the hot record.

**Fetch locality (why the split helps):** the small fixed `InstanceRecord` is fetched once per vertex (model + indices in a tight cache footprint). Variable data (light list) is fetched only in the fragment shader. Upload bandwidth for dirty instances drops ~9x vs the raw `PerDrawUboLayout`.

### 2.2 Batching (sort-based, memcmp key, `RenderItem` output)

**`BatchBuilder`** produces a flat array of **`RenderItem`**s. Each `RenderItem` carries a small POD **`BatchKey`** (~32 bytes):

```cpp
struct BatchKey
{
  uint32 meshId;
  uint32 vertVariantId;
  uint32 fragVariantId;
  uint32 textureSetId;   // packed material-texture-set identity
  uint32 renderStateSig; // packed cull/blend/depth/stencil/primitive bits
  uint32 renderObjectId; // -> RenderObject (env/material/anim bundle)
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
- **One draw per batch:** `drawElementsInstanced(count = run length)`, with the batch's `RenderObject` bound as a per-draw uniform.
- **Key includes:**
  - `textureSetId` - material textures are bound once per batch.
  - `renderStateSig` - cull/blend/depth/stencil/primitive differ -> separate batch (correctness).
  - `renderObjectId` - the env/material/anim bundle.
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
| 1 Deferred Opaque / 2 Deferred Alpha-Masked | TBD - confirm whether deferred is live or dead (Section 9) |
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

Generations live on **resources**, not objects. Consumers cache the generation of each resource they last consumed. This is automatic propagation: the generation is bumped inside the resource's mutator, so a missed setter cannot silently produce stale data.

| Resource | Generation bumps on | Affected consumers (auto-propagated) |
|---|---|---|
| `Material` | any param change | material table row -> all `RenderObject`s using it -> their instances |
| `Light` | transform/color/range | AABBTree query -> affected instances' light-list; light's shadow slot |
| `EnvironmentComponent` | transform | instances in that volume |
| `Skeleton`/`Animation` | anim data | skinning pose texture -> skinned instances |

- Each instance row / `RenderObject` row stores cached gens for its consumed resources. Upload/rebind only when a resource gen advanced AND the instance is visible.
- **Upload rule:** upload only `(visible AND any-consumed-gen-advanced)` rows + dirty table rows. Newly-visible rows are fully stale (full write). Static visible rows are never re-uploaded after their first write.
- **Threshold (small vs full upload):** if `dirtyCount < X`, upload rows individually (grouped by page/region); if `dirtyCount >= X`, re-upload the whole used region (avoids many small `texSubImage2D` calls).

### 2.6 Texture arrays (DEFERRED - Phase 9)

**Decision: defer.** Rationale:

- Texture arrays would remove `textureSetId` from the batch key (collapsing more batches), but only help the **same mesh + different texture** content pattern (billboards, cards, decals, crowd clothing variation, unique-sign buildings).
- Typical mobile instancing wins (foliage, repeated props, identical units) already batch perfectly, because those objects share textures too. Arrays add nothing there.
- Cost is a permanent packing pipeline: uniform layer size + format (resize/pad to power-of-two buckets), shared wrap mode per array sampler (tiling `GL_REPEAT` vs clamped must be separate arrays), layer-index management, memory overhead.
- On mobile tiler GPUs (Mali/Adreno), once per-object calls are gone, the remaining per-batch texture binds are cheap relative to fragment bandwidth.

**Revisit trigger (Phase 9, profiling-gated):** add texture arrays ONLY if (a) profiling shows batch count / texture binds as the bottleneck, AND (b) the target content is "same mesh, many unique textures."

**When implemented:** use `sampler2DArray` (NOT a 2D UV atlas - arrays preserve tiling/repeat and avoid mip bleeding), bucket by size + wrap mode, store per-instance texture indices. RHI plumbing already exists (`GL_TEXTURE_2D_ARRAY` in `GLBackend.cpp`, `Tex2DArray` in `Shader.h`).

---

## 3. Frame Loop

```
CPU (frame N):
  1. Frustum cull                 -> visible instances (reuse)
  2. BatchBuilder:
       a. for each visible instance, write a RenderItem
          (BatchKey = mesh + shader-variant + texture-set + render-state + renderObject)
       b. extract InstanceRecord (model, indices, flags) from PerDrawUboLayout/RenderJob
       c. refresh global tables (material/env/light-list/anim/RenderObject) per resource gens
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
         - bind RenderObject (per-draw uniform) + s_instanceData + table bindings
         - drawElementsInstanced(count = run length)
       Legacy per-draw for: translucent, shader materials, skinned
  9. Swap read/write buffers       -> next frame writes A, reads B (Phase 6 only)
```

**Mental model:** static instance data is written once; only `(visible AND gen-stale)` rows are re-uploaded. Draw calls go from `N_objects` to `N_batches` (or one MDI call on native). Per-object UBO upload and per-object state changes are eliminated on the instanced path. What remains: per-batch binds (program + material textures + RenderObject + one instanced draw) and per-frame global binds.

---

## 4. Phasing

Incremental. Each phase is independently shippable and measurable. The legacy per-draw path is always retained as the fallback, so a broken phase never blocks shipping. **Phase 1 (Instrumentation) is a prerequisite** - without it, no later phase's win is measurable.

### Phase 0 - Documentation sync (prerequisite)

- Fix `gdtk-overview.md` Section 4.5 UBO slot table (per-draw is slot 2, not 6; custom slots start at 7).
- Fix stale comments: `Renderer.h:141`, `Renderer.h:267`, `GLBackend.cpp:466`.
- **Success:** overview matches `RHI.h:50-62`.

### Phase 1 - Instrumentation (prerequisite)

- Extend the existing `Stats` system + GPU timer queries (`StartTimerQuery`/`EndTimerQuery`) to capture:
  - draw calls, state changes, pipeline switches, texture binds
  - uploaded bytes (instance rows + tables)
  - instance count, batch count, average batch size
  - shadow redraw count
  - culled objects, visible objects
  - CPU batch-build time, GPU frame time
- Surface as a debug overlay / `FrameStats` dump.
- **Success:** every later phase's success criterion is expressed as a measured number (baseline captured here). This is the only way to validate Phase 3's draw-call reduction, Phase 4's upload reduction, Phase 5's zero-shadow-redraw.

### Phase 2 - Instance data transport + `InstanceRecord` + `RenderObject` + `LoadInstance`

- **2a - Transport proof:** add `InstanceDataBuffer` RHI abstraction (Section 5) + `LoadInstance(id)` shader abstraction. Introduce `InstanceRecord` + `RenderObject`; in 2a they may initially mirror `PerDrawUboLayout` 1:1 to prove the transport. Render a SINGLE instance through the new path, verify pixel-identical vs the current UBO path (`instanceCount = 1`, no batching).
- **2b - Lean split + global tables:** switch to the v3 split (2.1) - `RenderObject` (shared, per-draw uniform) + lean `InstanceRecord`; build the material / env-volume / light-index / animation / RenderObject tables; apply the inverse-matrix heuristic. Measure per-instance byte count and upload bandwidth (Phase 1 metrics).
- **Success (2a):** one object renders identically through the data-buffer path. **Success (2b):** per-instance data ~80 bytes; visual parity; upload bytes < baseline.
- **Risk:** RGBA32F `texelFetch` extension story on WebGL 2 (Section 7). Note: native GLES 3.2 may use SSBO directly via the same `LoadInstance` interface.

### Phase 3 - `BatchBuilder` + sort-based batching + instanced draws (the headline win)

- Separate **`BatchBuilder`** component: input = visible render jobs, output = sorted `RenderItem` array consumed by the Renderer. The Renderer no longer builds batches - this decoupling enables future multithreaded recording, indirect draw, and Vulkan secondary command buffers.
- Sort by `BatchKey` (memcmp); contiguous runs = batches; one `drawElementsInstanced` per run.
- Translucent / shader materials / skinned stay on legacy per-draw path.
- **Success (measured):** draw-call count drops from `N_objects` to `N_batches` for static opaque PBR; visual parity; average batch size reported by Phase 1 metrics.
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

- Ping-pong two instance buffers (+ tables); render frame N from buffer A while CPU writes buffer B.
- On first allocation of an instance, write its full record + table rows to BOTH buffers.
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
// Sketch - exact names TBD, must follow AGENTS.md style.
class IGraphicsBackend {
  // Create/resize an instance data buffer holding up to maxInstances records of stride bytes.
  InstanceDataBufferHandle CreateInstanceDataBuffer(uint maxInstances, uint stride);
  void DestroyInstanceDataBuffer(InstanceDataBufferHandle);
  // Upload count records starting at firstIndex (only gen-stale rows).
  void UpdateInstanceData(InstanceDataBufferHandle, uint firstIndex, uint count, const void* data);
  // Bind for the upcoming instanced draw.
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
- `RenderObject` is bound per-draw as a uniform (small, ~16 bytes) - NOT fetched per-instance. Future scene-aware bindings (probes/decals/GI) added to `RenderObject` are therefore free at the per-instance level.

**Indexing:** `gl_InstanceID + u_instanceBase` (GLSL ES 3.00 provides `gl_InstanceID`; Vulkan uses `gl_InstanceIndex`). No per-instance vertex attribute required -> zero per-instance vertex bandwidth.

**Sampler/binding slot:** reserve a free slot for `s_instanceData` (and table bindings). In-use sampler slots: 0 (`s_diffuseColor`), 1 (`s_emissiveColor`), 2 (`s_skinningPose`), 3 (`s_blendWeights`), 4 (`s_metallicRoughness`), 8 (`s_shadowAtlas`), 9 (`s_normalMap`), plus IBL/AO. A high slot (e.g. 10) or a reserved block is fine (`TextureSlotCount = 32`).

**Layout (concrete, when texture-backed):**

- `STRIDE = ceil(sizeof(InstanceRecord) / 16)` RGBA32F texels per instance.
- `WIDTH` conservative (e.g. 2048, below `gl_MaxTextureSize`). Instances pack `WIDTH / STRIDE` per row, stacked vertically.
- With the lean `InstanceRecord` (~80 bytes -> STRIDE ~5), a 2048x2048 texture holds ~840k instances.

**Mirroring rule (add to `AGENTS.md`):** the C++ `InstanceRecord` / `RenderObject` / table rows and the GLSL `LoadInstance` layouts must agree on field order/offset. Add static asserts that `sizeof(...)` matches the shader-declared strides. (This replaces the per-draw std140 mirror for the instanced path; `PerDrawUboLayout`'s std140 mirror stays for the legacy per-draw path.)

---

## 6. Shader Changes

- **Shared include `instanceDataInc.shader`:** declares `InstanceRecord` + `RenderObject` layouts, the `LoadInstance(uint id)` function (backend-specific fetch: `texelFetch` on GL/WebGL, SSBO read on native/Vulkan), and table accessors (`LoadMaterial(idx)`, `LoadEnvVolume(idx)`, `LoadLightList(idx, count)`, `LoadAnim(idx)`). `LoadRenderObject` reads the per-draw uniform (not an instance fetch).
- **`defaultVertex.shader`:** when instanced, `InstanceRecord r = LoadInstance(gl_InstanceID + u_instanceBase);` use `r.model`; derive inverse/normal matrix via the uniformScale flag; read `RenderObject ro = LoadRenderObject(r.renderObjectIndex);` for env/material indices.
- **`defaultFragment.shader`:** fetch `MaterialCacheItem::Data` via `LoadMaterial(ro.materialIndex)`, light indices via `LoadLightList(r.lightListIndex, ...)`, env volumes via `LoadEnvVolume(ro.envVolumeIndex)`. Existing `material.*InUse` branches unchanged (uniform within a batch).
- **`skinning.shader`:** unchanged for now; Phase 7 reads per-instance keyframe via `LoadAnim(r.animKeyIndex)` and the shared `s_skinningPose` texture from `ro.skeletonIndex`.
- **`perDrawDataInc.shader`:** the existing std140 mirror stays for the legacy per-draw path.

---

## 7. Platform Capabilities & Risks

| Concern | Status / action |
|---|---|
| `RGBA32F` texture + `texelFetch` (nearest) on GLES 3.2 native | Supported; native may prefer SSBO (same `LoadInstance` interface). Verify on lowest target device. |
| `RGBA32F` on WebGL 2 | Confirm exact extension requirement for nearest/texelFetch (verify `EXT_color_buffer_float` is NOT needed for sampling-only). Pin a capability table before Phase 2. |
| Native GLES 3.2 SSBO / MDI | Available (`gl32.h`). `LoadInstance` -> SSBO; Phase 8 MDI. WebGL has neither -> stays texture + per-batch instanced. |
| `gl_InstanceID` (GLSL ES 3.00) | Available. Vulkan uses `gl_InstanceIndex`. |
| Max texture size | Use `gl_MaxTextureSize`; design `WIDTH` conservatively (2048) for mobile. |
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

---

## 9. Open Decisions (need confirmation)

1. **Backend abstraction:** add `InstanceDataBuffer` + `LoadInstance` to `IGraphicsBackend` (GL/WebGL = texture, native GLES 3.2 + Vulkan = SSBO). -> Recommended.
2. **Mobile light caps:** 4 dir / 6 point / 6 spot per object, gated on `TK_GL_ES_3_0` (or a new render-profile define). Pick final numbers.
3. **Hybrid scope:** instanced path for static opaque PBR only; translucent / shader materials / skinned remain legacy. -> Recommended.
4. **Deferred partitions:** confirm whether `RenderData` partitions 1-2 are live or dead. If dead, remove; if live, decide instanced vs per-draw.
5. **`RenderObject` / `InstanceRecord` field sets:** confirm exact fields, the inverse-matrix heuristic thresholds, and per-instance vs per-batch env placement (envOverride flag). Concretize table layouts before Phase 2b.
6. **Allocator:** confirm page size for the instance-row allocator (Phase 4) vs a simpler per-batch-contiguous scheme for v1.
7. **Instrumentation surface:** confirm which metrics are overlay vs logged, and the metric naming in `FrameStatType`.

---

## 10. Glossary

- **InstanceRecord:** lean per-instance record (~80 bytes): model, renderObjectIndex, lightListIndex, animKeyIndex, flags. Populated from `PerDrawUboLayout`/`RenderJob`.
- **RenderObject:** shared bundle (material/env/skeleton indices + future probes/decals/GI), bound per-draw uniform. The stable extension point. One per (mesh + material + env + anim) bundle.
- **RenderItem:** one draw-unit emitted by `BatchBuilder`, carrying a `BatchKey` + instance index. Sortable; maps to indirect draw args (Phase 8).
- **BatchKey:** 32-byte POD (mesh + shader-variant + texture-set + render-state + renderObject). Sort key for batching (memcmp).
- **BatchBuilder:** component that turns visible render jobs into a sorted `RenderItem` array consumed by the Renderer.
- **Global tables:** material / env-volume / light-index / animation / RenderObject tables, each with a declared lifetime (persistent / semi-persistent / frame-local).
- **LoadInstance / LoadRenderObject:** shader abstractions that fetch records by index/handle; hide texture-vs-SSBO transport.
- **Resource-side generation (dependency graph):** each resource (Material/Light/EnvVolume/Skeleton) carries a generation bumped in its mutator; consumers validate against cached gen -> automatic, robust invalidation.
- **PerDrawUboLayout** (`Renderer.h:151-182`): existing per-draw record (~1100 bytes), bound per-draw at UBO slot 2. The **source** for `InstanceRecord`/`RenderObject`, not the target schema. Stays for the legacy per-draw path.
- **RenderJob** (`Pass.h:135-151`): one draw call's worth of data today.
- **RenderData** (`Pass.h:166-189`): partitioned job list (culled / deferred / forward opaque / alpha-masked / translucent).
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
