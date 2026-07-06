#!/usr/bin/env python3
"""
GDTK dependency build script.

Cross-platform replacement for BuildDependencies.bat. Builds the vendored
dependencies in Dependency/ via CMake. Supports Windows and Linux out of
the box; macOS is wired up but untested.

All build artifacts are produced under Dependency/Intermediate/<Platform>/.
No system packages are required -- every dependency lives in the repo as a
git submodule.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Tuple

# Project layout -- the script lives in BuildScripts/, the GDTK root is its parent.
SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent
DEP_SRC = ROOT_DIR / "Dependency"
DEP_INT = ROOT_DIR / "Dependency" / "Intermediate"

# Default build configurations. RelWithDebInfo is omitted by default since
# it is rarely useful on Linux and roughly doubles the build time.
DEFAULT_CONFIGS = ["Debug", "Release"]


# Shared helpers -- terminal colors, toolchain probing, CMake invoker,
# clean helper. Anything that build_gdtk.py also needs lives in
# _common.py so a VS release bump only requires touching one place.
sys.path.insert(0, str(SCRIPT_DIR))
from _common import (  # noqa: E402  -- intentional import after sys.path tweak
    Style, info, ok, warn, err, section,
    which, require_tool,
    detect_platform, detect_generator, detect_parallel_jobs,
    _run_cmake, clean_platform,
)


# --------------------------------------------------------------------------- #
# Submodules                                                                  #
# --------------------------------------------------------------------------- #

def update_submodules() -> None:
    info("git submodule init")
    subprocess.run(["git", "submodule", "init"], cwd=ROOT_DIR, check=True)
    info("git submodule update")
    subprocess.run(["git", "submodule", "update"], cwd=ROOT_DIR, check=True)


# --------------------------------------------------------------------------- #
# CMake                                                                       #
# --------------------------------------------------------------------------- #

def _common_cmake_args(
    *,
    platform: str,
    skip_assimp: bool,
    skip_imgui: bool,
) -> List[str]:
    """Arguments shared by every configure invocation."""
    return [
        f"-DTK_PLATFORM={platform}",
        f"-DSKIP_ASSIMP={'TRUE' if skip_assimp else 'FALSE'}",
        f"-DSKIP_IMGUI={'TRUE' if skip_imgui else 'FALSE'}",
        f"-DTOOLKIT_DIR={ROOT_DIR}",
    ]


# Per-dep wrapper config files live under Dependency/Config/. They are
# tiny (a few dozen lines each) and are copied next to the prebuild
# artifacts so the root CMakeLists can import them via
# `find_package(<dep> REQUIRED CONFIG)`. Without this staging step the
# root build would have to keep a per-platform, per-config file-name
# table (libminizip<d>.a vs minizip<d>.lib vs zstd_static<d>.lib, ...)
# inside CMakeLists.txt
def stage_dep_configs(platform: str, config: str) -> None:
    """Copy Dependency/Config/<dep>-config.cmake to <deps>/<dep>/.

    The destination layout matches CMake's find_package CONFIG search
    paths: `<prefix>/<name>/<name>Config.cmake` and
    `<prefix>/<name>/<name>-config.cmake` are both standard probe
    locations. By putting each wrapper in its own `<dep>/` subdir
    we keep the prebuild artifacts (libfoo<d>.a, ...) next to their
    metadata without any extra cmake/ subdirectory layer.
    """
    config_src = ROOT_DIR / "Dependency" / "Config"
    if not config_src.is_dir():
        # The wrapper files are a build-system addition; if the source
        # tree is missing them we cannot proceed. Surface a clear error
        # so the user knows it is a checkout / merge issue, not a
        # prebuild failure.
        err(f"Dependency/Config/ directory missing at {config_src}.")
        err("It should contain <dep>-config.cmake wrapper files.")
        raise FileNotFoundError(str(config_src))

    dst_root = ROOT_DIR / "Dependency" / "Intermediate" / platform / config
    count = 0
    for src in sorted(config_src.glob("*.cmake")):
        # Source file name: SDL2-config.cmake -> dep_name = SDL2.
        # Destination file name: SDL2Config.cmake (CamelCase) because
        # that is the file name CMake's find_package CONFIG mode
        # probes for. `<name>-config.cmake` (all-lowercase + dash)
        # would also be accepted, but our dep names are camelCase
        # (SDL2, SDL2main, ...) so the CamelCase form reads better.
        dep_name = src.name[: -len("-config.cmake")]
        dst_dir = dst_root / dep_name
        dst_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst_dir / f"{dep_name}Config.cmake")
        count += 1
    info(f"Staged {count} wrapper config(s) under {dst_root}/<dep>/")


def build_windows(
    configs: List[str],
    *,
    skip_assimp: bool,
    skip_imgui: bool,
    parallel: int,
    generator: str,
    is_multiconfig: bool,
    cl_path: Optional[str],
    ninja_path: Optional[str],
) -> List[Tuple[str, bool]]:
    """Build the dependency tree on Windows. Returns per-config (ok?)."""
    results: List[Tuple[str, bool]] = []

    # When we picked the MSVC generator explicitly, pin the C/C++ compilers
    # to the exact cl.exe we located. CMake's multi-config generator will
    # otherwise sniff MSBuild and pick whichever toolset it finds first,
    # which can silently disagree with what GDTK expects (vc143 / vc145).
    msvc_compiler_args: List[str] = []
    if is_multiconfig and cl_path and generator.startswith("Visual Studio"):
        msvc_compiler_args = [
            f"-DCMAKE_C_COMPILER={cl_path}",
            f"-DCMAKE_CXX_COMPILER={cl_path}",
        ]

    if is_multiconfig:
        # One configure, three builds. Output goes to Intermediate/Windows/.
        build_dir = DEP_INT / "Windows"
        build_dir.mkdir(parents=True, exist_ok=True)
        configure_args = [
            "cmake", "-S", str(DEP_SRC), "-B", str(build_dir),
            "-G", generator, "-A", "x64",
            *([f"-DCMAKE_MAKE_PROGRAM={ninja_path}"] if ninja_path else []),
            "-DTK_WINDOWS=Windows",
            *msvc_compiler_args,
            *_common_cmake_args(
                platform="Windows",
                skip_assimp=skip_assimp,
                skip_imgui=skip_imgui,
            ),
        ]
        try:
            _run_cmake(configure_args)
        except subprocess.CalledProcessError:
            return [(c, False) for c in configs]

        for config in configs:
            section(f"Windows / {config}")
            # Build every dependency in the tree, then run
            # CopyDependencies on top of that to stage the runtime
            # DLLs next to Bin/ and Utils/. The two invocations
            # are separate because CopyDependencies' `DEPENDS`
            # list only mentions SDL2 + imgui -- building only
            # that target would skip minizip, zstd and assimp
            # (the static libs are link-time only, the assimp
            # DLL is staged by the engine's Import/CMakeLists
            # POST_BUILD, not by us). Building the default
            # target (ALL_BUILD) first ensures every
            # `add_subdirectory` from Dependency/CMakeLists.txt
            # is materialized before CopyDependencies runs.
            try:
                _run_cmake([
                    "cmake", "--build", str(build_dir),
                    "--config", config,
                    "--parallel", str(parallel),
                ])
                _run_cmake([
                    "cmake", "--build", str(build_dir),
                    "--config", config,
                    "--target", "CopyDependencies",
                    "--parallel", str(parallel),
                ])
                # Stage the Dependency/Config/*.cmake wrappers next to
                # the artifacts so the root CMakeLists can find them
                # via `find_package(<dep> REQUIRED CONFIG)`.
                stage_dep_configs("Windows", config)
                results.append((config, True))
            except subprocess.CalledProcessError:
                results.append((config, False))
    else:
        # Ninja: one configure+build per config, mirroring the bat's
        # behaviour of producing a per-config build directory.
        for config in configs:
            build_dir = DEP_INT / "Windows" / config
            build_dir.mkdir(parents=True, exist_ok=True)
            section(f"Windows / {config} (configure)")
            try:
                _run_cmake([
                    "cmake", "-S", str(DEP_SRC), "-B", str(build_dir),
                    "-G", generator,
                    *([f"-DCMAKE_MAKE_PROGRAM={ninja_path}"] if ninja_path else []),
                    f"-DCMAKE_BUILD_TYPE={config}",
                    "-DTK_WINDOWS=Windows",
                    *msvc_compiler_args,
                    *_common_cmake_args(
                        platform="Windows",
                        skip_assimp=skip_assimp,
                        skip_imgui=skip_imgui,
                    ),
                ])
            except subprocess.CalledProcessError:
                results.append((config, False))
                continue
            section(f"Windows / {config} (build)")
            try:
                _run_cmake([
                    "cmake", "--build", str(build_dir),
                    "--config", config,
                    "--parallel", str(parallel),
                ])
                results.append((config, True))
            except subprocess.CalledProcessError:
                results.append((config, False))
    return results


def build_linux_or_mac(
    platform: str,
    configs: List[str],
    *,
    skip_assimp: bool,
    skip_imgui: bool,
    parallel: int,
    generator: str,
    ninja_path: Optional[str],
) -> List[Tuple[str, bool]]:
    """Build the dependency tree on Linux/macOS. Single-config only."""
    results: List[Tuple[str, bool]] = []
    for config in configs:
        build_dir = DEP_INT / platform / config
        build_dir.mkdir(parents=True, exist_ok=True)
        section(f"{platform} / {config} (configure)")
        try:
            _run_cmake([
                "cmake", "-S", str(DEP_SRC), "-B", str(build_dir),
                "-G", generator,
                *([f"-DCMAKE_MAKE_PROGRAM={ninja_path}"] if ninja_path else []),
                f"-DCMAKE_BUILD_TYPE={config}",
                *_common_cmake_args(
                    platform=platform,
                    skip_assimp=skip_assimp,
                    skip_imgui=skip_imgui,
                ),
            ])
        except subprocess.CalledProcessError:
            results.append((config, False))
            continue
        section(f"{platform} / {config} (build)")
        try:
            # No --target CopyDependencies here: that target is only defined
            # in Dependency/CMakeLists.txt for Windows. On Linux the binaries
            # already land in DEP_INT/.../<Config> via CMAKE_*_OUTPUT_DIRECTORY.
            _run_cmake([
                "cmake", "--build", str(build_dir),
                "--parallel", str(parallel),
            ])
            # Stage the Dependency/Config/*.cmake wrappers next to the
            # artifacts so the root CMakeLists can find them via
            # `find_package(<dep> REQUIRED CONFIG)`.
            stage_dep_configs(platform, config)
            results.append((config, True))
        except subprocess.CalledProcessError:
            results.append((config, False))
    return results


# --------------------------------------------------------------------------- #
# Entry point                                                                 #
# --------------------------------------------------------------------------- #

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build GDTK's vendored dependencies via CMake.",
    )
    parser.add_argument(
        "--platform", choices=["Windows", "Linux", "Mac", "auto"],
        default="auto",
        help="Target platform. Default: auto-detect from sys.platform.",
    )
    parser.add_argument(
        "--configs", nargs="+", default=DEFAULT_CONFIGS,
        help="Build configurations to produce.",
    )
    parser.add_argument(
        "--skip-submodules", action="store_true",
        help="Skip `git submodule init/update`.",
    )
    parser.add_argument(
        "--skip-assimp", action="store_true",
        help="Skip the assimp build (saves time when not building Import).",
    )
    parser.add_argument(
        "--skip-imgui", action="store_true",
        help="Skip the imgui build (saves time when not building Editor).",
    )
    parser.add_argument(
        "--clean", action="store_true",
        help="Wipe Dependency/Intermediate/<Platform>/ before configuring.",
    )
    parser.add_argument(
        "-j", "--parallel", type=int, default=detect_parallel_jobs(),
        help="Parallel build jobs (default: nproc / CPU count).",
    )
    parser.add_argument(
        "--generator",
        choices=["auto", "msvc", "ninja", "make"],
        default="auto",
        help=(
            "CMake generator. Default 'auto' picks MSVC (cl.exe) on Windows "
            "when reachable, otherwise Ninja, otherwise the platform's "
            "fallback. 'msvc' forces the Visual Studio generator, 'ninja' "
            "forces Ninja (errors out if missing), 'make' forces Unix "
            "Makefiles on non-Windows."
        ),
    )
    args = parser.parse_args()

    platform = args.platform if args.platform != "auto" else detect_platform()

    print(f"{Style.BOLD}GDTK dependency build{Style.RESET}")
    print(f"  Platform:    {platform}")
    print(f"  Configs:     {', '.join(args.configs)}")
    print(f"  Root:        {ROOT_DIR}")
    print(f"  Parallel:    {args.parallel}")
    print(f"  Skip assimp: {args.skip_assimp}")
    print(f"  Skip imgui:  {args.skip_imgui}")

    require_tool("cmake")
    require_tool("git")

    if not args.skip_submodules:
        section("Updating submodules")
        try:
            update_submodules()
        except subprocess.CalledProcessError as e:
            err(f"git submodule update failed: {e}")
            return 1
    else:
        warn("Skipping submodule update (--skip-submodules).")

    if args.clean:
        section(f"Cleaning {platform} intermediates")
        clean_platform(DEP_INT / platform)

    generator, is_multiconfig, cl_path, ninja_path = detect_generator(
        platform, prefer=args.generator,
    )
    if platform == "Windows":
        if cl_path:
            print(f"  cl.exe:      {cl_path}")
        else:
            warn("cl.exe not found on PATH; relying on CMake's toolset probe.")
    if ninja_path:
        print(f"  ninja:       {ninja_path}")
    print(f"  Generator:   {generator}"
          f"{' (multi-config)' if is_multiconfig else ''}")

    section(f"Building dependencies for {platform}")
    try:
        if platform == "Windows":
            results = build_windows(
                args.configs,
                skip_assimp=args.skip_assimp,
                skip_imgui=args.skip_imgui,
                parallel=args.parallel,
                generator=generator,
                is_multiconfig=is_multiconfig,
                cl_path=cl_path,
                ninja_path=ninja_path,
            )
        else:
            results = build_linux_or_mac(
                platform, args.configs,
                skip_assimp=args.skip_assimp,
                skip_imgui=args.skip_imgui,
                parallel=args.parallel,
                generator=generator,
                ninja_path=ninja_path,
            )
    except KeyboardInterrupt:
        err("Interrupted.")
        return 130

    section("Summary")
    failed = 0
    for config, success in results:
        mark = f"{Style.GREEN}OK  {Style.RESET}" if success else f"{Style.RED}FAIL{Style.RESET}"
        print(f"  [{mark}] {platform} / {config}")
        if not success:
            failed += 1

    if failed:
        err(f"{failed} configuration(s) failed.")
        return 1
    ok("All configurations built successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
