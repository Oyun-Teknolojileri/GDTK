"""Runs inside RenderDoc's embedded python interpreter.

qrenderdoc executes this with ``--python`` and stays headless as long as the
script ends with ``sys.exit()``. Everything it needs comes from the environment,
because the embedded interpreter has neither a useful ``sys.argv`` nor
``__file__``:

    TK_RD_CAPTURE   absolute path of the .rdc to analyse
    TK_RD_OUTDIR    directory the report is written to
    TK_RD_RDCKIT    directory holding the rdckit package
    TK_RD_IMAGES    "0" to skip the PNG export
    TK_RD_SHADERS   "0" to skip the shader source export

The outcome lands in ``<outdir>/status.json``; the driver reads that file, since
qrenderdoc always exits with 0.
"""

import json
import os
import sys
import time
import traceback

# qrenderdoc's interpreter would otherwise leave a __pycache__ next to the tool
# (and the bytecode it caches is python 3.6, which no other run can use).
sys.dont_write_bytecode = True

STATUS_NAME = "status.json"


def write_status(out_dir, status):
    try:
        with open(os.path.join(out_dir, STATUS_NAME), "w") as handle:
            json.dump(status, handle, indent=2)
    except Exception:
        pass


def main():
    out_dir = os.environ.get("TK_RD_OUTDIR", "")
    rdc_path = os.environ.get("TK_RD_CAPTURE", "")
    kit_dir = os.environ.get("TK_RD_RDCKIT", "")

    status = {
        "ok": False,
        "started": time.time(),
        "elapsedSeconds": 0.0,
        "error": "",
        "capture": rdc_path,
        "outDir": out_dir,
        "report": "",
        "json": "",
        "images": 0,
    }

    started = time.time()

    try:
        if not rdc_path or not os.path.isfile(rdc_path):
            raise RuntimeError("capture file not found: %r" % rdc_path)
        if not out_dir:
            raise RuntimeError("TK_RD_OUTDIR is not set")
        if kit_dir and kit_dir not in sys.path:
            sys.path.insert(0, kit_dir)

        import rdckit.capture as capture
        import rdckit.report as report

        want_images = os.environ.get("TK_RD_IMAGES", "1") != "0"
        want_shaders = os.environ.get("TK_RD_SHADERS", "1") != "0"

        if not os.path.isdir(out_dir):
            os.makedirs(out_dir)

        model = capture.analyze(rdc_path, out_dir, want_images, want_shaders)
        json_path, md_path = report.write(model, out_dir)

        status["ok"] = True
        status["report"] = md_path
        status["json"] = json_path
        status["images"] = len(model.get("images", []))
        status["draws"] = model.get("frame", {}).get("draws", 0)
        status["passes"] = len(model.get("passes", []))
        status["issues"] = len(model.get("issues", []))

        print("report: %s" % md_path)
        print("draws: %s, passes: %s, images: %s" %
              (status["draws"], status["passes"], status["images"]))
    except Exception as exc:
        status["ok"] = False
        status["error"] = "%s: %s" % (type(exc).__name__, exc)
        status["traceback"] = traceback.format_exc()
        print("analysis failed: %s" % status["error"])

    status["elapsedSeconds"] = round(time.time() - started, 2)

    if out_dir:
        try:
            if not os.path.isdir(out_dir):
                os.makedirs(out_dir)
        except Exception:
            pass
        write_status(out_dir, status)

    return status


result = main()

# qrenderdoc only skips opening the UI when the script raises SystemExit; the
# outcome above is what the caller reads.
sys.exit(0 if result.get("ok") else 1)
