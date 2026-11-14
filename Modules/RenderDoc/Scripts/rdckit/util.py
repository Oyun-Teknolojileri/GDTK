"""Helpers shared by the RenderDoc capture report tool.

Pure python, no ``renderdoc`` import, so it can be imported by the driver
(any python 3) as well as by the analyzer (python 3.6, inside qrenderdoc).
"""

from __future__ import print_function

import os
import re

REPORT_SCHEMA = "gdtk-rendercapture/1"

_SAFE = re.compile(r"[^A-Za-z0-9._-]+")


def sanitize(text, fallback="unnamed"):
    """Turns any string into something safe for a file name."""
    cleaned = _SAFE.sub("_", str(text)).strip("_.")
    cleaned = cleaned[:80]
    return cleaned or fallback


def human_bytes(count):
    """Byte count as a short human readable string."""
    value = float(count)
    for unit in ("B", "KB", "MB", "GB"):
        if value < 1024.0 or unit == "GB":
            if unit == "B":
                return "%d B" % int(value)
            return "%.2f %s" % (value, unit)
        value /= 1024.0
    return "%.2f GB" % value


def human_count(count):
    """Thousands separator that does not depend on the locale."""
    return "{:,}".format(int(count))


def flag_names(value, enum_type, skip=("NoFlags", "Auto")):
    """Names of the members of a flags enum that are set in ``value``."""
    names = []
    for name in sorted(enum_type.__members__.keys()):
        if name in skip:
            continue
        try:
            if value & getattr(enum_type, name):
                names.append(name)
        except Exception:
            continue
    return names


def ensure_dir(path):
    """Creates ``path`` (and parents) when it does not exist yet."""
    if path and not os.path.isdir(path):
        try:
            os.makedirs(path)
        except OSError:
            if not os.path.isdir(path):
                raise
    return path


def rel_link(from_dir, target):
    """Relative, forward-slashed link from a report directory to a file."""
    try:
        rel = os.path.relpath(target, from_dir)
    except ValueError:
        # Different drives on Windows: fall back to the absolute path.
        rel = target
    return rel.replace("\\", "/")


def md_escape(text):
    """Escapes the characters that break a markdown table cell."""
    return str(text).replace("|", "\\|").replace("\n", " ").strip()


def enum_name(value, fallback="?"):
    """Name of a SWIG enum member, without the enum type.

    ``Topology.TriangleList`` -> ``TriangleList``. Newer RenderDoc bindings stringify an
    enum to its integer (``GraphicsAPI.OpenGL`` comes out as ``2``) and keep the name in
    the repr alone, as ``<GraphicsAPI.OpenGL: 2>``, so the name is read from there.
    """
    if value is None:
        return fallback

    text = repr(value)
    if text.startswith("<") and ":" in text:
        # <GraphicsAPI.OpenGL: 2> -> GraphicsAPI.OpenGL
        text = text[1 : text.rfind(":")]
    if len(text) > 1 and text[0] == text[-1] and text[0] in "'\"":
        text = text[1:-1]

    if "." in text:
        text = text.split(".")[-1]

    return text or fallback
