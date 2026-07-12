import os

import bpy
from bpy_extras.io_utils import ExportHelper

from .format_utils import (
    active_armature,
    bone_order,
    collect_armature_actions,
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

    def draw(self, context):
        self.layout.prop(self, "skeleton_id")
        self.layout.prop(self, "fps")

    def execute(self, context):
        result = export_anim_asset(
            context,
            self.filepath,
            skeleton_id=self.skeleton_id,
            fps=self.fps,
        )
        for warning in result.get("warnings", []):
            self.report({"WARNING"}, warning)
        for info in result.get("info", []):
            self.report({"INFO"}, info)
        if result["status"] != "FINISHED":
            self.report({"ERROR"}, result.get("error", "Animation export failed"))
            return {"CANCELLED"}
        return {"FINISHED"}


def export_anim_asset(context, filepath, skeleton_id, fps=30.0):
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

    if armature_obj.mode != "POSE":
        bpy.ops.object.mode_set(mode="POSE")

    skeleton_id = sanitize_name(skeleton_id)
    anim_basename = sanitize_name(os.path.splitext(os.path.basename(filepath))[0])
    output_dir = os.path.dirname(filepath)
    clips = []

    try:
        animation_data.use_nla = False
        for action in actions:
            animation_data.action = action
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
            clips.append(
                {
                    "name": clip_name,
                    "fps": fps,
                    "duration": sampled["duration"],
                    "tracks_file": tracks_file,
                }
            )
    finally:
        animation_data.action = previous_action
        animation_data.use_nla = previous_use_nla
        scene.frame_set(previous_frame)
        if previous_mode != "POSE":
            bpy.ops.object.mode_set(mode=previous_mode)

    if not clips:
        result["status"] = "CANCELLED"
        result["error"] = "No animation clips exported"
        return result

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
