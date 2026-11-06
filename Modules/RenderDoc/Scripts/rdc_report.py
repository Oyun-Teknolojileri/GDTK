#!/usr/bin/env python3
"""Turns a RenderDoc capture into an AI ready report: JSON, markdown and PNGs.

The heavy lifting runs inside RenderDoc's own python interpreter (qrenderdoc
ships one, the standalone renderdoc module is not part of a distributed build),
so this script is only a driver: it locates qrenderdoc, points the analyzer at
the capture and waits for the report.

Usage:

    python rdc_report.py Captures/boss_intro_frame42.rdc
    python rdc_report.py Captures/boss_intro_frame42.rdc --out /tmp/report --no-images

Output lands in ``<capture>.report/`` next to the capture unless ``--out`` says
otherwise: ``capture.md`` to read first, ``capture.json`` for the full model,
``images/`` for the pass pictures and ``shaders/`` for the shader sources.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ANALYZER = os.path.join(HERE, "rdc_analyze.py")
STATUS_NAME = "status.json"

WINDOWS_CANDIDATES = [
    os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "RenderDoc", "qrenderdoc.exe"),
    os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "RenderDoc", "qrenderdoc.exe"),
    os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "RenderDoc", "qrenderdoc.exe"),
]

POSIX_CANDIDATES = [
    "/usr/bin/qrenderdoc",
    "/usr/local/bin/qrenderdoc",
    "/opt/renderdoc/bin/qrenderdoc",
]


def find_qrenderdoc(explicit=None):
    """Locates the RenderDoc UI binary that carries the python interpreter."""
    if explicit:
        if os.path.isfile(explicit):
            return explicit
        raise SystemExit("qrenderdoc not found at %s" % explicit)

    env_hint = os.environ.get("TK_RENDERDOC_DIR") or os.environ.get("RENDERDOC_DIR")
    if env_hint:
        for name in ("qrenderdoc.exe", "qrenderdoc"):
            candidate = os.path.join(env_hint, name)
            if os.path.isfile(candidate):
                return candidate

    for candidate in WINDOWS_CANDIDATES + POSIX_CANDIDATES:
        if candidate and os.path.isfile(candidate):
            return candidate

    on_path = shutil.which("qrenderdoc") or shutil.which("qrenderdoc.exe")
    if on_path:
        return on_path

    raise SystemExit(
        "qrenderdoc was not found. Install RenderDoc, or point TK_RENDERDOC_DIR at its folder "
        "(the one holding qrenderdoc.exe).")


def report_dir_for(capture_path):
    """Default output directory: <capture without extension>.report next to it."""
    stem = os.path.splitext(os.path.abspath(capture_path))[0]
    return stem + ".report"


def run(capture_path, out_dir, qrenderdoc, images=True, shaders=True, timeout=900, verbose=False):
    """Runs the analyzer and returns the status dict it wrote."""
    out_dir = os.path.abspath(out_dir)
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    status_path = os.path.join(out_dir, STATUS_NAME)
    if os.path.isfile(status_path):
        os.remove(status_path)

    env = dict(os.environ)
    env["TK_RD_CAPTURE"] = os.path.abspath(capture_path)
    env["TK_RD_OUTDIR"] = out_dir
    env["TK_RD_RDCKIT"] = HERE
    env["TK_RD_IMAGES"] = "1" if images else "0"
    env["TK_RD_SHADERS"] = "1" if shaders else "0"

    command = [qrenderdoc, "--python", ANALYZER]
    started = time.time()

    if verbose:
        print("running: %s" % " ".join(command))

    # The analyzer writes its status file when it is done and then asks qrenderdoc to quit, but the UI
    # loop does not always end: a capture window that is open, a modal, or a slow teardown keeps the
    # process alive with the report already on disk. Waiting for the process then burns the whole
    # timeout for nothing, so the status file is what is waited on, and the process is stopped once it
    # is there. A process that dies on its own is still handled - the status file decides, not the
    # exit code.
    process = subprocess.Popen(
        command, env=env, cwd=out_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def _read_output():
        try:
            out, err = process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            out, err = b"", b""
        return out.decode("utf-8", "replace"), err.decode("utf-8", "replace")

    while True:
        if os.path.isfile(status_path):
            # Give the writer a moment to finish closing the file before it is read.
            time.sleep(0.25)
            break

        if process.poll() is not None:
            break

        if time.time() - started > timeout:
            process.kill()
            _read_output()
            raise SystemExit("qrenderdoc did not finish within %d seconds. A large capture can need "
                             "more time, pass --timeout to raise the limit." % timeout)

        time.sleep(0.25)

    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()

    stdout, stderr = _read_output()
    elapsed = time.time() - started

    if verbose and stdout.strip():
        print(stdout.strip())
    if verbose and stderr.strip():
        print(stderr.strip(), file=sys.stderr)

    if not os.path.isfile(status_path):
        raise SystemExit(
            "The analyzer produced no status file. qrenderdoc exited with %s after %.1fs.\n"
            "stdout:\n%s\nstderr:\n%s"
            % (process.returncode, elapsed, stdout.strip(), stderr.strip()))

    with open(status_path) as handle:
        status = json.load(handle)

    status["wallClockSeconds"] = round(elapsed, 2)
    if not status.get("ok"):
        raise SystemExit(
            "The analyzer failed: %s\n%s" % (status.get("error"), status.get("traceback", "")))

    return status


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Render a RenderDoc capture into a JSON / markdown / PNG report.")
    parser.add_argument("capture", help="path to the .rdc capture")
    parser.add_argument("--out", default=None,
                        help="report directory (default: <capture>.report next to the capture)")
    parser.add_argument("--qrenderdoc", default=None, help="path to qrenderdoc.exe")
    parser.add_argument("--no-images", action="store_true", help="skip the PNG export")
    parser.add_argument("--no-shaders", action="store_true", help="skip the shader source export")
    parser.add_argument("--timeout", type=float, default=900.0,
                        help="seconds to wait for the analysis (default 900)")
    parser.add_argument("--verbose", "-v", action="store_true", help="echo qrenderdoc output")

    args = parser.parse_args(argv)

    capture_path = os.path.abspath(args.capture)
    if not os.path.isfile(capture_path):
        raise SystemExit("no such capture: %s" % capture_path)

    out_dir = os.path.abspath(args.out) if args.out else report_dir_for(capture_path)
    qrenderdoc = find_qrenderdoc(args.qrenderdoc)

    status = run(capture_path, out_dir, qrenderdoc,
                 images=not args.no_images, shaders=not args.no_shaders,
                 timeout=args.timeout, verbose=args.verbose)

    print("report: %s" % status["report"])
    print("json:   %s" % status["json"])
    print("draws: %s, passes: %s, runs: %s, images: %s, findings: %s (%.1fs)" % (
        status.get("draws"), status.get("passes"), status.get("wallClockSeconds"),
        status.get("images"), status.get("issues"), status.get("elapsedSeconds", 0.0)))

    return 0


if __name__ == "__main__":
    sys.exit(main())
