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
        if platform == "Windows":
            # MSVC 18+ (VS 2026+) ships an STL built against Clang 20+
            # intrinsics (__builtin_verbose_trap, ...). Ninja invokes
            # cl.exe in classic MSVC mode which lacks those builtins,
            # producing 20+ STL errors. The MSBuild pipeline (Visual
            # Studio generator) routes through Clang's frontend and
            # works correctly. Force the VS generator instead.
            warn("--generator ninja on Windows with VS 2026+ is not supported.")
            warn("The MSVC STL requires the MSBuild pipeline (Clang frontend).")
            warn("Falling back to the Visual Studio generator.")
            return _vs_generator_name(), True, cl_path, ninja_path
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


# --------------------------------------------------------------------------- #
# Linux system package management                                             #
# --------------------------------------------------------------------------- #
#
# Why this lives here: GDTK's Linux build has a small number of hard
# system-level dependencies that the vendored dep tree (SDL2, glm,
# assimp, ...) cannot satisfy on its own. The two that matter today:
#
#   1. SDL2's X11 video backend dev packages -- always required now
#      because Dependency/CMakeLists.txt pins SDL_X11=ON, SDL_WAYLAND=OFF
#      (we want Win32-comparable windowing on Linux). Without these,
#      SDL2's CheckX11() macro silently disables the X11 driver and
#      the editor falls back to the dummy video driver -- a silent
#      window-not-shown failure mode.
#
#   2. Vulkan render backend dev packages -- only required when the
#      user passes --vulkan. Without them, the engine still builds but
#      `find_package(Vulkan)` / `find_package(shaderc)` fail.
#
# We support three distro families because that's where GDTK actually
# gets built:
#   * fedora  -- Fedora, RHEL, Rocky, Alma, Nobara, ...
#   * debian  -- Debian, Ubuntu, Mint, Pop, Elementary, Zorin, ...
#   * arch    -- Arch, Manjaro, Endeavour, Garuda, ...
#
# Anything else gets treated as "unknown" and we surface a manual
# install hint instead of guessing at the package manager.

LINUX_DISTRO_FEDORA = "fedora"
LINUX_DISTRO_DEBIAN = "debian"
LINUX_DISTRO_ARCH   = "arch"
LINUX_DISTROS = (LINUX_DISTRO_FEDORA, LINUX_DISTRO_DEBIAN, LINUX_DISTRO_ARCH)

# Display names for the three supported families, used in log output.
LINUX_DISTRO_DISPLAY_NAME: dict[str, str] = {
    LINUX_DISTRO_FEDORA: "Fedora / RHEL family (dnf)",
    LINUX_DISTRO_DEBIAN: "Debian / Ubuntu family (apt)",
    LINUX_DISTRO_ARCH:   "Arch family (pacman)",
}

# X11 dev packages -- always required on Linux (see header comment).
LINUX_X11_PACKAGES: dict[str, tuple[str, ...]] = {
    LINUX_DISTRO_FEDORA: (
        "libX11-devel",
        "libXext-devel",
        "libXrandr-devel",
        "libXcursor-devel",
        "libXi-devel",
        "libXinerama-devel",
        "libxkbcommon-devel",
    ),
    LINUX_DISTRO_DEBIAN: (
        "libx11-dev",
        "libxext-dev",
        "libxrandr-dev",
        "libxcursor-dev",
        "libxi-dev",
        "libxinerama-dev",
        "libxkbcommon-dev",
    ),
    LINUX_DISTRO_ARCH: (
        "libx11",
        "libxext",
        "libxrandr",
        "libxcursor",
        "libxi",
        "libxinerama",
        "libxkbcommon",
    ),
}

# Vulkan render backend dev packages -- only required when --vulkan.
# Mirrors the table documented in CMakeLists.txt and README so all three
# places stay in sync (single source of truth is *this* dict).
LINUX_VULKAN_PACKAGES: dict[str, tuple[str, ...]] = {
    LINUX_DISTRO_FEDORA: (
        "vulkan-loader-devel",
        "libshaderc-devel",
        "VulkanMemoryAllocator-devel",
        "mesa-vulkan-drivers",
    ),
    LINUX_DISTRO_DEBIAN: (
        "libvulkan-dev",
        "libshaderc-dev",
        "libvulkan-memory-allocator-dev",
        "mesa-vulkan-drivers",
    ),
    LINUX_DISTRO_ARCH: (
        "vulkan-headers",
        "vulkan-icd-loader",
        "shaderc",
        "vulkan-memory-allocator",
        "mesa-vulkan-drivers",
    ),
}

# Build-essential system packages -- always required on Linux.
#
# These are the *system* tools/headers the vendored dep tree needs but
# cannot fetch for itself, and that are NOT covered by the X11 list above:
#
#   * pkg-config / pkgconf -- CMake's find_package backend. Without it,
#     assimp's `find_package(ZLIB)` and SDL2's X11 probe both fail with a
#     cryptic "Could NOT find PkgConfig" deep inside the dep configure
#     (this is exactly what aborted the Ubuntu build before this gate
#     existed).
#   * zlib dev headers -- assimp builds with ASSIMP_BUILD_ZLIB=OFF, so it
#     has to find the system zlib; the dev headers are what's missing on a
#     fresh install.
#   * the C/C++ toolchain + ninja -- gcc/g++ (or the distro's "build
#     essentials" meta-package) plus the ninja build driver. CMake and git
#     are checked separately as tools (see tool_preflight); they live here
#     too on Linux so --install-deps can provision the whole set in one go.
#
# Package names are the real, queryable names per distro (verified against
# dpkg-query / rpm -q): Fedora's pkg-config provider is `pkgconf-pkg-config`
# (NOT `pkgconfig`, which is only a virtual rpm provides), and its ninja
# package is `ninja-build` (NOT `ninja`).
LINUX_BUILD_PACKAGES: dict[str, tuple[str, ...]] = {
    LINUX_DISTRO_DEBIAN: ("pkg-config", "zlib1g-dev", "build-essential", "ninja-build", "libgl1-mesa-dev"),
    LINUX_DISTRO_FEDORA: ("pkgconf-pkg-config", "zlib-devel", "gcc-c++", "ninja-build", "mesa-libGL-devel"),
    LINUX_DISTRO_ARCH:   ("pkgconf", "zlib", "gcc", "make", "ninja", "libgl"),
}


def _read_os_release() -> dict[str, str]:
    """Parse /etc/os-release into a flat dict.

    Returns an empty dict if /etc/os-release is missing or unreadable --
    the caller is expected to fall back to "unknown distro" in that case
    rather than crash, because on a stripped CI image the file may be
    gone and we still want to print a useful error.
    """
    result: dict[str, str] = {}
    try:
        with open("/etc/os-release", "r", encoding="utf-8") as f:
            for raw in f:
                line = raw.strip()
                if not line or line.startswith("#"):
                    continue
                if "=" not in line:
                    continue
                key, _, value = line.partition("=")
                result[key] = value.strip().strip('"').strip("'")
    except OSError:
        pass
    return result


def detect_linux_distro() -> str:
    """Best-effort identification of one of the supported Linux families.

    Reads /etc/os-release and matches on ID / ID_LIKE so derivatives
    (Ubuntu -> debian family, Manjaro -> arch family, ...) resolve
    automatically. Returns 'unknown' for anything we don't know how
    to provision -- the caller then surfaces a manual-install hint
    instead of trying to guess at the package manager.
    """
    info = _read_os_release()
    distro_id = info.get("ID", "").lower()
    like = info.get("ID_LIKE", "").lower()
    candidates = [distro_id]
    candidates.extend(s.strip() for s in like.split() if s.strip())

    for c in candidates:
        # Fedora / RHEL family. `fedora-asahi-remix`, `fedora-remix`, ...
        # all start with "fedora" so a prefix match is enough.
        if c.startswith("fedora") or c in ("rhel", "centos", "rocky", "almalinux", "nobara"):
            return LINUX_DISTRO_FEDORA
        # Debian / Ubuntu family. The Debian derivatives tend to keep
        # their own ID (linuxmint, elementary, zorin, pop) but list
        # "ubuntu" / "debian" in ID_LIKE so we cover both axes.
        if (c.startswith("debian") or c.startswith("ubuntu")
                or c in ("linuxmint", "elementary", "zorin", "pop", "neon", "kubuntu")):
            return LINUX_DISTRO_DEBIAN
        # Arch family.
        if c.startswith("arch") or c in ("manjaro", "endeavouros", "garuda", "artix"):
            return LINUX_DISTRO_ARCH
    return "unknown"


def _is_pkg_installed(distro: str, pkg: str) -> bool:
    """Return True if `pkg` is currently installed on this system.

    Each distro has a different "is X installed?" query:

      fedora:  `rpm -q X`            -- exit 0 iff installed
      debian:  `dpkg-query -W ...`   -- stdout contains "install ok installed"
      arch:    `pacman -Q X`         -- exit 0 iff installed

    We treat a query error (OSError, command missing) as "not installed"
    rather than raising, so a broken toolchain still produces a useful
    "please install X" message instead of a traceback.
    """
    try:
        if distro == LINUX_DISTRO_FEDORA:
            # --whatprovides handles virtual packages: e.g. Fedora >= 44
            # ships zlib-ng-compat-devel which provides zlib-devel but
            # does not own the "zlib-devel" package name directly.
            out = subprocess.run(
                ["rpm", "-q", "--whatprovides", pkg],
                capture_output=True, text=True, check=False,
            )
            return out.returncode == 0
        if distro == LINUX_DISTRO_DEBIAN:
            # dpkg-query with -W prints "<pkg>\t<status>"; the standard
            # format string we ask for is just the status field.
            out = subprocess.run(
                ["dpkg-query", "-W", "-f=${Status}", pkg],
                capture_output=True, text=True, check=False,
            )
            return out.returncode == 0 and "install ok installed" in out.stdout
        if distro == LINUX_DISTRO_ARCH:
            out = subprocess.run(
                ["pacman", "-Q", pkg],
                capture_output=True, text=True, check=False,
            )
            return out.returncode == 0
    except OSError:
        pass
    return False


def missing_system_packages(distro: str, packages) -> List[str]:
    """Return the subset of `packages` that are not currently installed.

    `packages` is an iterable; result preserves the input order so the
    user sees them in the same sequence the table above defines them.
    """
    return [p for p in packages if not _is_pkg_installed(distro, p)]


def install_system_packages(distro: str, packages: List[str]) -> None:
    """Install the given packages via the distro's package manager.

    Streams the package manager's own output to the terminal so the
    user sees dnf / apt / pacman progress directly (subprocess.run with
    stdout/stderr inherited -- no capture_output). Requires sudo; on
    Debian we run `apt-get update` first so the package index is fresh.

    Raises subprocess.CalledProcessError on failure, which the caller
    is expected to translate into a script abort.
    """
    # Prime the sudo timestamp BEFORE the package manager floods stdout.
    # `sudo -v` validates credentials and shows a clear password prompt
    # (or a cached-credentials message) so the user knows what's going on
    # instead of staring at a frozen terminal after dnf/apt starts.
    info("Package install requires superuser privileges.")
    info("If prompted, enter your sudo password below.")
    try:
        subprocess.run(["sudo", "-v"], check=True)
    except subprocess.CalledProcessError:
        err("sudo authentication failed -- cannot install packages without")
        err("superuser privileges. Re-run with --install-deps after resolving")
        err("the sudo access, or install the missing packages manually.")
        sys.exit(2)

    if distro == LINUX_DISTRO_DEBIAN:
        # apt-get install on a stale index hits "Unable to locate
        # package" on brand-new distros that have not been updated
        # since install. `update` is harmless if the index is fresh.
        info("$ sudo apt-get update")
        subprocess.run(["sudo", "apt-get", "update"], check=True)
        info(f"$ sudo apt-get install -y {' '.join(packages)}")
        subprocess.run(
            ["sudo", "apt-get", "install", "-y", *packages],
            check=True,
        )
        return

    if distro == LINUX_DISTRO_FEDORA:
        cmd = ["sudo", "dnf", "install", "-y", *packages]
    elif distro == LINUX_DISTRO_ARCH:
        # --needed skips packages already present (without it pacman
        # errors out on an "already up to date" target); --noconfirm
        # mirrors the non-interactive mode dnf/apt use by default.
        cmd = ["sudo", "pacman", "-S", "--needed", "--noconfirm", *packages]
    else:
        err(f"install_system_packages: unknown distro '{distro}'")
        sys.exit(2)

    info(f"$ {' '.join(cmd)}")
    subprocess.run(cmd, check=True)


# --------------------------------------------------------------------------- #
# Pre-flight checks                                                            #
# --------------------------------------------------------------------------- #

def tool_preflight(platform: str) -> None:
    """Verify the build tools GDTK needs are reachable. Python is assumed.

    Hard-fails (sys.exit) on a missing hard requirement (cmake, git) with a
    platform-specific install hint, and only warns on soft ones (ninja, the
    MSVC toolset) where a fallback generator exists. Runs on every platform
    before any configure/build step so a missing cmake/git surfaces here
    instead of as a bare "command not found" later.

    On Linux the C/C++ toolchain, pkg-config, zlib-dev and ninja are verified
    separately as system packages (see system_package_preflight) because they
    are installable through the distro's package manager and benefit from the
    --install-deps auto-install path; here we only probe the binary tools that
    a `which` can settle instantly.
    """
    section("Checking build tools")

    for tool in ("cmake", "git"):
        path = which(tool)
        if path:
            ok(f"{tool}: {path}")
            continue
        err(f"Required tool '{tool}' not found in PATH.")
        if platform == "Windows":
            err("  Install CMake (https://cmake.org/download) and Git")
            err("  (https://git-scm.com), then reopen your terminal.")
        else:
            distro = detect_linux_distro()
            hint = {
                LINUX_DISTRO_DEBIAN: "sudo apt-get install -y cmake git",
                LINUX_DISTRO_FEDORA: "sudo dnf install -y cmake git",
                LINUX_DISTRO_ARCH:   "sudo pacman -S --needed cmake git",
            }.get(distro, "install cmake and git via your package manager")
            err(f"  {hint}")
        sys.exit(2)

    ninja = which("ninja")
    if ninja:
        ok(f"ninja: {ninja}")
    else:
        warn("ninja not found -- a slower generator (Unix Makefiles / MSBuild)")
        warn("will be used. Install it for faster builds "
             "(Debian: ninja-build, Fedora: ninja-build, Arch: ninja).")

    if platform == "Windows":
        cl, _ = detect_compiler(platform)
        if cl:
            ok(f"cl.exe: {cl}")
        else:
            warn("MSVC (cl.exe) not found. Run from a Visual Studio Developer")
            warn("Command Prompt, or install VS with the 'Desktop development")
            warn("with C++' workload. CMake will surface the real error if a")
            warn("build toolset is genuinely missing.")


def system_package_preflight(
    platform: str,
    *,
    vulkan: bool,
    install_deps: bool,
) -> int:
    """Verify the Linux system packages GDTK needs are installed.

    On Linux GDTK has a small set of *system* (not vendored) dependencies the
    build cannot satisfy on its own:

      * build essentials -- pkg-config, zlib dev headers, the C/C++ toolchain
        and ninja. Without these the dep configure aborts with cryptic
        "Could NOT find PkgConfig / ZLIB" errors deep inside assimp (this is
        the exact failure that hit Ubuntu before the gate existed).
      * SDL2's X11 dev packages -- always required, because
        Dependency/CMakeLists.txt pins SDL_X11=ON, SDL_WAYLAND=OFF.
      * Vulkan loader / shaderc / VMA dev packages -- only required when
        `--vulkan` is passed.

    Behaviour:
      * Non-Linux / unknown distro / nothing missing -> return 0.
      * Missing packages, `--install-deps` set -> sudo-install them, re-probe,
        return 0 (or 2 on install failure).
      * Missing packages, `--install-deps` NOT set -> print a clear per-distro
        install command and return 2 so the caller aborts before launching the
        dep build, which would otherwise fail less helpfully inside assimp /
        SDL2's CheckX11().

    This gate runs before the vendored-dep tree is touched on purpose: it is
    about *system* packages, not the vendored deps, and silently ignoring a
    missing pkg-config / libX11-devel would produce a broken-looking build.
    """
    if platform != "Linux":
        return 0

    distro = detect_linux_distro()
    if distro == "unknown":
        warn("Could not identify your Linux distro from /etc/os-release.")
        warn("Install pkg-config, the zlib dev headers, the SDL2 X11 dev")
        warn("headers (and Vulkan dev headers if you pass --vulkan) manually")
        warn("-- see BuildScripts/README.md for the package list.")
        # Don't hard-fail: the FATAL_ERROR inside Dependency/CMakeLists will
        # catch missing X11 dev packages anyway, and a user on a distro we
        # don't recognise is better served by that specific message.
        return 0

    # Compose the required package list: build essentials + X11 always,
    # Vulkan if asked.
    required = list(LINUX_BUILD_PACKAGES[distro]) + list(LINUX_X11_PACKAGES[distro])
    section_name = "build + X11 dev packages"
    if vulkan:
        required.extend(LINUX_VULKAN_PACKAGES[distro])
        section_name = "build + X11 + Vulkan dev packages"

    section(f"Checking {section_name} on {LINUX_DISTRO_DISPLAY_NAME[distro]}")
    info(f"Required: {', '.join(required)}")
    missing = missing_system_packages(distro, required)

    if not missing:
        ok("All required system packages are present.")
        return 0

    err(f"Missing system packages: {', '.join(missing)}")

    if not install_deps:
        # No opt-in -> safe path: print the install command for the user's
        # specific distro and abort. We DO NOT auto-sudo on a plain invocation
        # because that would mean every fresh checkout reaches for root
        # privileges without an explicit user action.
        install_cmd = {
            LINUX_DISTRO_FEDORA: "sudo dnf install -y " + " ".join(missing),
            LINUX_DISTRO_DEBIAN: "sudo apt-get update && sudo apt-get install -y " + " ".join(missing),
            LINUX_DISTRO_ARCH:   "sudo pacman -S --needed --noconfirm " + " ".join(missing),
        }[distro]
        err("Re-run with --install-deps to install them automatically,")
        err(f"or run this yourself:\n  {install_cmd}")
        return 2

    # Auto-install path. Stream sudo's prompt + the package manager's own
    # output straight to the terminal so the user sees what is being added.
    info("Auto-installing missing system packages (--install-deps was set)...")
    try:
        install_system_packages(distro, missing)
    except subprocess.CalledProcessError as e:
        err(f"Package install failed with exit code {e.returncode}.")
        err("Re-run after fixing the package manager output above.")
        return 2

    still_missing = missing_system_packages(distro, required)
    if still_missing:
        err(f"Still missing after install: {', '.join(still_missing)}")
        return 2

    ok("All required system packages are now installed.")
    return 0