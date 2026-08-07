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
  Bin<Config>/                      Final binaries + shared dependencies
                                    (Editor, Launcher, Import, Packer,
                                    libToolKitd.so, libWorkspace.a,
                                    libSDL2-*.so, libimguid.so, ...)
  Intermediate/<Platform>/<Config>/ CMake state + per-module .o files

Per-platform generator selection matches build_dependencies.py so the
two scripts can run side by side without surprise: on Windows the
script prefers cl.exe (MSVC) when reachable, then Ninja, then the
Visual Studio generator; on Linux/macOS it prefers Ninja, then
Unix Makefiles.
"""

from __future__ import annotations

import argparse
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


# Shared helpers -- terminal colors, toolchain probing, CMake invoker,
# clean helper. Anything that build_dependencies.py also needs lives in
# _common.py so a VS release bump only requires touching one place.
sys.path.insert(0, str(SCRIPT_DIR))
from _common import (  # noqa: E402  -- intentional import after sys.path tweak
    Style, info, ok, warn, err, section,
    detect_platform, detect_generator, detect_parallel_jobs,
    _run_cmake, clean_platform,
    # Pre-flight checks shared with build_dependencies.py -- see _common.py.
    tool_preflight, system_package_preflight, vulkan_sdk_check,
)


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

def build_project(
    configs: List[str],
    *,
    platform: str,
    parallel: int,
    generator: str,
    is_multiconfig: bool,
    cl_path: Optional[str],
    ninja_path: Optional[str],
    targets: List[str],
    vulkan: bool,
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
                "cmake", "-Wno-deprecated", "-S", str(ROOT_DIR), "-B", str(int_dir),
                "-G", generator,
                *([f"-DCMAKE_MAKE_PROGRAM={ninja_path}"] if ninja_path and generator == "Ninja" else []),
            ]
            if is_multiconfig:
                # Multi-config (VS): --config selects the active
                # configuration at build time. -A x64 picks the
                # 64-bit toolchain on Windows.
                configure_args += ["-A", "x64"]
            # Always set CMAKE_BUILD_TYPE so the CMakeLists dependency
            # lookup (TK_DEPS_DIR) and the per-dep config wrappers can
            # locate per-config artifacts even when the generator itself
            # is multi-config (Visual Studio leaves it empty otherwise).
            configure_args += [f"-DCMAKE_BUILD_TYPE={config}"]
            configure_args += [
                *compiler_args,
                f"-DTK_VULKAN={'ON' if vulkan else 'OFF'}",
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

            # Install: stage shared dependencies into Bin<Config>/ so the
            # runtime directory is self-contained (RPATH only needs $ORIGIN).
            section(f"{platform} / {config} (install)")
            try:
                install_args = [
                    "cmake", "--install", str(int_dir),
                    "--config", config,
                    "--prefix", str(ROOT_DIR),
                ]
                _run_cmake(install_args)
            except subprocess.CalledProcessError:
                warn(f"Install step failed for {platform}/{config} -- "
                     "Bin<Config>/ may be missing shared dependencies.")
                # Don't fail the build; the binaries are still usable with
                # LD_LIBRARY_PATH / %PATH% pointing at TK_DEPS_DIR.
            results.append((config, True))
        except subprocess.CalledProcessError:
            results.append((config, False))

    return results


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
        "--vulkan", action="store_true",
        help=(
            "Enable the Vulkan render backend (TK_VULKAN=ON). Requires "
            "vulkan-loader-devel + libshaderc-devel + mesa-vulkan-drivers "
            "(Fedora) or libvulkan-dev + libshaderc-dev (Debian) installed."
        ),
    )
    parser.add_argument(
        "--install-deps", action="store_true",
        help=(
            "On Linux, auto-install the system dev packages GDTK needs "
            "(build essentials + SDL2 X11 + -- when --vulkan is set -- "
            "Vulkan loader/shaderc/VMA) via the distro's package manager "
            "+ sudo, if any are missing. By default the script just lists "
            "what's missing and aborts without touching the system."
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

    # Build-tool pre-flight (cmake/git/ninja/cl.exe) -- hard-fails on a
    # missing hard requirement with an install hint. Python is assumed.
    tool_preflight(platform)

    # Linux system package pre-flight (build essentials + X11 [+ Vulkan]).
    # Runs BEFORE the vendored-dep gate because if pkg-config / libX11-devel
    # is missing, the SDL2/assimp configure inside build_dependencies.py is
    # the one that will trip on it, and we'd rather give the user a clear
    # per-distro install command up front than a cryptic "Could NOT find
    # PkgConfig / ZLIB" FATAL_ERROR buried inside the dep build's cmake output.
    preflight_rc = system_package_preflight(
        platform,
        vulkan=args.vulkan,
        install_deps=args.install_deps,
    )
    if preflight_rc != 0:
        return preflight_rc

    # Windows Vulkan SDK check -- only when --vulkan, because the default
    # GL backend compiles without it. Linux Vulkan packages are handled
    # above by system_package_preflight.
    if args.vulkan:
        vulkan_rc = vulkan_sdk_check(platform)
        if vulkan_rc != 0:
            return vulkan_rc

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
                        "--generator", args.generator,
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
        clean_platform(ROOT_DIR / "Intermediate" / platform)

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

    if args.vulkan:
        print(f"  Vulkan:      ON (TK_VULKAN)")
    else:
        print(f"  Vulkan:      OFF")
    if args.install_deps:
        print(f"  Install-deps: ON (Linux system packages will be auto-installed via sudo if missing)")

    section(f"Building {platform} engine + tools")
    try:
        results = build_project(
            args.configs,
            platform=platform,
            parallel=args.parallel,
            generator=generator,
            is_multiconfig=is_multiconfig,
            cl_path=cl_path,
            ninja_path=ninja_path,
            targets=args.targets,
            vulkan=args.vulkan,
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
