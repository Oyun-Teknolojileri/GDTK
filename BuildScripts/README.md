# BuildScripts

Cross-platform Python scripts that drive the GDTK build. They replace the
old `BuildDependencies.bat` flow and exist so the same commands work on
Windows, Linux, and (in principle) macOS without any per-platform batch
forking.

## Prerequisites

| Tool       | Notes                                                          |
|------------|----------------------------------------------------------------|
| Python     | 3.6 or newer (uses only the standard library)                 |
| CMake      | 3.16 or newer                                                  |
| Git        | Required for the `submodule` step                              |
| A C/C++ compiler | MSVC (Windows), GCC or Clang (Linux), Apple Clang (macOS) |
| Ninja      | Optional but strongly recommended -- much faster than the alternatives |

> **No system packages are required.** Every dependency (SDL2, glm, assimp,
> minizip-ng, imgui, poolSTL, miniaudio) lives in `Dependency/` as a git
> submodule. The script pulls and builds them.

## `build_dependencies.py`

Builds the vendored dependencies in `Dependency/`. This is the equivalent
of the old `BuildDependencies.bat`.

### Quick start

```bash
# from the repo root
python3 BuildScripts/build_dependencies.py
```

This will:

1. Run `git submodule init` and `git submodule update`.
2. Detect the platform (`Windows` / `Linux` / `Mac`) and pick the best
   available CMake generator. On Windows the script prefers
   `cl.exe` (MSVC) when it is reachable, otherwise Ninja; on Linux/macOS
   the script prefers Ninja, otherwise Unix Makefiles. See
   [Generator selection on Windows](#generator-selection-on-windows).
3. Configure and build `Dependency/CMakeLists.txt` for the chosen
   configurations, producing binaries under
   `Dependency/Intermediate/<Platform>/<Config>/`.

### Common flags

```text
--platform {Windows,Linux,Mac,auto}    Default: auto-detect
--configs  CFG [CFG ...]              Default: Debug Release
--skip-submodules                     Skip git submodule update
--skip-assimp                         Skip assimp (faster when not building Import)
--skip-imgui                          Skip imgui (faster when not building Editor)
--clean                               Wipe Dependency/Intermediate/<Platform>/ first
-j, --parallel N                      Parallel build jobs (default: CPU count)
--generator {auto,msvc,ninja,make}    Default: auto
                                       auto: on Windows picks MSVC (cl.exe)
                                             if reachable, else Ninja, else
                                             the platform fallback.
                                             On non-Windows: Ninja > Make.
                                       msvc:  force Visual Studio generator
                                       ninja: force Ninja (errors if missing)
                                       make:  force Unix Makefiles
```

### Generator selection on Windows

On Windows the script prefers **cl.exe** (MSVC) because it matches the rest
of the GDTK build. `cl.exe` is located in one of two ways:

1. `cl` is already on `PATH` (e.g. you ran the script from a `vcvars64.bat`
   shell).
2. `cl` is not on `PATH` but VS is installed: the script probes
   `vswhere.exe` and resolves the latest `VC\\Tools\\MSVC\\*\\bin\\Hostx64\\x64\\cl.exe`
   together with the install path.

The CMake generator name is then picked from a small mapping table that
keys off the install-path parent folder, because Microsoft switched the
on-disk folder from a calendar year (`2022`) to a major version number
(`18` for VS 2026) in late 2025 while CMake kept using the historical
`Visual Studio <major> <year>` naming. The table looks like this:

| Install path ends in | CMake generator            |
|----------------------|----------------------------|
| `2022`               | `Visual Studio 17 2022`    |
| `18`                 | `Visual Studio 18 2026`    |
| `19`                 | `Visual Studio 19 2027`    |
| `2019`               | `Visual Studio 16 2019`    |
| `2017`               | `Visual Studio 15 2017`    |

If you upgrade Visual Studio to a release the table does not know about,
either add a row in `_VS_GENERATOR_BY_INSTALL_DIR` at the top of
`build_dependencies.py` or pass `--generator ninja` to sidestep the
mapping entirely.

When the MSVC generator is selected, the script pins
`-DCMAKE_C_COMPILER=<cl.exe>` and `-DCMAKE_CXX_COMPILER=<cl.exe>` to the exact
toolset it located, so CMake does not silently fall back to a different
MSVC version. Pass `--generator ninja` to force Ninja instead (useful in CI
or when you have a custom toolchain setup).

### Output layout

```
Dependency/
└── Intermediate/
    ├── Windows/
    │   ├── Debug/         *.lib, *.dll, *.pdb
    │   ├── RelWithDebInfo/
    │   └── Release/
    └── Linux/
        ├── Debug/         *.a, *.so
        └── Release/
```

> The `CopyDependencies` step (which copies DLLs next to the editor on
> Windows) is invoked automatically when the platform is `Windows` and the
> generator is the multi-config Visual Studio one. On Linux there is
> nothing to copy, so the step is skipped.

### Typical workflows

```bash
# Cold first-time build
python3 BuildScripts/build_dependencies.py

# Rebuild after pulling submodule updates
python3 BuildScripts/build_dependencies.py --skip-assimp --skip-imgui

# Only Debug, in a clean tree
python3 BuildScripts/build_dependencies.py --configs Debug --clean

# Pin to a specific platform (useful in CI)
python3 BuildScripts/build_dependencies.py --platform Linux --configs Release
```

## Adding more scripts

This folder is the new home for any cross-platform build glue. Future
scripts (engine build, test runner, packaging, etc.) should land here and
follow the same stdlib-only Python style.
