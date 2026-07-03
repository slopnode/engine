import os

import bpy

from .export_anim import export_anim_asset
from .export_geo import export_geo_asset
from .export_skeleton import export_skeleton_asset
from .format_utils import active_armature, bind_pose_context, sanitize_name


class EXPORT_OT_daggerlike_multiple(bpy.types.Operator):
    bl_idname = "export_scene.daggerlike_multiple"
    bl_label = "Export Daggerlike Multiple"
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

    def resolve_skeleton_id(self, context):
        if self.skeleton_name_source == "OBJECT":
            armature_obj = active_armature(context)
            if armature_obj is None:
                return ""
            return sanitize_name(armature_obj.name)
        return self.skeleton_id.strip()

    def execute(self, context):
        project_root = bpy.path.abspath(self.project_root).strip()
        if not project_root:
            self.report({"ERROR"}, "Project root is required")
            return {"CANCELLED"}

        asset_name = sanitize_name(self.asset_name.strip())
        if not asset_name:
            self.report({"ERROR"}, "Asset name is required")
            return {"CANCELLED"}

        skeleton_id = self.resolve_skeleton_id(context)
        if not skeleton_id:
            if self.skeleton_name_source == "OBJECT":
                self.report({"ERROR"}, "Select an armature to use its object name")
            else:
                self.report({"ERROR"}, "Skeleton ID is required")
            return {"CANCELLED"}

        skel_dir = os.path.join(project_root, "skeletons", asset_name)
        geo_dir = os.path.join(project_root, "geometry", asset_name)
        anim_dir = os.path.join(project_root, "animations", asset_name)

        for output_dir in (skel_dir, geo_dir, anim_dir):
            os.makedirs(output_dir, exist_ok=True)

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
                os.path.join(geo_dir, f"{asset_name}.geo"),
                {
                    "skeleton_id": skeleton_id,
                    "use_selection": self.use_selection,
                },
            ),
            (
                "animation",
                export_anim_asset,
                os.path.join(anim_dir, f"{asset_name}.anim"),
                {
                    "skeleton_id": skeleton_id,
                    "fps": self.fps,
                },
            ),
        )

        failures = []

        def run_exports(scene_pose_locked=False):
            nonlocal failures
            for label, export_fn, filepath, kwargs in exports:
                export_kwargs = dict(kwargs)
                if scene_pose_locked and label in ("skeleton", "geometry"):
                    export_kwargs["scene_pose_locked"] = True
                result = export_fn(context, filepath, **export_kwargs)
                for warning in result.get("warnings", []):
                    self.report({"WARNING"}, f"{label}: {warning}")
                for info in result.get("info", []):
                    self.report({"INFO"}, f"{label}: {info}")
                if result["status"] != "FINISHED":
                    failures.append(f"{label}: {result.get('error', 'export failed')}")

        armature_obj = active_armature(context)
        if armature_obj is not None:
            with bind_pose_context(context, armature_obj):
                run_exports(scene_pose_locked=True)
        else:
            run_exports()

        if failures:
            for failure in failures:
                self.report({"ERROR"}, failure)
            return {"CANCELLED"}

        self.report(
            {"INFO"},
            f"Exported '{asset_name}' to {project_root}",
        )
        return {"FINISHED"}


classes = (
    EXPORT_OT_daggerlike_multiple,
)
