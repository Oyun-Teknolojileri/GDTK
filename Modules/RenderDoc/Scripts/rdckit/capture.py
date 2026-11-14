"""Opens a RenderDoc capture and builds the model the report is written from.

Imports ``renderdoc``, so it only runs inside RenderDoc's embedded python
interpreter (that is how ``rdc_analyze.py`` is launched).
"""

from __future__ import print_function

import os
import time

import rdckit
from rdckit import util

# Boundary for the sampled pipeline state: every sample costs a replay up to its
# event, and the point of the sampling is to describe each pass, not each draw.
MAX_STATE_SAMPLES = 24

# Same for saved images: a capture with hundreds of passes would otherwise
# produce hundreds of PNGs nobody looks at.
MAX_IMAGES = 48

# Images one top level pass may contribute, nested sub passes included. The
# bloom chain alone is a dozen full quad passes, and twelve pictures of its mips
# do not help anyone read the frame.
MAX_IMAGES_PER_ROOT = 4

# Texture mappings for the files we write.
IMAGE_TYPE = "png"


def _safe_resource_name(resource):
    """RenderDoc label of a resource, or its id when the label is not valid UTF-8.

    A label is whatever the host passed to glObjectLabel. A build that hands the driver a buffer
    that is not a C string makes the replay hand back bytes that are not text, and a strict decode
    takes the whole report down with it.
    """
    try:
        return resource.name
    except UnicodeDecodeError:
        return str(int(resource.resourceId))



class CaptureAnalysis(object):
    """Replays one capture and collects everything the report needs."""

    def __init__(self, rdc_path, out_dir, want_images=True, want_shaders=True):
        self.rdc_path = rdc_path
        self.out_dir = out_dir
        self.want_images = want_images
        self.want_shaders = want_shaders

        self.rd = None
        self.cap = None
        self.ctl = None
        self.structured = None

        self.resource_names = {}
        self.texture_by_id = {}
        self.buffer_by_id = {}

        # RenderDoc hands out ResourceId objects; the report only wants the
        # numbers. Caching the objects by number is what makes it possible to
        # hand one back to SaveTexture() without rebuilding it.
        self.rid_cache = {}

        self.model = None
        self.issues = []

    # ------------------------------------------------------------------ setup

    def open(self):
        """Opens the capture and brings up the replay. Raises on failure."""
        import renderdoc as rd

        self.rd = rd

        self.cap = rd.OpenCaptureFile()
        result = self.cap.OpenFile(self.rdc_path, "", None)
        if result != rd.ResultCode.Succeeded:
            raise RuntimeError("RenderDoc could not open %s: %s" % (self.rdc_path, str(result)))

        if self.cap.LocalReplaySupport() == rd.ReplaySupport.Unsupported:
            raise RuntimeError(
                "The capture cannot be replayed on this machine (driver %s); remote replay is needed."
                % self.cap.DriverName())

        result, self.ctl = self.cap.OpenCapture(rd.ReplayOptions(), None)
        if result != rd.ResultCode.Succeeded or self.ctl is None:
            raise RuntimeError("RenderDoc could not start the replay: %s" % str(result))

        self.structured = self.ctl.GetStructuredFile()
        return self

    def close(self):
        if self.ctl is not None:
            try:
                self.ctl.Shutdown()
            except Exception:
                pass
            self.ctl = None
        if self.cap is not None:
            try:
                self.cap.Shutdown()
            except Exception:
                pass
            self.cap = None

    # ------------------------------------------------------------------ model

    def build(self):
        model = {
            "schema": util.REPORT_SCHEMA,
            "generatedAt": int(time.time()),
            "tool": "gdtk rdckit %s" % rdckit.__version__,
            "capture": self._capture_meta(),
            "engine": self._engine_meta(),
            "frame": {},
            "passes": [],
            "passPipeline": [],
            "actions": [],
            "resources": {"textures": [], "buffers": []},
            "images": [],
            "shaders": [],
            "issues": [],
        }

        self._collect_resources(model)
        root = self.ctl.GetRootActions()
        model["frame"] = self._frame_summary(root)
        self._walk_actions(root, model)
        self._collect_issues(model)

        if self.want_images:
            self._save_images(model)
        if self.want_shaders:
            self._save_shaders(model)

        model["passes"] = self._decorate_passes(model)
        model["issues"] = self.issues

        self.model = model
        return model

    # -------------------------------------------------------------- resources

    def rid(self, resource_id):
        """Records the ResourceId object behind a number and returns the number."""
        if resource_id is None:
            return 0
        value = int(resource_id)
        if value != 0:
            self.rid_cache[value] = resource_id
        return value

    def name_of(self, resource_id):
        """Human readable name of a resource id."""
        try:
            value = int(resource_id)
        except Exception:
            return ""
        if value == 0:
            return ""
        return self.resource_names.get(value, "Resource %d" % value)

    def _resource_object(self, value):
        return self.rid_cache.get(int(value))

    def _collect_resources(self, model):
        for resource in self.ctl.GetResources():
            self.resource_names[int(resource.resourceId)] = _safe_resource_name(resource)

        for texture in self.ctl.GetTextures():
            entry = {
                "id": self.rid(texture.resourceId),
                "name": self.name_of(texture.resourceId),
                "width": int(texture.width),
                "height": int(texture.height),
                "depth": int(texture.depth),
                "arraysize": int(texture.arraysize),
                "mips": int(texture.mips),
                "samples": int(texture.msSamp),
                "format": str(texture.format.Name()),
                "bytes": int(texture.byteSize),
                "cubemap": bool(texture.cubemap),
            }
            entry["bytesHuman"] = util.human_bytes(entry["bytes"])
            self.texture_by_id[entry["id"]] = entry
            model["resources"]["textures"].append(entry)

        for buffer in self.ctl.GetBuffers():
            entry = {
                "id": self.rid(buffer.resourceId),
                "name": self.name_of(buffer.resourceId),
                "length": int(buffer.length),
                "lengthHuman": util.human_bytes(int(buffer.length)),
            }
            self.buffer_by_id[entry["id"]] = entry
            model["resources"]["buffers"].append(entry)

        model["resources"]["textures"].sort(key=lambda item: -item["bytes"])
        model["resources"]["buffers"].sort(key=lambda item: -item["length"])

    # ---------------------------------------------------------------- metadata

    def _capture_meta(self):
        meta = {
            "path": self.rdc_path.replace("\\", "/"),
            "fileName": os.path.basename(self.rdc_path),
            "sizeBytes": os.path.getsize(self.rdc_path) if os.path.isfile(self.rdc_path) else 0,
            "driver": self.cap.DriverName(),
            "machine": self.cap.RecordedMachineIdent(),
            "renderdocVersion": self.rd.GetVersionString(),
        }
        meta["sizeHuman"] = util.human_bytes(meta["sizeBytes"])

        try:
            frame = self.ctl.GetFrameInfo()
            # OpenGL captures have no frame number; RenderDoc reports the
            # "NoFrameNumber" sentinel and the report is better off without it.
            number = int(frame.frameNumber)
            if 0 <= number < 0xFFFFFFFF:
                meta["frameNumber"] = number
            meta["captureTime"] = int(frame.captureTime)
        except Exception:
            pass

        try:
            meta["graphicsApi"] = util.enum_name(self.ctl.GetAPIProperties().pipelineType,
                                                 meta["driver"])
        except Exception:
            meta["graphicsApi"] = meta["driver"]

        return meta

    def _engine_meta(self):
        """The sidecar JSON the editor writes next to a capture, when it exists."""
        sidecar = os.path.splitext(self.rdc_path)[0] + ".json"
        if not os.path.isfile(sidecar):
            return None

        try:
            import json
            with open(sidecar) as handle:
                return json.load(handle)
        except Exception:
            return None

    def _frame_summary(self, root):
        lookup = self.rd.ActionFlags
        summary = {
            "rootActions": len(root),
            "actions": 0,
            "draws": 0,
            "instancedDraws": 0,
            "dispatches": 0,
            "clears": 0,
            "copies": 0,
            "markers": 0,
            "indices": 0,
        }

        stack = list(root)
        while stack:
            action = stack.pop()
            summary["actions"] += 1
            flags = action.flags
            if flags & lookup.Drawcall:
                summary["draws"] += 1
                summary["indices"] += int(action.numIndices)
                if flags & lookup.Instanced:
                    summary["instancedDraws"] += 1
            if flags & lookup.Dispatch:
                summary["dispatches"] += 1
            if flags & lookup.Clear:
                summary["clears"] += 1
            if flags & lookup.Copy or flags & lookup.Resolve:
                summary["copies"] += 1
            if flags & lookup.PushMarker:
                summary["markers"] += 1
            stack.extend(action.children)

        return summary

    # ----------------------------------------------------------------- actions

    def _action_name(self, action):
        if action.customName:
            return action.customName
        try:
            name = action.GetName(self.structured)
            if name:
                return name
        except Exception:
            pass
        return "event %d" % action.eventId

    def _outputs(self, action):
        outputs = []
        for resource_id in action.outputs:
            value = self.rid(resource_id)
            if value == 0:
                continue
            outputs.append({"id": value, "name": self.name_of(value)})
        return outputs

    def _walk_actions(self, root, model):
        """Flattens the action tree into pass markers and leaf actions."""
        rd = self.rd

        def visit(action, path, chain, depth):
            flags = action.flags
            name = self._action_name(action)

            if flags & rd.ActionFlags.PushMarker:
                child_path = path + [name]
                marker_id = "%s@%d" % ("/".join(child_path), int(action.eventId))
                entry = {
                    "id": marker_id,
                    "name": name,
                    "path": "/".join(child_path),
                    "passChain": list(chain),
                    "eventId": int(action.eventId),
                    "depth": depth,
                    "draws": 0,
                    "dispatches": 0,
                    "clears": 0,
                    "indices": 0,
                    "outputs": self._outputs(action),
                    "depthOut": self.rid(action.depthOut),
                    "image": None,
                    "depthImage": None,
                }
                model["passes"].append(entry)
                for child in action.children:
                    visit(child, child_path, chain + [marker_id], depth + 1)
                return

            entry = {
                "eventId": int(action.eventId),
                "name": name,
                "pass": "/".join(path),
                "passId": chain[-1] if chain else "",
                "passChain": list(chain),
                "depth": depth,
                "flags": util.flag_names(flags, rd.ActionFlags),
                "drawIndex": int(action.drawIndex),
                "numIndices": int(action.numIndices),
                "numInstances": int(action.numInstances),
                "outputs": self._outputs(action),
                "depthOut": self.rid(action.depthOut),
            }

            if flags & rd.ActionFlags.Drawcall:
                entry["type"] = "draw"
            elif flags & rd.ActionFlags.Dispatch:
                entry["type"] = "dispatch"
                entry["dispatchDimension"] = list(action.dispatchDimension)
            elif flags & rd.ActionFlags.Clear:
                entry["type"] = "clear"
            elif flags & rd.ActionFlags.Copy or flags & rd.ActionFlags.Resolve:
                entry["type"] = "copy"
            elif flags & rd.ActionFlags.Present:
                entry["type"] = "present"
            else:
                entry["type"] = "other"

            model["actions"].append(entry)
            for child in action.children:
                visit(child, path, chain, depth)

        for action in root:
            visit(action, [], [], 0)

        # Roll the leaf actions up into the pass they were recorded in, then
        # hand a parent marker the totals of the markers nested inside it.
        by_id = {}
        for pass_entry in model["passes"]:
            by_id[pass_entry["id"]] = pass_entry

        for entry in model["actions"]:
            owner = by_id.get(entry["passId"])
            if owner is None:
                continue
            if entry["type"] == "draw":
                owner["draws"] += 1
                owner["indices"] += entry["numIndices"]
            elif entry["type"] == "dispatch":
                owner["dispatches"] += 1
            elif entry["type"] == "clear":
                owner["clears"] += 1

        for pass_entry in reversed(model["passes"]):
            for other in model["passes"]:
                if other is pass_entry or pass_entry["id"] not in other["passChain"]:
                    continue
                pass_entry["draws"] += other["draws"]
                pass_entry["indices"] += other["indices"]
                pass_entry["clears"] += other["clears"]
                pass_entry["dispatches"] += other["dispatches"]

    # ------------------------------------------------------------------ issues

    def _collect_issues(self, model):
        for pass_entry in model["passes"]:
            if pass_entry["depth"] > 0:
                continue
            if pass_entry["draws"] == 0 and pass_entry["dispatches"] == 0:
                self._issue(
                    "empty-pass", "warning",
                    "Pass '%s' (event %d) recorded no draws. A pass that is expected to render "
                    "something but only clears or copies points at culling, a camera or a resource."
                    % (pass_entry["name"], pass_entry["eventId"]), pass_entry["eventId"])

        if model["frame"]["draws"] == 0:
            self._issue("no-draws", "error", "The captured frame contains no draw calls at all.", 0)

        for entry in model["actions"]:
            if entry["type"] != "draw":
                continue
            if entry["numIndices"] == 0:
                self._issue(
                    "empty-draw", "warning",
                    "Draw at event %d in pass '%s' has no indices or vertices."
                    % (entry["eventId"], entry["pass"]), entry["eventId"])
            if not entry["outputs"]:
                self._issue(
                    "no-output", "warning",
                    "Draw at event %d in pass '%s' has no colour output bound."
                    % (entry["eventId"], entry["pass"]), entry["eventId"])

    def _issue(self, kind, severity, message, event_id):
        self.issues.append({
            "kind": kind,
            "severity": severity,
            "message": message,
            "eventId": int(event_id),
        })

    # ------------------------------------------------------------------ images

    def _save_images(self, model):
        image_dir = os.path.join(self.out_dir, "images")
        util.ensure_dir(image_dir)

        saved = 0
        order = 0
        per_root = {}

        # A picture of every pass that rendered something, taken at its last
        # draw so it shows what the pass produced.
        for pass_entry in model["passes"]:
            if saved >= MAX_IMAGES:
                break
            if pass_entry["draws"] == 0:
                continue

            root_id = pass_entry["passChain"][0] if pass_entry["passChain"] else pass_entry["id"]
            if per_root.get(root_id, 0) >= MAX_IMAGES_PER_ROOT:
                continue

            written = self._last_written_output(model, pass_entry["id"])
            if written is None:
                continue
            last_event, targets, depth_out = written

            order += 1
            file_name = "%02d_%s.%s" % (order, util.sanitize(pass_entry["path"]), IMAGE_TYPE)
            path = os.path.join(image_dir, file_name)
            if self._save_texture(targets[0]["id"], last_event, path):
                model["images"].append({
                    "kind": "pass",
                    "pass": pass_entry["path"],
                    "eventId": last_event,
                    "resource": targets[0],
                    "file": util.rel_link(self.out_dir, path),
                    "caption": "%s output at event %d (%s)" %
                               (pass_entry["path"], last_event, targets[0]["name"]),
                })
                pass_entry["image"] = util.rel_link(self.out_dir, path)
                pass_entry["imageEventId"] = last_event
                pass_entry["imageResource"] = targets[0]
                saved += 1
                per_root[root_id] = per_root.get(root_id, 0) + 1

            # The depth buffer a pass wrote is what the passes after it sample,
            # so the first one that has a depth output gets a picture too.
            if saved < MAX_IMAGES and depth_out:
                depth_name = "%02d_%s_depth.%s" % (order, util.sanitize(pass_entry["path"]), IMAGE_TYPE)
                depth_path = os.path.join(image_dir, depth_name)
                if self._save_texture(depth_out, last_event, depth_path):
                    model["images"].append({
                        "kind": "depth",
                        "pass": pass_entry["path"],
                        "eventId": last_event,
                        "resource": {"id": depth_out, "name": self.name_of(depth_out)},
                        "file": util.rel_link(self.out_dir, depth_path),
                        "caption": "%s depth output at event %d" % (pass_entry["path"], last_event),
                    })
                    pass_entry["depthImage"] = util.rel_link(self.out_dir, depth_path)
                    saved += 1

        # The last colour target in the frame is the image the user saw.
        final_event, final_target = self._final_output(model)
        if final_event is not None and final_target:
            path = os.path.join(image_dir, "99_final_frame.%s" % IMAGE_TYPE)
            if self._save_texture(final_target["id"], final_event, path):
                model["images"].append({
                    "kind": "final",
                    "eventId": final_event,
                    "resource": final_target,
                    "file": util.rel_link(self.out_dir, path),
                    "caption": "Final frame at event %d" % final_event,
                })
                model["frame"]["finalImage"] = util.rel_link(self.out_dir, path)
                model["frame"]["finalResource"] = final_target

    def _last_event_of_pass(self, model, pass_id):
        """Event of the last action that wrote something inside the pass."""
        written = self._last_written_output(model, pass_id)
        return written[0] if written else None

    def _last_written_output(self, model, pass_id):
        """Colour targets the pass wrote last, and the event that wrote them.

        The outputs recorded on a marker describe the state where the marker
        sits, which for a pass that switches targets internally is the buffer it
        started from rather than the one it produced. Taking the last action that
        had colour outputs instead is what makes the per pass image show the
        pass result.
        """
        best = None

        for entry in model["actions"]:
            if entry["type"] not in ("draw", "copy", "clear"):
                continue
            if pass_id not in entry["passChain"]:
                continue
            if not entry["outputs"]:
                continue
            if best is None or entry["eventId"] > best["eventId"]:
                best = entry

        if best is None:
            return None

        return best["eventId"], best["outputs"], best["depthOut"]

    def _final_output(self, model):
        present_event = None
        for entry in model["actions"]:
            if entry["type"] == "present":
                present_event = entry["eventId"]

        if present_event is None:
            present_event = model["actions"][-1]["eventId"] if model["actions"] else 0

        final_target = None
        final_event = None
        for entry in model["actions"]:
            if entry["eventId"] > present_event:
                continue
            if entry["outputs"]:
                final_target = entry["outputs"][0]
                final_event = entry["eventId"]

        return final_event, final_target

    def _save_texture(self, resource_id, event_id, path):
        rd = self.rd
        texture = self._resource_object(resource_id)
        if texture is None:
            return False

        try:
            # SetFrameEvent() returns None through the python binding, so the
            # call is the whole contract: move the replay and save from here.
            self.ctl.SetFrameEvent(event_id, True)

            save = rd.TextureSave()
            save.resourceId = texture
            save.mip = 0
            save.destType = rd.FileType.PNG
            save.alpha = rd.AlphaMapping.Preserve

            detail = self.ctl.SaveTexture(save, path)
            if not os.path.isfile(path):
                self._issue("image-failed", "info",
                            "RenderDoc wrote no image for texture %s at event %d (%s)"
                            % (self.name_of(resource_id), event_id, str(detail)), event_id)
                return False

            return True
        except Exception as exc:
            self._issue("image-failed", "info",
                        "Could not save texture %s at event %d: %r"
                        % (self.name_of(resource_id), event_id, exc), event_id)
            return False

    # ----------------------------------------------------------------- shaders

    def _save_shaders(self, model):
        shader_dir = os.path.join(self.out_dir, "shaders")
        util.ensure_dir(shader_dir)

        sampled = 0
        for pass_entry in model["passes"]:
            if sampled >= MAX_STATE_SAMPLES:
                break
            if pass_entry["draws"] == 0:
                continue

            event_id = self._first_draw_event(model, pass_entry["id"])
            if event_id is None:
                continue
            sampled += 1

            state = self._sample_state(pass_entry, event_id)
            if state:
                model["passPipeline"].append(state)

            for stage in ("Vertex", "Pixel", "Compute"):
                info = self._save_shader(stage, event_id, shader_dir, pass_entry)
                if info:
                    model["shaders"].append(info)

    def _first_draw_event(self, model, pass_id):
        for entry in model["actions"]:
            if entry["type"] != "draw":
                continue
            if pass_id in entry["passChain"]:
                return entry["eventId"]
        return None

    def _sample_state(self, pass_entry, event_id):
        rd = self.rd
        try:
            # SetFrameEvent() returns None through the python binding; a failure
            # raises instead of returning a status.
            self.ctl.SetFrameEvent(event_id, True)

            pipe = self.ctl.GetPipelineState()
            state = {
                "pass": pass_entry["path"],
                "passId": pass_entry["id"],
                "eventId": int(event_id),
                "topology": util.enum_name(pipe.GetPrimitiveTopology()),
                "outputs": [],
                "depth": None,
                "stages": [],
                "textures": [],
            }

            for target in pipe.GetOutputTargets():
                value = self.rid(target.resource)
                if value != 0:
                    state["outputs"].append({"id": value, "name": self.name_of(value)})

            depth_target = self.rid(pipe.GetDepthTarget().resource)
            if depth_target != 0:
                state["depth"] = {"id": depth_target, "name": self.name_of(depth_target)}

            try:
                viewport = pipe.GetViewport(0)
                state["viewport"] = [float(viewport.x), float(viewport.y),
                                     float(viewport.width), float(viewport.height)]
            except Exception:
                pass

            for stage_name in ("Vertex", "Pixel", "Compute"):
                stage = getattr(rd.ShaderStage, stage_name, None)
                if stage is None:
                    continue
                reflection = pipe.GetShaderReflection(stage)
                if reflection is None:
                    continue

                stage_info = {
                    "stage": stage_name,
                    "entryPoint": reflection.entryPoint,
                    "encoding": util.enum_name(reflection.encoding),
                    "resourceId": self.rid(reflection.resourceId),
                    "resources": [],
                    "constants": [],
                }
                for resource in reflection.readOnlyResources:
                    if resource.isTexture:
                        stage_info["resources"].append(resource.name)
                for block in reflection.constantBlocks:
                    stage_info["constants"].append(block.name)
                state["stages"].append(stage_info)

            for stage_name in ("Vertex", "Pixel", "Compute"):
                stage = getattr(rd.ShaderStage, stage_name, None)
                if stage is None:
                    continue
                try:
                    used = pipe.GetReadOnlyResources(stage, True)
                except Exception:
                    continue
                for item in used:
                    value = self.rid(item.descriptor.resource)
                    if value == 0:
                        continue
                    entry = {"id": value, "name": self.name_of(value), "stage": stage_name}
                    texture = self.texture_by_id.get(value)
                    if texture:
                        entry["size"] = "%dx%d" % (texture["width"], texture["height"])
                    state["textures"].append(entry)

            return state
        except Exception as exc:
            self._issue("state-failed", "info",
                        "Could not sample pipeline state at event %d: %r" % (event_id, exc), event_id)
            return None

    def _save_shader(self, stage_name, event_id, shader_dir, pass_entry):
        rd = self.rd
        stage = getattr(rd.ShaderStage, stage_name, None)
        if stage is None:
            return None

        try:
            self.ctl.SetFrameEvent(event_id, True)

            reflection = self.ctl.GetPipelineState().GetShaderReflection(stage)
            if reflection is None:
                return None

            encoding = str(reflection.encoding)
            if "GLSL" not in encoding and "HLSL" not in encoding:
                return None

            raw = reflection.rawBytes
            if not raw:
                return None

            name = "%s_%s_%d.glsl" % (util.sanitize(pass_entry["path"]), stage_name.lower(), event_id)
            path = os.path.join(shader_dir, name)
            with open(path, "w") as handle:
                handle.write(raw.decode("utf-8", "replace"))

            return {
                "pass": pass_entry["path"],
                "passId": pass_entry["id"],
                "stage": stage_name,
                "eventId": int(event_id),
                "entryPoint": reflection.entryPoint,
                "encoding": util.enum_name(encoding),
                "file": util.rel_link(self.out_dir, path),
            }
        except Exception:
            return None

    # ------------------------------------------------------------------ polish

    def _decorate_passes(self, model):
        """Puts the passes in recording order and numbers them for the report."""
        passes = model["passes"]
        passes.sort(key=lambda entry: entry["eventId"])

        for index, pass_entry in enumerate(passes):
            pass_entry["order"] = index

        return passes


def analyze(rdc_path, out_dir, want_images=True, want_shaders=True):
    """Convenience wrapper: open, build the model, close. Returns the model."""
    analysis = CaptureAnalysis(rdc_path, out_dir, want_images, want_shaders)
    try:
        analysis.open()
        return analysis.build()
    finally:
        analysis.close()
