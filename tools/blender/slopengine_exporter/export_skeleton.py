import os

import bpy
from bpy_extras.io_utils import ExportHelper

from .format_utils import (
    bind_pose_transform,
    bind_pose_context,
    bone_order,
    bone_parent_index,
    format_quat,
    format_scale,
    format_vec3,
    sanitize_name,
    write_bind_file,
)


class EXPORT_OT_slopengine_skeleton(bpy.types.Operator, ExportHelper):
    bl_idname = "export_scene.slopengine_skeleton"
    bl_label = "Export Slopengine Skeleton"
    bl_description = "Export the selected armature as a .skel file"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".skel"
    filter_glob: bpy.props.StringProperty(default="*.skel", options={"HIDDEN"})

    skeleton_id: bpy.props.StringProperty(
        name="Skeleton ID",
        description="Identifier referenced by geo and anim files",
        default="",
    )

    def draw(self, context):
        self.layout.prop(self, "skeleton_id")

    def execute(self, context):
        result = export_skeleton_asset(
            context,
            self.filepath,
            skeleton_id=self.skeleton_id,
        )
        for warning in result.get("warnings", []):
            self.report({"WARNING"}, warning)
        for info in result.get("info", []):
            self.report({"INFO"}, info)
        if result["status"] != "FINISHED":
            self.report({"ERROR"}, result.get("error", "Skeleton export failed"))
            return {"CANCELLED"}
        return {"FINISHED"}


def export_skeleton_asset(context, filepath, skeleton_id, scene_pose_locked=False):
    result = {
        "status": "FINISHED",
        "error": None,
        "warnings": [],
        "info": [],
    }

    armature_obj = None
    if context.active_object and context.active_object.type == "ARMATURE":
        armature_obj = context.active_object
    else:
        for obj in context.selected_objects:
            if obj.type == "ARMATURE":
                armature_obj = obj
                break

    if armature_obj is None:
        result["status"] = "CANCELLED"
        result["error"] = "Select an armature to export"
        return result

    skeleton_id = skeleton_id.strip()
    if not skeleton_id:
        result["status"] = "CANCELLED"
        result["error"] = "Skeleton ID is required"
        return result

    order = bone_order(armature_obj.data)
    if not order:
        result["status"] = "CANCELLED"
        result["error"] = "Armature has no bones"
        return result

    skeleton_id = sanitize_name(skeleton_id)
    lines = [
        "(skeleton",
        f'  (id "{skeleton_id}")',
        "  (version 1)",
        "  (bones",
        "   (",
    ]

    def write_skeleton_files():
        for index, bone in enumerate(order):
            translation, rotation, scale = bind_pose_transform(armature_obj, bone)
            parent = bone_parent_index(bone, order)
            if parent == index:
                result["warnings"].append(
                    f"Bone '{bone.name}' has invalid parent index; exporting as root"
                )
                parent = -1
            lines.append(
                "    "
                f'(name "{bone.name}" parent {parent} bind '
                f"{format_vec3(translation)} "
                f"{format_quat(rotation)} "
                f"{format_scale(scale)})"
            )

        bind_path = os.path.splitext(filepath)[0] + ".bind"
        write_bind_file(bind_path, armature_obj, order)

    if scene_pose_locked:
        write_skeleton_files()
    else:
        with bind_pose_context(context, armature_obj):
            write_skeleton_files()

    lines.extend(["   )))"])

    with open(filepath, "w", encoding="utf-8") as file_obj:
        file_obj.write("\n".join(lines) + "\n")

    result["info"].append(f"Exported skeleton '{skeleton_id}' to {filepath}")
    return result


classes = (
    EXPORT_OT_slopengine_skeleton,
)
