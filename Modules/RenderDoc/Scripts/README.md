# RenderDoc capture tooling

Frame capture for the GDTK editor, driven by RenderDoc's in-application API, plus
a python side that turns a `.rdc` capture into a report an AI (or a human) can
read: JSON model, markdown digest, PNG images and shader sources.

The point is to make "capture this frame and tell me what is wrong with it" a
command line away, instead of a trip through the RenderDoc UI.

---

## 1. How it is put together

```
ToolKit/Source/RenderDocCapture.{h,cpp}   the capture core: binds the injected
                                          RenderDoc, drives the frame, writes the
                                          .rdc and its sidecar, serves the request
                                          file. Any host that links the engine gets it.
Dependency/utilities/renderdoc_app.h      RenderDoc's in-application API header (MIT)
Modules/RenderDoc/                        editor glue (static library the editor links)
  Source/RenderDocModule.{h,cpp}          capture directory, sidecar context, the
                                          four calls Editor/Source/main.cpp makes
  Source/RenderDocCommands.{h,cpp}        console commands and Modules menu entries
  Scripts/                                the report tooling (this folder)
Editor/Source/EditorApi.h                 the editor surface the module uses to
                                          register its commands and menu entries
ToolKit/Source/FrameCapture.{h,cpp}       engine side hook: RequestFrameCapture()
```

The core is in the engine so that a game *can* get it too: the engine knows
nothing about projects, so `RenderDocCapture::Init` takes callbacks for the capture
directory and for the extra sidecar fields; the editor answers
`<workspace>/<project>/Captures` plus workspace / project / scene, a game would
answer `<cwd>/Captures` and nothing else. Nothing in the game template calls it,
because not every game wants capture - see "Capturing in a game" below for the
four lines that opt in.

Everything the editor contributes on top - the console commands, the
`Modules -> RenderDoc` menu - lives in `Modules/RenderDoc`. The module's CMake
globs `*.cpp` only, so this `Scripts/` folder of python is invisible to the build.

Game code never links host symbols: it reaches the capture core through the engine
hook and never names RenderDoc.

```cpp
#include <FrameCapture.h>

void MyGame::Update(float deltaTime)
{
  if (bossFightStarted)
  {
    // No-op unless a host installed a capture handler and RenderDoc is present.
    ToolKit::RequestFrameCapture("boss_intro");
  }
}
```

The same call works from an editor plugin, and from the editor console
(`CaptureFrame boss_intro`), which is where the module registers its commands.

### How the module plugs into the editor

`Editor/Source/EditorApi.h` is the whole contract, and it is deliberately small:

```cpp
Editor::RegisterConsoleCommand("CaptureFrame", CaptureFrameExec);
Editor::UnregisterConsoleCommand("CaptureFrame");
Editor::WriteConsoleLine("Capture requested", LogType::Memo);

Editor::RegisterModuleMenuItem("RenderDoc", "Capture Frame", captureAction,
                               []() { return IsAvailable(); });
Editor::UnregisterModuleMenu("RenderDoc");
```

The command table is editor wide and lives outside `ConsoleWindow`, so a module
registers from its own `Init()` before any window exists. The console drains the
same table for its command list, its auto complete and its `ExecCommand`, so a
module command behaves like a built in one.

`RegisterModuleMenuItem` is what puts the module in the menu bar: entries land
under **Modules -> RenderDoc**, and the editor draws that menu from the registry
(`UI::ShowMenuModules`). The top level entry only exists while something is
registered, and the optional `enabled` predicate greys an entry out - which is
how `Capture Frame` and `Open Last Capture` disable themselves when the editor
was not started through RenderDoc.

## 2. Build gating

There is none: the module is linked into every editor config (Debug,
RelWithDebInfo, Release, MinSizeRel), because the editor is a development tool
and the code goes nowhere else.

RenderDoc still stays out of a shipped game, and for a stronger reason than a
preprocessor switch: no game target links `Modules/RenderDoc`, and the library
is only ever *looked up by name* in the running process - the module never loads
it. A published game has no RenderDoc code, no RenderDoc dependency and no
capture UI; the engine side it does link is the 60 line `FrameCapture` hook,
which names no graphics API and does nothing when no host installed a handler.

## 3. Taking a capture

RenderDoc has to be in the process **before** the graphics device is created, so
the editor is launched through it:

```powershell
# 1. Start the editor under RenderDoc (headless launch, no UI).
& "$env:ProgramFiles\RenderDoc\renderdoccmd.exe" capture -w `
    -d BinDebug -c BinDebug/editor BinDebug/Editor.exe `
    --workspace C:\path\to\Workspace --project-name <project>

# 2. Ask for a capture. This is the automation channel: write the request file,
#    the editor picks it up within a few frames.
Set-Content "<workspace>/<project>/Captures/request" -Value "boss_intro 1"
```

Captures land in `<workspace>/<project>/Captures/` as
`<label>_<timestamp>_capture.rdc` plus a `.json` sidecar and a
`last_capture.json` pointer that names the capture that finished last.

Three ways to ask, all through the same queue:

| From | How |
|------|-----|
| A tool / AI / CI | write `Captures/request` with `label [frameCount]` |
| The console, or Modules -> RenderDoc in the menu bar | `CaptureFrame boss_intro 2`, `CaptureStatus`, `CaptureOpen` |
| Game code | `ToolKit::RequestFrameCapture("boss_intro")` |

The capture is not opened inside the call: the request is queued and the editor
opens it at the next frame boundary, so the whole frame - present included - ends
up in the file. `frameCount` spans several frames (up to 60).

If RenderDoc is not in the process the console command `CaptureStatus` says so
and every request is a no-op, which is what a normal editor start looks like.

### Attaching to an editor that is already running

```powershell
& "$env:ProgramFiles\RenderDoc\renderdoccmd.exe" inject <editor pid>
```

The API is bound at startup, so an editor that started without RenderDoc stays
without it: restart it under RenderDoc instead.

## 4. Reading a capture

```powershell
python Modules/RenderDoc/Scripts/rdc_report.py <workspace>/<project>/Captures/boss_intro_*.rdc
```

The report directory defaults to `<capture>.report/` next to the capture:

```
boss_intro_20260101-120000_capture.rdc.report/
  capture.md      <- read this first
  capture.json    <- the full model
  images/         <- one PNG per pass output + the final frame
  shaders/        <- shader source of one draw per pass
```

`capture.md` holds the frame summary, the pass table with its images, a section
per pass (targets, shaders, sampled textures, first draws), the largest textures
and the mechanical findings. `capture.json` has all of that plus every action and
resource, for a tool that would rather parse than read.

Options: `--out DIR`, `--no-images`, `--no-shaders`, `--timeout SECONDS`,
`--qrenderdoc PATH`, `--verbose`.

### Why qrenderdoc

A distributed RenderDoc build does not ship the standalone `renderdoc` python
module, so the analysis runs inside the interpreter that `qrenderdoc.exe` embeds:

```
qrenderdoc.exe --python rdc_analyze.py      # reads the capture from the env
```

The script ends with `sys.exit()`, which is what tells qrenderdoc to skip its UI
and quit. `rdc_report.py` is the driver that does this and collects the result;
it needs no `renderdoc` module of its own, any python 3 will run it.

Set `TK_RENDERDOC_DIR` (or `RENDERDOC_DIR`) if RenderDoc is installed somewhere
unusual, or pass `--qrenderdoc`.

## 5. What the report is for

The passes are the engine's debug marker regions, so the report's pass list *is*
the render graph of the captured frame (`ShadowPass`, `ForwardPreProcessPass`,
`SSAOPass`, `BloomPass`, `GammaTonemapFxaaPass`, `EditorUI`, ...). Comparing the
images in recording order shows where a frame goes wrong, and
`passPipeline[].stages[].resources` in the JSON shows the samplers each pass's
first draw binds, which is where a missing texture shows up.

The mechanical findings at the end are deliberately conservative - an empty pass,
a draw with no indices, a draw with no colour output. They are hints, not a
verdict.

## 6. Capturing in a game (opt in)

Nothing in the game template calls the capture core, because not every game wants
it. A host that does want it needs four lines - the core is already inside
`TK::ToolKit`, so there is no extra library to link:

```cpp
#include "RenderDocCapture.h"

// PreInit(), before any graphics object exists: bind RenderDoc if it is in the
// process and install the RequestFrameCapture() handler.
RenderDocCapture::Init([]() -> String { return ConcatPaths({GetCurrentPath(), "Captures"}); });

// TK_Loop(), around the frame, present included:
RenderDocCapture::BeginFrame();
g_proxy->FrameBegin();
g_proxy->FrameUpdate();
g_proxy->FrameEnd();
RenderDocCapture::EndFrame();

// Exit(), while the engine is still alive:
RenderDocCapture::Shutdown();
```

Then the same three ways in work: `ToolKit::RequestFrameCapture("label")` from game
code, a `Captures/request` file, or RenderDoc's own hotkey. Launch the game through
RenderDoc so the hooks exist before the graphics device:

```powershell
renderdoccmd capture -w -d Codes/Bin -c Captures/game Codes/Bin/MyGame.exe
```

The sidecar lands next to the capture with no project or scene fields (a game has
neither), and the report reads it the same way.

## 7. Known limits

- **OpenGL and Vulkan only in practice**: the capture control is API agnostic,
  but GDTK only renders through those two.
- **The editor must be started through RenderDoc** (section 3).
- **Replay needs a GPU**: the report replays the frame, so it runs on a machine
  with a working driver of the same family as the capture.
- **Very large captures**: `MAX_IMAGES` / `MAX_STATE_SAMPLES` in
  `rdckit/capture.py` bound how much the report writes; `--no-images` skips the
  replay-heavy part entirely.
- **No perf counters**: RenderDoc counters need vendor support and are not part
  of this report. Use `PerfAnalysis/` for timing work.
