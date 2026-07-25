# GDTK Render Instancing Refactor - Roadmap

> **Status:** Draft v2 (design phase, pending review).
> **Targets:** Native mobile (GLES 3.2, see `ToolKit/Render/OpenGL/TKOpenGL.h` -> `GLES3/gl32.h`) and WebGL 2.0; Vulkan remains a first-class desktop/backend target.
> **Related docs:** `AGENTS.md` (coding standards, Pass/PassRequirements conventions, overview-sync rules), `gdtk-overview.md` (architecture), Section 4 (Rendering Subsystem).
> **Goal:** Collapse per-object draw calls and per-object state changes into a small number of instanced draws, without rewriting the renderer.

### Changelog

- **v2** - External design review incorporated. Key changes: (1) `PerDrawUboLayout` is no longer the instance schema; a lean, handle-based **`InstanceRecord`** is introduced, with heavy per-draw data (env volumes, material data, light lists) moved into global tables indexed per instance. (2) **`LoadInstance(id)`** abstraction hides the transport (texel vs SSBO) from shaders. (3) **`RenderState` added to the batch key.** (4) **Generation-based cache validation** replaces bool dirty flags. (5) Separate **`BatchBuilder`** component. (6) Native GLES 3.2 SSBO option noted.
- **v1** - Initial grounded draft.

---

## 0. Summary & Core Reframe

**This is not a new renderer.** The engine already computes everything an instanced pipeline needs. The refactor changes the **transport** of that data and the **draw granularity**:

- **From:** per draw call -> one `Renderer::Render(job)` per `RenderJob` -> one `BindPipeline` + one per-draw UBO upload (`FeedUniforms`) + one `Draw`.
- **To:** per batch -> one `drawElementsInstanced` per (mesh + shader-variant + material-texture-set + render-state-signature), reading per-instance records from an instance data buffer indexed by `gl_InstanceID`.

The per-instance data the new pipeline needs is **already computed**: it lives in `PerDrawUboLayout` (`ToolKit/Render/Renderer.h:151-182`), bound per-draw today at UBO slot 2 (`ReservedUniformBufferSlots::PerDrawData`). But `PerDrawUboLayout` is a **per-draw** record (bloated for per-instance use - see 2.1). So we reuse the **computation**, not the **layout**: a new lean **`InstanceRecord`** is populated from `PerDrawUboLayout`/`RenderJob` at batch-build time, and the heavy shared fields (environment volumes, material data, light lists) are externalized into global tables.

**Headline outcome:** draw calls go from `N_objects` to `N_batches` (e.g. 1000 objects across 20 mesh+material combos -> 20 draws). Per-object UBO upload and per-object state changes are eliminated for the static opaque PBR path. Per-instance data shrinks from ~1100 bytes (`PerDrawUboLayout`) to ~80-140 bytes (`InstanceRecord`).

---

## 1. Current Engine State (what we build on)

The refactor reuses existing machinery rather than rebuilding it. This table maps the design to the code that already exists.

| Capability | Already in engine? | Where | What is actually new |
|---|---|---|---|
| Per-instance data SOURCE (matrices, material, light indices, anim params) | YES | `PerDrawUboLayout` `Renderer.h:151-182` (per-draw UBO slot 2) | A lean `InstanceRecord` extracted from it at batch-build time (2.1) |
| Per-object CPU light assignment + active index lists | YES | `RenderJobProcessor::AssignLight` (`Pass.h:220`), `activePointLightIndices`/`activeSpotLightIndices` in `PerDrawUboLayout` | Lower per-object caps for the mobile profile |
| Spatial acceleration (BVH) | YES | `Scene::m_aabbTree`, `ToolKit/Source/AABBTree.h` (dynamic, proxy-based) | Add light-influence region query (sphere/AABB); do NOT build a new BVH |
| Tiered layered shadow atlas | YES | `ShadowAtlas` `ToolKit/Render/ShadowAtlas.h` (2 layers, Half/Quarter/Eighth, atomic batch alloc) | Shadow caching + hysteresis (2.4) |
| GPU-side skinning via texture | YES | `Renderer.cpp:237-276` (`updateAndBindSkinningTextures`), `AnimationPlayer::CreateAnimationDataTexture` `Animation.cpp:609-688`, `Resources/Engine/Shaders/skinning.shader` | Natural fallback for the non-instanced path; (optional) skinned instancing later |
| Runtime-branch PBR "ubershader" | YES | `Resources/Engine/Shaders/defaultFragment.shader` (texture presence via UBO flags: lines 50, 60, 70, 113) | None - existing branches work with per-instance material data, provided batches are uniform in texture-presence flags |
| Declarative pass binding | YES (migration effectively complete) | `PassRequirements` / `ApplyRequirements` `ToolKit/Render/Pass.h` | Coordinate so the instanced batch draw flows through `ApplyRequirements` |
| Texture arrays (RHI plumbing) | YES | `GL_TEXTURE_2D_ARRAY` `GLBackend.cpp:75,976`, `Tex2DArray` `Shader.h:40`, `sampler2DArray` (shadow atlas, blur) | DEFERRED to Phase 7 (see 2.6) |
| Instanced draws (`drawElementsInstanced`, `instanceCount > 1`) | NO | `DrawDesc.instanceCount` exists but defaults to 1 (`IGraphicsBackend.h:63`) | Core new work |
| Instance data buffer (texture/SSBO) | NO | - | New `InstanceDataBuffer` RHI abstraction (Section 5) |
| Material / env-volume / light-list global tables | PARTIAL | `MaterialCacheItem` (`Material.h:26-43`), `PointLightCache`/`SpotLightCache` UBOs (slots 4/5), `EnvironmentComponent` | Pack into instance-indexed tables (2.1) |
| Dirty-flag incremental uploads | PARTIAL | `AABBTree::Invalidate`, `Entity::m_aabbTreeNodeProxy` | Per-instance generation-based validation (2.5) |
| Shadow caching / hysteresis | NO | - | New (atop existing `ShadowAtlas`, generation-based) |
| Double buffering of instance data | NO | - | New, optional (Phase 5) |

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
- **Point/spot active INDICES per object:** today written into the per-draw UBO (`activePointLightIndices`, `activeSpotLightIndices` + counts). These are what move into the per-instance light-list table (2.1).

So per-instance light data = the active index lists only. The light data itself stays global.

### 1.4 RHI limits (`ToolKit/Render/RHI.h`)

| Constant | Current value | Cache capacity |
|---|---|---|
| `MaxDirectionalLightPerObject` | 8 | `DirectionalLightCacheItemCount = 12` |
| `MaxPointLightPerObject` | 24 | `PointLightCacheItemCount = 32` |
| `MaxSpotLightPerObject` | 24 | `SpotLightCacheItemCount = 32` |
| `TextureSlotCount` | 32 | - |
| `ShadowAtlasSlot` | 8 | - |

These are desktop-oriented. The mobile profile lowers the per-object caps (Section 2.3), which shrinks the per-instance record width.

---

## 2. Architecture

### 2.1 Instance data: lean `InstanceRecord` + global tables

> **Design principle (biggest risk avoided):** per-draw and per-instance needs diverge over time. Do NOT adopt `PerDrawUboLayout` as the instance schema. Reuse its **computation**, design a dedicated lean **`InstanceRecord`**.

`PerDrawUboLayout` is ~1100 bytes/instance. Most of that is either derivable or shared:

- 6 `Mat4` (model, modelWithoutTranslate, inverseModel, inverseTransposeModel, iblRotation, iblSecondaryRotation) - only `model` is the true per-instance input; the rest are derived or shared.
- `DrawCommand` env volumes: 2 x 11 `Vec4` = 352 bytes PER INSTANCE - but environment volumes are **scene-level spatial structures shared by many nearby objects**. Inlining them per instance is the largest waste.
- `MaterialCacheItem::Data` (4 `Vec4`) - shared by every object using the same material.

**`InstanceRecord` (target ~80-140 bytes, populated at batch-build time):**

```cpp
// Lean per-instance record. Populated from PerDrawUboLayout / RenderJob.
struct InstanceRecord
{
  Mat4  model;                   // world matrix (only true per-instance transform)
  uint  materialIndex;           // -> global material table (MaterialCacheItem::Data)
  uint  envVolumeIndex;          // -> global env-volume table (primary)
  uint  secondaryEnvVolumeIndex; // -> global env-volume table (IBL blend), or -1
  uint  lightListIndex;          // -> global light-index list table (point+spot)
  uint  animIndex;               // -> animation table (skinned/animated only; 0 = none)
  uint  flags;                   // bit0: uniformScale (skip shader inverse), bit1: isSkinned, ...
};
// ~64 (Mat4) + 24 = 88 bytes -> ~6 RGBA32F texels.
```

**Derived in shader (not stored):** `inverseModel`, `inverseTransposeModel`/normal matrix, `modelWithoutTranslate`, IBL rotations. If `flags & uniformScale`, the normal matrix is just `mat3(model)` (cheap); otherwise compute the 3x3 inverse-transpose per vertex.

**Global tables (instance-indexed, built from already-computed data):**

| Table | Contents | Source in engine | Built when |
|---|---|---|---|
| Material table | active `MaterialCacheItem::Data` (4 `Vec4` each) | `Material.h:26-43`, `Material::GetCacheItem()` | per visible-material set; persistent with dirty updates (rarely change) |
| Env-volume table | active `EnvironmentComponent`s (11 `Vec4` each: params, min/max, inverse/world transforms) | `RenderJob::EnvironmentVolume`, `AssignEnvironment` | per visible env-volume set |
| Light-index table | per-instance active point/spot index lists into the global light cache UBOs | `PerDrawUboLayout` index arrays | per instance (variable size) |
| Animation table | keyframe/blend/skin params for skinned instances | `PerDrawUboLayout` anim fields | per skinned instance; static instances -> null entry |

The light data itself (point/spot) stays in the existing global cache UBOs (slots 4/5); only the per-instance index lists move.

**Why this layout (fetch locality + bandwidth):** the small fixed `InstanceRecord` is fetched once per vertex (model + material + env indices in a tight cache footprint). The variable light-index list is fetched only in the fragment shader (where lights are evaluated). Upload bandwidth for dirty instances drops ~9x vs the raw `PerDrawUboLayout`.

### 2.2 Batching

**Batch key = (Mesh, vertex-shader-variant, fragment-shader-variant, bound-texture-set, render-state-signature).**

- Program identity in the cache is `(vert-variant-pointer, frag-variant-pointer)` (`GpuProgram.h:111`), so variant specialization is already the cache granularity.
- **Texture-set in the key:** material textures (diffuse/normal/ORM) are bound once per batch. Two objects with the same mesh + shader but different bound textures must be different batches (unless texture arrays are used - deferred, 2.6).
- **RenderState in the key (added):** the same shader + mesh + textures can still differ in `cullMode` (double-sided materials), `blendFunction`, `depthWrite`, stencil, primitive. The pipeline state object differs, so the batch must break. For opaque PBR the RenderState is mostly homogeneous (depth test/write on, cull back) so it collapses in practice, but it MUST be in the key for correctness. Signature = hash of the material-derived active state bits that affect the pipeline.
- **Within a batch, `material.*InUse` flags are uniform** -> the existing runtime branches in `defaultFragment.shader` (lines 50, 60, 70, 113) stay uniform -> no divergent branching. Per-instance varying fields: color, emissive, metallic, roughness, alpha threshold, light indices.

**The instanced batches map onto the existing partition split:** the `DrawAlphaMasked` define flip (`ForwardPass.cpp:140,150,171`) becomes a per-batch program variant, exactly as today.

**Legacy (non-instanced) path - retained:**

- **Translucent** (forward-translucent partition): needs per-fragment depth sort; cannot naively batch. Stays per-draw.
- **Shader materials** (`Material->IsShaderMaterial()`): own program per material. Stays per-draw.
- **Skinned meshes:** GPU skinning is keyed by `(skeletonId, animationId)`; each job currently has its own bone-pose texture bind. Stays per-draw by default. (Optional Phase 6: batch skinned instances sharing skeleton+animation, with per-instance skin params moved into the animation table.)

**Draw type mapping for the instanced path:**

| RenderData partition (`Pass.h:166-189`) | Instanced path? |
|---|---|
| 0 Culled | not drawn |
| 1 Deferred Opaque / 2 Deferred Alpha-Masked | TBD - confirm whether deferred is live or dead (open question, Section 9) |
| 3 Forward Opaque | YES - instanced |
| 4 Forward Alpha-Masked | YES - instanced (`DrawAlphaMasked=1` variant) |
| 5 Forward Translucent | NO - legacy per-draw |

### 2.3 CPU light assignment (reuse, do not rebuild)

- `RenderJobProcessor::AssignLight` already assigns per-object lights and produces the active index lists. Reuse as-is.
- Reuse `Scene::m_aabbTree` for light-vs-object intersection instead of a new BVH. Add a region query (sphere/AABB) to `AABBTree` (it already has `Traverse`, `Invalidate`, `Rebuild`). On light move, query affected objects and bump their light-list generation (2.5) + the light's shadow generation (2.4).
- **Mobile profile:** gate lower per-object caps on a render profile (e.g. `TK_GL_ES_3_0` -> mobile). Proposal: 4 directional / 6 point / 6 spot per object on mobile (current 8/24/24 is desktop-oriented). Because these constants are `constexpr`, profile selection is compile-time. **The per-instance record / table widths must be derived from the active profile**, not hardcoded - so the layout computation reads the same constants the shader mirrors.

### 2.4 Shadow atlas (mostly done; add caching + hysteresis)

`ShadowAtlas` (`ShadowAtlas.h`) already implements a 2-layer, Half/Quarter/Eighth tiered atlas with atomic batch allocation and a single FBO + `glFramebufferTextureLayer`. Do not rebuild.

**New work:**

- **Shadow caching (generation-based, not bool dirty):** each cached slot stores the generations it was rendered with. A slot is re-rendered only when an input generation advanced past the cached one.
  - `LightGeneration` - bumps on light transform/color/range change.
  - `GeometryGeneration` - bumps when any shadow-casting object moved within the light's influence volume (detected via the AABBTree region query).
  - `ViewGeneration` - bumps on camera-driven tier change.
  - Skip redraw when all three match the cached values. (Pull-model validation is robust: a missed setter cannot silently produce a stale shadow.)
- **Hysteresis:** add a tolerance margin to the tier-selection distance check (Half/Quarter/Eighth) so a light near a tier boundary does not thrash between resolutions every frame (e.g. promote to a larger tier at distance D, demote only beyond D + margin).
- **No double buffering for the atlas** (correct as proposed): reading and writing different slots of the same array texture in one frame is fine; the hardware handles it.

### 2.5 Dirty-flag system (generation-based)

- Reuse the existing spatial invalidation (`AABBTree::Invalidate`, `Entity::m_aabbTreeNodeProxy`).
- **Per-instance validation via generations** (same pull model as 2.4): each instance row tracks `materialGen`, `transformGen`, `lightListGen`, `envGen`. Upload a row only when its source generation advanced past the cached copy AND the instance is visible.
- **Triggers:**

| Event | Generation bumped |
|---|---|
| Object moved / transformed | `transformGen`; if AABBTree region changed -> `lightListGen` + `envGen` |
| Material scalar changed (color, roughness, etc.) | material table row dirty (material is shared -> one bump affects all instances using it) |
| Light moved / changed | AABBTree query -> affected objects' `lightListGen`; light's `LightGeneration` |
| Object becomes visible (frustum) | allocate row -> all gens stale -> full write |
| Object culled out | free row (no upload) |

- **Upload rule:** upload only rows that are `(visible AND any-gen-stale)`. Newly-visible rows are fully stale by definition. Static visible rows are never re-uploaded after their first write.
- **Threshold (small vs full upload):** if `dirtyCount < X`, upload rows individually (grouped by page/region for contiguity); if `dirtyCount >= X`, re-upload the whole used region (avoids many small `texSubImage2D` calls).

### 2.6 Texture arrays (DEFERRED - Phase 7)

**Decision: defer.** Rationale:

- Texture arrays would remove `bound-texture-set` from the batch key (collapsing more batches), but only help the **same mesh + different texture** content pattern (billboards, cards, decals, crowd clothing variation, unique-sign buildings).
- Typical mobile instancing wins (foliage, repeated props, identical units) already batch perfectly under mesh + material-texture-set batching, because those objects share textures too. Arrays add nothing there.
- Cost is a permanent packing pipeline: uniform layer size + format (resize/pad to power-of-two buckets), shared wrap mode per array sampler (tiling `GL_REPEAT` vs clamped must be separate arrays), layer-index management, memory overhead from padding.
- On mobile tiler GPUs (Mali/Adreno), once per-object calls are gone, the remaining per-batch texture binds are cheap relative to fragment bandwidth.

**Revisit trigger (Phase 7, profiling-gated):** add texture arrays ONLY if (a) profiling shows batch count / texture binds as the bottleneck, AND (b) the target content is "same mesh, many unique textures."

**When implemented:** use `sampler2DArray` (NOT a 2D UV atlas - arrays preserve tiling/repeat and avoid mip bleeding), bucket by size + wrap mode, store per-instance texture indices in the instance record. The RHI plumbing already exists (`GL_TEXTURE_2D_ARRAY` in `GLBackend.cpp`, `Tex2DArray` in `Shader.h`).

---

## 3. Frame Loop

```
CPU (frame N):
  1. Frustum cull                 -> visible instances this frame (reuse)
  2. BatchBuilder:
       a. group visible instances by batch key
          (mesh + shader-variant + texture-set + render-state-signature)
       b. extract lean InstanceRecord per instance from PerDrawUboLayout/RenderJob
       c. build/refresh global tables (material / env / light-list / anim),
          assign indices
  3. Allocate/free rows          -> newly visible = new row (all gens stale)
                                    culled-out = free row
  4. Generation check             -> visible AND any source gen advanced
  5. Upload dirty rows + tables   -> write-buffer (B) via texSubImage2D (GL/WebGL)
                                    or SSBO write (Vulkan)

GPU (frame N):
  6. Shadow pass                  -> re-render only slots whose gen advanced
  7. Forward pass:
       For each instanced batch (from BatchBuilder output):
         - ApplyRequirements      -> program, framebuffer, pass RenderState
         - bind material textures (diffuse/normal/ORM) once
         - bind s_instanceData + table textures/buffers
         - drawElementsInstanced(count = batch size)
       Legacy per-draw for: translucent, shader materials, skinned
  8. Swap read/write buffers      -> next frame writes A, reads B (Phase 5 only)
```

**Mental model (validated):** static instance data is written once; only `(visible AND gen-stale)` rows are re-uploaded. Draw calls go from `N_objects` to `N_batches`. Per-object UBO upload and per-object state changes are eliminated on the instanced path. What remains: per-batch binds (program + material textures + one instanced draw) and per-frame global binds (camera/light cache UBOs, shadow atlas, IBL, instance data + tables).

---

## 4. Phasing

Incremental. Each phase is independently shippable and measurable. The legacy per-draw path is always retained as the fallback, so a broken phase never blocks shipping.

### Phase 0 - Documentation sync (prerequisite)

- Fix `gdtk-overview.md` Section 4.5 UBO slot table (per-draw is slot 2, not 6; custom slots start at 7).
- Fix stale comments: `Renderer.h:141`, `Renderer.h:267`, `GLBackend.cpp:466`.
- **Success:** overview matches `RHI.h:50-62`.

### Phase 1 - Instance data transport + `InstanceRecord` + `LoadInstance`

- **1a - Transport proof:** add `InstanceDataBuffer` RHI abstraction (Section 5) + the `LoadInstance(id)` shader abstraction. Introduce `InstanceRecord`; in 1a it may initially mirror `PerDrawUboLayout` 1:1 to prove the transport. Render a SINGLE instance through the new path and verify pixel-identical output vs the current UBO path (`instanceCount = 1`, no batching).
- **1b - Lean record + global tables:** switch `InstanceRecord` to the lean handle-based form (2.1); build the material / env-volume / light-list / animation tables; derive inverse/normal matrices in shader. Measure per-instance byte count and upload bandwidth.
- **Success (1a):** one object renders identically through the data-buffer path. **Success (1b):** per-instance data ~80-140 bytes; visual parity maintained.
- **Risk:** RGBA32F `texelFetch` extension story on WebGL 2 (Section 7). Note: on native GLES 3.2 the backend may use SSBO directly via the same `LoadInstance` interface.

### Phase 2 - `BatchBuilder` + batching + instanced draws (the headline win)

- Introduce a **separate `BatchBuilder`** component: input = visible render jobs, output = a `BatchList` (the thin command stream consumed by the Renderer). The Renderer no longer builds batches itself - this decoupling enables future multithreaded recording, indirect draw, and Vulkan secondary command buffers.
- Group visible forward-opaque / forward-alpha-masked jobs by batch key including **RenderState-signature** (2.2).
- Replace the per-job loop in `RenderOpaqueHelper` (`ForwardPass.cpp:224-248`) with: consume `BatchList` -> per batch, one `drawElementsInstanced`.
- Translucent / shader materials / skinned stay on legacy per-draw path.
- **Success:** draw-call count drops from `N_objects` to `N_batches` for static opaque PBR; visual parity maintained.
- **Risk:** batch granularity must include texture-set AND RenderState, or pipelines/branches diverge.
- **Scope guard:** the `BatchList` is a thin command stream, NOT a full render graph. A full render graph / GPU-driven pipeline is a separate future track that builds on this output.

### Phase 3 - Generation-based incremental uploads + allocator

- Per-instance generation validation (2.5).
- Upload only `(visible AND gen-stale)` rows + dirty table rows.
- Threshold: small dirty set -> per-row upload (grouped by region); large dirty set -> whole-region re-upload.
- **Allocator:** page/chunk allocator for instance rows (locality for upload, L2 cache, future streaming) over a plain free-list. Resolution of the stable-index (for gen tracking) vs compact-index (for locality) tension: stable handle = (page, offset); dirty uploads grouped per page.
- **Success:** a static scene with a few moving objects uploads near-zero bytes per frame.

### Phase 4 - Shadow caching + hysteresis (atop existing `ShadowAtlas`)

- Generation-based slot validation (`LightGeneration` / `GeometryGeneration` / `ViewGeneration`, 2.4).
- Hysteresis margin on tier transitions.
- **Success:** a scene with static shadows issues zero shadow draws per frame.

### Phase 5 - Double buffering (optional, profiling-gated)

- Ping-pong two instance buffers (+ tables); render frame N from buffer A while CPU writes buffer B.
- On first allocation of an instance, write its full record + table rows to BOTH buffers.
- **Note:** introduces 1 frame of latency. Acceptable for most content; revisit for camera-locked objects.
- **Success:** no `texSubImage2D`-induced stalls under high dirty churn.

### Phase 6 - Skinned instancing (optional)

- Batch skinned instances sharing skeleton + animation (same `s_skinningPose` texture).
- Per-instance skin params move into the animation table (indexed by `animIndex`).
- **Success:** crowds of identical animated characters batch.

### Phase 7 - Texture arrays (optional, profiling-gated)

- See 2.6. Only if content is "same mesh, many unique textures" and batch count is the bottleneck.

### Vulkan parity

Threaded through Phase 1 (the `LoadInstance` interface lets Vulkan use SSBO directly while GL/WebGL uses a texture). Every phase's definition of done includes "Vulkan renders identically."

---

## 5. RHI / Backend Abstraction

Add a logical instance-data concept to `IGraphicsBackend` so the per-instance transport is backend-agnostic. **Shaders never see the transport** - they call `LoadInstance(id)` and access fields; the fetch mechanism lives in a backend-specific include.

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
| Native GLES 3.2 | SSBO (available - preferred over texture) | SSBO read |
| Vulkan | SSBO | SSBO read |
| (future D3D12) | StructuredBuffer | SRV read |
| (future Metal) | Buffer | buffer read |

- Because `LoadInstance` hides the fetch, switching a backend from texture to SSBO changes only the backend include, never the shader body. This also means **WebGPU / Metal can be added later without touching shaders.**

**Indexing:** `gl_InstanceID + u_instanceBase` (GLSL ES 3.00 provides `gl_InstanceID`; Vulkan uses `gl_InstanceIndex`). No per-instance vertex attribute required -> zero per-instance vertex bandwidth.

**Sampler/binding slot:** reserve a free slot for `s_instanceData` (and table bindings). In-use sampler slots: 0 (`s_diffuseColor`), 1 (`s_emissiveColor`), 2 (`s_skinningPose`), 3 (`s_blendWeights`), 4 (`s_metallicRoughness`), 8 (`s_shadowAtlas`), 9 (`s_normalMap`), plus IBL/AO. A high slot (e.g. 10) or a reserved block is fine (`TextureSlotCount = 32`).

**Layout (concrete, when texture-backed):**

- `STRIDE = ceil(sizeof(InstanceRecord) / 16)` RGBA32F texels per instance.
- `WIDTH` conservative (e.g. 2048, below `gl_MaxTextureSize`). Instances pack `WIDTH / STRIDE` per row, stacked vertically.
- With the lean `InstanceRecord` (~88 bytes -> STRIDE ~6), a 2048x2048 texture holds ~700k instances.

**Mirroring rule (add to `AGENTS.md`):** the C++ `InstanceRecord` and the GLSL `LoadInstance` layout must agree on field order/offset. Add a static assert that `sizeof(InstanceRecord)` matches the shader-declared stride. Same rule applies to each global table's row layout. (This replaces the per-draw std140 mirror for the instanced path; `PerDrawUboLayout`'s std140 mirror stays for the legacy per-draw path.)

---

## 6. Shader Changes

- **Shared include `instanceDataInc.shader`:** declares the `InstanceRecord` layout and the `LoadInstance(uint id)` function (backend-specific fetch: `texelFetch` on GL/WebGL, SSBO read on native/Vulkan). Also declares table accessors (`LoadMaterial(idx)`, `LoadEnvVolume(idx)`, `LoadLightList(idx)`, `LoadAnim(idx)`).
- **`defaultVertex.shader`:** when instanced, `InstanceRecord r = LoadInstance(gl_InstanceID + u_instanceBase);` use `r.model`; derive inverse/normal matrices (skip inverse if `r.flags & uniformScale`); read env/material indices from `r`.
- **`defaultFragment.shader`:** fetch `MaterialCacheItem::Data` via `LoadMaterial(r.materialIndex)`, light indices via `LoadLightList(r.lightListIndex)`, env volumes via `LoadEnvVolume(...)`. The existing `material.*InUse` branches are unchanged (uniform within a batch).
- **`skinning.shader`:** unchanged for now; Phase 6 reads per-instance skin params via `LoadAnim(r.animIndex)`.
- **`perDrawDataInc.shader`:** the existing std140 mirror stays for the legacy per-draw path.

---

## 7. Platform Capabilities & Risks

| Concern | Status / action |
|---|---|
| `RGBA32F` texture + `texelFetch` (nearest) on GLES 3.2 native | Supported; native may prefer SSBO instead (same `LoadInstance` interface). Verify on lowest target device. |
| `RGBA32F` on WebGL 2 | Sampling float textures: confirm exact extension requirement for nearest/texelFetch (verify `EXT_color_buffer_float` is NOT needed for sampling-only). Pin a capability table before Phase 1. |
| Native GLES 3.2 SSBO | Available (`gl32.h`). `LoadInstance` lets the native backend use SSBO while WebGL uses texture - no shader change. |
| `gl_InstanceID` (GLSL ES 3.00) | Available. Vulkan uses `gl_InstanceIndex`. |
| Max texture size | Use `gl_MaxTextureSize`; design `WIDTH` conservatively (2048) for mobile. |
| Divergent branching | Prevented by including texture-set in the batch key. |
| RenderState divergence | Prevented by including render-state-signature in the batch key. |
| Small `texSubImage2D` churn | Phase 3 threshold + whole-region fallback. |
| Double-buffer latency | Phase 5 only; 1-frame lag, documented. |

---

## 8. Documentation Debt (Phase 0)

- `gdtk-overview.md` Section 4.5 UBO slot table is stale (per-draw shown as slot 6; actual slot 2, `RHI.h:50-62`).
- Three stale "slot 6" comments: `Renderer.h:141`, `Renderer.h:267`, `GLBackend.cpp:466`.
- `PerDrawUboLayout` "active migration" docstring is stale (migration is complete).

---

## 9. Open Decisions (need confirmation)

1. **Backend abstraction:** add `InstanceDataBuffer` + `LoadInstance` to `IGraphicsBackend` (GL/WebGL = texture, native GLES 3.2 + Vulkan = SSBO), keeping all backends first-class. -> Recommended.
2. **Mobile light caps:** 4 dir / 6 point / 6 spot per object, gated on `TK_GL_ES_3_0` (or a new render-profile define). Pick final numbers.
3. **Hybrid scope:** instanced path for static opaque PBR only; translucent / shader materials / skinned remain on the legacy per-draw path. -> Recommended (low-risk migration).
4. **Deferred partitions:** confirm whether `RenderData` partitions 1-2 (deferred opaque / deferred alpha-masked) are live or dead code. If dead, remove; if live, decide whether they go instanced or stay per-draw.
5. **`InstanceRecord` field set (from review):** confirm the exact field set and which derived matrices are computed in-shader vs stored. Concretize material/env/light-list/anim table layouts before Phase 1b.
6. **Allocator:** confirm page size for the instance-row allocator (Phase 3) vs a simpler per-batch-contiguous scheme for v1.

---

## 10. Glossary

- **InstanceRecord:** the lean, handle-based per-instance record (~80-140 bytes). Populated from `PerDrawUboLayout`/`RenderJob` at batch-build time. Distinct from `PerDrawUboLayout` by design (per-draw vs per-instance needs diverge).
- **PerDrawUboLayout** (`Renderer.h:151-182`): the existing per-draw record (~1100 bytes), bound per-draw at UBO slot 2. The **source** for `InstanceRecord`, not the target schema. Stays for the legacy per-draw path.
- **Global tables:** material / env-volume / light-list / animation tables, instance-indexed, holding the heavy shared data externalized out of `InstanceRecord`.
- **LoadInstance(id):** shader abstraction that fetches an `InstanceRecord` by index; hides texture-vs-SSBO transport.
- **BatchBuilder:** separate component that turns visible render jobs into a `BatchList` (thin command stream) consumed by the Renderer.
- **Batch key:** `(Mesh, vert-variant, frag-variant, bound-texture-set, render-state-signature)`.
- **RenderJob** (`Pass.h:135-151`): one draw call's worth of data today (Entity, Mesh, Material, lights, transform, animData).
- **RenderData** (`Pass.h:166-189`): partitioned job list (culled / deferred / forward opaque / alpha-masked / translucent).
- **RenderJobProcessor** (`Pass.h:191-253`): builds jobs, separates partitions, assigns lights/environments, sorts.
- **PassRequirements / ApplyRequirements** (`Pass.h:37-71`): declarative per-draw binding; 7-step deterministic bind.
- **ShadowAtlas** (`ShadowAtlas.h`): 2-layer tiered shadow array texture.
- **AABBTree** (`ToolKit/Source/AABBTree.h`): dynamic spatial structure on `Scene`, reused for light intersection.
- **Generation-based validation:** pull-model cache validity via version counters (instance rows, shadow slots) instead of bool dirty flags.

---

## 11. References (file index)

- Forward pass: `ToolKit/Render/RenderPass/ForwardPass.cpp` (`Render` 47-77, opaque 129-156, `RenderOpaqueHelper` 224-248, translucent 158-222).
- Per-job draw: `ToolKit/Render/Renderer.cpp` (`Render(job)` 232-356, `Render(jobs)` 381-389, `FeedUniforms` 1088-1153, RenderState compose 290-319, skinning lambda 237-276).
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
