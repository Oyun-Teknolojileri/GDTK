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
import os
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


# --------------------------------------------------------------------------- #
# Terminal helpers                                                            #
# --------------------------------------------------------------------------- #

def _isatty() -> bool:
    return sys.stdout.isatty() and sys.stderr.isatty()


class Style:
    """ANSI color escapes -- disabled when output is not a TTY."""

    _enabled = _isatty()

    RESET = "\033[0m" if _enabled else ""
    BOLD = "\033[1m" if _enabled else ""
    DIM = "\033[2m" if _enabled else ""
    RED = "\033[31m" if _enabled else ""
    GREEN = "\033[32m" if _enabled else ""
    YELLOW = "\033[33m" if _enabled else ""
    BLUE = "\033[34m" if _enabled else ""
    CYAN = "\033[36m" if _enabled else ""


def info(msg: str) -> None:
    print(f"{Style.CYAN}>>>{Style.RESET} {msg}")


def ok(msg: str) -> None:
    print(f"{Style.GREEN}+++{Style.RESET} {msg}")


def warn(msg: str) -> None:
    print(f"{Style.YELLOW}!!!{Style.RESET} {msg}", file=sys.stderr)


def err(msg: str) -> None:
    print(f"{Style.RED}xxx{Style.RESET} {msg}", file=sys.stderr)


def section(title: str) -> None:
    print()
    print(f"{Style.BOLD}{Style.BLUE}== {title} =={Style.RESET}")
    print()


# --------------------------------------------------------------------------- #
# Toolchain detection                                                         #
# --------------------------------------------------------------------------- #

def which(cmd: str) -> Optional[str]:
    """shutil.which wrapper for clarity at call sites."""
    return shutil.which(cmd)


def require_tool(name: str) -> str:
    path = which(name)
    if not path:
        err(f"Required tool '{name}' not found in PATH.")
        err("Install it (or activate the relevant environment) and retry.")
        sys.exit(2)
    return path


def detect_platform() -> str:
    if sys.platform == "win32":
        return "Windows"
    if sys.platform.startswith("linux"):
        return "Linux"
    if sys.platform == "darwin":
        return "Mac"
    err(f"Unsupported platform: {sys.platform}")
    sys.exit(2)


def detect_generator(platform: str) -> Tuple[str, bool]:
    """Return (generator, is_multi_config).

    Ninja is preferred everywhere because it is fast and consistent. On
    Windows without Ninja we fall back to a multi-config Visual Studio
    generator. On Linux/macOS without Ninja we fall back to Unix Makefiles.
    """
    if which("ninja"):
        return "Ninja", False

    if platform == "Windows":
        # Primary Windows target: VS 2022 (vc143 / v145 toolset).
        return "Visual Studio 17 2022", True

    # Linux / macOS fallback.
    return "Unix Makefiles", False


def detect_parallel_jobs() -> int:
    return os.cpu_count() or 1


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


def _run_cmake(args: List[str]) -> None:
    """Run a cmake invocation, printing the command first."""
    cmd_str = " ".join(str(a) for a in args)
    info(f"$ {cmd_str}")
    subprocess.run(args, check=True)


def build_windows(
    configs: List[str],
    *,
    skip_assimp: bool,
    skip_imgui: bool,
    parallel: int,
    generator: str,
    is_multiconfig: bool,
) -> List[Tuple[str, bool]]:
    """Build the dependency tree on Windows. Returns per-config (ok?)."""
    results: List[Tuple[str, bool]] = []

    if is_multiconfig:
        # One configure, three builds. Output goes to Intermediate/Windows/.
        build_dir = DEP_INT / "Windows"
        build_dir.mkdir(parents=True, exist_ok=True)
        configure_args = [
            "cmake", "-S", str(DEP_SRC), "-B", str(build_dir),
            "-G", generator, "-A", "x64",
            "-DTK_WINDOWS=Windows",
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
            build_args = [
                "cmake", "--build", str(build_dir),
                "--config", config,
                "--target", "CopyDependencies",
                "--parallel", str(parallel),
            ]
            try:
                _run_cmake(build_args)
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
                    f"-DCMAKE_BUILD_TYPE={config}",
                    "-DTK_WINDOWS=Windows",
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
            results.append((config, True))
        except subprocess.CalledProcessError:
            results.append((config, False))
    return results


def clean_platform(platform: str) -> None:
    target = DEP_INT / platform
    if target.exists():
        info(f"Removing {target}")
        shutil.rmtree(target)
    else:
        info(f"Nothing to clean at {target}")


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
        clean_platform(platform)

    generator, is_multiconfig = detect_generator(platform)
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
            )
        else:
            results = build_linux_or_mac(
                platform, args.configs,
                skip_assimp=args.skip_assimp,
                skip_imgui=args.skip_imgui,
                parallel=args.parallel,
                generator=generator,
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
