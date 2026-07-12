bl_info = {
    "name": "Slopengine Exporter",
    "author": "Slopengine",
    "version": (0, 1, 0),
    "blender": (4, 0, 0),
    "location": "File > Export > Slopengine",
    "description": "Export slopengine skeleton, geo, and anim assets",
    "category": "Import-Export",
}

import bpy

from . import export_anim, export_geo, export_multiple, export_skeleton

modules = (
    export_skeleton,
    export_geo,
    export_anim,
    export_multiple,
)


class TOPBAR_MT_file_export_slopengine(bpy.types.Menu):
    bl_idname = "TOPBAR_MT_file_export_slopengine"
    bl_label = "Slopengine"

    def draw(self, context):
        self.layout.operator(
            export_multiple.EXPORT_OT_slopengine_multiple.bl_idname,
            text="Multiple",
            icon="EXPORT",
        )
        self.layout.separator()
        self.layout.operator(
            export_geo.EXPORT_OT_slopengine_geo.bl_idname,
            text="Geometry",
            icon="MESH_DATA",
        )
        self.layout.operator(
            export_anim.EXPORT_OT_slopengine_anim.bl_idname,
            text="Animation",
            icon="ANIM",
        )
        self.layout.operator(
            export_skeleton.EXPORT_OT_slopengine_skeleton.bl_idname,
            text="Skeleton",
            icon="ARMATURE_DATA",
        )


def menu_func_export(self, context):
    self.layout.menu(
        TOPBAR_MT_file_export_slopengine.bl_idname,
        text="Slopengine",
        icon="EXPORT",
    )


def register():
    for module in modules:
        for cls in module.classes:
            bpy.utils.register_class(cls)
    bpy.utils.register_class(TOPBAR_MT_file_export_slopengine)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(TOPBAR_MT_file_export_slopengine)
    for module in reversed(modules):
        for cls in reversed(module.classes):
            bpy.utils.unregister_class(cls)
