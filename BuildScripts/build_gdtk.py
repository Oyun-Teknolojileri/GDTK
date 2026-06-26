#!/usr/bin/env python3
"""
GDTK engine + tools build script.

Cross-platform replacement for the old hand-rolled .bat / .sh build
flow. Configures and builds the root GDTK CMake project, which produces
the engine shared library (ToolKit), the Workspace helper library, and
the Editor / Launcher / Import / Packer executables.

Vendored dependencies in Dependency/ are expected to be built ahead of
time via BuildScripts/build_dependencies.py -- the root CMakeLists
imports them through IMPORTED targets and FATAL_ERRORs at configure
time if they are missing.

Build artifacts land in:
  Bin/                              Final binaries (Editor, Launcher,
                                    libToolKitd.so, libWorkspace.a, ...)
  Intermediate/<Platform>/<Config>/ CMake state + per-module .o files

Per-platform generator selection matches build_dependencies.py so the
two scripts can be run side by side without surprise: on Windows the
script prefers cl.exe (MSVC) when reachable, then Ninja, then the
Visual Studio generator; on Linux/macOS it prefers Ninja, then
Unix Makefiles.
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
DEP_INT = ROOT_DIR / "Dependency" / "Intermediate"

# Default build configurations. RelWithDebInfo and MinSizeRel are
# included for completeness but the main development loop is Debug.
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
# version number ("18" for VS 2026) in late 2025, while CMake keeps
# using the historical "Visual Studio <major> <year>" naming. Keep
# this table in sync with whatever is current whenever a new VS
# release lands.
_VS_GENERATOR_BY_INSTALL_DIR: dict[str, str] = {
    "2022": "Visual Studio 17 2022",
    "18":   "Visual Studio 18 2026",
    "19":   "Visual Studio 19 2027",
    "2019": "Visual Studio 16 2019",
    "2017": "Visual Studio 15 2017",
}

_VS_GENERATOR_NEWEST_FIRST: tuple[str, ...] = (
    "Visual Studio 18 2026",
    "Visual Studio 17 2022",
    "Visual Studio 16 2019",
)


def _vs_generator_for_install_path(install_path: str) -> Optional[str]:
    """Pick the CMake generator name that matches a given VS install path.

    The parent of the edition folder is what we key off. For
    `C:\\Program Files\\Microsoft Visual Studio\\18\\Community` the key
    is `"18"`, and the mapping returns `"Visual Studio 18 2026"`.
    """
    parent = Path(install_path.rstrip("\\/")).parent.name
    return _VS_GENERATOR_BY_INSTALL_DIR.get(parent)


def _default_vs_generator() -> str:
    """Newest installed-or-known VS generator we can name without probing."""
    return _VS_GENERATOR_NEWEST_FIRST[0]


def detect_compiler(platform: str) -> Tuple[Optional[str], Optional[str]]:
    """Return (cl_path, vs_install_path) if an MSVC toolset is reachable."""
    direct = which("cl")
    if direct:
        return direct, None

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
                candidates.sort(reverse=True)
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

    Selection rules mirror build_dependencies.py so the two scripts
    behave consistently when run in sequence.

    Windows:
      - cl.exe reachable: pick the VS generator that matches the
        installed VS release (VS 2022 -> "Visual Studio 17 2022",
        VS 2026 -> "Visual Studio 18 2026", ...).
      - cl.exe not on PATH but Ninja exists: pick Ninja.
      - else: fall back to the newest-known VS generator (CMake will
        surface a helpful "toolset not found" error).
    """
    cl_path: Optional[str] = None
    vs_install_path: Optional[str] = None
    if platform == "Windows":
        cl_path, vs_install_path = detect_compiler(platform)
    ninja_path = which("ninja")

    def _vs_generator_name() -> str:
        if vs_install_path:
            resolved = _vs_generator_for_install_path(vs_install_path)
            if resolved:
                return resolved
        return _default_vs_generator()

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

    if platform == "Windows":
        # We pick the Visual Studio generator (MSBuild) on Windows
        # because the MSVC 18 (Visual Studio 2026) C++ STL is built
        # against Clang 20+ and is only invokable through the
        # MSBuild-driven compile pipeline. Ninja + cl.exe (even with
        # the MSVC toolset pinned) skips that pipeline and trips
        # STL1000 ("Unexpected compiler version, expected Clang 20
        # or newer"). Pinning cl.exe still works for the linker and
        # the dependency-tool side of the build.
        if cl_path:
            return _vs_generator_name(), True, cl_path, ninja_path
        if ninja_path:
            return "Ninja", False, cl_path, ninja_path
        return _vs_generator_name(), True, cl_path, ninja_path

    if ninja_path:
        return "Ninja", False, cl_path, ninja_path
    return "Unix Makefiles", False, cl_path, ninja_path


def detect_parallel_jobs() -> int:
    return os.cpu_count() or 1


# --------------------------------------------------------------------------- #
# Dependency gate                                                              #
# --------------------------------------------------------------------------- #

def check_dependencies(platform: str, config: str) -> Tuple[bool, Path]:
    """Check whether Dependency/Intermediate/<Plat>/<Cfg> is populated.

    Returns (present, deps_dir). When the directory is missing or the
    SDL2 spot-check fails, present is False and the caller is expected
    to invoke build_dependencies.py to populate it. A True result
    with the SDL2 spot-check still missing is a soft warning -- CMake
    will surface a clearer error if a real dep is actually missing.

    The root CMakeLists imports ToolKit's runtime deps from there and
    FATAL_ERRORs if any of them are missing. Catching the absence
    here produces a friendlier message that points the user at
    build_dependencies.py, and lets us auto-invoke it instead of
    failing the engine build outright.
    """
    deps_dir = DEP_INT / platform / config
    if not deps_dir.exists():
        return False, deps_dir

    # Cheap spot-check: every config directory should at least contain
    # the SDL2 .so / .dll we linked against. The list matches what
    # build_dependencies.py produces today; if a new dep is added
    # there, the worst case is that this gate is a no-op, not a false
    # negative -- CMake will still surface a clear error.
    if platform == "Windows":
        candidates = ["SDL2d.dll", "SDL2.dll"]
    else:
        candidates = ["libSDL2-2.0.so", "libSDL2-2.0d.so"]
    if not any((deps_dir / c).exists() for c in candidates):
        warn(f"{deps_dir} exists but no SDL2 runtime artifact was found in it.")
        warn("build_dependencies.py may have been interrupted -- re-run it.")
        return False, deps_dir
    return True, deps_dir


# --------------------------------------------------------------------------- #
# CMake                                                                       #
# --------------------------------------------------------------------------- #

def _run_cmake(args: List[str]) -> None:
    """Run a cmake invocation, printing the command first."""
    cmd_str = " ".join(str(a) for a in args)
    info(f"$ {cmd_str}")
    subprocess.run(args, check=True)


def build_project(
    configs: List[str],
    *,
    platform: str,
    parallel: int,
    generator: str,
    is_multiconfig: bool,
    cl_path: Optional[str],
    targets: List[str],
) -> List[Tuple[str, bool]]:
    """Configure and build the root GDTK CMake project. Returns per-config (ok?)."""
    results: List[Tuple[str, bool]] = []

    # Pin C/C++ compilers to the located cl.exe when the MSVC generator
    # is in use. The single-config generators (Ninja on Linux,
    # Unix Makefiles) inherit the toolchain from PATH, so this only
    # matters for the Visual Studio generator.
    compiler_args: List[str] = []
    if cl_path and generator.startswith("Visual Studio"):
        compiler_args = [
            f"-DCMAKE_C_COMPILER={cl_path}",
            f"-DCMAKE_CXX_COMPILER={cl_path}",
        ]

    # The intermediate dir matches what the root CMakeLists.txt lays
    # out: Intermediate/<Platform>/<Config>/. We feed it as the build
    # dir for cmake, so re-runs are incremental and not a full
    # re-configure.
    target_arg: List[str] = ["--target", *targets] if targets else []

    for config in configs:
        int_dir = ROOT_DIR / "Intermediate" / platform / config
        int_dir.mkdir(parents=True, exist_ok=True)

        section(f"{platform} / {config} (configure)")
        try:
            configure_args = [
                "cmake", "-S", str(ROOT_DIR), "-B", str(int_dir),
                "-G", generator,
            ]
            if is_multiconfig:
                # Multi-config (VS): --config selects the active
                # configuration at build time. -A x64 picks the
                # 64-bit toolchain on Windows.
                configure_args += ["-A", "x64"]
            else:
                # Single-config (Ninja / Unix Makefiles): pick the
                # build type up front.
                configure_args += [f"-DCMAKE_BUILD_TYPE={config}"]
            configure_args += [
                *compiler_args,
            ]
            _run_cmake(configure_args)
        except subprocess.CalledProcessError:
            results.append((config, False))
            continue

        section(f"{platform} / {config} (build)")
        try:
            build_args = [
                "cmake", "--build", str(int_dir),
                "--config", config,
                "--parallel", str(parallel),
                *target_arg,
            ]
            _run_cmake(build_args)
            results.append((config, True))
        except subprocess.CalledProcessError:
            results.append((config, False))

    return results


def clean_platform(platform: str) -> None:
    target = ROOT_DIR / "Intermediate" / platform
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
        description="Build the GDTK engine + tools via CMake.",
    )
    parser.add_argument(
        "--platform", choices=["Windows", "Linux", "Mac", "auto"],
        default="auto",
        help="Target platform. Default: auto-detect from sys.platform.",
    )
    parser.add_argument(
        "--configs", nargs="+", default=DEFAULT_CONFIGS,
        help="Build configurations to produce (default: Debug Release).",
    )
    parser.add_argument(
        "--target", dest="targets", action="append", default=[],
        help=(
            "CMake target to build. May be passed multiple times. "
            "Default: build everything (no --target means \"all\")."
        ),
    )
    parser.add_argument(
        "--no-deps-check", action="store_true",
        help=(
            "Skip the Dependency/Intermediate/<Plat>/<Cfg> gate. By "
            "default, the script checks the dependency tree and "
            "auto-invokes build_dependencies.py for any missing "
            "configurations; pass this flag to opt out (e.g. when you "
            "know you are about to run a configure-only pass and do "
            "not want the script to spawn a second build)."
        ),
    )
    parser.add_argument(
        "--clean", action="store_true",
        help="Wipe Intermediate/<Platform>/ before configuring.",
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

    print(f"{Style.BOLD}GDTK engine build{Style.RESET}")
    print(f"  Platform:    {platform}")
    print(f"  Configs:     {', '.join(args.configs)}")
    print(f"  Root:        {ROOT_DIR}")
    print(f"  Parallel:    {args.parallel}")
    if args.targets:
        print(f"  Targets:     {', '.join(args.targets)}")
    else:
        print(f"  Targets:     (all)")

    require_tool("cmake")

    if not args.no_deps_check:
        section("Checking dependencies")
        missing_configs: List[str] = []
        for config in args.configs:
            present, _ = check_dependencies(platform, config)
            if not present:
                missing_configs.append(config)
        if missing_configs:
            # Build the dependency tree for the missing configurations
            # before touching the engine. The dependency script shares
            # the same --platform / --configs / --generator flag
            # vocabulary, so we forward them verbatim -- minus the
            # engine-specific flags (--target, --clean, ...) that
            # build_dependencies.py does not understand.
            warn(
                "Dependency tree is missing for: "
                f"{', '.join(f'{platform}/{c}' for c in missing_configs)}"
            )
            warn("Invoking build_dependencies.py to populate it before continuing.")
            try:
                subprocess.run(
                    [
                        sys.executable,
                        str(SCRIPT_DIR / "build_dependencies.py"),
                        "--platform", platform,
                        "--configs", *missing_configs,
                        "--parallel", str(args.parallel),
                    ],
                    check=True,
                )
            except subprocess.CalledProcessError as e:
                err(f"build_dependencies.py failed with exit code {e.returncode}.")
                err("The dependency tree is required to build the engine -- aborting.")
                return 1
            ok("Dependency tree populated.")
        else:
            ok("Dependency tree present -- skipping build_dependencies.py.")

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

    section(f"Building {platform} engine + tools")
    try:
        results = build_project(
            args.configs,
            platform=platform,
            parallel=args.parallel,
            generator=generator,
            is_multiconfig=is_multiconfig,
            cl_path=cl_path,
            targets=args.targets,
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
