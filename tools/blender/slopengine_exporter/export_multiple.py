import os

import bpy

from .export_anim import export_anim_asset
from .export_geo import export_geo_asset
from .export_skeleton import export_skeleton_asset
from .format_utils import active_armature, bind_pose_context, parse_clip_filter, sanitize_name


def export_multiple_asset(
    context,
    project_root,
    asset_name,
    skeleton_id,
    fps=30.0,
    use_selection=True,
    clip_names=None,
    only=None,
    geo_name=None,
):
    result = {
        "status": "FINISHED",
        "error": None,
        "warnings": [],
        "info": [],
    }

    project_root = bpy.path.abspath(project_root).strip()
    if not project_root:
        result["status"] = "CANCELLED"
        result["error"] = "Project root is required"
        return result

    asset_name = sanitize_name(asset_name.strip())
    if not asset_name:
        result["status"] = "CANCELLED"
        result["error"] = "Asset name is required"
        return result

    skeleton_id = skeleton_id.strip()
    if not skeleton_id:
        result["status"] = "CANCELLED"
        result["error"] = "Skeleton ID is required"
        return result

    skel_dir = os.path.join(project_root, "skeletons", asset_name)
    geo_dir = os.path.join(project_root, "geometry", asset_name)
    anim_dir = os.path.join(project_root, "animations", asset_name)

    for output_dir in (skel_dir, geo_dir, anim_dir):
        os.makedirs(output_dir, exist_ok=True)

    geo_name = sanitize_name((geo_name or asset_name).strip())
    exports = (
        (
            "skeleton",
            export_skeleton_asset,
            os.path.join(skel_dir, f"{asset_name}.skel"),
            {"skeleton_id": skeleton_id},
        ),
        (
            "geometry",
            export_geo_asset,
            os.path.join(geo_dir, f"{geo_name}.geo"),
            {
                "skeleton_id": skeleton_id,
                "use_selection": use_selection,
            },
        ),
        (
            "animation",
            export_anim_asset,
            os.path.join(anim_dir, f"{asset_name}.anim"),
            {
                "skeleton_id": skeleton_id,
                "fps": fps,
                "clip_names": clip_names,
            },
        ),
    )

    if only:
        exports = tuple(entry for entry in exports if entry[0] in only)
        if not exports:
            result["status"] = "CANCELLED"
            result["error"] = "Nothing selected to export"
            return result

    failures = []

    def run_exports(labels, scene_pose_locked=False):
        for label, export_fn, filepath, kwargs in exports:
            if label not in labels:
                continue
            export_kwargs = dict(kwargs)
            if scene_pose_locked and label in ("skeleton", "geometry"):
                export_kwargs["scene_pose_locked"] = True
            export_result = export_fn(context, filepath, **export_kwargs)
            for warning in export_result.get("warnings", []):
                result["warnings"].append(f"{label}: {warning}")
            for info in export_result.get("info", []):
                result["info"].append(f"{label}: {info}")
            if export_result["status"] != "FINISHED":
                failures.append(f"{label}: {export_result.get('error', 'export failed')}")

    armature_obj = active_armature(context)
    if armature_obj is not None:
        with bind_pose_context(context, armature_obj):
            run_exports(("skeleton", "geometry"), scene_pose_locked=True)
        run_exports(("animation",))
    else:
        run_exports(("skeleton", "geometry", "animation"))

    if failures:
        result["status"] = "CANCELLED"
        result["error"] = "; ".join(failures)
        return result

    result["info"].append(f"Exported '{asset_name}' to {project_root}")
    return result


class EXPORT_OT_slopengine_multiple(bpy.types.Operator):
    bl_idname = "export_scene.slopengine_multiple"
    bl_label = "Export Slopengine Multiple"
    bl_description = (
        "Export skeleton, geometry, and animation for one asset into "
        "project geometry, skeletons, and animations folders"
    )
    bl_options = {"REGISTER", "UNDO"}

    project_root: bpy.props.StringProperty(
        name="Project Root",
        description="Base package directory containing geometry, skeletons, and animations",
        subtype="DIR_PATH",
        default="",
    )

    asset_name: bpy.props.StringProperty(
        name="Asset Name",
        description="Shared name for skeleton, geo, and anim exports",
        default="",
    )

    skeleton_name_source: bpy.props.EnumProperty(
        name="Skeleton Name",
        description="How to determine the skeleton id referenced by geo and anim files",
        items=(
            ("CUSTOM", "Custom", "Use the Skeleton ID field"),
            ("OBJECT", "Object Name", "Use the selected armature object name"),
        ),
        default="OBJECT",
    )

    skeleton_id: bpy.props.StringProperty(
        name="Skeleton ID",
        description="Identifier referenced by geo and anim files",
        default="",
    )

    fps: bpy.props.FloatProperty(
        name="FPS",
        description="Sample rate for exported animations",
        default=30.0,
        min=1.0,
    )

    use_selection: bpy.props.BoolProperty(
        name="Selection Only",
        description="Export all selected mesh objects into the geo file",
        default=True,
    )

    clips: bpy.props.StringProperty(
        name="Clips",
        description="Comma-separated action names to export; empty exports every action",
        default="",
    )

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    def draw(self, context):
        self.layout.prop(self, "project_root")
        self.layout.prop(self, "asset_name")
        self.layout.prop(self, "skeleton_name_source")
        if self.skeleton_name_source == "CUSTOM":
            self.layout.prop(self, "skeleton_id")
        self.layout.prop(self, "fps")
        self.layout.prop(self, "use_selection")
        self.layout.prop(self, "clips")

    def resolve_skeleton_id(self, context):
        if self.skeleton_name_source == "OBJECT":
            armature_obj = active_armature(context)
            if armature_obj is None:
                return ""
            return sanitize_name(armature_obj.name)
        return self.skeleton_id.strip()

    def execute(self, context):
        skeleton_id = self.resolve_skeleton_id(context)
        if not skeleton_id:
            if self.skeleton_name_source == "OBJECT":
                self.report({"ERROR"}, "Select an armature to use its object name")
            else:
                self.report({"ERROR"}, "Skeleton ID is required")
            return {"CANCELLED"}

        result = export_multiple_asset(
            context,
            self.project_root,
            self.asset_name,
            skeleton_id,
            fps=self.fps,
            use_selection=self.use_selection,
            clip_names=parse_clip_filter(self.clips),
        )
        for warning in result.get("warnings", []):
            self.report({"WARNING"}, warning)
        for info in result.get("info", []):
            self.report({"INFO"}, info)
        if result["status"] != "FINISHED":
            self.report({"ERROR"}, result.get("error", "Export failed"))
            return {"CANCELLED"}
        return {"FINISHED"}


classes = (
    EXPORT_OT_slopengine_multiple,
)
