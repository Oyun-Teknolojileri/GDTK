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


# Map of Visual Studio install-path suffix -> CMake generator name.
# Microsoft switched the on-disk folder from a year ("2022") to a major
# version number ("18" for VS 2026) in late 2025, while CMake keeps using
# the historical "Visual Studio <major> <year>" naming. Keep this table
# in sync with whatever is current whenever a new VS release lands.
_VS_GENERATOR_BY_INSTALL_DIR: dict[str, str] = {
    "2022": "Visual Studio 17 2022",
    "18":   "Visual Studio 18 2026",
    "19":   "Visual Studio 19 2027",
    # Fallback for older installs that still live under a year folder.
    "2019": "Visual Studio 16 2019",
    "2017": "Visual Studio 15 2017",
}

# Newest first -- used when we have to pick a generator without an
# installation path (e.g. cl.exe is already on PATH from a vcvars shell).
_VS_GENERATOR_NEWEST_FIRST: tuple[str, ...] = (
    "Visual Studio 18 2026",
    "Visual Studio 17 2022",
    "Visual Studio 16 2019",
)


def _vs_generator_for_install_path(install_path: str) -> Optional[str]:
    """Pick the CMake generator name that matches a given VS install path.

    The parent of the edition folder is what we key off. For
    `C:\\Program Files\\Microsoft Visual Studio\\18\\Community` the key
    is `"18"`, and the mapping returns `"Visual Studio 18 2026"`. For
    the pre-2026 layout (`...\\2022\\Community`) the key is `"2022"`.
    """
    parent = Path(install_path.rstrip("\\/")).parent.name
    return _VS_GENERATOR_BY_INSTALL_DIR.get(parent)


def _default_vs_generator() -> str:
    """Newest installed-or-known VS generator we can name without probing."""
    return _VS_GENERATOR_NEWEST_FIRST[0]


def detect_compiler(platform: str) -> Tuple[Optional[str], Optional[str]]:
    """Return (cl_path, vs_install_path) if an MSVC toolset is reachable.

    We probe a few different ways because cl.exe is rarely on PATH
    out of the box -- typically it lives somewhere under
    `C:\\Program Files\\Microsoft Visual Studio\\<ver-or-year>\\<edition>\\VC\\Tools\\MSVC\\<ver>\\bin\\Hostx64\\x64\\`
    and is only added to PATH inside a `vcvars64.bat` shell.

    `vs_install_path` is returned alongside cl_path so the caller can
    resolve the correct CMake generator name for the installed VS
    release (the install-folder suffix no longer matches the year).
    """
    # 1. Already on PATH (vcvars was sourced, or VS is in PATH).
    direct = which("cl")
    if direct:
        # We don't know which install path it came from -- use vcvars.
        # The newest-known generator name is a safe bet; CMake will
        # fall through to whichever VS is actually registered.
        return direct, None

    # 2. Probe the VS install root via vswhere.exe (ships with VS 2017+).
    vswhere = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
    if platform == "Windows" and vswhere.exists():
        try:
            out = subprocess.run(
                [
                    str(vswhere),
                    "-latest",
                    "-products", "*",
                    "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                    "-property", "installationPath",
                ],
                capture_output=True,
                text=True,
                check=True,
            )
            install_path = out.stdout.strip()
            if install_path:
                candidates = list(
                    (Path(install_path) / "VC" / "Tools" / "MSVC").glob("*")
                )
                candidates.sort(reverse=True)  # newest toolset first
                for toolset in candidates:
                    cl = toolset / "bin" / "Hostx64" / "x64" / "cl.exe"
                    if cl.exists():
                        return str(cl), install_path
        except (subprocess.CalledProcessError, OSError):
            pass

    return None, None


def detect_generator(
    platform: str,
    prefer: str = "auto",
) -> Tuple[str, bool, Optional[str], Optional[str]]:
    """Return (generator, is_multi_config, cl_path, ninja_path).

    Selection rules (Windows):
      - If --generator msvc OR (auto AND cl.exe is reachable):
          pick the Visual Studio generator that matches the installed
          VS release (VS 2022 -> "Visual Studio 17 2022", VS 2026 ->
          "Visual Studio 18 2026", ...).
      - Else if Ninja exists (auto or explicit): pick Ninja.
      - Else fall back to the newest-known VS generator anyway; CMake
        will surface the actual missing toolset error so the user can
        fix the environment.

    On Linux/macOS the same generator choices apply but cl.exe is
    irrelevant -- we use Ninja if available, else Unix Makefiles.

    `cl_path` and `ninja_path` are returned so the caller can wire
    `-DCMAKE_C_COMPILER=cl` / `-DCMAKE_CXX_COMPILER=cl` when needed.
    """
    cl_path: Optional[str] = None
    vs_install_path: Optional[str] = None
    if platform == "Windows":
        cl_path, vs_install_path = detect_compiler(platform)
    ninja_path = which("ninja")

    # Decide which VS generator name to use when we have to.
    def _vs_generator_name() -> str:
        if vs_install_path:
            resolved = _vs_generator_for_install_path(vs_install_path)
            if resolved:
                return resolved
        return _default_vs_generator()

    # Manual override.
    if prefer == "msvc":
        if platform == "Windows":
            return _vs_generator_name(), True, cl_path, ninja_path
        warn("--generator msvc is a no-op on non-Windows platforms; using default.")
    elif prefer == "ninja":
        if ninja_path:
            return "Ninja", False, cl_path, ninja_path
        err("--generator ninja requested but `ninja` is not on PATH.")
        sys.exit(2)
    elif prefer == "make":
        if platform != "Windows":
            return "Unix Makefiles", False, cl_path, ninja_path
        warn("--generator make is a no-op on Windows; using default.")

    # Auto-selection.
    if platform == "Windows":
        # On Windows we prefer cl.exe / MSVC when it is reachable -- that
        # is the platform's primary toolchain and matches the rest of the
        # GDTK build. Ninja is the second choice.
        if cl_path:
            return _vs_generator_name(), True, cl_path, ninja_path
        if ninja_path:
            return "Ninja", False, cl_path, ninja_path
        # Last resort: still pick a VS generator so CMake produces a
        # helpful error that names the missing toolset, rather than a
        # generic "no generator" failure.
        return _vs_generator_name(), True, cl_path, ninja_path

    # Linux / macOS.
    if ninja_path:
        return "Ninja", False, cl_path, ninja_path
    return "Unix Makefiles", False, cl_path, ninja_path


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
    cl_path: Optional[str],
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
        clean_platform(platform)

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
