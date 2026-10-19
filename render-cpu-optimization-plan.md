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

## 2. Where the CPU time goes today (measured, Release build)

Measured with `Utils/Bench` in a **Release** build and with the engine profiler on
(`--profile`), 1280x720, 2025 visible props, 7570 draw calls per frame. The profiler adds
roughly 30 percent to the absolute number, so the shares are what matter here, not the
milliseconds; the profiler-free baseline is 9.2-9.9 ms.

| Where | ms | Share |
|---|---|---|
| `ShadowPass::Render` (4 directional cascades + 1 spot, 5645 caster draws) | 9.48 | 70% |
| `ForwardPreProcessPass::Render` (~1000 draws) | 1.49 | 11% |
| `ForwardRenderPass::Render` (~1000 draws) | 1.35 | 10% |
| `ForwardSceneRenderPath::SetPassParams` (job creation, cull, light and env assignment) | 0.93 | 7% |
| SSAO + sky + SSR + gamma + resolve | ~0.15 | 1% |
| **Total** | **13.58** | |

Per draw call, `Renderer::Render` costs **0.93 us of exclusive time** (7.19 ms over 7708
calls), and that is what every one of those 7570 draws pays.

Three conclusions, and the first one reorders the whole plan:

1. **Shadow map rendering is the bottleneck, not the forward pass.** One caster is drawn up
   to five times per frame (once per directional cascade plus the spot), so 2025 props turn
   into 5645 shadow draws. Anything that reduces draw count, or that draws shadows without
   one job per caster per cascade, attacks 70 percent of the cost.
2. **The per-draw CPU bookkeeping is smaller than it looked in Debug.** Measured:
   `SetMaterial` (the sampler slot lookups) 0.028 ms, `SetDataTextures` 0.065 ms,
   `SetLights` 0.269 ms, `FeedUniforms` 0.169 ms per frame. The whole sampler-slot cache is
   therefore worth under one percent, and the light hoist about two. They stay on the list
   as cheap cleanups, not as the plan's spine. An earlier version of this table was written
   from Debug figures and overestimated them by an order of magnitude: Debug keeps iterator
   checks, does not inline, and inflates exactly those operations.
3. **Shadow caster job creation is real:** `ShadowPass::RenderShadowCasters` spends 2.32 ms
   of exclusive time per frame on its five AABB queries and the jobs they build, on top of
   the draws themselves.

The estimate that "2000 draws cost 10-26 ms" was wrong: in Release a draw costs ~1.3 us, so
the same 2000 draws cost about 2.6 ms. The reported CPU bottleneck at 7570 calls is real,
but it lives in the shadow pass and in the sheer number of draw calls, not in string
lookups or matrix inverses.
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

### Measurement protocol

**All performance numbers come from a Release build.** A Debug build is not usable here:
MSVC keeps iterator debug checks, does not inline, and its `std::string` / `std::vector`
operations cost several times what they cost in Release. That does not only inflate the
absolute numbers, it distorts the *relative* ones: the string lookups and container churn
that stages A1 and A2 remove are exactly the operations Debug penalises most, so a Debug
comparison would report a win several times larger than the one a player sees.

Rules that follow from that:

1. `cmake --build Intermediate/Windows/Release --config Release --target Bench`, and the
   numbers come from `BinRelease/Bench.exe`. `BinDebug` is for correctness only.
2. The reference set is per build configuration: the manifest signature carries
   `debug` / `release` as its first token, so a Debug run can never be compared against a
   Release reference. The Release reference lives in `--baseline-set release`.
3. Every stage reports `drawCalls`, `renderCpuMs` median and p95 from the same tag pair
   (`--tag before` / `--tag a1`), on an idle machine, with at least 120 measured frames.

The Debug numbers recorded in the first progress entry below are therefore diagnostic: they
proved the harness works, nothing else.
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

The order changed after the Release measurement in section 2. The shadow pass owns 70
percent of the render CPU, so batching is the spine of the plan and it starts there; the
per-draw cleanups are kept as cheap complements, not as the main event.

| Stage | Scope | Expected win | Status |
|---|---|---|---|
| 0 | `Utils/Bench`, Release baseline, profiler mode | measurement only | **done**, awaiting commit approval |
| S1 | Shadow pass, cheap half: reuse the caster job list across the cascades instead of rebuilding it five times a frame | measured: 0.45 ms (5%) | **done**, awaiting commit approval |
| S2 | Shadow pass, big half: batch the casters so four cascades plus the spot stop costing 5645 draws | up to 6 ms (53%) | pending |
| F1 | Forward pass: same batching treatment for the ~1900 forward and prepass draws | up to 2.5 ms (20%) | pending |
| A1 | Cache resolved sampler slots per program | measured at under 1%; do it while touching the code | pending |
| A2 | Hoist light index computation out of the draw into the job | measured at about 2% | pending |
| A3 | Precompute inverse transpose and environment volume inverses once per frame | about 1% | pending |
| A4 | Move `Mesh::Init` / `Material::Init` to job build time | about 1% | pending |
| D | Documentation: `gdtk-overview.md`, `AGENTS.md` if conventions changed | - | pending |

Each stage is measured on its own with `--tag <stage>`, and its numbers are appended to the
progress log. A stage that does not move the median beyond the 8 percent run to run spread
is reported as "no measurable win" and is either dropped or kept only for clarity.

**Shadow cascade count is a lever worth naming**: with four cascades each caster is drawn up
to four times, so a scene that is CPU bound in the shadow pass can trade shadow quality for
draw calls without any engine change at all. The bench can measure that today by varying the
cascade count and distances, and the finding belongs in the report either way.

Acceptance criteria for every stage that touches rendering: Bench image difference under
threshold, `drawCalls` and `renderCpuMs` reported before and after, and the user's demo game
project still builds and runs.

Regression safety net: `EngineSettings` gets `EnableInstancing` and `MinInstancesToBatch` so
any stage can be switched off at runtime.
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

### 2026-10-19 - Release measurement, and the plan it rewrote

The user asked whether the numbers were being taken in Release. They were not: everything up
to this point was `BinDebug`, which is not a usable measurement build. Fixed:

- Release dependency and engine trees built (`Intermediate/Windows/Release`, `BinRelease`).
- `Utils/Bench` gained `--profile`, which turns the engine profiler on and dumps its tree at
  the end of the run. The profiler costs an rdtsc pair per scope and the draw path has one
  per draw, so it is **off** by default and only enabled by that flag.
- The build configuration is now the first token of the reference scene signature
  (`release 1280x720 grid 45 ...`), so a Debug run can never be compared against a Release
  reference. The Release reference lives in `--baseline-set release`.

**Release baseline** (`--tag before-release`, profiler off): 7570 draw calls, `renderCpuMs`
9.2-9.9 ms median across runs. That is **1.3 us per draw**, against 8.1 us measured in Debug
for the same scene. Debug inflated the number by 6.4x.

**Profile breakdown** (profiler on, totals inflated by about 30 percent):

| Where | ms | Share |
|---|---|---|
| `ShadowPass::Render` | 9.48 | 70% |
| `ForwardPreProcessPass::Render` | 1.49 | 11% |
| `ForwardRenderPass::Render` | 1.35 | 10% |
| `SetPassParams` | 0.93 | 7% |
| everything else | 0.15 | 1% |

Within a draw, `Renderer::Render` is 0.93 us of exclusive time. The items the plan was
originally built around measured much smaller than the Debug figures suggested: sampler slot
lookups 0.028 ms and data texture binding 0.065 ms per frame together (under 1 percent), the
light index work 0.269 ms (2 percent), the UBO fill 0.169 ms.

So the plan was reordered in section 6: shadow caster batching is the main stage now, and the
per-draw cleanups are kept as cheap complements. One caster is drawn up to five times per
frame (four directional cascades plus the spot), which is why 2025 props become 5645 shadow
draws.

Also recorded: the run to run spread in Release is about 8 percent (9.2 vs 9.9 ms), wider
than the 2 percent seen in Debug because the absolute time is smaller. A stage has to move
the median beyond that to count.
### 2026-10-19 - S1 done: one caster job list per frame instead of five

`ShadowPass` rebuilt the whole caster job list for every shadow map: four directional
cascades plus the spot, so five full `CreateRenderJobs` passes a frame over the same
entities. The jobs do not depend on the light or the cascade - mesh, material, transform and
world bounding box are identical - so they are now built once in `PrepareShadowCasterJobs()`
and each cascade only filters them with a bounding box test against its own frustum.

```
before  renderCpuMs 8.51 - 8.80   draw calls 7570
after   renderCpuMs 8.14 - 8.16   draw calls 7570
```

A 5 percent win, and the draw call count is byte for byte the same, which is the check that
the drawn set did not change. The three runs after the change are tightly clustered
(8.14 / 8.15 / 8.16) while the three before spread 8.51 - 8.80, so the 0.45 ms is real even
though it sits under the 8 percent run to run spread.

Less than the profiled 2.3 ms of `RenderShadowCasters` suggested, because the profiler
inflates that function's exclusive time, and because the new per cascade filter is a linear
scan over the whole job list (2025 jobs times five) where the old code used the AABB tree's
hierarchical query. On a much larger scene that scan would grow linearly, which is the next
thing to fix if this path is touched again.

**Scaling measurement, taken while checking whether the win could show up at all**: render
CPU is linear in draw count, 1.05 - 1.34 us per draw from 822 to 7456 draws, with no knee at
the high end. So the frame is CPU bound, not GPU bound, and removing draw calls is the lever
that scales. That is what makes S2 (batching) worth doing rather than speculative.

The shadow pass is now 5.3 ms of the 9.1 ms profiled total (was 9.5), and 3.2 ms of what is
left is the 5645 caster draws themselves - one `Renderer::Render` per caster per cascade.
That is precisely what S2 removes.
### 2026-10-19 - the image diff question: one bug found and fixed, one source still open

The reference diff grew from 0.0002 (first scene) to 4.5-15.7, with one run at 157. The
honest account of what that is:

**A real bug in the bench, found and fixed.** The capture read the **back buffer**. A run
during this investigation returned a purely white frame (35 KB PNG against 2.4 MB for a real
one), which is what the 157 diff was: a blank capture compared against a real reference.
Back buffer content is undefined while the window is occluded or covered, and the benchmark
is often run with a terminal in front of it. `GameRenderer` now exposes the framebuffer it
composited into (`GetOutputFramebuffer()`), and the bench reads that, which is the same
reason the editor's thumbnail code reads the viewport's resolved texture instead of the
screen.

**Why the number is larger than it used to be**, independent of the bug:

1. The metric is a mean over the whole image, and the scene now has a large plaza of white,
   fully rough, non-metallic floor. That surface multiplies its albedo by the irradiance, so
   any difference in the environment lighting shows up over a huge, bright area. The first
   scene was mostly a mirror floor, where the diffuse term barely participates and a change
   in irradiance is almost invisible.
2. There are now more things whose state can differ per process: two environment volumes
   sharing the sky HDRI, a shadow casting spot, and a shadowed directional light.

**Ruled out, each by experiment** (paired runs, same configuration):

| Suspect | Test | Result |
|---|---|---|
| Window occlusion / composition | `--hidden` | still differs, so not the window |
| The bench's back buffer read | new capture path | fixed the blank frame, but some pairs still differ |
| Local environment volumes | `--env 0` | still differs |
| SSAO / tonemap | `--no-ssao`, `--no-tonemap` | pairs matched in those runs |
| Driver shader cache | file count, size and newest timestamp around runs | no writes during a run, no correlation |
| Disk caches in the repo | newest files under Resources and Config | nothing written |
| Different work between runs | `drawCalls` per frame | exactly equal, so the same passes draw the same geometry |

So: same draw work, discrete and sticky image states that flip every few processes, and no
file or cache behind it. The remaining candidate worth a test is the **HDRI bake**:
`Hdri::LoadOrGenerateIrradianceCaches` runs as a render task, and the sky plus both volumes
initialize the same texture, so what the prefiltered chain ends up containing can depend on
GPU work that is not fully ordered against the first frames. The experiment is to render the
same scene with a procedural `GradientSky` (`--gradient-sky`, not implemented yet): if the
states collapse, the bake is the source.

**Consequence for the workflow, decided rather than deferred:** the image check is a
**diagnostic** until that is settled, not the acceptance gate. It still catches what matters
most (a blank frame, a missing pass, a broken capture - the 157 and 20-plus cases). Stage
acceptance uses the thing that is reproducible: `renderCpuMs` and `drawCalls` from the same
tag pair, plus a visual read of the PNGs by the agent.
## 9. How to resume

1. Read section 8 first, it says which stage is in flight.
2. `git log --oneline -8` shows the last approved stage commit.
3. **Build Release, always, for numbers:**
   `python BuildScripts/build_gdtk.py --configs Release --target Bench`
   (or `cmake --build Intermediate/Windows/Release --config Release --target Bench` once the
   tree exists). Debug is built with the same commands against `Intermediate/Windows/Debug`
   and is used only for correctness work.
4. Reference run:
   `BinRelease\Bench.exe --tag before-release --baseline-set release --out Bench/Out`
5. Where the time goes:
   `BinRelease\Bench.exe --tag prof --profile --baseline-set release --out Bench/Out --frames 60`
   and read the `Bench: prof` tree from the end of the output.
6. A probe while iterating. Note `--results Bench/Out`: `Bench/Results` is versioned and
   should only ever hold a stage's recorded number, not every experiment:
   `BinRelease\Bench.exe --tag probe --frames 40 --warmup 10 --capture-frames 20 --baseline-set probe --results Bench/Out --out Bench/Out`
7. Compare against the previous stage with `Bench/Results/<tag>.json`, check the image diff,
   then ask the user for the commit decision.

Known environment facts:

- Release tree: `Intermediate/Windows/Release`, binaries in `BinRelease/`. Debug tree:
  `Intermediate/Windows/Debug`, binaries in `BinDebug/`. Generator `Visual Studio 18 2026`.
- The Release dependency tree did not exist before this work and was built by
  `BuildScripts/build_gdtk.py`, which builds the missing dependency configurations itself.
- The Vulkan backend is off by default (`TK_VULKAN` is OFF).
- `Utils/Bench` compiles with `TK_GL_3_0` and `TK_DLL_IMPORT`, like `Packer`.
- Reference images: `C:\Users\Cihan\Desktop\GDTK-Bench-Reference\<set>\`, pointed at from
  `Bench/bench.txt`. Set `release` holds the Release reference, `default` the older Debug one.
- The demo game project lives at `C:\Users\Cihan\Documents\TKMyWorkSpace\Demo`.
