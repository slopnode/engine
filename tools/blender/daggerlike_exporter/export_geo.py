import os

import bpy
from bpy_extras.io_utils import ExportHelper

from .format_utils import (
    apply_world_transform_to_mesh,
    armature_from_mesh,
    bind_pose_context,
    bone_order,
    build_vertex_weights,
    extract_material_parts,
    is_identity_transform,
    merge_geo_parts,
    mesh_objects_from_selection,
    sanitize_name,
    triangulate_mesh,
    write_vert_file,
    write_weights_file,
)


class EXPORT_OT_daggerlike_geo(bpy.types.Operator, ExportHelper):
    bl_idname = "export_scene.daggerlike_geo"
    bl_label = "Export Daggerlike Geo"
    bl_description = "Export selection into one .geo container with shared vertex buffers"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".geo"
    filter_glob: bpy.props.StringProperty(default="*.geo", options={"HIDDEN"})

    skeleton_id: bpy.props.StringProperty(
        name="Skeleton ID",
        description="Skeleton used for skinning, leave empty for static geo",
        default="",
    )

    use_selection: bpy.props.BoolProperty(
        name="Selection Only",
        description="Export all selected mesh objects into the same geo file",
        default=True,
    )

    def draw(self, context):
        self.layout.prop(self, "skeleton_id")
        self.layout.prop(self, "use_selection")

    def execute(self, context):
        result = export_geo_asset(
            context,
            self.filepath,
            skeleton_id=self.skeleton_id,
            use_selection=self.use_selection,
        )
        for warning in result.get("warnings", []):
            self.report({"WARNING"}, warning)
        for info in result.get("info", []):
            self.report({"INFO"}, info)
        if result["status"] != "FINISHED":
            self.report({"ERROR"}, result.get("error", "Geometry export failed"))
            return {"CANCELLED"}
        return {"FINISHED"}


def export_geo_asset(context, filepath, skeleton_id, use_selection=True, scene_pose_locked=False):
    result = {
        "status": "FINISHED",
        "error": None,
        "warnings": [],
        "info": [],
    }

    mesh_objects = mesh_objects_from_selection(context) if use_selection else []
    if not mesh_objects:
        if context.active_object and context.active_object.type == "MESH":
            mesh_objects = [context.active_object]
        else:
            result["status"] = "CANCELLED"
            result["error"] = "Select at least one mesh object to export"
            return result

    output_dir = os.path.dirname(filepath)
    geo_basename = sanitize_name(os.path.splitext(os.path.basename(filepath))[0])
    vert_file = f"{geo_basename}.vert"
    weights_file = f"{geo_basename}.weights"
    skeleton_id = skeleton_id.strip()
    export_armature = None
    collected_parts = []
    baked_transform_count = 0

    for obj in mesh_objects:
        armature_obj = armature_from_mesh(obj)
        vertex_weights = None

        if armature_obj is not None:
            if export_armature is None:
                export_armature = armature_obj
            elif export_armature != armature_obj:
                result["status"] = "CANCELLED"
                result["error"] = (
                    "Selected meshes use different armatures; export one rig per geo file"
                )
                return result

            bone_order_list = bone_order(armature_obj.data)
            vertex_weights, weight_stats = build_vertex_weights(obj, bone_order_list)
            if weight_stats["unmatched_groups"]:
                result["warnings"].append(
                    f"{obj.name}: unmatched vertex groups "
                    f"{', '.join(weight_stats['unmatched_groups'][:8])}"
                )
            if weight_stats["zero_weight_vertices"] > 0:
                result["warnings"].append(
                    f"{obj.name}: {weight_stats['zero_weight_vertices']} "
                    "vertices with no bone influence"
                )

        mesh = obj.data.copy()
        try:
            if armature_obj is not None:
                matrix = armature_obj.matrix_world.inverted() @ obj.matrix_world
            else:
                matrix = obj.matrix_world.copy()
            apply_world_transform_to_mesh(mesh, matrix)
            if not is_identity_transform(matrix):
                baked_transform_count += 1

            triangulate_mesh(mesh)
            parts = extract_material_parts(mesh, obj, vertex_weights)
            collected_parts.extend(parts)
        finally:
            bpy.data.meshes.remove(mesh)

    merged = merge_geo_parts(collected_parts)
    if not merged["primitives"]:
        result["status"] = "CANCELLED"
        result["error"] = "No geometry to export"
        return result

    vertex_count = len(merged["positions"]) // 3
    if export_armature is not None:
        if merged["weights"] is None or len(merged["weights"]) != vertex_count:
            result["status"] = "CANCELLED"
            result["error"] = "All selected meshes must be skinned to the same armature"
            return result

    if export_armature is not None:
        if not skeleton_id:
            result["status"] = "CANCELLED"
            result["error"] = "Skinned geo requires a skeleton id (e.g. biped)"
            return result
    elif skeleton_id:
        result["warnings"].append(
            "Skeleton id ignored because selection has no skinned meshes"
        )
        skeleton_id = ""

    def write_geo_files():
        write_vert_file(
            os.path.join(output_dir, vert_file),
            merged["positions"],
            merged["normals"],
            merged["uvs"],
            merged["indices"],
        )

        has_weights = merged["weights"] is not None
        if has_weights:
            write_weights_file(
                os.path.join(output_dir, weights_file),
                merged["weights"],
            )

        lines = ["(geo", "  (vertices implicit)"]
        if has_weights:
            lines.append("  (weights implicit)")
        if skeleton_id:
            lines.append(f'  (skeleton "{sanitize_name(skeleton_id)}")')
        lines.append("  (primitives")
        lines.append("   (")

        for primitive in merged["primitives"]:
            lines.append(f'    (name "{primitive["id"]}"')
            lines.append(f'     material "{primitive["material"]}"')
            lines.append(f'     vertex-offset {primitive["vertex_offset"]}')
            lines.append(f'     vertex-count {primitive["vertex_count"]}')
            lines.append(f'     index-offset {primitive["index_offset"]}')
            lines.append(f'     index-count {primitive["index_count"]}')
            lines.append("    )")

        lines.extend(["   ))", ")"])

        with open(filepath, "w", encoding="utf-8") as file_obj:
            file_obj.write("\n".join(lines) + "\n")

    if export_armature is not None and not scene_pose_locked:
        with bind_pose_context(context, export_armature):
            write_geo_files()
    else:
        write_geo_files()

    if baked_transform_count > 0:
        result["info"].append(
            f"Baked object transforms for {baked_transform_count} mesh(es)"
        )

    result["info"].append(
        f"Exported {len(merged['primitives'])} primitive(s) to {filepath}"
    )
    return result


classes = (
    EXPORT_OT_daggerlike_geo,
)
