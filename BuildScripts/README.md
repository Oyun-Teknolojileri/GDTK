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
   available CMake generator (`Ninja` > `Visual Studio 17 2022` on
   Windows, `Ninja` > `Unix Makefiles` elsewhere).
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
```

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
