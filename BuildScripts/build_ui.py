#!/usr/bin/env python3
"""
GDTK build configurator -- a single-window Tkinter UI that exposes
every flag of build_gdtk.py and build_dependencies.py so you can dial
in a build without leaving the GUI or remembering the command line.

It is a thin front end: it collects the selected options into a command
list, shows a live preview of the exact command it is about to run, and
when you hit "Build" it spawns the chosen script as a subprocess and
streams its stdout/stderr into a log pane. No build logic lives here --
the two entry points in this folder stay the single source of truth, so
anything they learn to do (a new --flag, a new platform) shows up here
by adding one widget.

Stdlib only (tkinter + subprocess + threading), matching the rest of
this folder's style.

Run with:
    python BuildScripts/build_ui.py
"""

from __future__ import annotations

import os
import queue
import re
import shlex
import subprocess
import sys
import threading
from pathlib import Path

# --------------------------------------------------------------------------- #
# tkinter gate (Linux: not always in the base Python package)                 #
# --------------------------------------------------------------------------- #
# python3-tkinter is a separate package on Fedora/Debian/Arch and is not
# pulled in by the "Python 3" meta-package on minimal installs. If it is
# missing we detect the distro and install it via the native package manager
# before importing. Failing that we print the install command and abort
# with a clear message rather than a bare ModuleNotFoundError.
try:
    import tkinter as tk
    from tkinter import messagebox, ttk
except ModuleNotFoundError:
    # ---------------------------------------------------------------- #
    # Quick inline distro probe (mirrors _common.detect_linux_distro   #
    # without pulling in _common itself, which may in turn import      #
    # modules that aren't available in a minimal Python install).       #
    # ---------------------------------------------------------------- #
    _distro = "unknown"
    if sys.platform.startswith("linux"):
        try:
            _info = {}
            with open("/etc/os-release", "r", encoding="utf-8") as _f:
                for _line in _f:
                    _line = _line.strip()
                    if not _line or _line.startswith("#"):
                        continue
                    if "=" in _line:
                        _k, _v = _line.split("=", 1)
                        _v = _v.strip().strip('"').strip("'")
                        _info[_k] = _v
            _did = _info.get("ID", "").lower()
            _like = _info.get("ID_LIKE", "").lower()
            _cands = [_did] + [s.strip() for s in _like.split() if s.strip()]
            for _c in _cands:
                if _c.startswith("fedora") or _c in ("rhel", "centos", "rocky", "almalinux", "nobara"):
                    _distro = "fedora"
                    break
                if (_c.startswith("debian") or _c.startswith("ubuntu")
                        or _c in ("linuxmint", "elementary", "zorin", "pop", "neon", "kubuntu")):
                    _distro = "debian"
                    break
                if _c.startswith("arch") or _c in ("manjaro", "endeavouros", "garuda", "artix"):
                    _distro = "arch"
                    break
        except Exception:
            pass

    _PKG = {"fedora": "python3-tkinter", "debian": "python3-tk", "arch": "tk"}.get(_distro)
    _CMD = {"fedora": f"sudo dnf install -y {_PKG}" if _PKG else None,
            "debian": f"sudo apt-get install -y {_PKG}" if _PKG else None,
            "arch": f"sudo pacman -S --needed --noconfirm {_PKG}" if _PKG else None}.get(_distro)

    if _PKG and _CMD:
        print(f"[build_ui] tkinter not found. Installing {_PKG} ...")
        _install_ok = False
        try:
            subprocess.run(_CMD.split(), check=True)
            _install_ok = True
        except subprocess.CalledProcessError:
            pass

        if _install_ok:
            print("[build_ui] tkinter installed — restarting ...")
            os.execv(sys.executable, [sys.executable, *sys.argv])
        else:
            print(f"[build_ui] Auto-install failed. Run manually: {_CMD}")
    elif sys.platform.startswith("linux"):
        print("[build_ui] tkinter is missing. Install it with your package manager and retry.")
        if _distro == "unknown":
            print("          (could not detect distro — look for python3-tkinter / python3-tk)")
    else:
        print("[build_ui] tkinter is missing. Install it for your platform and retry.")
    sys.exit(1)

# --------------------------------------------------------------------------- #
# Layout                                                                      #
# --------------------------------------------------------------------------- #

# The UI lives next to the scripts it drives; the GDTK root is its parent,
# so subprocess cwd is set there and the build scripts resolve the repo
# layout the same way they do when invoked by hand.
SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent

BUILD_ENGINE = SCRIPT_DIR / "build_gdtk.py"
BUILD_DEPS = SCRIPT_DIR / "build_dependencies.py"

# The four CMake configurations. Debug + Release are the script defaults
# (see DEFAULT_CONFIGS in both entry points); RelWithDebInfo and MinSizeRel
# are valid CMake build types the scripts happily forward.
CONFIGS = ["Debug", "Release", "RelWithDebInfo", "MinSizeRel"]

# CMake targets the root project defines (README "Output layout"). "All"
# is a UI-only pseudo-target that maps to passing no --target at all,
# which is the scripts' "build everything" behaviour.
TARGETS = ["ToolKit", "Workspace", "Editor", "Launcher", "Import", "Packer"]

PLATFORMS = ["auto", "Windows", "Linux", "Mac"]
GENERATORS = ["auto", "msvc", "ninja", "make"]

DEFAULT_JOBS = os.cpu_count() or 1

# --------------------------------------------------------------------------- #
# ANSI colour parsing for the log pane                                        #
# --------------------------------------------------------------------------- #
#
# The build scripts tag their output with ANSI escapes (see Style in
# _common.py) when they think they are talking to a TTY. A subprocess
# whose stdout we capture is NOT a TTY, so the scripts usually emit
# plain text -- which is why we *also* colour by line prefix (>>>, +++,
# !!!, xxx, ==) so the log is readable either way. When ANSI codes do
# arrive, the parser below handles them segment-by-segment.

_ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")

# CMake/Sgr code -> Tk text tag (see BuildApp._configure_log_tags).
_ANSI_TAG_BY_CODE = {
    "31": "error",
    "32": "ok",
    "33": "warn",
    "34": "section",
    "36": "info",
}

# Plain-text line prefixes used by info()/ok()/warn()/err()/section().
def _tag_for_plain_line(line: str):
    stripped = line.lstrip()
    for prefix, tag in ((">>>", "info"), ("+++", "ok"),
                        ("!!!", "warn"), ("xxx", "error"),
                        ("==", "section")):
        if stripped.startswith(prefix):
            return tag
    return None


def _ansi_segments(text: str):
    """Split `text` into (chunk, tag-or-None) pairs, peeling off ANSI codes.

    A style like section()'s BOLD+BLUE emits two separate escapes
    (\\x1b[1m then \\x1b[34m); we walk them in order, switching the
    active tag as we go and resetting on \\x1b[0m. Colour codes win over
    plain bold so "1;34"-style combined sequences also land on the
    intended tag.
    """
    segments = []
    current = None
    pos = 0
    for m in _ANSI_RE.finditer(text):
        chunk = text[pos:m.start()]
        if chunk:
            segments.append((chunk, current))
        pos = m.end()
        codes = m.group()[2:-1]  # strip "\x1b[" ... "m"
        parts = codes.split(";") if codes else [""]
        if "0" in parts or codes == "":
            current = None
            continue
        # Prefer a colour code; fall back to bold ("1").
        chosen = next((_ANSI_TAG_BY_CODE[p] for p in parts if p in _ANSI_TAG_BY_CODE), None)
        if chosen is None and "1" in parts:
            chosen = "bold"
        if chosen:
            current = chosen
    tail = text[pos:]
    if tail:
        segments.append((tail, current))
    return segments


# --------------------------------------------------------------------------- #
# Main application                                                            #
# --------------------------------------------------------------------------- #

class BuildApp:
    """Owns every widget, the build subprocess, and the log drain loop."""

    _POLL_MS = 100  # how often the UI thread drains the worker's log queue

    def __init__(self, root: tk.Tk):
        self.root = root
        self.proc: subprocess.Popen | None = None
        self.worker: threading.Thread | None = None
        self.log_queue: queue.Queue = queue.Queue()
        self.running = False

        # -- tk variables (one source of truth the command builder reads) -----
        self.mode = tk.StringVar(value="engine")  # "engine" | "deps"
        self.platform = tk.StringVar(value="auto")
        self.generator = tk.StringVar(value="auto")
        self.jobs = tk.IntVar(value=DEFAULT_JOBS)
        self.clean = tk.BooleanVar(value=False)

        self.config_vars = {c: tk.BooleanVar(value=c in ("Debug", "Release"))
                            for c in CONFIGS}

        self.target_all = tk.BooleanVar(value=True)
        self.target_vars = {t: tk.BooleanVar(value=False) for t in TARGETS}

        self.vulkan = tk.BooleanVar(value=False)
        self.install_deps = tk.BooleanVar(value=False)
        self.no_deps_check = tk.BooleanVar(value=False)
        self.skip_submodules = tk.BooleanVar(value=False)
        self.skip_assimp = tk.BooleanVar(value=False)
        self.skip_imgui = tk.BooleanVar(value=False)

        self._build_ui()
        self._apply_mode()         # show/hide the per-mode panels
        self._update_preview()     # first paint of the command line
        self.root.after(self._POLL_MS, self._drain_queue)

    # ------------------------------------------------------------------ #
    # Widget construction                                                #
    # ------------------------------------------------------------------ #

    def _build_ui(self) -> None:
        self.root.title("GDTK Build")
        self.root.geometry("960x720")
        self.root.minsize(760, 560)

        container = ttk.Frame(self.root, padding=10)
        container.pack(fill=tk.BOTH, expand=True)

        # -- Build mode --------------------------------------------------
        mode_box = ttk.LabelFrame(container, text="Build mode", padding=8)
        mode_box.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        mode_box.columnconfigure(2, weight=1)
        ttk.Radiobutton(mode_box, text="Engine + Tools (build_gdtk.py)",
                        variable=self.mode, value="engine",
                        command=self._apply_mode).grid(row=0, column=0, sticky="w")
        ttk.Radiobutton(mode_box, text="Dependencies only (build_dependencies.py)",
                        variable=self.mode, value="deps",
                        command=self._apply_mode).grid(row=0, column=1, sticky="w", padx=(16, 0))

        # -- Common options (shared by both scripts) ----------------------
        common = ttk.LabelFrame(container, text="Common options", padding=8)
        common.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        common.columnconfigure(5, weight=1)

        ttk.Label(common, text="Platform:").grid(row=0, column=0, sticky="w")
        ttk.Combobox(common, textvariable=self.platform, values=PLATFORMS,
                     width=10, state="readonly").grid(row=0, column=1, sticky="w", padx=(4, 16))

        ttk.Label(common, text="Generator:").grid(row=0, column=2, sticky="w")
        ttk.Combobox(common, textvariable=self.generator, values=GENERATORS,
                     width=10, state="readonly").grid(row=0, column=3, sticky="w", padx=(4, 16))

        ttk.Label(common, text="Jobs (-j):").grid(row=0, column=4, sticky="w")
        ttk.Spinbox(common, from_=1, to=128, width=5,
                    textvariable=self.jobs,
                    command=self._update_preview).grid(row=0, column=5, sticky="w", padx=(4, 0))

        ttk.Label(common, text="Configs:").grid(row=1, column=0, sticky="nw", pady=(8, 0))
        cfg_inner = ttk.Frame(common)
        cfg_inner.grid(row=1, column=1, columnspan=6, sticky="w", pady=(8, 0))
        for i, c in enumerate(CONFIGS):
            ttk.Checkbutton(cfg_inner, text=c, variable=self.config_vars[c],
                            command=self._update_preview).grid(row=0, column=i, padx=(0, 12))

        ttk.Checkbutton(common, text="Clean before build (--clean)",
                        variable=self.clean,
                        command=self._update_preview).grid(row=2, column=0, columnspan=6,
                                                           sticky="w", pady=(8, 0))

        # -- Engine-only panel -------------------------------------------
        self.engine_frame = ttk.LabelFrame(
            container, text="Engine + Tools options", padding=8)
        self.engine_frame.grid(row=2, column=0, sticky="ew", pady=(0, 8))
        self.engine_frame.columnconfigure(7, weight=1)

        ttk.Label(self.engine_frame, text="Targets:").grid(row=0, column=0, sticky="nw")
        tgt_inner = ttk.Frame(self.engine_frame)
        tgt_inner.grid(row=0, column=1, columnspan=7, sticky="w")
        self.target_checks: dict[str, ttk.Checkbutton] = {}
        all_btn = ttk.Checkbutton(tgt_inner, text="All (no --target)",
                                  variable=self.target_all,
                                  command=self._on_target_all_toggle)
        all_btn.grid(row=0, column=0, padx=(0, 12))
        for i, t in enumerate(TARGETS, start=1):
            cb = ttk.Checkbutton(tgt_inner, text=t, variable=self.target_vars[t],
                                 command=self._update_preview)
            cb.grid(row=0, column=i, padx=(0, 12))
            self.target_checks[t] = cb

        flags_row1 = ttk.Frame(self.engine_frame)
        flags_row1.grid(row=1, column=0, columnspan=8, sticky="w", pady=(8, 0))
        ttk.Checkbutton(flags_row1, text="Vulkan backend (--vulkan)",
                        variable=self.vulkan,
                        command=self._update_preview).grid(row=0, column=0, padx=(0, 16))
        ttk.Checkbutton(flags_row1, text="Install-deps / Linux (--install-deps)",
                        variable=self.install_deps,
                        command=self._update_preview).grid(row=0, column=1, padx=(0, 16))
        ttk.Checkbutton(flags_row1, text="Skip dep auto-build (--no-deps-check)",
                        variable=self.no_deps_check,
                        command=self._update_preview).grid(row=0, column=2)

        # -- Dependencies-only panel -------------------------------------
        self.deps_frame = ttk.LabelFrame(
            container, text="Dependencies options", padding=8)
        self.deps_frame.grid(row=2, column=0, sticky="ew", pady=(0, 8))

        drow = ttk.Frame(self.deps_frame)
        drow.grid(row=0, column=0, sticky="w")
        ttk.Checkbutton(drow, text="Skip submodules (--skip-submodules)",
                        variable=self.skip_submodules,
                        command=self._update_preview).grid(row=0, column=0, padx=(0, 16))
        ttk.Checkbutton(drow, text="Skip assimp (--skip-assimp)",
                        variable=self.skip_assimp,
                        command=self._update_preview).grid(row=0, column=1, padx=(0, 16))
        ttk.Checkbutton(drow, text="Skip imgui (--skip-imgui)",
                        variable=self.skip_imgui,
                        command=self._update_preview).grid(row=0, column=2)

        # -- Command preview ---------------------------------------------
        preview_box = ttk.LabelFrame(container, text="Command preview", padding=8)
        preview_box.grid(row=3, column=0, sticky="ew", pady=(0, 8))
        preview_box.columnconfigure(0, weight=1)
        self.preview_var = tk.StringVar(value="")
        ttk.Entry(preview_box, textvariable=self.preview_var,
                  state="readonly", font=("Consolas", 9)).grid(
            row=0, column=0, sticky="ew")

        # -- Build log ----------------------------------------------------
        log_box = ttk.LabelFrame(container, text="Build log", padding=4)
        log_box.grid(row=4, column=0, sticky="nsew", pady=(0, 8))
        log_box.rowconfigure(0, weight=1)
        log_box.columnconfigure(0, weight=1)
        container.rowconfigure(4, weight=1)
        container.columnconfigure(0, weight=1)

        self.log_text = tk.Text(log_box, wrap=tk.NONE, undo=False,
                                bg="#1e1e1e", fg="#d4d4d4",
                                insertbackground="#d4d4d4",
                                font=("Consolas", 9), padx=6, pady=4,
                                state=tk.DISABLED)
        log_scroll_y = ttk.Scrollbar(log_box, orient=tk.VERTICAL,
                                     command=self.log_text.yview)
        log_scroll_x = ttk.Scrollbar(log_box, orient=tk.HORIZONTAL,
                                     command=self.log_text.xview)
        self.log_text.config(yscrollcommand=log_scroll_y.set,
                             xscrollcommand=log_scroll_x.set)
        self.log_text.grid(row=0, column=0, sticky="nsew")
        log_scroll_y.grid(row=0, column=1, sticky="ns")
        log_scroll_x.grid(row=1, column=0, sticky="ew")
        self._configure_log_tags()

        # -- Action buttons ----------------------------------------------
        buttons = ttk.Frame(container)
        buttons.grid(row=5, column=0, sticky="ew", pady=(0, 0))
        buttons.columnconfigure(0, weight=1)
        right = ttk.Frame(buttons)
        right.grid(row=0, column=1, sticky="e")
        ttk.Button(right, text="Clear log", command=self._clear_log).grid(
            row=0, column=0, padx=(0, 8))
        self.build_btn = ttk.Button(right, text="Build ▶", command=self._start_build)
        self.build_btn.grid(row=0, column=1, padx=(0, 8))
        self.stop_btn = ttk.Button(right, text="Stop ■", command=self._stop_build,
                                   state=tk.DISABLED)
        self.stop_btn.grid(row=0, column=2)

    def _configure_log_tags(self) -> None:
        """Map the logical tags to colours on the dark log background."""
        self.log_text.tag_configure("info", foreground="#4ec9b0")
        self.log_text.tag_configure("ok", foreground="#73c991")
        self.log_text.tag_configure("warn", foreground="#d7ba7d")
        self.log_text.tag_configure("error", foreground="#f48771")
        self.log_text.tag_configure("section", foreground="#569cd6")
        self.log_text.tag_configure("bold", font=("Consolas", 9, "bold"))

    # ------------------------------------------------------------------ #
    # Reactivity                                                        #
    # ------------------------------------------------------------------ #

    def _apply_mode(self) -> None:
        """Swap the Engine / Dependencies panel and refresh the preview."""
        if self.mode.get() == "engine":
            self.engine_frame.grid()
            self.deps_frame.grid_remove()
        else:
            self.deps_frame.grid()
            self.engine_frame.grid_remove()
        self._update_preview()

    def _on_target_all_toggle(self) -> None:
        all_on = self.target_all.get()
        for cb in self.target_checks.values():
            cb.configure(state=tk.DISABLED if all_on else tk.NORMAL)
        self._update_preview()

    def _selected_configs(self) -> list[str]:
        return [c for c in CONFIGS if self.config_vars[c].get()]

    def _selected_targets(self) -> list[str]:
        return [t for t in TARGETS if self.target_vars[t].get()]

    def _jobs_value(self) -> int:
        try:
            return int(self.jobs.get())
        except (tk.TclError, ValueError):
            return DEFAULT_JOBS

    def _build_command(self) -> list[str]:
        """Turn the current widget state into the exact argv the script gets."""
        is_engine = self.mode.get() == "engine"
        script = BUILD_ENGINE if is_engine else BUILD_DEPS
        cmd: list[str] = [sys.executable, str(script)]

        if self.platform.get() != "auto":
            cmd += ["--platform", self.platform.get()]
        sel_configs = self._selected_configs()
        if sel_configs:
            cmd += ["--configs", *sel_configs]
        cmd += ["-j", str(self._jobs_value())]
        if self.generator.get() != "auto":
            cmd += ["--generator", self.generator.get()]
        if self.clean.get():
            cmd += ["--clean"]

        if is_engine:
            if not self.target_all.get():
                for t in self._selected_targets():
                    cmd += ["--target", t]
            if self.vulkan.get():
                cmd += ["--vulkan"]
            if self.install_deps.get():
                cmd += ["--install-deps"]
            if self.no_deps_check.get():
                cmd += ["--no-deps-check"]
        else:
            if self.skip_submodules.get():
                cmd += ["--skip-submodules"]
            if self.skip_assimp.get():
                cmd += ["--skip-assimp"]
            if self.skip_imgui.get():
                cmd += ["--skip-imgui"]
        return cmd

    def _update_preview(self) -> None:
        cmd = self._build_command()
        self.preview_var.set("$ " + shlex.join(cmd))
        # A build needs at least one configuration; grey out Build otherwise.
        can_build = bool(self._selected_configs()) and not self.running
        self.build_btn.configure(state=tk.NORMAL if can_build else tk.DISABLED)

    # ------------------------------------------------------------------ #
    # Build lifecycle                                                   #
    # ------------------------------------------------------------------ #

    def _start_build(self) -> None:
        if self.running:
            return
        sel_configs = self._selected_configs()
        if not sel_configs:
            messagebox.showwarning(
                "No configuration selected",
                "Select at least one build configuration (Debug / Release / ...).")
            return
        cmd = self._build_command()
        self._append_marked("$ " + shlex.join(cmd), "info")
        self._append_marked("", None)

        self.running = True
        self.build_btn.configure(state=tk.DISABLED)
        self.stop_btn.configure(state=tk.NORMAL)
        self.worker = threading.Thread(target=self._run_process, args=(cmd,),
                                       daemon=True)
        self.worker.start()

    def _run_process(self, cmd: list[str]) -> None:
        """Worker thread: spawn the script, feed every line to the queue."""
        try:
            self.proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,           # line-buffered so the log streams live
                cwd=str(ROOT_DIR),
                env=os.environ.copy(),
            )
        except OSError as e:
            self.log_queue.put(("error", f"Failed to launch build: {e}\n"))
            self.log_queue.put(("done", 1))
            return

        assert self.proc.stdout is not None
        try:
            for line in self.proc.stdout:
                self.log_queue.put(("line", line))
        finally:
            rc = self.proc.wait()
            self.log_queue.put(("done", rc))

    def _stop_build(self) -> None:
        if not (self.proc and self.proc.poll() is None):
            return
        self._append_marked("Stopping build...", "warn")
        try:
            # terminate() only hits the direct child; on Windows the build
            # spawns cmake -> cl/ninja, so tear down the whole tree.
            if sys.platform == "win32":
                subprocess.run(
                    ["taskkill", "/PID", str(self.proc.pid), "/T", "/F"],
                    capture_output=True)
            else:
                self.proc.terminate()
        except OSError:
            pass

    def _drain_queue(self) -> None:
        """UI thread: move queued log lines into the Text widget."""
        try:
            while True:
                kind, payload = self.log_queue.get_nowait()
                if kind == "line":
                    self._append_raw(payload)
                elif kind == "error":
                    self._append_marked(payload, "error")
                elif kind == "done":
                    self._finalize_build(payload)
        except queue.Empty:
            pass
        self.root.after(self._POLL_MS, self._drain_queue)

    def _finalize_build(self, rc: int) -> None:
        self.running = False
        if rc == 0:
            self._append_marked(f"Process exited with code {rc}.", "ok")
        else:
            self._append_marked(f"Process exited with code {rc}.", "error")
        self.stop_btn.configure(state=tk.DISABLED)
        self._update_preview()

    # ------------------------------------------------------------------ #
    # Log writing                                                       #
    # ------------------------------------------------------------------ #

    def _append_raw(self, text: str) -> None:
        """Write a captured stdout chunk, colouring by ANSI or line prefix."""
        self.log_text.configure(state=tk.NORMAL)
        try:
            if _ANSI_RE.search(text):
                for chunk, tag in _ansi_segments(text):
                    self.log_text.insert(tk.END, chunk, (tag,) if tag else ())
            else:
                # No ANSI (the common case from a captured pipe): colour by
                # the script's plain-text markers so >>> / +++ / !!! / xxx
                # still read in colour, line by line.
                for line in text.splitlines(keepends=True):
                    tag = _tag_for_plain_line(line)
                    self.log_text.insert(tk.END, line, (tag,) if tag else ())
            self.log_text.see(tk.END)
        finally:
            self.log_text.configure(state=tk.DISABLED)

    def _append_marked(self, text: str, tag) -> None:
        self.log_text.configure(state=tk.NORMAL)
        try:
            self.log_text.insert(tk.END, text + "\n", (tag,) if tag else ())
            self.log_text.see(tk.END)
        finally:
            self.log_text.configure(state=tk.DISABLED)

    def _clear_log(self) -> None:
        self.log_text.configure(state=tk.NORMAL)
        self.log_text.delete("1.0", tk.END)
        self.log_text.configure(state=tk.DISABLED)

    # ------------------------------------------------------------------ #
    # Window close                                                      #
    # ------------------------------------------------------------------ #

    def on_close(self) -> None:
        if self.running:
            self._stop_build()
        self.root.destroy()


# --------------------------------------------------------------------------- #
# Entry point                                                                 #
# --------------------------------------------------------------------------- #

def main() -> int:
    try:
        import tkinter  # noqa: F401  -- fail fast with a clear message
    except ImportError:
        sys.stderr.write(
            "Tkinter is not available in this Python install.\n"
            "  Windows: use python.org's installer (tkinter is bundled).\n"
            "  Linux:   install python3-tk (Debian/Ubuntu) or python3-tkinter (Fedora).\n")
        return 2

    root = tk.Tk()
    app = BuildApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
