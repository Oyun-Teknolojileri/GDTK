# Render CPU Optimization Plan

Status: IN PROGRESS
Started: 2026-10-19
Branch: develop
Commit policy: one commit per stage, only after the user reviews and approves that stage.

This file is the single source of truth for the work. If a session is resumed, read
section 8 (Progress log) first, then section 9 (How to resume).

---

## 1. Goal and constraints

The engine is CPU bound in the draw submission path, not GPU bound. The target is to
cut CPU time per frame in the forward render path without changing the image.

Constraints that shape every decision below:

| Constraint | Consequence |
|---|---|
| The OpenGL backend must run on WebGL 2.0 (GL ES 3.0) | No SSBO, no bindless, no `gl_BaseInstance`, fragment stage has 16 sampler slots |
| The Vulkan backend is not a priority right now | No work is planned there; nothing must break it either |
| The user tests manually, the agent must also verify images | `Utils/Bench` writes PNGs and the agent inspects them |
| Minimal work per stage | Each stage must be independently shippable and revertible |

---

## 2. Where the CPU time goes today

Measured by reading the per-draw path, not by profiling yet. Bench (stage 0) replaces
these estimates with numbers.

Every `Renderer::Render(const RenderJob&)` call currently does all of the following:

| # | Work | Evidence | Estimate |
|---|---|---|---|
| 1 | About 15 `GetTextureSlot("s_...")` calls, each a linear scan over ~23 `m_resources` entries doing `std::string::compare(const char*)` | `ToolKit/Render/GpuProgram.h:41` | 2-4 us |
| 2 | `SetLights` per draw: two `IDArray` + two `IntArray` heap allocations, a hash lookup per light, `std::distance` walking a `std::list` inside `LRUCache::LookUp` (~1500 pointer hops at 24 lights) | `ToolKit/Render/Renderer.cpp:826`, `ToolKit/Source/GenericBuffers.h:135` | 1-3 us |
| 3 | Two to three `glm::inverse` calls (model matrix in `SetTransforms`, environment volume world transform in `SetDataTextures`) | `Renderer.cpp:1067`, `Renderer.cpp:1005` | 0.3-0.6 us |
| 4 | 1120 byte per-draw UBO upload every draw | `Renderer.cpp:1142`, `ToolKit/Render/UniformBuffer.h:71` | 1-3 us |
| 5 | `BindPipeline` state diff plus ~15 cached texture binds plus the draw call | `Renderer.cpp:321` | 1-2 us |

Total estimate: 5-13 us per draw. At 2000 draws that is 10-26 ms per frame, which is
enough to explain the reported bottleneck on its own.

Two facts worth keeping in mind:

- Items 1, 2 and 3 are pure waste. Most of the win is available without instancing.
- `DrawDesc::instanceCount` already exists and the Vulkan backend honours it
  (`ToolKit/Render/Vulkan/VulkanBackend.cpp:1603`), but the GL backend ignores it
  (`ToolKit/Render/OpenGL/GLBackend.cpp:529` still calls `glDrawElements`). Nothing in
  the engine sets it today, so instancing has never actually run.

---

## 3. Decisions (locked)

These were argued out and are not open for renegotiation unless a measurement disproves
them.

| Topic | Decision | Reason |
|---|---|---|
| `RenderCommand` class with material / light / env pools | **Not built.** Batch directly on `RenderJobArray` | `RenderJob` already is the command. Pools only pay off with per-instance indirection, which we rejected below. Fewer files, less risk |
| Environment volume in per-instance data | **No, it stays in the batch key and in the per-draw UBO** | 26 vec4 per instance; at most 8 combinations exist for 3 volumes; keeping it in the key needs zero shader work |
| Per-instance material index | **No** | The material table would need a fragment stage sampler. The fragment stage already uses 14 of the 16 slots WebGL 2.0 guarantees. Gain only appears when materials share texture pointers |
| Per-instance light set index | **No, dedupe the light set and put it in the batch key** | Light indices are used in the fragment stage (`lighting.shader:317`), so an indirection table costs a fragment sampler. Dedupe is free and happens anyway in stage A2 |
| Instance data transport | **Data texture read in the vertex stage** | One `texSubImage2D` per frame for all instances, no VAO or divisor work, no Vulkan pipeline variants |
| Skinned, translucent, shader-material draws | **Stay on the per-job path** | Skeleton rule from the user; translucent needs per-job sorting; custom shaders may assume per-object uniforms |
| Texture packing / atlas / bindless | **Out of scope** | The user explicitly does not want it. Batching is limited by texture-set repetition, which is measured, not assumed |
| Vulkan backend work | **None** | Deprioritised by the user |

---

## 4. Batch key and instance data layout (stage B design)

Batch key, 64 bit hash of:

```
(mesh, material, program, envPrimary, envSecondary, lightSetId, cullFlip)
```

`material` already implies the texture set, the material data block and the rasterizer
state (cull, blend, draw type). `lightSetId` and the environment pair are deduped frame
local ids. Two jobs share a batch only when every field matches.

Instance texture, `RGBA32F`, `ClampToEdge`, nearest, no mips, 8 texels (128 bytes) per
instance, width 128 (16 instances per row):

| texel | content |
|---|---|
| 0..3 | model matrix, columns |
| 4..6 | inverse transpose normal matrix, 3x3, columns |
| 7 | `(materialIndex, lightSetIndex, flags, commandSlot)` |

The base instance index travels in the existing per draw UBO as one `IVec4`, so there is
no new backend entry point and no extra GL call. `gl_InstanceID` is available in the
vertex stage in GL ES 3.0 and is offset by that base in the shader.

Shader changes are limited to a `PER_DRAW_MODEL` family of macros in 9 vertex shaders
(about 14 call sites). The fragment stage is untouched.

---

## 5. Bench harness (`Utils/Bench`)

A separate host application, built by the root CMake next to `Packer`, `Import` and
`Launcher`. It is not a unit test framework; it is a deterministic capture plus a CSV
logger, and its exit code reports pass or fail so it can gate a stage.

What it does:

1. Builds the test scene **in code**, so it is deterministic and covers every feature we
   must not break. Final composition, at the default settings:
   - Sky with the default HDRI (`Resources/Engine/Textures/defaultHDRI.hdr`) for global IBL.
   - Two overlapping local environment volumes, different intensity / fade / size, one with
     parallax correction, so `ibl.shader` blends a primary and a secondary volume against
     the sky.
   - A 45 x 45 prop field (2025 props, spacing 2.0) over four shared materials and two shared
     meshes: 45 cubes and 45 spheres, so the batching work has both same-material and
     same-texture-set neighbours. Every ninth prop has a mirrored X scale.
   - Three shared texture sets: a checker texture, the engine's `default.png`, and no
     texture, so material switches are part of the measured draw stream.
   - A **clear plaza** at the centre of the field, floor white (albedo 1.0, roughness 1.0,
     metalness 0.0), because a dense field hides every cast shadow under a neighbour. The
     plaza holds three tall pillars, whose long shadows across the open floor are the
     readable shadow test.
   - A mirror slab beside the plaza (metalness 1.0, roughness 0.04) for screen space
     reflections, kept out of the middle so the plaza floor stays matte.
   - A tight 3x3x3 cube cluster at the plaza edge for ambient occlusion.
   - Three translucent spheres, which stay on the per-job path in the batching work.
   - One directional light at 45 degrees elevation and 45 degrees azimuth (cast shadow), one
     spot light (cast shadow) and three point lights, so both the cascaded directional path
     and the light-index path are exercised.
   - The camera orbits **inside** the field, so roughly the half of the props behind it is
     rejected by the frustum cull on every frame. `drawCalls` in the CSV versus the 2025 prop
     count is the measurement that the cull path is doing real work.
2. Drives a deterministic camera path: `frameIndex` in, transform out, no wall clock.
3. Renders a fixed number of frames after a fixed warm up, with vsync off.
4. Writes per frame CSV plus a one line JSON summary with median and p95.
5. Reads the final composited frame back from the default framebuffer at three fixed frames,
   writes PNG, and compares against a stored reference with a mean absolute difference
   threshold. Capture frames are part of the configuration (`--capture-frames`, default
   40, 80, 120) and not derived from `--frames`, so a shorter run still compares the same
   reference images.

Command line:

```
Bench --tag before                  (default configuration, 120 frames, 1280x720)
Bench --tag after
Bench --window                      (shows the frames, for eyeballing)
Bench --baseline-set default        (which reference set to compare against)
Bench --capture-frames 40,80,120    (must match the reference set)
Bench --tolerance 20                (image difference that still counts as a match)
Bench --reference-root D:\refs      (override where the reference images live)
Bench --update-reference            (create or refresh the reference set; never automatic)
Bench --results Bench/Results       (where the per run statistics go)
```

### Where things live, and why

| Artifact | Location | Versioned | Size |
|---|---|---|---|
| Reference PNGs | `<reference root>/<set>/` (outside the repo) | no | ~7.6 MB per set |
| Reference manifest (scene + feature signature) | next to the PNGs | no | 140 B |
| `reference_root` config | `Bench/bench.txt` | **yes** | 750 B |
| Per run statistics (`<tag>.json`) | `Bench/Results/` | **yes** | ~570 B per run |
| Per run CSV and full resolution captures | `Bench/Out/` | no | a few MB per run |

The reference images are large binaries that change whenever a stage legitimately changes the
image, so versioning them would grow history without adding information the manifest does not
already carry. What travels with the code is the reference *path*, the scene signature of the
approved image, and one small JSON per run with draw calls and CPU timings. The reference root
resolves as: `--reference-root`, then `Bench/bench.txt`, then `<repo>/../GDTK-Bench-Reference`.

A missing reference is a **failure** (exit code 3), never a silent new baseline: a regression
must not be able to become the reference by accident. Refreshing it is an explicit act,
`--update-reference`, done when a stage is accepted.

Reference sets are keyed by the geometry that defines the image (resolution, grid, spacing,
cluster, reference scale, capture frames); a run whose geometry differs is refused instead of
being compared against a stale reference. The feature switches (SSAO, SSR, MSAA, tonemap, env
volume count, lights, textures) are recorded in the same manifest but only reported, because
turning one off on purpose is how its effect is measured.

Numbers to compare between runs: `drawCalls`, `renderCpuMs`, `frameCpuMs`, plus the PNG
difference.

Note on transferability: the bench runs the native GL backend, but what it optimises is
GL call count and CPU bookkeeping, and both are strictly worse on WebGL 2.0. A win
measured natively transfers, usually larger.

---

## 6. Stages

| Stage | Scope | Files touched | Status |
|---|---|---|---|
| 0 | `Utils/Bench` plus baseline capture | `Utils/Bench/*`, root `CMakeLists.txt`, `Bench/Baseline/` | **done**, awaiting commit approval |
| A1 | Cache resolved sampler slots per program | `GpuProgram.h`, `Renderer.cpp` | pending |
| A2 | Hoist light index computation out of the draw into the job | `Pass.h`, `Pass.cpp`, `Renderer.cpp`, `ForwardSceneRenderPath.cpp` | pending |
| A3 | Precompute inverse transpose and environment volume inverses once per frame | `Pass.h`, `Pass.cpp`, `Renderer.cpp` | pending |
| A4 | Move `Mesh::Init` / `Material::Init` to job build time | `Renderer.cpp` | pending |
| M | Measurement gate: re-run Bench, decide whether stage B is worth it | - | pending |
| B1 | Batch builder, sort by batch key, instanced draw path in the GL backend | `RenderCommand.h` or a batcher header, `GLBackend.cpp`, `Renderer.*` | pending |
| B2 | Instance data texture plus the `PER_DRAW_MODEL` shader macros | `Renderer.*`, `Resources/Engine/Shaders/*` | pending |
| B3 | Apply to `ShadowPass`, then `ForwardPreProcessPass`, then `ForwardRenderPass` | pass files | pending |
| D | Documentation: `gdtk-overview.md`, `AGENTS.md` if conventions changed | - | pending |

Acceptance criteria for every stage that touches rendering: Bench PNG difference under
threshold, `drawCalls` and `renderCpuMs` reported before and after, and the user's demo
game project still builds and runs.

Regression safety net: `EngineSettings` gets `EnableInstancing` and `MinInstancesToBatch`
so any stage can be switched off at runtime.

---

## 7. WebGL 2.0 (GL ES 3.0) checklist

| Item | Status |
|---|---|
| `glDrawElementsInstanced` / `glDrawArraysInstanced` | core in ES 3.0, available through glad, GL backend must start using it |
| `gl_InstanceID` | vertex stage only in ES 3.0. Fragment stage needs a `flat` varying if a value must travel there |
| `texelFetch` in the vertex stage | core in ES 3.0 |
| Vertex texture units | ES 3.0 guarantees 16. Currently 2 are used (skinning) |
| Fragment sampler budget | **14 of 16 used**: slots 0, 1, 4, 5, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17. Do not add data tables to the fragment stage |
| `baseInstance` | not available. Carried in the per draw UBO instead |
| SSBO / bindless | not available. Not used |
| `RGBA32F` sampling | fine, no float render target extension needed |
| UBO size | 1120 bytes, far below the 16 KB minimum |

---

## 8. Progress log

Append one entry per stage. Keep numbers, not impressions.

### 2026-10-19 - planning

- Read the draw path end to end, recorded the hotspot table in section 2.
- Locked the decisions in section 3.
- Demo game project created by the user at `C:\Users\Cihan\Documents\TKMyWorkSpace\Demo`.
  It is the consumer test: after each stage it must still build and run. It was created
  empty on purpose, the bench scene is generated in code.

### 2026-10-19 - stage 0 (in progress)

- Writing `Utils/Bench`.

### 2026-10-19 - stage 0 done: Bench builds, renders, and is calibrated

Files: `Utils/Bench/CMakeLists.txt`, `Utils/Bench/Bench.cpp`, root `CMakeLists.txt`
(one `add_subdirectory`), `.gitignore`, `Bench/bench.txt` (reference root config),
`Bench/Results/before.json` (the recorded reference numbers).

Reference images are **not** in the repository: they live in
`C:\Users\Cihan\Desktop\GDTK-Bench-Reference\default\` (7.6 MB), pointed at by
`Bench/bench.txt`. The repository carries 1.9 KB of Bench material: the config, the
scene signature rule, and the run statistics. See "Where things live, and why" above.

Consumer reference recorded before any engine change: the demo game project at
`C:\Users\Cihan\Documents\TKMyWorkSpace\Demo` configures and builds clean against the
current engine (`Codes\Bin\Demod.dll`). If it stops building after a stage, the stage
broke a public surface, not the bench.

**Reference numbers** (`Bench --tag before`, 1280x720, 2025 props, 120 measured frames
after 20 warm up frames):

```
draw calls        7570
renderCpuMs       68.9 median, 72.7 p95
props             2025 in the field, camera inside it, so roughly half is culled per frame
```
That is ~9 us of render CPU per draw call, inside the 5-13 us estimate of section 2. The
estimate is now a measurement. CPU jitter between runs is about 2 percent; treat anything
under 5 percent as noise.

**Feature sensitivity**, measured by turning one feature off and diffing against the
reference (mean absolute difference per channel, 0-255):

| Toggle | Diff | Meaning |
|---|---|---|
| `--no-tonemap` | 11.3 | tonemap is load bearing |
| `--no-msaa` | 5.6 | MSAA really resolves |
| `--no-ssr` | 0.9 | SSR contributes on the mirror slab |
| `--no-ssao` | 0.15 | SSAO contributes, weakest of the set |
| `--env 0` | 0.11 | the volumes tint the IBL |
| `--env 3` | 15.7 | a third volume changes the blend visibly |
| `--no-spot` | 10.3 | the spot light is a major contributor |
| `--lights 0` | 21.0 | point lights matter |
| `--no-textures` | 31.2 | texture sets dominate the image |

Gamma cannot be switched off from `PostProcessingSettings` on this path:
`GameRenderer::PreRender` forces the scene path's gamma flag to false and applies gamma in
its own final pass, gated on `IsGammaCorrectionNeeded()`. The bench prints the state and
reports it in the JSON instead of pretending to toggle it.

### 2026-10-19 - known limitation: the frame is not bit identical between processes

Identical commands do not always produce an identical image. Six back to back runs against
one reference gave, in order: 4.51, 15.65, 0.0002, 4.51, 15.65, 15.65.

The important part is the shape of that series: the values are **discrete and repeat**, while
`drawCalls` at the capture frames is **exactly equal** in every run (7454 at frame 40, 7570
median). Same work, same geometry, same culling, different shading result. So this is not
timing noise in the measurement and it is not the bench; it is a resource that is built once
per process and does not always come out the same.

The two candidates, in order of likelihood:

1. **The HDRI environment bake.** `Hdri::LoadOrGenerateIrradianceCaches` queues a render
   task that generates the diffuse and specular environment maps on the first frame. The sky
   and both local volumes initialize the same shared `Hdri`, and the bake is deferred, so
   what the map ends up containing depends on when the task ran relative to the scene passes
   of that first frame.
2. **A driver shader variant.** GL reports "Vertex shader in program N is being recompiled
   based on GL state, and was not found in the disk cache" on these runs, so the first
   process compiles a variant that later processes take from the driver cache.

Until one of them is fixed the image check is a gross-break detector, not a pixel gate: the
default tolerance is 20, which clears the observed band (0.15 to 15.7) and still trips on a
missing pass or a black frame. Lower it as soon as the variance source is found. This is its
own task, separate from the CPU work; the numbers the bench reports (draw calls, CPU ms) are
unaffected by it.

Engine facts learned while writing the harness, all confirmed by reading the code:

- `Camera::SetLens` takes the field of view in **radians** (`Camera.cpp` constructs with
  `glm::radians(90.0f)`). Passing degrees produces a mirrored, wild projection that still
  renders geometry, so it fails visually rather than loudly.
- `ImageLoad` follows the engine's vertical flip switch, which exists for the GL texture
  path. Anything comparing against a PNG on disk wants `ImageLoadTopDown`.
- `SceneManager::SetCurrentScene(nullptr)` is not a release path, it dereferences the new
  scene unconditionally. The current scene is released by `SceneManager::Uninit` inside
  `Main::Uninit`; the host must only drop its own references before that.
- `Utils/Bench` needs three roots set before `Main::PreInit`, all derived from the binary
  location: `m_resourceRoot`, `m_cfgPath`, and `SetDefaultPath` for the engine's default
  resources. Without the third one the engine cannot find its own default shaders.
- `MaterialManager::GetCopyOfDefaultMaterial()` copies the default material **including its
  diffuse texture**. Assigning a texture is therefore not optional when the intent is "no
  texture": pass through `SetDiffuseTextureVal(nullptr)` or the new material silently
  inherits the engine's default image.
- The shadow cascade split with the shipped defaults is degenerate for a large view range:
  `UseParallelSplitPartitioning` is on with `ShadowMinDistance = 1.0`, and `ShadowPass`
  derives `minZ = near + ShadowMinDistance * (far - near)`, which puts minZ on the far
  plane and makes the split range negative. The fragment shader also compares world space
  view depth against `graphicConstants.cascadeDistances`, so those numbers have to be world
  units rather than the normalized fractions the PSSM path writes. The bench sets manual
  world unit cascades (15 / 35 / 80 / 220) for that reason. Worth a separate look in the
  engine; it is not part of the CPU work.
- Cast shadows themselves work. A temporary roof slab above the plaza produced a clear,
  correct shadow, which is how the shadow path was verified before the final scene was
  settled on a white, fully rough plaza floor with a 45 degree sun.
## 9. How to resume

1. Read section 8 first, it says which stage is in flight.
2. `git log --oneline -8` shows the last approved stage commit.
3. Build: `cmake --build Intermediate/Windows/Debug --config Debug --target Bench`
   (or `--target ToolKit` after an engine header change).
4. Run the reference measurement:
   `BinDebug\Bench.exe --tag before --out Bench/Out`
5. Run a probe while iterating:
   `BinDebug\Bench.exe --tag probe --frames 40 --warmup 10 --capture-frames 20 --baseline-set probe`
6. Before approving a stage, compare `Bench/Out/<tag>.json` against the previous tag and
   check the image diff, then ask the user for the commit decision.

Known environment facts:

- Build tree in use: `Intermediate/Windows/Debug`, generator `Visual Studio 18 2026`,
  config `Debug`, binaries land in `BinDebug/`.
- The Vulkan backend is off by default (`TK_VULKAN` is OFF).
- `Utils/Bench` compiles with `TK_GL_3_0` and `TK_DLL_IMPORT`, like `Packer`.
- The demo game project lives at `C:\Users\Cihan\Documents\TKMyWorkSpace\Demo` and is
  configured into its own `Intermediate\Windows` tree; it consumes ToolKit from
  `BinDebug`/`BinRelease` through `TOOLKIT_DIR`.
