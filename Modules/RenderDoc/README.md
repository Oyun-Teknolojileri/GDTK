# RenderDoc frame capture

Capture a frame of the engine - in the editor or in a game - and turn it into something a human
or an AI can read: a JSON model, a markdown digest, one PNG per pass and the shader sources.

The point is to make "capture this frame and tell me what is wrong with it" a command line away
instead of a trip through the RenderDoc UI, and to make the same question answerable from an agent
that can only run commands.

---

## 1. How the module is put together

```
ToolKit/Source/RenderDocCapture.{h,cpp}   the capture core: binds the injected RenderDoc, drives
                                          the frame, writes the .rdc and its sidecar, serves the
                                          request file. Any host that links the engine gets it.
ToolKit/Source/FrameCapture.{h,cpp}       engine hook: RequestFrameCapture() -> host handler
Dependency/utilities/renderdoc_app.h      RenderDoc's in-application API header (MIT)

Modules/RenderDoc/
  Source/RenderDocModule.{h,cpp}          editor glue: capture directory, sidecar context, the
                                          four calls Editor/Source/main.cpp makes
  Source/RenderDocCommands.{h,cpp}        console commands and Modules menu entries
  Scripts/rdc_report.py                   report driver (plain python 3)
  Scripts/rdc_analyze.py                  the analyzer, runs inside qrenderdoc's interpreter
  Scripts/rdckit/                         capture.py (model), report.py (markdown), util.py
```

The core lives in the engine because a published game links only `TK::ToolKit` and a game may
still want to capture. What stays editor specific is where captures go, what the sidecar records
and the commands and menu the module contributes; the editor answers `<workspace>/<project>/Captures`
plus workspace / project / scene, a game answers whatever its host callback says.

Two rules the whole thing rests on:

- **RenderDoc has to be in the process before the graphics device is created.** The core looks the
  library up by name (`librenderdoc.so`, `renderdoc.dll`, `libVkLayer_GLES_RenderDoc.so`) and never
  loads it, because the capture hooks have to be installed before the driver is initialised. Start
  the host through RenderDoc; injecting afterwards cannot bind the API.
- **A capture is always queued, never opened inside the call.** `BeginFrame()` opens it, `EndFrame()`
  closes it after the present, so a request that arrives mid frame gets the next whole frame.

---

## 2. Capturing from the editor

The editor is a normal SDL host: launch it through RenderDoc, then ask for a capture.

### 2.1 Launch

Linux (this repository, Debug build; verified):

```bash
cd <engine root>
renderdoccmd capture -w \
  -d BinDebug \
  -c "/home/<user>/Documents/TKWorkspace/<Project>/Captures/tk" \
  "$PWD/BinDebug/Editor"
```

Windows:

```powershell
& "$env:ProgramFiles\RenderDoc\renderdoccmd.exe" capture -w `
    -d BinDebug -c "<workspace>\<project>\Captures\tk" BinDebug\Editor.exe
```

`-c` is only the file name *template*: every capture appends its own prefix and timestamp. The
editor prints `RenderDoc <api> attached, captures go to <dir>` at startup when the binding worked,
and `CaptureStatus` says so from the console when it did not.

The editor takes `--workspace <path>` and `--project-name <project>` when it should open a specific
project instead of the last one:

```bash
renderdoccmd capture -w -d BinDebug -c "$PWD/Captures/tk" \
  "$PWD/BinDebug/Editor" --workspace /home/<user>/Documents/TKWorkspace --project-name MultiverseGo
```

### 2.2 Ask for a capture

Four entry points, one queue:

| From | How |
|------|-----|
| A tool / CI / agent | write `<capture dir>/request` with `label [frameCount]` |
| The editor console | `CaptureFrame <label> [frames]`, `CaptureStatus`, `CaptureOpen` |
| The menu bar | **Modules -> RenderDoc** -> Capture Frame / Capture Status / Open Last Capture |
| Game code or a plugin | `ToolKit::RequestFrameCapture(label, frames)` |

The request file is the one that matters for automation: it is polled about four times a second,
read as `<label> <frameCount>`, then deleted, so writing it twice before the editor notices gives
one capture:

```bash
printf 'tk_single 1' > "<workspace>/<project>/Captures/request"
```

`frameCount` spans several frames in one file, clamped to `[1, 60]`. A multi frame capture records
one continuous event stream, so every frame's passes appear in recording order.

### 2.3 What lands on disk

```
<workspace>/<project>/Captures/
  <label>_<YYYYMMDD-HHMMSS>_capture.rdc    the capture
  <label>_<YYYYMMDD-HHMMSS>_capture.json   the sidecar
  last_capture.json                        a copy of the sidecar written last
  request                                  the automation channel (only while pending)
```

Sidecar of a real editor capture:

```json
{
  "capture": ".../Captures/tk_single_20261109-180420_capture.rdc",
  "label": "tk_single",
  "frameCount": 1,
  "hostFrame": 280,
  "graphicsApi": "OpenGL",
  "engineVersion": "v0.5.1",
  "config": "Debug",
  "capturedAt": 1794236673,
  "project": "MultiverseGo",
  "scene": "unity-city.scene",
  "workspace": "/home/<user>/Documents/TKWorkspace/"
}
```

`hostFrame` is the host's own frame counter, not RenderDoc's (an OpenGL capture has no frame
number of its own). A game's sidecar has the same fields minus `project`, `scene` and `workspace`.

---

## 3. Capturing from a game (opt in)

The game template deliberately does not call the capture core: not every game wants RenderDoc in
its loop. Opting in is three call sites in the game's `Main.cpp` (or any host that links the
engine), and nothing else - the core is already inside `TK::ToolKit`.

```cpp
#include <RenderDocCapture.h>

void PreInit()
{
  ...
  // Before any graphics object exists: bind RenderDoc if this process was started through it,
  // and point captures at <cwd>/Captures.
  RenderDocCapture::Init([]() -> String { return ConcatPaths({GetCurrentPath(), "Captures"}); });
}

void TK_Loop()
{
  if (g_proxy->SyncFrameTime())
  {
    RenderDocCapture::BeginFrame();   // opens a queued capture, present included

    g_proxy->FrameBegin();
    g_proxy->FrameUpdate();
    g_proxy->FrameEnd();

    RenderDocCapture::EndFrame();     // closes it once the frame has been presented
  }
}

void Exit()
{
  ...
  RenderDocCapture::Shutdown();
}
```

Build a standalone Linux game and run it through RenderDoc (verified with the template sources in
a scratch directory):

```bash
cmake -S <project> -B <project>/Intermediate/Linux -G Ninja \
  -DTK_PLATFORM=Linux -DCMAKE_BUILD_TYPE=Debug -DTOOLKIT_DIR=<engine root>
cmake --build <project>/Intermediate/Linux
renderdoccmd capture -w -c "<project>/Bin/Captures/game" "<project>/Bin/<Game>"
```

`-DTK_PLATFORM=Linux` matters: without it the project CMake treats the build as an editor plugin
and produces a `.so` the editor loads instead of an executable. The game resolves its own
resources relative to the working directory, so run it from a layout that has `../Config` and
`../Resources` next to it (a `Bin/` directory in the project root, which is what the packer
produces).

Then the same three ways in work: `RequestFrameCapture()` from game code, the request file in
`<cwd>/Captures`, or nothing at all - RenderDoc's own hotkey (`F12`) captures even a game that
never calls the core; it just does not write the sidecar.

A game that draws nothing captures nothing: the bare template with no scene loaded produces a
capture with zero draw calls, which is expected rather than a broken setup.

---

## 4. Reading a capture

```bash
python3 Modules/RenderDoc/Scripts/rdc_report.py \
  "<workspace>/<project>/Captures/tk_single_20261109-180420_capture.rdc"
```

Options: `--out DIR` (default `<capture>.report/` next to the capture), `--no-images`,
`--no-shaders`, `--timeout SECONDS`, `--qrenderdoc PATH`, `--verbose`.

```
tk_single_20261109-180420_capture.rdc.report/
  capture.md      <- read this first
  capture.json    <- the full model: passes, actions, resources, sampled state, findings
  status.json     <- how the analyzer exited (the driver reads it, not qrenderdoc's exit code)
  images/         <- one PNG per pass output, plus the final frame
  shaders/        <- shader source of one draw per pass
```

Real output for the two captures taken in this repository (RenderDoc 1.45, Fedora, Debug editor,
`unity-city.scene`, MSAA x4, SSR on):

| Capture | Draw calls | Marker regions | Images | Report time | Capture size |
|---------|-----------|----------------|--------|-------------|--------------|
| `tk_single` (1 frame) | 3,411 | 24 | 29 | 9.3 s | 105 MB |
| `tk_multi` (5 frames) | 17,055 | 120 | 49 | 20.8 s | 113 MB |

A multi frame capture is one file: the counts scale with the frames, and the same pass shows up
once per frame.

### What is in `capture.md`

- A header table: capture, graphics API, engine version and config, project, scene, host frame.
- **At a glance**: draws, instanced draws, indices, dispatches, clears, copies, marker regions,
  resource counts, and the final frame as an image.
- **Passes**: the engine's debug marker regions in recording order with the draw and index count,
  the colour target, the depth output and a link to that pass's PNG. The list *is* the render
  graph of the captured frame (`ShadowPass`, `ForwardPreProcessPass`, `SSAOPass`, `CubeMapPass`,
  `ForwardRenderPass`, `BloomPass`, `SsrPass`, `GammaTonemapFxaaPass`, `GizmoPass`,
  `BillboardPass`, `EditorUI`, ...).
- **A section per pass**: targets, bound shaders, sampled textures, the first draws - the place a
  missing or wrong binding shows up.
- **Largest textures** and **findings**: conservative mechanical hints (a pass with no draws, a
  draw with no indices, a draw with no colour output). Hints, not a verdict.

### Reading a capture for a bug

1. Compare the pass images in recording order. The first pass whose output looks wrong is where
   the frame goes bad; everything after it inherits the problem.
2. Cross check the pass table: a pass with no draws, a pass writing to an unexpected target, a
   pass whose output its consumer does not sample.
3. `capture.json` -> `passPipeline[].textures[].name` and `.stages[].resources` list what the first
   draw of each pass reads and samples.
4. `shaders/` holds the source of those draws; a pass that renders black with the right inputs is
   usually a shader or a transform problem.
5. Timing is not in this report on purpose. Use `PerfAnalysis/` for that.

### Why qrenderdoc

A distributed RenderDoc build does not ship the standalone `renderdoc` python module, so the
analysis has to run inside the interpreter `qrenderdoc` embeds:

```
qrenderdoc --python rdc_analyze.py      # reads the capture from the environment
```

`rdc_analyze.py` ends with `sys.exit()`, which is what tells qrenderdoc to skip its UI and quit.
`rdc_report.py` is the driver that does this, collects `status.json` and prints the summary; it
needs no `renderdoc` module of its own, so any python 3 runs it. It finds `qrenderdoc` through
`TK_RENDERDOC_DIR` / `RENDERDOC_DIR`, then the usual install locations, then `PATH`; `--qrenderdoc`
overrides all of it.

---

## 5. Gotchas and limits

**The library must be in the process before the device.** `renderdoccmd inject <pid>` after the
editor started leaves the API unbound: the core reports unavailable and every request is a no-op.
`CaptureStatus` prints exactly that, and `Modules -> RenderDoc` greys the capture entries out.

**A capture can be refused with "no frame capturer".** RenderDoc drops the whole GL device when the
process calls legacy fixed function OpenGL (`glMatrixMode`, `glOrtho`, the client state array
functions), which a GL capture tool cannot hook. SDL's *accelerated* window surface path uses
exactly those calls, and the editor's splash screen is what triggered it here: the symptom is
`RenderDoc refused the capture. Was a frame presented inside it?` in the editor log, and
`Unsupported function glMatrixMode used` / `Couldn't find matching frame capturer` in RenderDoc's
own log (`/tmp/RenderDoc/RenderDoc_<timestamp>.log` on Linux). `SplashScreen::Run()` now pins
`SDL_HINT_FRAMEBUFFER_ACCELERATION` to `0` so the splash blits in software and the device survives;
`SDL_FRAMEBUFFER_ACCELERATION=0` in the environment has the same effect for a host that does its own
surface blits.

**Replay needs a GPU** of the same family as the capture. On a machine without one, `--no-images`
skips the replay heavy part but the model still needs the replay to open.

**Captures are large.** One Debug frame of `unity-city.scene` is around 105 MB, five frames around
113 MB, because RenderDoc deduplicates across a single file. `MAX_IMAGES` / `MAX_STATE_SAMPLES` in
`Scripts/rdckit/capture.py` bound how much the *report* writes.

**OpenGL and Vulkan only in practice** - the capture control is API agnostic, but the engine only
renders through those two. The web build has no dynamic libraries, so the module lookup compiles to
"not found" there and the whole thing is inert.

**No perf counters**: RenderDoc counters need vendor support and are not part of this report.

---

## 6. Reference

### Request file

`<capture dir>/request`, plain text, `label [frameCount]`, for example `boss_intro 2`. Polled about
every eight frames; deleted before it is acted on, so a rejected request does not fire again.

### Console commands and menu

| Command | Menu entry | What it does |
|---------|-----------|--------------|
| `CaptureFrame <label> [frames]` | Capture Frame | queues a capture (label defaults to `editor`) |
| `CaptureStatus` | Capture Status | API version, library path, capture dir, count, last file, last error |
| `CaptureOpen` | Open Last Capture | opens the last `.rdc` in the RenderDoc UI |

### Engine API (`ToolKit/Source/RenderDocCapture.h`)

```cpp
bool Init(const CaptureRootFn& rootFn, const CaptureContextFn& contextFn = nullptr);
void Shutdown();
void BeginFrame();          // top of the frame, before anything is drawn
void EndFrame();            // after the present
bool IsAvailable();
Status GetStatus();
bool RequestCapture(const String& label, int frameCount = 1);
bool OpenLastCapture();
String GetCaptureDir();
```

`RequestFrameCapture(label, frames)` (`FrameCapture.h`) is the tool agnostic entry point: it
forwards to whatever handler the host installed, and does nothing in a build where nobody did.

### Editor API (`Editor/Source/EditorApi.h`)

A module registers its commands and menu entries through the editor's small surface, which is how
this module contributes its three commands and the **Modules -> RenderDoc** menu, and how it
unregisters them on shutdown. `RegisterModuleMenuItem` takes an optional `enabled` predicate, which
is what greys the capture entries out while RenderDoc is not in the process.
