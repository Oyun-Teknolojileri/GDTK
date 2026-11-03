"""Writes the JSON model and the markdown digest.

Pure python: it only touches the dict ``rdckit.capture`` built, never RenderDoc.
"""

from __future__ import print_function

import json
import os
import time

from rdckit import util

MAX_DRAW_ROWS = 12
MAX_TEXTURE_ROWS = 25
MAX_STATE_TEXTURES = 12


def write(model, out_dir):
    """Writes capture.json and capture.md into ``out_dir``. Returns the paths."""
    util.ensure_dir(out_dir)

    json_path = os.path.join(out_dir, "capture.json")
    md_path = os.path.join(out_dir, "capture.md")

    with open(json_path, "w") as handle:
        json.dump(model, handle, indent=2)

    with open(md_path, "w") as handle:
        handle.write(markdown(model, out_dir))

    return json_path, md_path


# --------------------------------------------------------------------- markdown


def markdown(model, out_dir):
    lines = []
    add = lines.append

    capture = model.get("capture", {})
    engine = model.get("engine") or {}
    frame = model.get("frame", {})

    title = engine.get("label") or os.path.splitext(capture.get("fileName", "capture"))[0]
    add("# RenderDoc capture: %s" % title)
    add("")

    add("| | |")
    add("|---|---|")
    add("| Capture | `%s` (%s) |" % (capture.get("path", "?"), capture.get("sizeHuman", "?")))
    add("| Graphics API | %s |" % capture.get("graphicsApi", capture.get("driver", "?")))
    if capture.get("frameNumber") is not None:
        add("| Frame | %s |" % capture.get("frameNumber"))
    if engine:
        add("| Engine | GDTK %s, config %s |" %
            (engine.get("engineVersion", "?"), engine.get("config", "?")))
        add("| Project | %s |" % (engine.get("project") or "-"))
        add("| Scene | %s |" % (engine.get("scene") or "-"))
        add("| Editor frame | %s |" % engine.get("editorFrame", "?"))
    add("| Analysed with | RenderDoc %s, %s |" %
        (capture.get("renderdocVersion", "?"), time.strftime("%Y-%m-%d %H:%M")))
    add("")

    # ------------------------------------------------------------- at a glance
    add("## At a glance")
    add("")
    add("| Metric | Value |")
    add("|---|---|")
    add("| Draw calls | %s |" % util.human_count(frame.get("draws", 0)))
    add("| Instanced draws | %s |" % util.human_count(frame.get("instancedDraws", 0)))
    add("| Indices submitted | %s |" % util.human_count(frame.get("indices", 0)))
    add("| Dispatch calls | %s |" % util.human_count(frame.get("dispatches", 0)))
    add("| Clears | %s |" % util.human_count(frame.get("clears", 0)))
    add("| Copy / resolve actions | %s |" % util.human_count(frame.get("copies", 0)))
    add("| Marker regions | %s |" % util.human_count(frame.get("markers", 0)))
    add("| Textures / buffers | %d / %d |" %
        (len(model.get("resources", {}).get("textures", [])),
         len(model.get("resources", {}).get("buffers", []))))
    add("")

    if frame.get("finalImage"):
        add("![Final frame](%s)" % frame["finalImage"])
        add("")

    # ----------------------------------------------------------------- passes
    passes = model.get("passes", [])
    if passes:
        add("## Passes")
        add("")
        add("Render passes are the debug marker regions the engine emits, in recording order. "
            "`draws` counts the draw calls inside the region, nested regions included.")
        add("")
        add("| # | Pass | Event | Draws | Indices | Colour target | Depth output | Image |")
        add("|---|---|---|---|---|---|---|---|")
        for index, pass_entry in enumerate(passes):
            nested = "/" in pass_entry.get("path", "")
            label = pass_entry.get("path") if nested else pass_entry.get("name")
            target = pass_entry.get("imageResource") or (pass_entry.get("outputs") or [None])[0]
            colour = target["name"] if target else "-"
            depth = _resource_label(model, pass_entry.get("depthOut"))
            image = "[image](%s)" % pass_entry["image"] if pass_entry.get("image") else "-"
            add("| %d | %s | %d | %d | %s | %s | %s | %s |" % (
                index,
                util.md_escape(label),
                pass_entry.get("eventId", 0),
                pass_entry.get("draws", 0),
                util.human_count(pass_entry.get("indices", 0)),
                util.md_escape(colour),
                util.md_escape(depth),
                image))
        add("")

    # -------------------------------------------------------- pass details
    pipeline_by_pass = {}
    for state in model.get("passPipeline", []):
        pipeline_by_pass[state.get("passId")] = state

    shaders_by_pass = {}
    for shader in model.get("shaders", []):
        shaders_by_pass.setdefault(shader.get("passId"), []).append(shader)

    for pass_entry in passes:
        # Detail sections are for the passes a reader thinks in. A sub pass is a
        # step inside its parent and is covered by the parent's section plus its
        # own row in the pass table.
        if pass_entry.get("depth", 0) > 0:
            continue
        if pass_entry.get("draws", 0) == 0:
            continue
        pass_id = pass_entry.get("id")
        state = pipeline_by_pass.get(pass_id)
        shaders = shaders_by_pass.get(pass_id, [])

        add("### %s (event %d)" % (pass_entry.get("name"), pass_entry.get("eventId")))
        add("")

        if pass_entry.get("image"):
            add("![%s](%s)" % (pass_entry["name"], pass_entry["image"]))
            add("")
        if pass_entry.get("depthImage"):
            add("Depth output: ![depth](%s)" % pass_entry["depthImage"])
            add("")

        add("- Draw calls: %d, indices: %s" %
            (pass_entry.get("draws", 0), util.human_count(pass_entry.get("indices", 0))))

        rendered = pass_entry.get("imageResource")
        if rendered:
            add("- Rendered into: `%s` (id %d), shown at event %s" %
                (rendered["name"], rendered["id"], pass_entry.get("imageEventId")))

        outputs = pass_entry.get("outputs") or []
        if outputs and (not rendered or outputs[0]["id"] != rendered["id"]):
            add("- Marker output: " + ", ".join("`%s` (id %d)" % (o["name"], o["id"]) for o in outputs))
        if pass_entry.get("depthOut"):
            add("- Depth output: `%s` (id %d)" % (_resource_label(model, pass_entry["depthOut"]),
                                                  pass_entry["depthOut"]))

        if state:
            add("- Sampled at event %d, topology %s" %
                (state.get("eventId"), state.get("topology", "?")))
            if state.get("viewport"):
                add("- Viewport: %s" % _viewport_text(state["viewport"]))
            for stage in state.get("stages", []):
                add("- %s shader (`%s`): samplers %s" % (
                    stage.get("stage"),
                    stage.get("entryPoint") or "-",
                    ", ".join("`%s`" % name for name in stage.get("resources", [])) or "-"))
            used = state.get("textures", [])[:MAX_STATE_TEXTURES]
            if used:
                add("- Textures read: " +
                    ", ".join("`%s`%s" % (entry["name"], " (%s)" % entry["size"] if entry.get("size") else "")
                              for entry in used))

        for shader in shaders:
            add("- %s shader source: [%s](%s)" % (shader["stage"], os.path.basename(shader["file"]),
                                                  shader["file"]))

        draw_rows = [entry for entry in model.get("actions", [])
                     if entry.get("type") == "draw"
                     and pass_entry.get("id") in entry.get("passChain", [])]
        if draw_rows:
            add("")
            add("| Event | Draw | Indices | Instances | Output |")
            add("|---|---|---|---|---|")
            for entry in draw_rows[:MAX_DRAW_ROWS]:
                outputs = entry.get("outputs") or []
                add("| %d | %s | %s | %d | %s |" % (
                    entry["eventId"],
                    util.md_escape(entry.get("name", "")),
                    util.human_count(entry.get("numIndices", 0)),
                    entry.get("numInstances", 0),
                    util.md_escape(outputs[0]["name"] if outputs else "-")))
            if len(draw_rows) > MAX_DRAW_ROWS:
                add("")
                add("_%d more draw calls in this pass, see `capture.json`._" % (len(draw_rows) - MAX_DRAW_ROWS))
        add("")

    # --------------------------------------------------------------- resources
    textures = model.get("resources", {}).get("textures", [])
    if textures:
        add("## Largest textures")
        add("")
        add("| Id | Name | Size | Format | Mips | Samples | Memory |")
        add("|---|---|---|---|---|---|---|")
        for texture in textures[:MAX_TEXTURE_ROWS]:
            add("| %d | `%s` | %dx%d | %s | %d | %d | %s |" % (
                texture["id"],
                util.md_escape(texture["name"]),
                texture["width"],
                texture["height"],
                texture["format"],
                texture["mips"],
                texture["samples"],
                texture["bytesHuman"]))
        add("")

    # ----------------------------------------------------------------- issues
    issues = model.get("issues", [])
    add("## Findings")
    add("")
    if issues:
        for issue in issues:
            add("- **%s** (%s, event %d): %s" % (
                issue["kind"], issue["severity"], issue["eventId"], issue["message"]))
    else:
        add("- Nothing jumped out while walking the frame. That is not a clean bill of health, "
            "only the absence of the mechanical problems this tool looks for.")
    add("")

    # ------------------------------------------------------------------ files
    add("## Files in this report")
    add("")
    add("- `capture.json` - the machine readable model: passes, actions, resources, "
        "sampled pipeline state, findings.")
    images = model.get("images", [])
    if images:
        add("- `images/` - %d PNGs, one per pass output plus the final frame." % len(images))
    if model.get("shaders"):
        add("- `shaders/` - %d shader sources, one per pass and stage." % len(model["shaders"]))
    add("")
    add("## Reading the capture for a bug")
    add("")
    add("1. Compare the pass images in recording order. The first pass whose output looks wrong "
        "is where the frame goes bad; everything after it inherits the problem.")
    add("2. Cross check the pass table: a pass with no draws, a pass writing to an unexpected "
        "target, or a pass whose output the next pass does not sample.")
    add("3. In `capture.json`, `passPipeline[].textures[].name` and `.stages[].resources` list "
        "what the first draw of each pass reads and samples, which is where a missing or wrong "
        "binding shows up.")
    add("4. `shaders/` holds the source of those draws; a pass that renders black with the right "
        "inputs is usually a shader or a transform problem.")

    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------- helpers


def _resource_label(model, resource_id):
    """Name of a resource id, looked up in the texture list."""
    if not resource_id:
        return "-"
    for texture in model.get("resources", {}).get("textures", []):
        if texture["id"] == resource_id:
            return texture["name"]
    return "id %d" % resource_id


def _viewport_text(viewport):
    try:
        return "%dx%d at (%d, %d)" % (int(viewport[2]), int(viewport[3]),
                                      int(viewport[0]), int(viewport[1]))
    except Exception:
        return "?"
