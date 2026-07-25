# GDTK Render Instancing Refactor - Roadmap

> **Status:** Draft v1 (design phase, pending review).
> **Targets:** Native mobile (GLES 3.2, see `ToolKit/Render/OpenGL/TKOpenGL.h` -> `GLES3/gl32.h`) and WebGL 2.0; Vulkan remains a first-class desktop/backend target.
> **Related docs:** `AGENTS.md` (coding standards, Pass/PassRequirements conventions, overview-sync rules), `gdtk-overview.md` (architecture), Section 4 (Rendering Subsystem).
> **Goal:** Collapse per-object draw calls and per-object state changes into a small number of instanced draws, without rewriting the renderer.

---

## 0. Summary & Core Reframe

**This is not a new renderer.** The engine already computes everything an instanced pipeline needs. The refactor changes the **transport** of that data and the **draw granularity**:

- **From:** per draw call -> one `Renderer::Render(job)` per `RenderJob` -> one `BindPipeline` + one per-draw UBO upload (`FeedUniforms`) + one `Draw`.
- **To:** per batch -> one `drawElementsInstanced` per (mesh + shader-variant + material-texture-set), reading per-instance records from a data texture indexed by `gl_InstanceID`.

The per-instance record the new pipeline needs **already exists**: `PerDrawUboLayout` (`ToolKit/Render/Renderer.h:151-182`). Today it is bound per-draw at UBO slot 2 (`ReservedUniformBufferSlots::PerDrawData`). We move those records into an instance data buffer (texture on GL/WebGL, SSBO on Vulkan) and index them per instance.

**Headline outcome:** draw calls go from `N_objects` to `N_batches` (e.g. 1000 objects across 20 mesh+material combos -> 20 draws). Per-object UBO upload and per-object state changes are eliminated for the static opaque PBR path.

---

## 1. Current Engine State (what we build on)

The refactor reuses existing machinery rather than rebuilding it. This table maps the design to the code that already exists.

| Capability | Already in engine? | Where | What is actually new |
|---|---|---|---|
| Per-instance data record (matrices, material, light indices, anim params) | YES | `PerDrawUboLayout` `Renderer.h:151-182` | Transport: store as rows in a data buffer, index per instance (today: bound per-draw at UBO slot 2) |
| Per-object CPU light assignment + active index lists | YES | `RenderJobProcessor::AssignLight` (`Pass.h:220`), `activePointLightIndices`/`activeSpotLightIndices` in `PerDrawUboLayout` | Lower per-object caps for the mobile profile |
| Spatial acceleration (BVH) | YES | `Scene::m_aabbTree`, `ToolKit/Source/AABBTree.h` (dynamic, proxy-based) | Add light-influence region query (sphere/AABB); do NOT build a new BVH |
| Tiered layered shadow atlas | YES | `ShadowAtlas` `ToolKit/Render/ShadowAtlas.h` (2 layers, Half/Quarter/Eighth, atomic batch alloc) | Shadow caching (static reuse) + hysteresis on tier transitions |
| GPU-side skinning via texture | YES | `Renderer.cpp:237-276` (`updateAndBindSkinningTextures`), `AnimationPlayer::CreateAnimationDataTexture` `Animation.cpp:609-688`, `Resources/Engine/Shaders/skinning.shader` | Natural fallback for non-instanced path; (optional) skinned instancing later |
| Runtime-branch PBR "ubershader" | YES | `Resources/Engine/Shaders/defaultFragment.shader` (texture presence via UBO flags: lines 50, 60, 70, 113) | None - the existing branches work with per-instance material data, provided batches are uniform in texture-presence flags |
| Declarative pass binding | YES (migration effectively complete) | `PassRequirements` / `ApplyRequirements` `ToolKit/Render/Pass.h` | Coordinate so the instanced batch draw flows through `ApplyRequirements` |
| Texture arrays (RHI plumbing) | YES | `GL_TEXTURE_2D_ARRAY` `GLBackend.cpp:75,976`, `Tex2DArray` `Shader.h:40`, `sampler2DArray` (shadow atlas, blur) | DEFERRED to Phase 7 (see 2.6) |
| Instanced draws (`drawElementsInstanced`, `instanceCount > 1`) | NO | `DrawDesc.instanceCount` exists but defaults to 1 (`IGraphicsBackend.h:63`) | Core new work |
| Instance data buffer (texture/SSBO) | NO | - | New `InstanceDataBuffer` RHI abstraction (Section 5) |
| Dirty-flag incremental uploads | PARTIAL | `AABBTree::Invalidate`, `Entity::m_aabbTreeNodeProxy` | Per-instance dirty for the data buffer |
| Shadow caching / hysteresis | NO | - | New (atop existing `ShadowAtlas`) |
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

### 1.2 Per-draw UBO (slot 2) - migration is complete

The per-draw UBO is bound at **slot `ReservedUniformBufferSlots::PerDrawData == 2`** (`ToolKit/Render/RHI.h:50-62`), NOT slot 6. The "active migration" docstring is stale: every per-draw value already flows through this UBO; there are no remaining `SetUniform`/`glUniform*` calls for per-draw data in `Render(job)`.

- `Renderer::FeedUniforms` (`Renderer.cpp:1088-1153`) populates `PerDrawUboLayout`, then `Invalidate()` + `Map()` + `m_backend->SubmitPerDrawData(...)`.
- GL: `SubmitPerDrawData` is a no-op (the UBO is already bound via `glBindBufferBase`).
- Vulkan: `memcpy` into a per-frame dynamic-offset ring (`AllocatePerDrawSlot`), offset fed as `pDynamicOffsets[0]`.

**Stale comments to fix (Phase 0):** `Renderer.h:141` (`// PerDrawGpuBuffer (slot 6)`), `Renderer.h:267` (per-draw UBO), `GLBackend.cpp:466` (`// slot 6, 'PerDrawData' UBO`).

### 1.3 Light binding model (important for what stays global vs per-instance)

- **Directional lights:** bound ONCE per pass into the global `directionalLightBuffer` (slot 3). NOT per-instance.
- **Point/spot light DATA:** lives in global cache UBOs (`PointLightCache` slot 4, `SpotLightCache` slot 5), capacity `PointLightCacheItemCount=32`, `SpotLightCacheItemCount=32` (`RHI.h`).
- **Point/spot active INDICES per object:** today written into the per-draw UBO (`activePointLightIndices`, `activeSpotLightIndices` + counts). These are what move into the per-instance record.

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

### 2.1 Instance data record (schema + transport)

**Schema = `PerDrawUboLayout`** (`Renderer.h:151-182`). Approximate contents:

- 6 `Mat4` (model, modelWithoutTranslate, inverseModel, inverseTransposeModel, iblRotation, iblSecondaryRotation)
- viewport (`Vec2`)
- `DrawCommand` (2 global `Vec4` + 2 environment volumes x 11 `Vec4` each)
- `MaterialCacheItem::Data` (4 `Vec4`, `Material.h:26-43`)
- `activePointLightIndices` + `activeSpotLightIndices` (int arrays) + counts
- animation scalars (`keyFrameData`, `blendFrameData`, `animBlendFactor`, `skinParams`)

Size today is roughly ~1100 bytes/instance (exact value computed from the layout struct; must be asserted to match the GLSL side).

**Transport = a new `InstanceDataBuffer` abstraction on `IGraphicsBackend`** (Section 5):

- **GL / WebGL:** an `RGBA32F` `GL_TEXTURE_2D`, read via `texelFetch` (no SSBO in GLES 3.0 / WebGL 2). Nearest filtering (texelFetch ignores filter state).
- **Vulkan:** an SSBO (or a UBO array if within limits). Same logical schema.
- The C++ side writes the same `PerDrawUboLayout` bytes; only the backend decides texture vs SSBO.

**Indexing = `gl_InstanceID + u_instanceBase`** (GLSL ES 3.00 provides `gl_InstanceID`; Vulkan uses `gl_InstanceIndex`). No per-instance vertex attribute is required -> zero per-instance vertex bandwidth. (A per-instance attribute `a_ObjectIndex` is the fallback if a base-offset uniform is undesirable.)

**Layout (concrete):**

- `STRIDE = ceil(sizeof(PerDrawUboLayout) / 16)` RGBA32F texels per instance, rounded up for future fields.
- Pick a texture `WIDTH` (e.g. 2048, safe below `gl_MaxTextureSize`). Instances pack `WIDTH / STRIDE` per row, stacked vertically.
- `fetchTexel(instanceIndex, k)` = `texelFetch(s_instanceData, ivec2((instanceIndex % perRow) * STRIDE + k, instanceIndex / perRow), 0)`.
- Max instances = `WIDTH * HEIGHT / STRIDE`. A 2048x2048 texture at STRIDE~70 holds ~60k instances - ample.

**Sampler slot:** reserve a free texture slot for `s_instanceData`. In-use sampler slots: 0 (`s_diffuseColor`), 1 (`s_emissiveColor`), 2 (`s_skinningPose`), 3 (`s_blendWeights`), 4 (`s_metallicRoughness`), 8 (`s_shadowAtlas`), 9 (`s_normalMap`), plus IBL/AO. A high slot (e.g. 10) or a reserved block is fine (`TextureSlotCount = 32`).

**Row allocator:** a free-list over `[0, maxInstances)`. Each visible instance gets a stable `globalTextureIndex` while visible; freed when culled out. Newly-visible = newly-allocated = inherently dirty.

### 2.2 Batching

**Batch key = (Mesh, vertex-shader-variant, fragment-shader-variant, bound-texture-set).**

- Program identity in the cache is `(vert-variant-pointer, frag-variant-pointer)` (`GpuProgram.h:111`), so variant specialization is already the cache granularity.
- **Why texture-set is part of the key:** material textures (diffuse/normal/ORM) are bound once per batch. Two objects with the same mesh + shader but different bound textures must be different batches (unless texture arrays are used - deferred, 2.6).
- **Within a batch, `material.*InUse` flags are uniform** -> the existing runtime branches in `defaultFragment.shader` (lines 50, 60, 70, 113) stay uniform -> no divergent branching. Per-instance varying fields: color, emissive, metallic, roughness, alpha threshold, light indices.

**The two instanced batches map onto the existing partition split:** the `DrawAlphaMasked` define flip (`ForwardPass.cpp:140,150,171`) becomes a per-batch program variant, exactly as today.

**Legacy (non-instanced) path - retained:**

- **Translucent** (forward-translucent partition): needs per-fragment depth sort; cannot naively batch. Stays per-draw.
- **Shader materials** (`Material->IsShaderMaterial()`): own program per material. Stays per-draw.
- **Skinned meshes:** GPU skinning is keyed by `(skeletonId, animationId)`; each job currently has its own bone-pose texture bind. Stays per-draw by default. (Optional Phase 6: batch skinned instances sharing skeleton+animation, with per-instance skin params moved into the data record.)

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
- Reuse `Scene::m_aabbTree` for light-vs-object intersection instead of a new BVH. Add a region query (sphere/AABB) to `AABBTree` (it already has `Traverse`, `Invalidate`, `Rebuild`). On light move, query affected objects and mark their `isLightsDirty` + the light's `isShadowDirty`.
- **Mobile profile:** gate lower per-object caps on a render profile (e.g. `TK_GL_ES_3_0` -> mobile). Proposal: 4 directional / 6 point / 6 spot per object on mobile (current 8/24/24 is desktop-oriented). Because these constants are `constexpr`, profile selection is compile-time. **The per-instance record width must be derived from the active profile**, not hardcoded - so the layout computation reads the same constants the shader mirrors.

### 2.4 Shadow atlas (mostly done; add caching + hysteresis)

`ShadowAtlas` (`ShadowAtlas.h`) already implements a 2-layer, Half/Quarter/Eighth tiered atlas with atomic batch allocation and a single FBO + `glFramebufferTextureLayer`. Do not rebuild.

**New work:**

- **Shadow caching:** if a light and all geometry in its influence volume are static, the slot's shadow is valid across frames -> skip redraw. Track `isShadowDirty` per allocated slot. Invalidation triggers: light transform/color change, any shadow-casting object moved within the light's influence, camera-driven tier change.
- **Hysteresis:** add a tolerance margin to the tier-selection distance check (Half/Quarter/Eighth) so a light near a tier boundary does not thrash between resolutions every frame (e.g. promote to a larger tier at distance D, demote only beyond D + margin).
- **No double buffering for the atlas** (correct as proposed): reading and writing different slots of the same array texture in one frame is fine; the hardware handles it.

### 2.5 Dirty-flag system

- Reuse the existing spatial invalidation (`AABBTree::Invalidate`, `Entity::m_aabbTreeNodeProxy`).
- **New: per-instance dirty for the data buffer.** Triggers:

| Event | Invalidation |
|---|---|
| Object moved / transformed | recompute matrix; re-trigger light assignment if the AABBTree region changed -> `isLightsDirty`; mark instance row dirty |
| Material scalar changed (color, roughness, etc.) | mark instance row dirty (material data) |
| Light moved / changed | query AABBTree for affected objects -> their `isLightsDirty`; light's `isShadowDirty = true` |
| Object becomes visible (frustum) | allocate row -> inherently dirty (full write) |
| Object culled out | free row (no upload) |

- **Upload rule:** upload only rows that are `(visible AND dirty)`. Newly-visible rows are dirty by definition. Static visible rows are never re-uploaded after their first write.

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
  2. Batch visible instances      -> group by (mesh + shader-variant + texture-set)
  3. Allocate/free rows          -> newly visible = new row (inherently dirty)
                                    culled-out = free row
  4. Dirty-check                  -> visible AND changed since last frame
                                    (matrix / light indices / material scalars)
  5. Upload dirty rows            -> write-buffer (B) via texSubImage2D (GL/WebGL)
                                    or SSBO write (Vulkan)

GPU (frame N):
  6. Shadow pass                  -> redraw only isShadowDirty slots into the atlas
  7. Forward pass:
       For each instanced batch:
         - ApplyRequirements      -> program, framebuffer, pass RenderState
         - bind material textures (diffuse/normal/ORM) once
         - bind s_instanceData (read-buffer A)
         - drawElementsInstanced(count = batch size)
       Legacy per-draw for: translucent, shader materials, skinned
  8. Swap read/write buffers      -> next frame writes A, reads B (Phase 5 only)
```

**Mental model (validated):** static instance data is written once; only `(visible AND dirty)` rows are re-uploaded. Draw calls go from `N_objects` to `N_batches`. Per-object UBO upload and per-object state changes are eliminated on the instanced path. What remains: per-batch binds (program + material textures + one instanced draw) and per-frame global binds (camera/light cache UBOs, shadow atlas, IBL, instance data).

---

## 4. Phasing

Incremental. Each phase is independently shippable and measurable. The legacy per-draw path is always retained as the fallback, so a broken phase never blocks shipping.

### Phase 0 - Documentation sync (prerequisite)

- Fix `gdtk-overview.md` Section 4.5 UBO slot table (per-draw is slot 2, not 6; custom slots start at 7).
- Fix stale comments: `Renderer.h:141`, `Renderer.h:267`, `GLBackend.cpp:466`.
- **Success:** overview matches `RHI.h:50-62`.

### Phase 1 - Instance data transport (proof, no batching)

- Add `InstanceDataBuffer` RHI abstraction on `IGraphicsBackend` (Section 5).
- Implement GL backend: `RGBA32F` `GL_TEXTURE_2D` + `texSubImage2D` writes.
- Write `PerDrawUboLayout` for a SINGLE instance into the buffer; render one instance via the new path (`texelFetch` in shader) and verify pixel-identical output vs the current UBO path.
- No batching yet (1 instance per draw, `instanceCount = 1`).
- **Success:** one object renders identically through the data-texture path.
- **Risk:** RGBA32F `texelFetch` extension story on WebGL 2 (Section 7).

### Phase 2 - Batching + instanced draws (the headline win)

- Group visible forward-opaque / forward-alpha-masked jobs by batch key (2.2).
- Replace the per-job loop in `RenderOpaqueHelper` (`ForwardPass.cpp:224-248`) with: build batch list -> per batch, one `drawElementsInstanced`.
- Translucent / shader materials / skinned stay on legacy per-draw path.
- **Success:** draw-call count drops from `N_objects` to `N_batches` for static opaque PBR; visual parity maintained.
- **Risk:** batch granularity must include texture-set, or runtime branches diverge.

### Phase 3 - Dirty-flag incremental uploads

- Per-instance dirty tracking (2.5).
- Upload only `(visible AND dirty)` rows.
- Whole-region re-upload fallback if the dirty fraction is high (avoids many small `texSubImage2D` calls).
- **Success:** a static scene with a few moving objects uploads near-zero bytes per frame.

### Phase 4 - Shadow caching + hysteresis (atop existing `ShadowAtlas`)

- `isShadowDirty` per slot; static-light + static-geometry reuse.
- Hysteresis margin on tier transitions.
- **Success:** a scene with static shadows issues zero shadow draws per frame.

### Phase 5 - Double buffering (optional, profiling-gated)

- Ping-pong two instance buffers; render frame N from buffer A while CPU writes buffer B.
- On first allocation of an instance, write its full record to BOTH buffers.
- **Note:** introduces 1 frame of latency. Acceptable for most content; revisit for camera-locked objects.
- **Success:** no `texSubImage2D`-induced stalls under high dirty churn.

### Phase 6 - Skinned instancing (optional)

- Batch skinned instances sharing skeleton + animation (same `s_skinningPose` texture).
- Move per-instance skin params (`_skinParams`, `_keyFrameData`, `_blendFrameData`) from the single per-draw UBO into the instance record.
- **Success:** crowds of identical animated characters batch.

### Phase 7 - Texture arrays (optional, profiling-gated)

- See 2.6. Only if content is "same mesh, many unique textures" and batch count is the bottleneck.

### Vulkan parity

Threaded through Phase 1 (SSBO implementation of `InstanceDataBuffer`) so Vulkan does not become a separate project. Every phase's definition of done includes "Vulkan renders identically."

---

## 5. RHI / Backend Abstraction

Add a logical instance-data concept to `IGraphicsBackend` so the per-instance transport is backend-agnostic:

```
// Sketch - exact names TBD, must follow AGENTS.md style.
class IGraphicsBackend {
  // Create/resize an instance data buffer holding up to maxInstances records of stride bytes.
  InstanceDataBufferHandle CreateInstanceDataBuffer(uint maxInstances, uint stride);
  void DestroyInstanceDataBuffer(InstanceDataBufferHandle);
  // Upload count records starting at firstIndex (only dirty rows).
  void UpdateInstanceData(InstanceDataBufferHandle, uint firstIndex, uint count, const void* data);
  // Bind for the upcoming instanced draw (GL: texture slot; Vulkan: SSBO descriptor).
  void BindInstanceData(InstanceDataBufferHandle, uint baseInstance);
};
```

- **GL / WebGL:** backed by `RGBA32F` `GL_TEXTURE_2D`; `BindInstanceData` binds the texture to `s_instanceData` and sets `u_instanceBase`.
- **Vulkan:** backed by an SSBO; `BindInstanceData` updates the descriptor set + push-constant base offset.

**New mirroring rule (add to `AGENTS.md`):** unlike the std140 UBO structs, the instance record is a flat texel array on GL/WebGL. The C++ writer and the GLSL reader must agree on a `STRIDE`-texel layout. Add a build-time/static-assert that `sizeof(PerDrawUboLayout)` matches the shader-declared stride (mirror of the existing std140 mirroring rule, but for texel layout instead of std140).

---

## 6. Shader Changes

- **`Resources/Engine/Shaders/defaultVertex.shader`:** add the instance-data fetch. When `u_isInstanced`, compute the instance index from `gl_InstanceID + u_instanceBase`, fetch matrices from `s_instanceData` via `fetchTexel`, and use them instead of the per-draw UBO matrices. Keep the non-instanced path (`#ifdef` / uniform branch) so legacy draws still work.
- **`Resources/Engine/Shaders/defaultFragment.shader`:** fetch `MaterialCacheItem::Data` and light indices from `s_instanceData` (same `fetchTexel` helper) when instanced. The existing `material.*InUse` branches are unchanged (uniform within a batch).
- **`skinning.shader`:** unchanged for now; Phase 6 moves per-instance skin params into the instance record.
- **Shared include:** add an `instanceDataInc.shader` declaring the texel-fetch helper and the record layout (the GLSL mirror of `PerDrawUboLayout`).
- **`perDrawDataInc.shader`:** the existing std140 mirror stays for the legacy per-draw path.

---

## 7. Platform Capabilities & Risks

| Concern | Status / action |
|---|---|
| `RGBA32F` texture creation + `texelFetch` (nearest) on GLES 3.2 native | Supported. Verify on lowest target device. |
| `RGBA32F` on WebGL 2 | Sampling float textures: confirm exact extension requirement (likely fine for nearest/texelFetch; verify `EXT_color_buffer_float` is NOT needed for sampling-only). Pin a capability table before Phase 1. |
| `gl_InstanceID` (GLSL ES 3.00) | Available. Vulkan uses `gl_InstanceIndex`. |
| Max texture size | Use `gl_MaxTextureSize`; design `WIDTH` conservatively (2048) for mobile. |
| Divergent branching | Prevented by including texture-set in the batch key. |
| Small `texSubImage2D` churn | Phase 3 whole-region fallback when dirty fraction is high. |
| Double-buffer latency | Phase 5 only; 1-frame lag, documented. |

---

## 8. Documentation Debt (Phase 0)

- `gdtk-overview.md` Section 4.5 UBO slot table is stale (per-draw shown as slot 6; actual slot 2, `RHI.h:50-62`).
- Three stale "slot 6" comments: `Renderer.h:141`, `Renderer.h:267`, `GLBackend.cpp:466`.
- `PerDrawUboLayout` "active migration" docstring is stale (migration is complete).

---

## 9. Open Decisions (need confirmation)

1. **Backend abstraction:** add `InstanceDataBuffer` to `IGraphicsBackend` (GL = texture, Vulkan = SSBO), keeping Vulkan first-class. -> Recommended.
2. **Mobile light caps:** 4 dir / 6 point / 6 spot per object, gated on `TK_GL_ES_3_0` (or a new render-profile define). Pick final numbers.
3. **Hybrid scope:** instanced path for static opaque PBR only; translucent / shader materials / skinned remain on the legacy per-draw path. -> Recommended (low-risk migration).
4. **Deferred partitions:** confirm whether `RenderData` partitions 1-2 (deferred opaque / deferred alpha-masked) are live or dead code. If dead, remove; if live, decide whether they go instanced or stay per-draw.

---

## 10. Glossary

- **PerDrawUboLayout** (`Renderer.h:151-182`): the per-instance record. Today bound per-draw at UBO slot 2; the refactor moves it into an instance data buffer.
- **RenderJob** (`Pass.h:135-151`): one draw call's worth of data today (Entity, Mesh, Material, lights, transform, animData).
- **RenderData** (`Pass.h:166-189`): partitioned job list (culled / deferred / forward opaque / alpha-masked / translucent).
- **RenderJobProcessor** (`Pass.h:191-253`): builds jobs, separates partitions, assigns lights/environments, sorts.
- **PassRequirements / ApplyRequirements** (`Pass.h:37-71`): declarative per-draw binding; 7-step deterministic bind.
- **ShadowAtlas** (`ShadowAtlas.h`): 2-layer tiered shadow array texture.
- **AABBTree** (`ToolKit/Source/AABBTree.h`): dynamic spatial structure on `Scene`, reused for light intersection.
- **Batch key:** `(Mesh, vert-variant, frag-variant, bound-texture-set)`.

---

## 11. References (file index)

- Forward pass: `ToolKit/Render/RenderPass/ForwardPass.cpp` (`Render` 47-77, opaque 129-156, `RenderOpaqueHelper` 224-248, translucent 158-222).
- Per-job draw: `ToolKit/Render/Renderer.cpp` (`Render(job)` 232-356, `Render(jobs)` 381-389, `FeedUniforms` 1088-1153, skinning lambda 237-276).
- Per-instance record: `ToolKit/Render/Renderer.h:151-182` (`PerDrawUboLayout`).
- Pass / RenderJob / RenderData / RenderJobProcessor: `ToolKit/Render/Pass.h`.
- RHI slots + limits: `ToolKit/Render/RHI.h:17-62`.
- Shadow atlas: `ToolKit/Render/ShadowAtlas.h`.
- Spatial accel: `ToolKit/Source/AABBTree.h`, `ToolKit/Resources/Scene.h:315`.
- Skinning data: `ToolKit/Resources/Animation.cpp:609-688`, `Resources/Engine/Shaders/skinning.shader`.
- PBR ubershader: `Resources/Engine/Shaders/defaultFragment.shader` (flags at 50, 60, 70, 113).
- Material data: `ToolKit/Resources/Material.h:26-43` (`MaterialCacheItem::Data`), `:117-134`.
- Program cache: `ToolKit/Render/GpuProgram.h:111`.
- GLES target: `ToolKit/Render/OpenGL/TKOpenGL.h:13` (`GLES3/gl32.h`).
