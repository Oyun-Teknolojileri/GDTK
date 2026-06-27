#!/usr/bin/env python3
"""
GDTK build scripts -- shared helpers.

Internal module imported by build_dependencies.py and build_gdtk.py.
Anything that both scripts need (terminal colors, toolchain probing,
the CMake invoker, the per-platform clean helper) lives here so the
two entry points stay focused on their own job (dep build vs.
engine build) and so a Visual Studio release that adds a new
install-path suffix only has to be wired up in one place.

Not intended to be run directly.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Tuple


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

    # Linux / macOS.
    if ninja_path:
        return "Ninja", False, cl_path, ninja_path
    return "Unix Makefiles", False, cl_path, ninja_path


def detect_parallel_jobs() -> int:
    return os.cpu_count() or 1


# --------------------------------------------------------------------------- #
# CMake invoker                                                               #
# --------------------------------------------------------------------------- #

def _run_cmake(args: List[str]) -> None:
    """Run a cmake invocation, printing the command first."""
    cmd_str = " ".join(str(a) for a in args)
    info(f"$ {cmd_str}")
    subprocess.run(args, check=True)


# --------------------------------------------------------------------------- #
# Clean helper                                                                #
# --------------------------------------------------------------------------- #

def clean_platform(platform_root: Path) -> None:
    """Remove a per-platform directory tree if it exists.

    Called by both scripts with their respective roots:
      build_dependencies.py: Dependency/Intermediate/<Platform>
      build_gdtk.py:        Intermediate/<Platform>

    Parametrising on the path keeps this helper reusable without
    each script having to know the other's layout.
    """
    if platform_root.exists():
        info(f"Removing {platform_root}")
        shutil.rmtree(platform_root)
    else:
        info(f"Nothing to clean at {platform_root}")