import os
import re

import bpy
from bpy_extras.io_utils import ExportHelper

from .format_utils import (
    active_armature,
    assign_action,
    bone_order,
    collect_armature_actions,
    parse_clip_filter,
    sample_action_frames,
    sanitize_name,
    write_tracks_file,
)


class EXPORT_OT_slopengine_anim(bpy.types.Operator, ExportHelper):
    bl_idname = "export_scene.slopengine_anim"
    bl_label = "Export Slopengine Anim"
    bl_description = "Export all actions assigned to the selected armature as one .anim file"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".anim"
    filter_glob: bpy.props.StringProperty(default="*.anim", options={"HIDDEN"})

    skeleton_id: bpy.props.StringProperty(
        name="Skeleton ID",
        description="Skeleton this animation targets",
        default="",
    )

    fps: bpy.props.FloatProperty(
        name="FPS",
        description="Sample rate for the exported animation",
        default=30.0,
        min=1.0,
    )

    clips: bpy.props.StringProperty(
        name="Clips",
        description="Comma-separated action names to export; empty exports every action",
        default="",
    )

    def draw(self, context):
        self.layout.prop(self, "skeleton_id")
        self.layout.prop(self, "fps")
        self.layout.prop(self, "clips")

    def execute(self, context):
        result = export_anim_asset(
            context,
            self.filepath,
            skeleton_id=self.skeleton_id,
            fps=self.fps,
            clip_names=parse_clip_filter(self.clips),
        )
        for warning in result.get("warnings", []):
            self.report({"WARNING"}, warning)
        for info in result.get("info", []):
            self.report({"INFO"}, info)
        if result["status"] != "FINISHED":
            self.report({"ERROR"}, result.get("error", "Animation export failed"))
            return {"CANCELLED"}
        return {"FINISHED"}


CLIP_RE = re.compile(r'\(name "([^"]+)"(.*?)\)\s*(?=\(name "|\)\)|$)', re.S)
FIELD_RE = re.compile(r'^\s*(fps|duration|tracks|event)\s+(.*)$', re.M)


def read_existing_bank(filepath):
    clips = {}
    if not os.path.exists(filepath):
        return clips
    with open(filepath, "r", encoding="utf-8") as file_obj:
        text = file_obj.read()
    for match in CLIP_RE.finditer(text):
        name = match.group(1)
        clip = {"name": name, "fps": 0.0, "duration": 0.0, "tracks_file": "", "events": []}
        for field in FIELD_RE.finditer(match.group(2)):
            key, value = field.group(1), field.group(2).strip().rstrip(")")
            if key == "fps":
                clip["fps"] = float(value)
            elif key == "duration":
                clip["duration"] = float(value)
            elif key == "tracks":
                clip["tracks_file"] = value.strip('"')
            elif key == "event":
                parts = value.split(None, 1)
                if len(parts) == 2:
                    clip["events"].append((int(parts[0]), parts[1].strip().strip('"')))
        clips[name] = clip
    return clips


def marker_events(action, frame_start, scene_fps, fps, frame_count):
    events = []
    for marker in getattr(action, "pose_markers", []):
        index = int(round((marker.frame - frame_start) / scene_fps * fps))
        index = max(0, min(frame_count - 1, index))
        events.append((index, sanitize_name(marker.name)))
    return events


def merge_events(*lists):
    merged = []
    for events in lists:
        for event in events:
            if event not in merged:
                merged.append(event)
    merged.sort()
    return merged


def export_anim_asset(context, filepath, skeleton_id, fps=30.0, clip_names=None):
    result = {
        "status": "FINISHED",
        "error": None,
        "warnings": [],
        "info": [],
    }

    armature_obj = active_armature(context)
    if armature_obj is None:
        result["status"] = "CANCELLED"
        result["error"] = "Select an armature to export animation from"
        return result

    skeleton_id = skeleton_id.strip()
    if not skeleton_id:
        result["status"] = "CANCELLED"
        result["error"] = "Skeleton ID is required"
        return result

    animation_data = armature_obj.animation_data
    if animation_data is None:
        result["status"] = "CANCELLED"
        result["error"] = "Armature has no animation data"
        return result

    actions = collect_armature_actions(armature_obj)
    if not actions:
        result["status"] = "CANCELLED"
        result["error"] = "Armature has no actions; assign an action or add NLA strips"
        return result

    if clip_names:
        wanted = {name for name in clip_names}
        wanted.update(sanitize_name(name) for name in clip_names)
        found = {action.name for action in actions} | {
            sanitize_name(action.name) for action in actions
        }
        for name in clip_names:
            if name not in found and sanitize_name(name) not in found:
                result["warnings"].append(f"Requested clip '{name}' not found on armature")
        actions = [
            action
            for action in actions
            if action.name in wanted or sanitize_name(action.name) in wanted
        ]
        if not actions:
            result["status"] = "CANCELLED"
            result["error"] = "None of the requested clips exist on the armature"
            return result

    order = bone_order(armature_obj.data)
    if not order:
        result["status"] = "CANCELLED"
        result["error"] = "Armature has no bones"
        return result

    scene = context.scene
    previous_frame = scene.frame_current
    previous_mode = armature_obj.mode
    previous_action = animation_data.action
    previous_use_nla = animation_data.use_nla
    previous_pose_position = armature_obj.data.pose_position

    if armature_obj.mode != "POSE":
        bpy.ops.object.mode_set(mode="POSE")
    armature_obj.data.pose_position = "POSE"

    skeleton_id = sanitize_name(skeleton_id)
    anim_basename = sanitize_name(os.path.splitext(os.path.basename(filepath))[0])
    output_dir = os.path.dirname(filepath)
    existing = read_existing_bank(filepath)
    clips = []

    try:
        animation_data.use_nla = False
        for action in actions:
            assign_action(animation_data, action)
            sampled = sample_action_frames(
                context,
                armature_obj,
                order,
                action,
                fps,
            )
            if sampled is None or not sampled["frames"]:
                result["warnings"].append(
                    f"Skipped action '{action.name}' with invalid frame range"
                )
                continue

            clip_name = sanitize_name(action.name)
            tracks_file = f"{anim_basename}_{clip_name}.tracks"
            write_tracks_file(
                os.path.join(output_dir, tracks_file),
                sampled["frames"],
            )
            previous = existing.get(clip_name, {})
            events = merge_events(
                previous.get("events", []),
                marker_events(
                    action,
                    int(action.frame_range[0]),
                    scene.render.fps,
                    fps,
                    len(sampled["frames"]),
                ),
            )
            clips.append(
                {
                    "name": clip_name,
                    "fps": fps,
                    "duration": sampled["duration"],
                    "tracks_file": tracks_file,
                    "events": events,
                }
            )
    finally:
        armature_obj.data.pose_position = previous_pose_position
        assign_action(animation_data, previous_action)
        animation_data.use_nla = previous_use_nla
        scene.frame_set(previous_frame)
        if previous_mode != "POSE":
            bpy.ops.object.mode_set(mode=previous_mode)

    if not clips:
        result["status"] = "CANCELLED"
        result["error"] = "No animation clips exported"
        return result

    exported_names = {clip["name"] for clip in clips}
    kept = 0
    for name, clip in existing.items():
        if name in exported_names or not clip["tracks_file"]:
            continue
        if not os.path.exists(os.path.join(output_dir, clip["tracks_file"])):
            continue
        clips.append(clip)
        kept += 1
    if kept:
        result["info"].append(f"Kept {kept} existing clip(s) not in this export")

    lines = [
        "(anim",
        f'  (skeleton "{skeleton_id}")',
        "  (clips",
        "   (",
    ]
    for clip in clips:
        lines.append(f'    (name "{clip["name"]}"')
        lines.append(f'     fps {clip["fps"]:.6g}')
        lines.append(f'     duration {clip["duration"]:.6g}')
        for frame, event_name in clip.get("events", []):
            lines.append(f'     event {frame} "{event_name}"')
        lines.append(f'     tracks "{clip["tracks_file"]}")')
    lines.extend(["   ))", ")"])

    with open(filepath, "w", encoding="utf-8") as file_obj:
        file_obj.write("\n".join(lines) + "\n")

    clip_names = ", ".join(clip["name"] for clip in clips)
    result["info"].append(
        f"Exported {len(clips)} clip(s) [{clip_names}] to {filepath}"
    )
    return result


classes = (
    EXPORT_OT_slopengine_anim,
)
