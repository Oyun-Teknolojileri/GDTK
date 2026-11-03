# PerfAnalysis

Device measurement snapshots. Each folder here is one investigation: the report, the scripts that
produced it, and the raw run logs it cites. Nothing in here is built or tested by CMake.

## stage 1 - "small-scene" on SM-G950F (Galaxy S8, Exynos 8895 / Mali-G71 MP20)

`stage1-bottleneck-report.md` is the write-up. Short version: the 1080x2220 frame takes 162 ms,
the GPU needs 140 ms of it, the CPU sits at 36 % of one core, and the cost is per-pixel work
(fill and shading), not colour bandwidth, not vertex work, not draw submission.

The measurements exist because `Stats::GetRenderTime` had no working gpu half on GL ES - the
timer query was compiled out unless the platform was Windows. That gap is what the engine part of
this commit closes; the logs below are what the fixed stat and the per-pass profiler reported.

### How a run was taken

```
powershell -NoProfile -File run-experiment.ps1 -Label base -Seconds 14
powershell -NoProfile -File run-experiment.ps1 -Label res50 -Set "res=50"
powershell -NoProfile -File run-experiment.ps1 -Label res25_noshadow -Set "res=25,shadow=0"
```

Each run force-stops the app, applies `debug.tk.*` system properties, starts it cold, waits for the
scene, samples the frame period from SurfaceFlinger and the process CPU from `/proc`, dumps the
`PERF`/`PROF` lines from logcat, then force-stops the app again. `measure.ps1` is the older
external-only variant, kept because it needs no instrumented build.

Runs are short and separated on purpose: a phone left rendering for minutes throttles and every
later number is then measured on a different machine.

### Runs

`period`/`fps` are from SurfaceFlinger, `draws`/`gpu` from the in-app probe. `gpu` is the engine's
`GL_TIME_ELAPSED_EXT` number and wobbles by up to +-20 %; the frame period is the trustworthy one.

| log | configuration | period | fps | draws | gpu | status |
| --- | --- | --- | --- | --- | --- | --- |
| `logs/base.log` | stock settings | 162 ms | 6.2 | 186 | broken (4.29 s) | superseded: timer read 0xFFFFFFFF |
| `logs/base2.log` | stock settings | 161 ms | 6.2 | 186 | 142 ms | first real gpu number |
| `logs/base3.log` | stock settings | 162 ms | 6.2 | 186 | 139 ms | reference run |
| `logs/base4.log` | stock settings | 124 ms | 8.1 | 93 | 106 ms | knob leak, see below: shadows were off |
| `logs/res50.log` | `res=50` (540x1110) | 68 ms | 14.5 | 185 | 55 ms | reference |
| `logs/res25.log` | `res=25` (270x555) | 53 ms | 18.5 | 185 | 33 ms | reference |
| `logs/res25_noshadow.log` | `res=25,shadow=0` | 34 ms | 29.6 | 91 | 22 ms | second point for the resolution-independent fit |
| `logs/shadowoff.log` | `shadow=0` | 124 ms | 8.1 | 93 | 107 ms | reference |
| `logs/sceneoff.log` | `mesh=0` | 55 ms | 18.3 | 19 | 37 ms | reference (same run as `nomesh.log`) |
| `logs/skyonly.log` | `mesh=0,shadow=0,ssao=0,bloom=0,tm=0` | 20 ms | 49 | 4 | 14 ms | reference |
| `logs/nohdr.log` | `hdr=0` (RGBA8 targets) | 156 ms | 6.4 | 186 | 132 ms | reference |
| `logs/msaa4.log` | `msaa=4` | 182 ms | 5.5 | 186 | 175 ms | reference |
| `logs/msaa0b.log` | `msaa=0` | 138 ms | 7.2 | 268 | 142 ms | see the culling note in the report |
| `logs/nobloom.log` | `bloom=0` | 156 ms | 6.4 | 174 | 200 ms | gpu reading is suspect (above period) |
| `logs/nossao.log` | `ssao=0` | 151 ms | 6.6 | 147 | 151 ms | reference |
| `logs/nocull.log` | `cull=0` | 168 ms | 5.9 | 268 | 170 ms | reference |
| `logs/nobatch.log` | `batch=0` | 161 ms | 6.2 | 297 | 140 ms | reference |
| `logs/res50_nopost.log` | `res=50,ssao=0,bloom=0` | 55 ms | 18.1 | 135 | - | reference |
| `logs/res50_noshadow.log` | `res=50,shadow=0` | 48 ms | 21.1 | 91 | 43 ms | reference |
| `logs/noaa.log` | `msaa=0` | 141 ms | 7.1 | 269 | 143 ms | superseded by `msaa0b.log` |
| `logs/nopost.log` | intended `ssao=0,bloom=0,tm=0` | 141 ms | 7.1 | 148 | 141 ms | discarded: only `ssao=0` arrived, comma parsing |
| `logs/res50b.log` | `res=50` | 17 ms | 60 | 19 | 13 ms | discarded: every knob was still off, vsync capped |

Two method failures are kept in the logs on purpose, because both silently produce plausible
numbers:

- `setprop debug.tk.x ''` is refused by the property service on Android 9, so a knob from the
  previous run stays set. `noshadow.log` and `base4.log` measured a config that still had
  `mesh=0`/`shadow=0` from the run before. The script now resets every knob to `-1`, which every
  knob reads as "not set".
- `powershell -File script.ps1 -Set "a=1,b=2"` passes the list as one string, so only the first
  entry is applied. `nopost.log` believes it turned three features off and turned off one.

`logs/publish-*.log` are the Packer/gradle runs that produced the APKs: `publish-1.log` is the
build that failed on a missing include, `publish-2.log` the first good one, `publish-3.log` added
the exclusive-time profiler dump and the timer guards, `publish-4.log` added the `shadow`/`mesh`
knobs.

`results.txt` is the raw one-line-per-run summary the scripts appended. Its first line predates the
CPU formula fix (it reads `cpu=343.3%`, the same run is 34.3 %) and the last block of lines comes
from runs that the table above marks as discarded.

### Notes for whoever repeats this

- The device is a 2017 flagship: 1440x2960 panel, `wm size` override at 1080x2220, 60 Hz, no
  readable GPU counters (`/sys/kernel/gpu` is SELinux-denied, there is no mali devfreq node), so
  the GPU side is measured through `GL_EXT_disjoint_timer_query` and the MIF devfreq clock, which
  is readable and follows the pixel count.
- `adb shell wm size 720x1480` re-renders at a smaller window but the app has to be restarted for
  the render target to follow; `debug.tk.res` does the same thing without a restart because it
  rebuilds the viewport at startup.
