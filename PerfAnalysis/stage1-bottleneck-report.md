# small-scene on SM-G950F (Galaxy S8, Exynos 8895 / Mali-G71 MP20) - stage 1: who is slow

Measured 2026-10-31 with a Release (RelWithDebInfo native) build, one cold app start per
measurement, app force-stopped between runs. Every number below is the mean over a 12-14 s
window after a 10 s warmup (scene load + shader warmup).

Tools:
- `PerfAnalysis/run-experiment.ps1` - cold start, apply knobs, sample, log, force stop.
- `PerfAnalysis/measure.ps1` - external frame pacing from SurfaceFlinger.
- In-app probe (`Codes/PerfProbe.h`) - frame period, CPU submit time, present block time,
  engine CPU/GPU render time, draw counts, and a per-pass profiler dump, all to logcat.

## 1. The answer

The frame is **GPU bound**. At 1080x2220 the GPU needs ~140 ms of a 162 ms frame, the CPU only
uses ~36 % of one core, and the present call does not block (1.5 ms). The CPU spends the whole
frame parked inside GL calls waiting for the GPU - `glBindFramebuffer` alone accounts for 54 ms of
wall clock per frame - which is what a saturated GPU looks like from the CPU side, not a CPU bound
frame.

| measurement | value |
| --- | --- |
| window | 1080x2220 (display override; panel is 1440x2960) |
| frame period | 162 ms  -> 6.2 fps |
| GPU render time (EXT_disjoint_timer_query) | 140 ms |
| CPU render submit (chrono around the render path) | 110-130 ms wall, of which ~58 ms is CPU time |
| CPU share of one core | 36 % |
| present block | 1.5 ms |
| draw calls / passes | 186 / 13 |
| VRAM | 271 MB |

The two numbers that matter: **140 ms of 162 ms is GPU**, and the CPU is idle 2/3 of the time
while it waits. Nothing on the CPU side is the limiter at this resolution.

## 2. Where the 162 ms goes

Turning one feature off per run, same build, same view:

| run | what changed | period | fps | draws | GPU ms |
| --- | --- | --- | --- | --- | --- |
| base | everything on | 162 ms | 6.2 | 186 | 140 |
| shadowoff | lights do not cast | 124 ms | 8.1 | 93 | 110-120 |
| sceneoff | all mesh entities hidden | 55 ms | 18.3 | 19 | 37 |
| skyonly | + ssao/bloom/tonemap off | 20 ms | 49 | 4 | 14 |

Derived budget at 1080x2220:

```
shadows (2 cascades, 1024 atlas, ~93 caster draws)      38 ms   23 %
scene pass (prepass + depth grid + forward opaque)      69 ms   43 %
ssao + bloom(5) + tonemap                               35 ms   22 %
sky + gamma + copy + clears (4 passes, nothing else)    20 ms   12 %
                                                       ------
                                                       162 ms
```

Everything is additive here: the four groups are independent levers, not alternatives.

## 3. Fill/bandwidth or shader work?

Per-pixel work, not colour-buffer bandwidth, not draw submission:

| run | change | period | delta |
| --- | --- | --- | --- |
| res50 | 540x1110 (4x fewer pixels) | 68 ms | -94 ms |
| res25 | 270x555 (16x fewer pixels) | 53 ms | -109 ms |
| nohdr | RGBA8 instead of RGBA16F | 156 ms | -6 ms |
| msaa4 | 2x -> 4x MSAA | 182 ms | +20 ms |
| batch0 | no instanced batching, 297 draws | 161 ms | -1 ms |
| cull0 | occlusion culling off, 268 draws | 168 ms | +6 ms |

- Pixel count is the dominant term: 4x fewer pixels = 2.4x faster, and the fit over all three
  resolutions is `T = ~37 ms + 52 ms per megapixel`, i.e. 77 % of the frame is per-pixel.
- Halving the colour format (RGBA16F -> RGBA8 on every target) buys 4 %. If colour traffic were
  the limit it would be far more.
- Doubling the MSAA sample count costs 12 %: sampled-buffer traffic matters, but it is not the
  main term.
- Draw count is nearly free: +111 draws (batching off) changes the frame by 1 ms, and 268 draws
  with culling off is 6 ms *slower* than 186 with culling on (culling is worth keeping).

So the cost is what a pixel does, not how the pixels are stored, and not how many draws it takes
to get them there. A trivial full-screen pass at this resolution costs ~3.5 ms of GPU; the scene
pass costs the equivalent of ~20 such passes per pixel, which is shader work plus overdraw, not
bandwidth.

One counter-signal worth recording: the memory bus (MIF devfreq) sits at its maximum 1794 MHz
while the scene renders at 100 % and drops to the idle 1014 MHz at 25 % resolution, so the
pipeline does pull a lot of memory. It is demand that follows the pixel count, not a bus that
caps the frame.

### 3b. Vertex work and the resolution independent part

Two points per configuration are enough to separate the fixed (per frame / per vertex) cost from
the per-pixel cost, since `T = fixed + k * pixels` and only `k` depends on the view size. The
quarter resolution run with shadows off pins the second point down:

| configuration | 1080x2220 (2.4 Mpix) | 270x555 (0.15 Mpix) | fit: fixed + per Mpix |
| --- | --- | --- | --- |
| shadows on | 162 ms | 53 ms | 46 ms + 48 ms/Mpix |
| shadows off | 124 ms | 34 ms | 28 ms + 40 ms/Mpix |

Which reads as:

```
per-pixel shading of the scene + post chain            96 ms   (40 ms/Mpix)
per-pixel shadow sampling (PCF in the forward pass)    20 ms   (8 ms/Mpix)
shadow map rendering, cascades at a fixed atlas        18 ms
everything else that is not a pixel: vertex work of
the scene and the casters, draws, clears, the final
full window blit, driver overhead                      28 ms
                                                      ------
                                                      162 ms
```

Vertex and per-object work is therefore **not** the limit: about 46 ms of the 162 ms is
resolution independent at all, and part of that (18 ms) is the shadow atlas, which is still fill
work, just at a size the view does not control. Raising or lowering geometry cost would move the
frame by a few milliseconds; the pixel side owns the other 116 ms.

The 28 ms that is left over also explains the floor. At quarter resolution with shadows off the
frame is 33.7 ms and the process sits at 99.9 % of one core - the CPU becomes the limiter, and
nothing about the GPU can push past ~30 fps from there. That 28-34 ms is the next wall, and it is
mostly the full-window blit plus the draw submission path, neither of which
`RenderResolutionScale` shrinks.

## 4. What the CPU does spend time on

Per-pass profiler dump (inclusive/exclusive ms per frame, base run):

```
SetFramebuffer                 inc 54.2  exc 54.2
CopyFrameBuffer                inc 17.8  exc 17.8
Render                         inc 138.9 exc 10.8
FeedUniforms                   inc  6.4  exc  6.4
CopyTexture                    inc  4.0  exc  3.6
RenderShadowCasters            inc  9.7  exc  2.0
RenderOpaque                   inc  5.5  exc  1.9
FullQuadPass                   inc  9.5  exc  1.2
```

`SetFramebuffer` is not slow code: it is the first call after the driver's command queue fills up,
so it absorbs the wait for the GPU. Its 54 ms shrinks to 26 ms with shadows off and to 22 ms with
the scene hidden, i.e. it tracks GPU load. The genuine CPU work is the ~50 ms spread across
`Render`/`FeedUniforms`/`RenderOpaque`/`CopyTexture`.

That ~45-58 ms of CPU per frame is the second wall. At 25 % resolution the GPU drops to 33 ms and
the CPU share rises to 86 % - the app is then CPU bound and cannot exceed ~20 fps no matter how
cheap the GPU side gets.

## 5. Ranked levers

1. Render resolution. `RenderResolutionScale 0.5` (540x1110, upscaled) takes the frame from
   162 ms to 68 ms. Nothing else moves the needle that much for one setting, and the engine
   documents the setting for exactly this.
2. Shadows, 38 ms at full resolution: ~18 ms rendering two cascades at a 1024 atlas with ~93
   caster draws, plus ~20 ms of per-pixel PCF sampling in the forward pass (the split comes from
   section 3b, and it means both the atlas and the sampling have to come down). One cascade or a
   smaller atlas fixes the first half, a lower PCF sample count the second.
3. Post chain, 35 ms: SSAO ~11 ms, bloom 5 iterations ~6 ms, and the rest is tonemap/gamma at full
   resolution. Bloom iterations and SSAO half-resolution are cheap knobs.
4. MSAA: 2x at 1080x2220 costs ~20-30 ms. Note the side effect found here - with MSAA off the
   depth grid fails and occlusion culling stops dropping anything (culled 52 -> 0, +82 draws), so
   measure MSAA with the culling counters in view, or the win is partly cancelled.
5. The forward pass itself (69 ms with shadows excluded): high-poly city geometry, PBR + IBL +
   shadow PCF per pixel, plus transparent glass and skirt/foliage style materials. This is the
   long-term one: material simplification, LOD, and cutting overdraw.
6. The CPU submission path: invisible today because the GPU hides it, but at quarter resolution
   with shadows off the frame is 34 ms and the CPU is at 99.9 % of one core, so it caps the app at
   ~30 fps once the GPU work comes down. That is the stage 2 topic, and the full-window blit and
   the per-draw path are the two places to look.

## 6. Notes on the measurement itself

- `Stats::GetRenderTime` reported GPU 1.00 ms on Android until this session: the GL timer query
  path in `GLBackend` was compiled out with `if constexpr (TK_PLATFORM == PLATFORM::TKWindows)`
  and `GL_TIME_ELAPSED_EXT` does not exist in the GLES headers. It now resolves
  `GL_EXT_disjoint_timer_query` at runtime (Mali-G71 supports it) and reports a real number. The
  same change fixes the CPU half of that stat, which was negative whenever the render path nested
  (the game renderer wraps the scene path, which opens its own timer pair).
- The per-pass numbers come from `Pass::PreRender/PostRender`, which now also open a profiler
  scope next to the existing GPU debug group, so the stats API reports a per-pass breakdown.
- GPU timer readings wobble by +-20 % and can read higher than the frame period when the frame
  rate is unstable (a query result that spans a present). The frame period, sampled externally
  from SurfaceFlinger and in-app, is the trustworthy number; GPU ms is used as a cross-check.
- Frame periods alternate (146 / 178 ms) at 1080x2220, so there is a two-frame periodic pattern in
  the pipeline (max period 270-280 ms). Not investigated yet; it is not a shader compile (no
  compiles in the steady state log).
- The big cores sit at 1.26 GHz of 2.31 GHz and the memory bus at max, so the phone is not
  thermally throttled during these runs. Runs are kept short and cold for that reason.

## 7. Knobs used (no rebuild needed)

```
adb shell setprop debug.tk.res 50      render resolution percentage, 10..200
adb shell setprop debug.tk.msaa 0      msaa sample count, 0 to disable
adb shell setprop debug.tk.hdr 0       rgba8 instead of rgba16f
adb shell setprop debug.tk.ssao 0      screen space ambient occlusion
adb shell setprop debug.tk.bloom 0     bloom iterations, 0 to disable
adb shell setprop debug.tk.tm 0        tonemapping
adb shell setprop debug.tk.cull 0      depth grid occlusion culling
adb shell setprop debug.tk.batch 0     instanced batching
adb shell setprop debug.tk.shadow 0    lights stop casting
adb shell setprop debug.tk.mesh 0      every mesh entity hidden
adb shell setprop debug.tk.perf 0      silence the frame log
adb shell setprop debug.tk.prof 0      silence the per pass dump
```

Knobs are read once, when the scene comes up, and every knob treats `-1` as "not set", which is
what `run-experiment.ps1` resets them to (`setprop x ''` is refused on this Android version and a
stale knob silently changes the next run - found the hard way).
