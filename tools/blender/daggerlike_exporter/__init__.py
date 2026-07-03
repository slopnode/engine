bl_info = {
    "name": "Daggerlike Exporter",
    "author": "Daggerlike",
    "version": (0, 1, 0),
    "blender": (4, 0, 0),
    "location": "File > Export > Daggerlike",
    "description": "Export daggerlike skeleton, geo, and anim assets",
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


class TOPBAR_MT_file_export_daggerlike(bpy.types.Menu):
    bl_idname = "TOPBAR_MT_file_export_daggerlike"
    bl_label = "Daggerlike"

    def draw(self, context):
        self.layout.operator(
            export_multiple.EXPORT_OT_daggerlike_multiple.bl_idname,
            text="Multiple",
            icon="EXPORT",
        )
        self.layout.separator()
        self.layout.operator(
            export_geo.EXPORT_OT_daggerlike_geo.bl_idname,
            text="Geometry",
            icon="MESH_DATA",
        )
        self.layout.operator(
            export_anim.EXPORT_OT_daggerlike_anim.bl_idname,
            text="Animation",
            icon="ANIM",
        )
        self.layout.operator(
            export_skeleton.EXPORT_OT_daggerlike_skeleton.bl_idname,
            text="Skeleton",
            icon="ARMATURE_DATA",
        )


def menu_func_export(self, context):
    self.layout.menu(
        TOPBAR_MT_file_export_daggerlike.bl_idname,
        text="Daggerlike",
        icon="EXPORT",
    )


def register():
    for module in modules:
        for cls in module.classes:
            bpy.utils.register_class(cls)
    bpy.utils.register_class(TOPBAR_MT_file_export_daggerlike)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(TOPBAR_MT_file_export_daggerlike)
    for module in reversed(modules):
        for cls in reversed(module.classes):
            bpy.utils.unregister_class(cls)
