# BuildScripts

Cross-platform Python scripts that drive the GDTK build. They replace the
old `BuildDependencies.bat` flow and exist so the same commands work on
Windows, Linux, and (in principle) macOS without any per-platform batch
forking.

## Prerequisites

| Tool       | Notes                                                          |
|------------|----------------------------------------------------------------|
| Python     | 3.6 or newer (uses only the standard library)                  |
| CMake      | 3.16 or newer                                                  |
| Git        | Required for the `submodule` step                              |
| A C/C++ compiler | MSVC (Windows), GCC or Clang (Linux), Apple Clang (macOS)  |
| Ninja      | Optional but strongly recommended -- much faster than the alternatives |

> All dependency *libraries* (SDL2, glm, assimp, minizip-ng, imgui,
> poolSTL, miniaudio) live in `Dependency/` as git submodules and the
> script pulls + builds them. The one exception on Linux is SDL2's
> X11 video backend, which is now the primary Linux backend (we want
> Win32-comparable windowing behavior and don't want SDL2 to prefer
> Wayland). X11 ships as a system package on every desktop distro,
> but the matching `*-devel` headers must be installed **before**
> running the build:

### Linux X11 dev packages

| Distro          | Packages                                                                                  |
|-----------------|-------------------------------------------------------------------------------------------|
| Fedora / RHEL   | `libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXinerama-devel libxkbcommon-devel` |
| Debian / Ubuntu | `libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxkbcommon-dev` |
| Arch            | `libx11 libxext libxrandr libxcursor libxi libxinerama libxkbcommon`                      |

On a Wayland session the resulting binary talks to Xwayland, which
is exactly the path we want here: it gives the Win32-style input
and coordinate semantics we depend on, instead of Wayland's. If
SDL2 configure fails because none of those modules are reachable,
the dependency build aborts with the same package list inline.

`build_gdtk.py` enforces this pre-flight automatically on Linux: it
detects the distro family from `/etc/os-release` and probes every
package in the table above. By default a missing package just
prints the per-distro install command and aborts (no implicit
sudo). Pass `--install-deps` to have the script run the install
itself. With `--vulkan` the same gate also covers the Vulkan
loader / shaderc / VMA dev packages.

### Optional: Vulkan backend (`--vulkan`)

The Vulkan render path is **opt-in** because it pulls in heavy system
libraries that most CI images don't carry by default. To enable it,
install the platform's Vulkan + shaderc + VMA dev packages **before**
running the build:

| Distro                | Packages                                                                                  |
|-----------------------|-------------------------------------------------------------------------------------------|
| Fedora / RHEL         | `vulkan-loader-devel libshaderc-devel VulkanMemoryAllocator-devel mesa-vulkan-drivers`     |
| Debian / Ubuntu       | `libvulkan-dev libshaderc-dev libvulkan-memory-allocator-dev mesa-vulkan-drivers`         |
| Arch                  | `vulkan-headers vulkan-icd-loader shaderc vulkan-memory-allocator`                        |
| Windows               | LunarG Vulkan SDK (`VULKAN_SDK` env var set to the SDK root)                              |

Then:

```bash
python3 BuildScripts/build_gdtk.py --vulkan
```

The build script forwards `--vulkan` as `-DTK_VULKAN=ON` to CMake,
which:

1. Pulls `ToolKit/Render/Vulkan/*.cpp` into the GLOB'd source list
   (otherwise they are filtered out and only the GL backend builds).
2. Defines `TK_VULKAN` so `#ifdef`-guarded Vulkan branches in
   Renderer / Shader / Resources / Entities activate.
3. Links `libvulkan.so.1` + `libshaderc_shared.so.1` on Linux
   (resolved by absolute path -- distros ship inconsistent SONAMEs)
   and `vulkan-1.lib` on Windows.
4. Adds the VMA header path: implicit `/usr/include` on Linux,
   `$VULKAN_SDK/Include/vma` on Windows.

Without `--vulkan` (the default) the engine builds the legacy GL
backend and zero Vulkan source files participate in the compile.

## Scripts at a glance

| Script | Purpose |
|---|---|
| `_common.py` | Internal helpers shared by the two entry points: terminal colors, toolchain probing, the CMake invoker, the per-platform clean helper. **Not meant to be run directly.** |
| `build_gdtk.py` | **Main entry point.** Configures + builds the root CMake project (ToolKit, Workspace, Editor, Launcher, Import, Packer). Auto-invokes `build_dependencies.py` if the dep tree is missing. |
| `build_dependencies.py` | **Advanced / explicit.** Builds the vendored dep tree only, without touching the engine. Use this when you want to warm up the dep cache without configuring GDTK, or when you want to drive the two steps separately (e.g. in CI). |

For 99% of workflows, **only `build_gdtk.py` is needed** -- the dep tree is
populated on demand.

## Quick start

```bash
# Cold first-time build -- runs dep build then engine build.
python3 BuildScripts/build_gdtk.py

# Cold first-time build on a fresh Linux box. Without --install-deps
# the script aborts with a "missing X11 dev packages" list if
# libX11-devel etc. are not yet installed; --install-deps has it
# sudo-install the lot via dnf / apt / pacman.
python3 BuildScripts/build_gdtk.py --install-deps

# Rebuild after a ToolKit source change (deps already present, so the
# script logs "skipping build_dependencies.py" and goes straight to
# the engine).
python3 BuildScripts/build_gdtk.py --target ToolKit

# Just the editor, in Debug only.
python3 BuildScripts/build_gdtk.py --configs Debug --target Editor

# Vulkan build. --install-deps also covers the Vulkan + shaderc + VMA
# dev packages if missing.
python3 BuildScripts/build_gdtk.py --vulkan --install-deps
```

The script announces what it is doing at every step:

| Output line                                          | Meaning                                                            |
|------------------------------------------------------|--------------------------------------------------------------------|
| `Dependency tree is missing for: <Plat>/<Cfg>`       | One or more configs are not yet built; the script is going to fix it. |
| `Invoking build_dependencies.py to populate it ...`  | The dep script is being spawned with the same `--platform` / `--configs` / `--parallel`. |
| `Dependency tree populated.`                         | Auto-invoked dep build finished; the engine build proceeds.        |
| `Dependency tree present -- skipping build_dependencies.py.` | Deps already on disk; the script does not re-run them.       |

## `build_gdtk.py` flags

```text
--platform {Windows,Linux,Mac,auto}    Default: auto-detect
--configs  CFG [CFG ...]              Default: Debug Release
--target  TGT                         Build only TGT. May be passed
                                      multiple times. Default: all.
--vulkan                              Build with the Vulkan render backend
                                      (TK_VULKAN=ON). Requires the
                                      platform's Vulkan + shaderc + VMA
                                      dev packages -- see "Optional:
                                      Vulkan backend" above.
--install-deps                        Linux only. If SDL2 X11 dev
                                      packages (and, with --vulkan,
                                      Vulkan loader + shaderc + VMA
                                      dev packages) are missing,
                                      install them via the distro's
                                      package manager + sudo
                                      automatically. Without this flag
                                      the script just lists what's
                                      missing and aborts.
--no-deps-check                       Skip the dep auto-invoke (default
                                      behaviour is to build deps on
                                      demand when missing).
--clean                               Wipe Intermediate/<Platform>/ first
-j, --parallel N                      Parallel build jobs (default: CPU count)
--generator {auto,msvc,ninja,make}    See "Generator selection" below.
```

### Generator selection

`build_gdtk.py` (and `build_dependencies.py`) probe for the best CMake
generator using the same logic on Windows:

1. `cl.exe` on `PATH` (e.g. you ran the script from a `vcvars64.bat` shell)
   -> the Visual Studio generator matching the installed VS release.
2. `cl.exe` not on `PATH` but VS is installed -> `vswhere.exe` resolves
   the latest `VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe` and the install
   path.
3. Otherwise Ninja (if available) or the platform fallback
   (Unix Makefiles on Linux/macOS).

The generator name is picked from a small mapping table that keys off
the VS install-path parent folder, because Microsoft switched the
on-disk folder from a calendar year (`2022`) to a major version number
(`18` for VS 2026) in late 2025 while CMake kept using the historical
`Visual Studio <major> <year>` naming:

| Install path ends in | CMake generator            |
|----------------------|----------------------------|
| `2022`               | `Visual Studio 17 2022`    |
| `18`                 | `Visual Studio 18 2026`    |
| `19`                 | `Visual Studio 19 2027`    |
| `2019`               | `Visual Studio 16 2019`    |
| `2017`               | `Visual Studio 15 2017`    |

If you upgrade Visual Studio to a release the table does not know about,
either add a row in `_VS_GENERATOR_BY_INSTALL_DIR` at the top of
`_common.py` (single source of truth, picked up by both scripts) or
pass `--generator ninja` to sidestep the mapping entirely.

When the MSVC generator is selected, both scripts pin
`-DCMAKE_C_COMPILER=<cl.exe>` and `-DCMAKE_CXX_COMPILER=<cl.exe>` to the
exact toolset they located, so CMake does not silently fall back to a
different MSVC version. Pass `--generator ninja` to force Ninja instead
(useful in CI or with a custom toolchain setup).

## Output layout

```
Bin/                                  Final binaries the engine ships
  ├── Editor(.exe)
  ├── Launcher(.exe)
  ├── libToolKitd.so / ToolKitd.dll   # with --vulkan: NEEDITED libvulkan.so.1,
  │                                    libshaderc_shared.so.1, libSPIRV-Tools.so
  └── libWorkspace.a / Workspace.lib
Intermediate/<Platform>/<Config>/     CMake state + per-module .o files
  ├── build-ToolKit/
  ├── build-Editor/
  ├── build-Launcher/
  ├── build-Import/
  └── build-Packer/
Dependency/Intermediate/<Platform>/<Config>/   Vendored dep artifacts
  ├── libSDL2-2.0d.so / SDL2d.lib
  ├── libminizipd.a / minizipd.lib
  ├── libzstdd.a / zstd_staticd.lib
  ├── libassimpd.so / assimpd.lib
  ├── libimguid.so / imguid.lib
  └── <dep>/<dep>Config.cmake   (wrapper for find_package CONFIG mode)
```

## `build_dependencies.py` flags

Use this when you want to drive the two steps explicitly or skip the
engine build entirely. The auto-invoke in `build_gdtk.py` calls this
script under the hood, so behaviour is identical to running it by hand.

```text
--platform {Windows,Linux,Mac,auto}    Default: auto-detect
--configs  CFG [CFG ...]              Default: Debug Release
--skip-submodules                     Skip git submodule update
--skip-assimp                         Skip assimp (faster when not building Import)
--skip-imgui                          Skip imgui (faster when not building Editor)
--clean                               Wipe Dependency/Intermediate/<Platform>/ first
-j, --parallel N                      Parallel build jobs (default: CPU count)
--generator {auto,msvc,ninja,make}    Same semantics as build_gdtk.py
```

> **Note:** `build_dependencies.py` has no `--vulkan` flag because the
> dependency tree (SDL2, glm, assimp, ...) is identical regardless of
> which render backend the engine selects. Vulkan's own libraries
> (libvulkan, libshaderc, VMA) are system-provided and consumed
> directly from the OS package set, not built as submodules. The
> `--vulkan` flag is engine-side only; see `build_gdtk.py --help`.

## Adding more scripts

This folder is the new home for any cross-platform build glue. Future
scripts (test runner, packaging, etc.) should land here and follow the
same stdlib-only Python style. Anything shared between scripts belongs
in `_common.py`.