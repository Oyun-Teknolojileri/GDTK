# Light Probe Volume (Diffuse GI) -- Implementation Plan

> Working plan for baked diffuse global illumination from a light probe grid in GDTK.
> Status: design only, nothing implemented yet.
> Keep this file in sync while the work is in progress; fold the permanent description into
> `gdtk-overview.md` (section 4) once the feature lands and delete this file.

---

## 1. Goal

The forward path shades direct light per object (`lighting.shader`) and adds image based
lighting from the sky plus up to two local environment volumes (`ibl.shader`). The sky supplies
diffuse and specular; a local volume supplies the specular half, and the ambient (diffuse) term is
what the baked probe volume replaces. There is no
bounce term: a room lit by one lamp has a black ceiling, a wall next to a bright window does
not glow, and the sky irradiance is applied with full visibility, so an interior is lit as if
its roof did not exist. That missing term is the diffuse GI ask.

The method is the one the reference engines use: distribute probes over the scene, measure the
incoming radiance at each probe, compress it into a low order spherical harmonic expansion, and
interpolate that expansion per pixel at shading time. The result is low frequency, so it
survives a very small number of coefficients and a very small amount of data.

The measuring is the part this plan spends most of its words on, because measuring it by
rendering a cubemap at the probe only sees the light that has already arrived. A sealed room
behind a door needs the light followed through the door, which is a path trace (5.2), not a
capture (5.3).

What it buys, in the order the user notices it:

1. Interiors stop being lit by the sky. The probe sees the walls, so a room under a roof gets
   the sky through its windows only.
2. Surfaces receive coloured bounce from their neighbours.
3. The sun and the local lamps produce a fill that the current IBL cannot produce at all,
   because the IBL only knows the sky and two hand placed cubemaps.

It does not do specular bounce (that stays with the environment volumes), it does not react
to moving lights, and it is not a replacement for SSAO (that stays for contact scale).

---

## 2. Why a probe grid and not screen space GI

A screen space diffuse GI (one or two ray steps against the g buffer, accumulated over frames)
is cheaper to author because it needs no bake, and the engine already has the pieces for it
(`SsaoPass` and `SsrPass` both march the pre process g buffer). It is the wrong first move
here:

| Property | Probe grid, baked | Screen space GI |
|---|---|---|
| Off screen light | Yes, the trace finds it | No, it leaks at every screen edge |
| Cost at 1080p | 2 or 6 texture fetches plus ~20 flops | 8 to 24 taps plus a half res blur plus temporal history |
| Cost on a moving camera | None | Reprojection, history rejection, ghosting |
| WebGL 2 | Nothing but texture sampling | Several extra full screen passes per frame |
| Reacts to dynamic light | No | Yes |
| Authoring | An editor bake | None, but tune the ray count and the history weight |

For a web target that has to hold 60 fps on an integrated GPU, the per frame cost is the
decisive number, and the bake is an authoring step that runs on a desktop. So: probe grid
first, screen space later if dynamic GI is ever needed on top.

---

## 3. WebGL 2 reality check

WebGL 2 is GL ES 3.0. The relevant limits and what each one rules out:

| Feature | WebGL 2 | Consequence for this design |
|---|---|---|
| Compute shaders | No | No GPU ray tracing, no GPU probe solve, no compute based filtering. |
| Shader storage buffers, image load/store | No | Probe data has to arrive as a texture, not a buffer. |
| 3D textures (`sampler3D`) | Yes, core | Available, but the engine has no `Texture3D` path at all today (see 6.4). |
| 2D array textures (`sampler2DArray`) | Yes, core | Already used (`s_shadowAtlas`). |
| Hardware trilinear on `RGBA16F` | Yes | Half float is filterable in ES 3.0; `RGBA32F` needs `OES_texture_float_linear`. |
| Float render targets | Extension (`EXT_color_buffer_float`) | Only the bake on the desktop touches this. The web build only samples, so it does not need it. |
| `glReadPixels` of a float target | Extension | Bake only, on the desktop. |
| Fragment texture units | 16 guaranteed | The forward shader already declares up to slot 17. **This has to be measured on the target browser before 3 more samplers are added.** See 9.1. |
| `MAX_3D_TEXTURE_SIZE` | 256 guaranteed | A probe grid of 32x16x32 fits easily. |

Two conclusions fall out of the table:

1. **The bake is an authoring time job on the desktop, and the web build only samples.** A
   published web build must carry the baked blob and never run the capture. This is the
   property that makes the whole method web friendly.
2. **Everything at runtime is a handful of texture fetches**, which is exactly what GL ES 3.0
   is good at. No new backend entry point is required for the first version. See 6.4.

---

## 4. What is stored

### 4.1 Quantity

Per probe, 9 RGB coefficients of the sphere of incoming radiance, convolved with the clamped
cosine and divided by pi, i.e. the same quantity the engine's irradiance cubemaps already
hold (`irradianceGenerateFrag.shader` computes `E / pi` too, which is why `EvalSky` can do
`diffuseColor * texture(...)` directly). That convention is what lets the probe term drop
into `IBLPBR` without a fudge factor.

Bake side, from a captured cube:

```
c_i  = sum over cube texels of  L(omega) * Y_i(omega) * dOmega
c_0  *= 1.0            (A_0 = pi   -> A_0 / pi)
c_1..3  *= 2.0 / 3.0   (A_1 = 2 pi / 3)
c_4..8  *= 1.0 / 4.0   (A_2 = pi / 4)
```

Runtime side:

```
E(n) / pi = c_0 * 0.282095
          + c_1 * (0.488603 * y) + c_2 * (0.488603 * z) + c_3 * (0.488603 * x)
          + c_4 * (1.092548 * x * y) + c_5 * (1.092548 * y * z)
          + c_6 * (0.315392 * (3 z^2 - 1))
          + c_7 * (1.092548 * x * z) + c_8 * (0.546274 * (x * x - y * y))
```

Both sides have to use one basis order: `(Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22)`
as above. A transposed or reordered basis is the classic failure mode here and it looks like
"GI lights the scene from the wrong side", not like a data bug.

Solid angle of a cube texel at `(u, v)` in `[-1, 1]`:

```
dOmega = 4 / (res * res * pow(1 + u*u + v*v, 1.5))
```

### 4.2 Layout on the GPU

The arithmetic decides the layout, so it is worth doing before the table. A coefficient is a
colour triple, three floats. L2 is nine coefficients, 27 floats per probe. One RGBA16F texel
carries four floats, so L2 needs at least seven texels per probe: three textures cannot hold
it, and six fetches cannot carry it either (6 x 4 = 24 < 27). The first version of this plan
claimed otherwise; the table below is the corrected one.

**What is implemented: L1, four coefficients, 12 floats, three texels.** The bake still
produces L2 by default (`--quality l2`), and the runtime reads the first four coefficients of
it, which is exact: the L1 coefficients of an L2 projection are the L1 projection.

Each texel holds one colour channel of all four coefficients, so a fetch returns four scalars
of the same channel. The three channels are the layers of one 2D array texture, which keeps the
probe on a single sampler:

| Layer | R | G | B | A |
|---|---|---|---|---|
| 0 (`s_probeSH`) | c0.r | c1.r | c2.r | c3.r |
| 1 | c0.g | c1.g | c2.g | c3.g |
| 2 | c0.b | c1.b | c2.b | c3.b |

Each layer is `resX` wide and `resY * resZ` tall: the z slices are stacked vertically, so
a slice is a block of `resY` rows. This buys hardware bilinear filtering across the two grid
axes inside a slice for free, and leaves only the z axis to be interpolated by hand:

```
uvw      = (worldPos - volumeMin) * invVolumeSize       // volume local space, 0..1
ty       = clamp(uvw.y * res.y - 0.5, 0.0, res.y - 1.0) + 0.5   // stay inside the slice
zf       = clamp(uvw.z * res.z - 0.5, 0.0, res.z - 1.0)
z0       = floor(zf);  z1 = min(z0 + 1.0, res.z - 1.0);  tz = zf - z0
st0      = vec2(uvw.x, (z0 * res.y + ty) / (res.y * res.z))
st1      = vec2(uvw.x, (z1 * res.y + ty) / (res.y * res.z))
```

Six `texture()` fetches (three textures times two slices), one `mix` per texture, then the
four term dot. The y clamp is not cosmetic: without it the bilinear footprint of the first and
last row of a slice reaches into the neighbouring slice. Implementation:
`Resources/Engine/Shaders/probeGI.shader`.

L2 storage, when the extra band is wanted: seven channels rather than three, i.e. seven
samplers and fourteen fetches, or a 3D texture with seven coefficient planes per probe (one
unit, fourteen fetches, needs the `Texture3D` work from 6.4). Both are a shader and an upload
change, not a format change, because the `.pgi` already stores all nine.

### 4.3 Size

| Grid | Probes | GPU (L2, 3 x RGBA16F) | Disk (float32 raw) |
|---|---|---|---|
| 8 x 4 x 8 | 256 | 6 KB | 27 KB |
| 16 x 8 x 16 | 2048 | 48 KB | 216 KB |
| 32 x 16 x 32 | 16384 | 384 KB | 1.7 MB |

This is small enough that the pack step does not need to compress it, though zstd will.

### 4.4 Where the probes go

The grid is dense, so the cost is `volume / spacing^3`, and the spacing is the author's main
lever. The bake estimate assumes 512 samples, ~4 rays per path and 8 cores:

| Volume | Spacing | Probes | GPU (L2) | Bake |
|---|---|---|---|---|
| 20 x 4 x 20 m room | 1.0 m | 1,600 | 38 KB | under a second |
| 60 x 8 x 60 m level chunk | 1.0 m | 28,800 | 690 KB | a few seconds |
| 200 x 20 x 200 m outdoors | 2.0 m | 100,000 | 2.4 MB | tens of seconds |
| 200 x 20 x 200 m outdoors | 1.0 m | 800,000 | 19 MB | about a minute |

A scene wide auto fit is therefore viable up to a few hundred thousand probes and stops being
viable past that - but memory is not the reason to keep volumes author placed. The reason is
that a dense grid spends its resolution uniformly, while a city needs 1 m in the rooms and 4 m
over the plaza. Author placed boxes are how that budget gets spent where it matters.

Three placement models, and the plan takes the first two:

| Model | Build cost | Quality per probe | Verdict |
|---|---|---|---|
| Author placed box, dense grid | Low, the environment component pattern again | Good when the box is snug | v1 |
| Auto fit box to the scene or the selection, spacing from a probe budget | Low, same component plus an Auto button | Coarse outdoors, fine for a single building | v1.5 |
| Hand placed probe cloud plus 3D Delaunay tetrahedralization | High: Delaunay at bake, an indirection texture per pixel at shading | Best | Not now |

Unity's classic light probes are the third row, which is how they afford a handful of probes in
a large hall. The price is a tetrahedralization at bake time and an indirection lookup at
shading time. A dense box grid reaches the same quality with more probes and none of that
machinery, which is the right trade while the feature is new.

Rules the volume follows:

- The UI takes a **spacing in metres**, not a resolution. `res = clamp(ceil(size / spacing), 2, 32)`
  per axis, and the inspector shows the probe count, the GPU bytes and the estimated bake time
  before the button is pressed, so a 12 cm spacing is caught before it becomes an hour.
- Probes inside geometry are invalid. Detect them at bake time with six short rays per probe
  (a hit on both sides of an axis within half a cell), skip their tracing, and fill from the
  nearest valid neighbour so the trilinear interpolation does not pull toward black.
- The useful probe near a wall sits slightly inside the room. Pushing an invalid probe along
  the axis with the most clearance by half a cell is cheap and is the difference between a lit
  corner and a grey one.
- Several volumes per scene, **one bound at a time**. The renderer assigns a job the smallest
  volume containing it, the same rule `AssignEnvironment` (`Pass.cpp:499`) uses for the
  environment volumes, and `ForwardPass` already batches jobs by that assignment
  (`ForwardPass.cpp:297`), so one texture set is bound per batch. This is chunking, not
  seamless blending: neighbouring volumes should overlap by at least a cell so the fade band
  inside each box lands where the other box's data is already valid.

If blending two overlapping volumes is ever needed, the unit budget decides the quality: one
volume at L2 is three samplers, two volumes at L2 is six. Two volumes at L1 is two samplers,
which is the only two volume configuration that fits comfortably (see 9.1).

---

## 5. Bake

### 5.0 What the capture actually contains (and why it is not just direct light)

The thing that trips people up: the cube is not a picture of "the direct light". A pixel of
the cube is the radiance leaving the surface it sees, and the radiance leaving a surface is
that surface's own answer to everything that hit it. For a Lambert wall:

```
L_out = albedo / pi * E_wall
```

so a wall with a sunlit irradiance of 1.0 and an albedo of 0.8 leaves 0.25. A probe facing
that wall over a solid angle of pi (a whole hemisphere of wall) accumulates

```
E_probe = L_out * pi = albedo * E_wall = 0.8
```

That is 80 percent of the wall's own irradiance arriving back as fill light, out of a single
capture, with no "indirect light" term anywhere in the renderer. The indirect part of global
illumination is not something the renderer computes; it is what the integral in the SH
projection computes over the captured radiance. The renderer only has to draw lit surfaces,
which it already does.

The same argument answers "will it look black?" only when the surfaces the probe can see are
themselves directly lit. If they are in shadow, the probe is dark, and for a single bounce
that darkness is correct: the light has not arrived yet. The case that decides the design is
the sealed room with one door, and it gets its own section (5.1).

What ends up in the cube, and why it matters:

| Content | In the cube? | Comment |
|---|---|---|
| Direct light reflected by surfaces | Yes | This is bounce 1 |
| The sky, with correct visibility | Yes, the sky pass draws it | A room sees the sky through its windows only; this is the sky leak fix |
| Emissive materials | Yes | A monitor or a lamp shade feeds the probes |
| Shadows | Yes | The capture draws with the shadow atlas, so a shadowed surface leaves no light |
| A light source seen directly | Only if its geometry is drawn | A point light's bulb is a tiny solid angle; a sun disc in the HDRI can be large (see below) |
| Second and higher bounces | No | See 5.5 |

Two traps:

- **Ambient IBL has to be off during the capture.** If `EvalSky` and the local volumes light
  the surfaces in the capture, those surfaces are lit by an ambient sampled by normal only,
  with no occlusion: a wall inside a sealed room would still receive the full sky irradiance
  and the bounce would be inflated by it. Set the sky illumination and the volume intensities
  to zero for the capture (`EvalSky` already returns zero when the intensity is zero) while
  leaving `m_sky` in place, so the sky geometry is still drawn into the cube and the probe
  still sees it through the windows. Let the probe supply the ambient.
- **A sun disc in the HDRI double counts the directional light.** If the sky image carries a
  physical sun, every probe integrates it on top of the sun light, and the value can exceed
  what `RGBA16F` holds (65504). Either keep the sun out of the sky texture and light with a
  directional light, or clamp the captured radiance and store a per volume scale.

### 5.1 The dark room: why one capture is not enough

The case that decides the whole design. A sealed room with one door. The corridor outside is
daylight lit, the room itself gets no direct light at all.

A probe at the back of the room renders its cube and sees four walls, a floor and a ceiling,
none of them directly lit, plus the bright door rectangle. From back there the door subtends a
small solid angle, so the cube is almost black and so is the SH. That is not a bug in the
projection, it is the truth about a single bounce, and it is the objection raised against the
naive version of this method. It is correct.

Iterating does fix it, one bounce per pass:

| Pass | What the capture sees | The room |
|---|---|---|
| 1 | Dark walls, one small bright patch at the door | Almost black, a faint glow at the door |
| 2 | The wall next to the door is lit now | Light creeps one wall inward |
| 3 | The side walls carry it | The room has a fill |
| 4 to 6 | The back wall and the ceiling join in | Converged for this geometry |

So raster plus iteration is not "always black", it is "black for K passes, where K is the
bounce depth this geometry needs", and every pass is a full capture pass. In a deep room, or a
corridor behind another corridor, K reaches six or more, and every pass pushes the signal
through L2 SH again, which flattens it a little more each time.

That is the honest comparison, and it is why the baker is a path tracer (5.2) and the capture
is kept as a preview path (5.3).

### 5.2 The baker: a CPU path tracer

A path tracer does not need the light to be visible from the probe, it follows the light where
it goes. A ray leaves the probe, hits the door frame, bounces into the corridor, hits the
sunlit wall and comes back with the corridor's radiance: one path, no iteration, no dependence
on how many bounces the light needed to arrive. That is the quantity the SH projection wants,
computed the way its definition says it.

It is also cheaper here, which is the part that is easy to get wrong. A capture pass costs
`probes x 6 x visible draws` GPU draws plus a readback per probe. A path trace costs
`probes x samples x bounces` rays on the CPU and touches no GPU state:

| Work | Raster capture | Path traced |
|---|---|---|
| 256 probes | 1536 scene renders per pass, one pass per bounce | 256 x 512 paths, a few million rays |
| 2048 probes | 12k scene renders per pass | 1M paths, tens of millions of rays |
| Bound by | draw count and bounce depth | triangle count and sample count |
| Runs on | a render task, GPU, one frame at a time | worker threads, no GPU |

A ray against a 1M triangle scene in a decent BVH costs a few hundred nanoseconds per core, so
the 2048 probe case is seconds on the frame pool. The capture case is bound by draw calls,
which is the thing this engine is worst at, and it pays that cost once per bounce.

What it needs in this codebase:

| Piece | Status |
|---|---|
| CPU triangles in world space | `Mesh::m_clientSideVertices` and `m_clientSideIndices` are retained: `Pass.cpp:192` calls `MeshComponent::Init(false)`, which does not flush them |
| Triangle BVH | Does not exist. `AABBTree` (`AABBTree.cpp`) is an entity box tree and its `RayQuery` tests boxes, for picking. A static triangle BVH is the bulk of the work |
| Ray triangle intersection | New, Moller-Trumbore is enough |
| Surface albedo | `Material::GetColorVal()` and `GetEmissiveColorVal()`, optionally times the diffuse texture average. Alpha mask is a hit test; alpha blend is treated as opaque |
| Direct light sampling | `DirectionalLight`, `PointLight`, `SpotLight` already expose position, direction, radius, intensity. Each is sampled with a shadow ray |
| Sky | Sampled for rays that miss: the HDRI cubemap, or the `GradientSky` parameters analytically |
| Skinned / animated meshes | Baked at the rest pose. Documented, not solved |
| Denoise | Not needed at 512+ samples for a low frequency SH; clamp the radiance instead |

The shading model inside the baker is deliberately simpler than the forward shader: Lambert
diffuse, emissive and the sky. Specular does not survive an L2 projection in any meaningful
way, and matching the GPU's microfacet model would only make the bake slower and noisier.

It runs headless, so a CI build can refresh a stale bake through the Packer without a GPU.

### 5.2.1 How it actually runs

Three stages, and the split matters because the scene is not thread safe:

```
main thread    snapshot: flatten the visible static meshes into world space triangles,
               copy the lights and the sky, build the BVH
bulk pool      one task per probe, no locks: each writes its own 9 coefficients
               cancel flag checked once per 64 samples, progress counter for the UI
main thread    upload the SH textures, mark the component baked, repaint the inspector
```

`WorkerManager::BulkPool` is the pool for this: its own comment says it is for "batch work
that may take the whole machine, such as texture encoding in the tools", and it is null on the
web, which is correct because the web build never bakes. The parallel loop is the same
`poolstl` idiom the render path already uses:

```cpp
using poolstl::iota_iter;
std::for_each(TKExecByConditional(true, WorkerManager::BulkPool),
              iota_iter<size_t>(0), iota_iter<size_t>(probes.size()),
              [&](size_t i) { probes[i].sh = TraceProbe(ctx, i); });
```

Kernel, per probe:

```
rng = Rng(hash(probeIndex, bakeSeed))        // deterministic: a re-bake is bit identical
for s in 0..samples:
    dir = UniformSphere(rng)
    L   = TracePath(probePos, dir, rng)      // next event estimation at every bounce
    c  += L * Y(dir) * (4 * pi / samples)    // same projection the capture path uses
apply the cosine convolution (1, 2/3, 1/4) and divide by pi
```

Uniform directions rather than cosine weighted ones, because that keeps the projection code
identical to the capture path in 5.3 and lets the same traced radiance later feed anything
else the bake wants. At 512 samples the extra variance lands in L2 and is invisible after the
evaluation.

Reservations:

| Item | Cost |
|---|---|
| Snapshot, 1M triangles | ~50 MB, 1 to 2 s |
| BVH build, 1M triangles, binned SAH | 2 to 5 s, ~2N nodes |
| Rays, 256 probes x 512 samples | a few million, seconds across cores |
| Memory peak | snapshot + BVH, freed at the end of the bake |

Determinism is deliberate: the seed comes from the probe index and a bake seed stored on the
component, so the producer can diff two bakes of the same scene and see only real changes.


### 5.3 The raster capture, kept for preview

```
for each probe p:
    place the capture camera at the probe world position
    render the scene into a res x res cube target, 6 faces
        - sky on, so the probe sees the sky through openings
        - ambient IBL OFF (see 5.0), direct light and emissive on
        - shadow atlas built ONCE for the whole bake, not per face
        - SSAO / SSR / bloom / DoF / tonemap / FXAA OFF (linear HDR only)
        - depth grid and occlusion culling OFF
    read back the 6 faces
    project to 9 RGB coefficients on the CPU
    store
```

The capture reuses `Renderer::RenderToCubeMap` (`Renderer.cpp:1831`), which already builds the
six face views, disables post processing and writes `RGBA16F`. Two things have to change for
bake use:

1. It takes a `ForwardSceneRenderPath*` and renders a full frame per face. For a bake the
   path is 6 x probes frames, so the shadow pass has to be hoisted out of the loop and the
   depth grid / occlusion culling have to be off. Either a dedicated
   `ProbeBakeRenderPath` or a flag on `ForwardSceneRenderPath` that skips those passes.
2. The result is a `CubeMap` that lives on the GPU (`Consume`d from a render target). The
   bake needs the pixels, so it reads them back with `IGraphicsBackend::ReadPixels` on the
   face framebuffer, the same call the depth grid uses.

Resolution: 16 x 16 per face is plenty for an L2 capture of a low frequency signal; 32 is the
upper bound worth trying. The cost of the bake is not pixels, it is draw calls: probes x 6 x
visible draws. For 256 probes and ~300 visible draws per face that is about 460k draws, so
expect tens of seconds, not milliseconds. It runs on a render task (`RenderSystem::AddRenderTask`,
the same mechanism `EnvironmentComponent::CaptureEnvironment` uses) with progress and a
cancel flag in the component.

### 5.4 Validity and leaks

- A probe placed inside geometry captures near black. Detect it after the projection (DC
  magnitude under a threshold, or a depth test on the capture) and mark the cell invalid, then
  fill it from the nearest valid neighbours so the interpolation does not pull the room toward
  black.
- A cell that straddles a wall still bleeds a little. Reduce it by sampling the grid at
  `worldPos + normal * cellSize * 0.5` (normal offset) and by keeping the grid reasonably
  dense near walls. Do not attempt per sample visibility tests; they cost more than they buy
  at this frequency.
- Sky leak into interiors is solved by the capture itself, which is the main quality win of
  the whole feature.

### 5.5 Convergence and cost

For the path traced baker the convergence knob is the sample count per probe, plus a bounce
limit and Russian roulette. 512 stratified cosine weighted samples put the L0 term within a
fraction of a percent on a Lambert scene; what is left lives in L2 and is invisible after the
SH is evaluated for a diffuse normal. Cost is linear in probes x samples, independent of how
deep the light had to travel - that is the property the raster path does not have.

For the raster preview one capture is one bounce, and at an albedo of `r` the full indirect
series is

```
E_indirect_total = E_bounce1 * (1 + r + r^2 + ...) = E_bounce1 / (1 - r)
```

so at `r = 0.8` the first bounce is only a fifth of the converged indirect. The iteration is
Jacobi: upload zero, capture, upload, capture again with the previous result bound as ambient.
Each iteration adds exactly one bounce and the albedo below 1 makes it a contraction. It is
useful as a preview and as a sanity check against the ray tracer on an open scene, not as the
shipping bake: its cost is linear in the bounce depth, and the door room from 5.1 is exactly
the case where that depth is largest.

---

## 6. Engine integration

### 6.0 Where the code lives

The bake is a separate static library, `Modules/LightProbeBake`, built the way
`Modules/RenderDoc` is (`Modules/RenderDoc/CMakeLists.txt`): its own `CMakeLists.txt`, one
`add_subdirectory` in the root, linked into `Editor` and `Packer`, never into the game template
and never into the web build.

| Piece | Target | Why |
|---|---|---|
| `ProbeVolumeData` and its file read / write | ToolKit | The runtime and the component have to read it |
| Component, shader, UBO fields, `SetDataTextures` binding | ToolKit | Every game samples the volume |
| Snapshot, BVH, path tracer, SH projection | `Modules/LightProbeBake` | Never runs in a shipped game; dead weight in the wasm |
| Bake button, inspector, probe gizmos | Editor | UI |
| `ProbeBake` CLI (scene + volume name -> `.pgi`) | second target in the same module | CI and Packer refresh, headless, no editor |

Dependency direction is one way: `ToolKit <- LightProbeBake <- Editor`. The module's public
surface is one call:

```cpp
// Modules/LightProbeBake/Source/LightProbeBake.h
struct BakeRequest
{
  Scene*             scene;          // read synchronously on the calling thread
  Mat4               volumeTransform;
  Vec3               volumeSize;
  Vec3               positionOffset;
  UVec3              resolution;
  int                samples;
  int                bounces;
  uint32             seed;
  std::atomic<bool>* cancel;
  std::function<void(float)> onProgress; // 0..1
};

struct BakeResult
{
  ProbeVolumeData data;   // the same struct the component serializes
  String          error;
};

TK_LIGHT_PROBE_BAKE_API bool Bake(const BakeRequest& request, BakeResult& result);
```

The snapshot is plain data (world space positions, normals, material colours) and the BVH is
plain data, so the module retains no engine object after the snapshot returns and the object
lifetime rules in AGENTS.md are satisfied by construction. The engine pointers it touches
(`Scene`, `MeshComponent`, `Light`, `SkyBase`) are all valid for the duration of the
synchronous snapshot on the main thread.

Why not inside ToolKit: ToolKit is what the game and the wasm carry. A path tracer is a few
thousand lines no shipped game executes, and every entry point would have to be `TK_API` and
survive the web platform split.

Why a CLI as well as the library: the editor wants progress and cancel in-process, and CI
wants a process it can run on a build machine. Same library, two entry points. The CLI boots
the engine with `RenderSystem::UseNullBackend()`, the pattern AGENTS.md already documents for
headless verification, so it needs no window and no GPU.

### 6.1 Component

`ToolKit/Components/LightProbeVolumeComponent.h/.cpp`, modelled on `EnvironmentComponent`
(`Size`, `PositionOffset`, `CaptureResolution`, `Bake` as `VariantCallback`, serialized params):

| Parameter | Meaning |
|---|---|
| `Size`, `PositionOffset` | OBB of the volume, in the owning entity's local space |
| `Spacing` | Metres between probes, the UI derives `resolution` from it (4.4) |
| `Resolution` | Derived and read only, shown so the count is visible |
| `Bounces` | Path bounce limit, 1..8, default 4 |
| `Samples` | Paths per probe, 128..2048, default 512 |
| `Fit` / `Auto Fit` | Editor action: fit the box to the selection (or the scene) and pick the spacing from a probe budget |
| `Bake` | Callback that starts the bake task |
| `ShowProbes` | Editor only: draw the probe points, invalid ones in red |
| `Quality` | L1 / L2, decides the shader define |
| `AoStrength` | How much SSAO multiplies the probe term |

The scene keeps the probe volumes the way it keeps environment volumes
(`Scene::AddEnvironmentVolume` / `GetEnvironmentVolumes`), and each job is assigned the smallest
volume containing it (4.4). A scene with no probe volume renders exactly as it does today.

### 6.2 Renderer

- The job's probe volume reaches the renderer the way `EnvironmentVolume` does: a field on
  `RenderJob` (`Pass.h:140`), filled by `AssignEnvironment` (`Pass.cpp:499`) and part of the
  `ForwardPass` sort key (`ForwardPass.cpp:297`), so a batch only ever needs one texture set
  bound.
- `Renderer::SetDataTextures` (`Renderer.cpp:1054`) binds the array `DataTexture` from
  `job.ProbeVolume`, next to the sky and volume branches, and only when the program declares
  `s_probeSH` (`m_currentProgram->GetTextureSlot`), so custom shader materials that do not
  include `ibl.shader` pay nothing.
- Volume transform, min, inv size and resolution go into the `PerDrawData` UBO next to the
  existing `vol0*` / `vol1*` blocks (`perDrawDataInc.shader`, `PerDrawUboLayout` in
  `Renderer.h`). Both sides change in lockstep; there is room.
- A `DataTexture` with `FormatRGBA16F` and `TypeFloat` uploads through
  `GLBackend::CreateTexture` (`data = tex->m_imagef ? ... : tex->m_image`). Array targets needed
  one addition there: the declared `Layers` are copied as one contiguous layer major block
  (`glTexSubImage3D` on GL, one copy region per layer on Vulkan), because the array branch used
  to allocate empty storage only. Something still has to call `Map()` on it when the bake
  finishes.

### 6.3 Shader

New include `Resources/Engine/Shaders/probeGI.shader`:

```glsl
#ifndef PROBE_GI
#define PROBE_GI

TK_SAMPLER_BINDING(6) uniform sampler2DArray s_probeSH;

// Returns E / pi for the world normal n at worldPos, and writes the volume weight the
// caller uses to blend against the sky and the local volumes.
vec3 ProbeIrradiance(vec3 worldPos, vec3 n, out float weight)
{
  weight = 0.0;
  if (perDraw._probeVolume.w < 0.5)          // x: enabled, y: intensity, z: ao strength
  {
    return vec3(0.0);
  }

  vec3 local = (perDraw._probeInvTransform * vec4(worldPos, 1.0)).xyz;
  vec3 uvw   = local * perDraw._probeInvSize + 0.5;   // volume is centered

  // Inside is a hard box; the weight fades over the last half cell so the hand off at the
  // boundary does not step.
  vec3 edge = min(uvw, 1.0 - uvw);
  weight    = clamp(min(min(edge.x, edge.y), edge.z) / 0.5, 0.0, 1.0);
  uvw       = clamp(uvw, 0.0, 1.0);

  ... z slice math from 4.2, 6 fetches, mix, then the 9 term dot ...

  // An L2 projection of a fully occluded direction can undershoot zero, and a negative
  // irradiance subtracts light. The bake self test measures about -0.01 on a blocked normal.
  return max(E, vec3(0.0));
}
#endif
```

In `ibl.shader` / `defaultFragment.shader`:

```glsl
float w;
vec3 probe = ProbeIrradiance(v_worldPos, n, w) * (1.0 - E) * diffuseColor;
probe     *= mix(1.0, ambientOcclusion, perDraw._probeVolume.z);

// inside IBLPBR: the probe replaces the DIFFUSE part of sky + local volumes,
// the specular part keeps coming from the existing cubemaps.
diffuse = mix(diffuseSkyAndVolumes, probe, w);
```

That blend rule matters: the probe capture already contains the sky, so adding it on top of
the sky diffuse double counts. Outside the volume the pixel keeps today's behaviour, which
also means a scene with no probe volume renders exactly as it does now.

### 6.4 The `Texture3D` upgrade (deferred)

With a real `sampler3D` the same data costs three fetches instead of six, and the hardware
does the z interpolation. That needs `GraphicTypes` target, `GL_TEXTURE_3D` in `GLBackend`,
`VK_IMAGE_TYPE_3D` in Vulkan, a `Tex3D` view type in `Shader.cpp`, and a `DataTexture` variant
that uploads slice by slice. Worth doing as a phase 4 optimisation, not as a prerequisite.
The 2D stacked layout in 4.2 is a strict subset of it and the shader differs only in the
sampling block.

---

## 7. Data and serialization

- Bake output: a small binary file, `ProbeCache/<SceneName>_<VolumeId>.pgi` under the project
  resources, so a scene XML does not carry a base64 blob. Precedent: `Hdri` writes its baked
  irradiance to `EnvCacheMap` (`Types.h`, `TKIrradianceCacheFolder`) and only stores the path.
- Header: magic, version, grid resolution, volume min/max, L1/L2 flag, per probe 9 x RGB
  float32 plus a validity bit.
- The component serializes the path plus a hash of the bake settings (size, resolution,
  bounces). Hash mismatch means "stale bake", which the editor reports and the runtime simply
  uses (a stale bake still looks better than no bake).
- Missing file behaves like "no probe volume": the shader define is not set, slot 18-20 stay
  unbound, and the frame is identical to today's.

---

## 8. Editor

- A component inspector (`Editor/Components/EditorLightProbeVolume.*`, modelled on
  `EditorEnvironmentComponent`): the parameters, the derived probe count, GPU bytes and bake
  estimate, a **Bake** button and a **Show Probes** toggle that draws a point per probe,
  invalid ones in red, coloured by the DC coefficient (a cheap heat map: bright where the light
  is), so an author can see the grid is where they think it is.
- **Fit to selection** and **Auto fit to scene** create or resize the box and pick the spacing
  from a probe budget (default 150k probes), which is the zero-effort path for a level that is
  one building.
- The bake is per scene and blocks nothing: it runs on the frame pool and the viewport keeps
  drawing. The component needs a cancel path so a bake does not survive a scene unload (this
  is also an object lifetime rule: no engine object in a static, see AGENTS.md).

---

## 9. Risks and unknowns

### 9.1 Texture unit budget

The forward shader declares samplers up to slot 17 (`s_skyIrradiance`, `s_skySpecular`). GL ES
3.0 guarantees 16 fragment texture units; desktop drivers usually report 32, and the engine
never queries `GL_MAX_TEXTURE_IMAGE_UNITS`. Adding three samplers may therefore work on the
desktop and fail on a browser or phone.

**Measure first** (`gl.getParameter(gl.MAX_TEXTURE_IMAGE_UNITS)` in the target browser, and
check the GL error log for `glActiveTexture` failures, since `ResetUsedTextureSlots` walks all
32 slots every frame). Mitigations, in order of preference:

1. L1 for the web: one extra sampler (slot 18), nothing else changes.
2. Reuse the secondary volume slots (11, 12) when no secondary volume is active. Fragile,
   only if 1 is not enough.
3. Merge the two local volume sampler sets into one 2D array and recover three slots.
4. Compile a forward variant without the sky specular chain on the web.

### 9.2 Interpolation artefacts

Trilinear pulls corners toward the average, which reads as "the room is lit by a blob". Cheap
fixes: a finer grid near walls, the normal offset from 5.4, and eventually tetrahedral
interpolation (four probes instead of eight, no darkening). Do not start with tetrahedral; it
needs the grid connectivity rules and it is harder to debug.

### 9.3 Bake time

Two different costs, worth keeping apart:

- The path traced baker: triangles x samples x bounces. The BVH build dominates on a large
  scene (seconds), the rays are cheap. It is linear in the probe count and independent of the
  bounce depth the geometry needs.
- The raster preview: draw calls, `probes x 6 x visible draws` per bounce. This is the one
  that scales badly, and it is another reason it stays a preview.

### 9.4 Quality with moving lights

A baked probe cannot respond to a light that moves. v1 accepts this. A later option is the
same amortised trick the reflection volumes already use (a capture task per frame for one
probe) which would let a small number of dynamic probes update, but on the web that is one
probe per frame at best.

---

## 10. Phases

### Phase 0 -- sampling path with synthetic data (desktop)

Goal: prove the shader and the blend rule before spending time on the baker.

- `probeGI.shader`, the sampler bindings, the `PerDrawData` fields, the define.
- A hard coded 8x4x8 grid of a smooth analytic function (a gradient plus one bright spot),
  uploaded from C++ at startup.
- Expected: a scene lit by the analytic field, no seams at slice boundaries, the term drops
  to zero outside the box, and a scene with the volume disabled is pixel identical to today.

Acceptance: side by side screenshots; no GL errors; no change in frame time outside the
volume.

### Phase 1 -- ray tracing core (desktop)

- The `Modules/LightProbeBake` target: `CMakeLists.txt` modelled on `Modules/RenderDoc`, the
  `Bake()` entry point from 6.0, and the `ProbeVolumeData` struct plus its file read / write in
  ToolKit.
- Scene snapshot (world space triangles and the material fields the baker needs), a triangle
  BVH, ray triangle intersection.
- Direct light sampling with shadow rays, sky sampling for misses. Zero bounces.
- Cross-check against the raster capture: `RenderToCubeMap` already gives the same probe from
  the GPU, so a probe that sees directly lit surfaces has to agree between the two bakers
  within noise. That comparison is the unit test for the ray tracer, and it is worth having
  before any bounce code exists.

Acceptance: raster capture and zero bounce path trace agree on an open scene within a few
percent of the mean; a debug view of the hit normals and the first hit distance looks right;
a 1M triangle scene builds its BVH in seconds, not minutes.

### Phase 2 -- bounces, SH and component (desktop)

- Russian roulette and a bounce limit, SH projection of the traced radiance, radiance clamp.
- `LightProbeVolumeComponent` with spacing derived resolution, probe count / bytes / bake
  estimate in the inspector, scene registration, serialization, Bake with progress and cancel.
- Per job volume assignment (`AssignEnvironment` plus the `ForwardPass` sort key),
  `Renderer::SetDataTextures` binding, `probeGI.shader`, the `IBLPBR` blend rule.

Acceptance: the door room from 5.1 fills with light in one bake; the room behind the wall is
lit by its own window and not by the sky; the bake survives a save / load / repack; the frame
with the volume disabled is pixel identical to today; a two volume scene shows no seam while
the camera moves between them.

### Phase 3 -- web

- L1 fallback, the unit budget check from 9.1, a quality setting that is honoured by the
  published build.
- Headless Chrome (SwiftShader) boot with a baked volume, one captured frame, and a
  comparison against the desktop GL frame at the same camera.

Acceptance: identical looking frame, no `INVALID_OPERATION` in the browser console, frame
time within the budget measured on the same scene without the feature.

### Phase 4 -- quality and authoring

- Sample count and clamp tuned against a reference, so the default is a number and not a
  guess; a convergence report in the bake log.
- Invalid probe culling (six ray test), fill from neighbours, the half cell push off walls.
- **Fit to selection** and **Auto fit to scene** with a probe budget, so a level gets a volume
  without hand placing one.
- Optional: `Texture3D` path, three fetches.

Acceptance: a probe grid over `unity-city.scene` does not leak through the roofs; raising the
sample count by 4x changes the frame by less than the noise floor the author can see; auto fit
on that scene lands under the probe budget and bakes in under a minute.

### Phase 5 -- dynamic (only if asked for)

A moving light cannot be baked. The amortised option is the one the reflection volumes already
use: a capture, or a small path trace, for a handful of probes per frame.

### Phase 1 status (landed)

What exists today:

- `ToolKit/Resources/ProbeVolume.h/.cpp`: `ProbeVolumeData` and the `.pgi` reader / writer, plus
  the `UVec3` typedef the engine was missing.
- `Modules/LightProbeBake`: scene snapshot, triangle BVH (binned SAH, built over an index array),
  path tracer (next event estimation, cosine bounce, Russian roulette), SH projection, the
  parallel bake over `WorkerManager::BulkPool`, cancellation and the buried probe fill.
- `ProbeBake.exe`: `--root/--scene/--out` plus `--res/--samples/--bounces/--box/--quality/--seed`,
  and `--selftest`, which checks the projection against a uniform sky and an emissive ceiling
  analytically with no scene loaded.

Measured on `alafranga/Resources/Scenes/unity-city.scene` (2.83M triangles, 6885 entities,
Release build):

| Work | Time |
|---|---|
| Snapshot + BVH build | 1.08 s |
| 2048 probes x 128 samples x 3 bounces | 0.05 s |
| Whole run, including the scene load | 1.13 s |
| Output | 218 KB (`16x8x16`, L2) |

The same run in a Debug build spends 24.8 s in the BVH build, which is the number to quote when
someone times a bake from the editor before the release tooling exists.

Not done yet: the raster cross check from Phase 1 (needs the editor path), the runtime shader,
the component and the editor UI.

One finding that reached the shader spec: an L2 projection of a fully occluded direction can
come out slightly negative (the selftest's occluded normal reads -0.009). `probeGI.shader` has
to clamp its result to zero before it multiplies the albedo, or a shadowed corner subtracts
light.

---

## 11. Verification

- Desktop GL, Vulkan, and headless Chrome (SwiftShader), as the other render work uses.
- `dumpbin` style check is not relevant, but the web build has to be republished before
  testing, and `?loglevel=1` keeps the console readable.
- A RenderDoc capture of one frame with the volume on: the forward pass should show the probe
  array sampler bound, `s_probeSH`, in the pass' texture list. If it is missing, the slot lookup
  failed and the shader silently read a default texture.
- The probe cache file has to survive the Pak round trip (pack, run from the pak, not from a
  loose resources folder).
- The `ProbeBake` CLI on the same scene as the editor bake has to produce a bit identical
  `.pgi`: that is what the deterministic seed from 5.2.1 is for, and it is the cheapest check
  that the module and the editor path share one code path.

---

## 12. Open questions

1. Does the target browser report 16 texture units? (9.1, blocks the L2 choice)
2. Two volumes meeting at an open doorway: does the per-job assignment (4.4) hold up there, or
   does that boundary need a real blend? The per-job rule puts the seam exactly at the wall in
   most cases, which is why it is worth trying before adding a second set of samplers.
3. Should the CLI live in `Modules/LightProbeBake` or in `Utils/` next to Packer? (6.0 puts it
   in the module; the `Utils/` tools are the other convention in this repo)
4. Should the probe term be applied to alpha-masked foliage? It is a full surface term, so
   yes, but foliage is where a coarse grid shows the most.
