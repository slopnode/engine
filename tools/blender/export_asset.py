"""Headless slopengine asset export.

    blender -b --python tools/blender/export_asset.py -- \
        --input source.blend|source.glb|source.fbx \
        --root path/to/package --asset name [--skeleton id] [--fps 30] \
        [--clips a,b,c] [--armature name] [--meshes a,b] \
        [--rename-material OLD=NEW ...]

Writes skeletons/<asset>/, geometry/<asset>/, and animations/<asset>/ under
--root, exactly like File > Export > Slopengine > Multiple does from the UI.
Blender is only the intermediary here: the source file is imported into an
empty scene and exported straight back out in the engine's own formats.
"""

import argparse
import os
import sys

import bpy

ADDON_DIR = os.path.dirname(os.path.abspath(__file__))
if ADDON_DIR not in sys.path:
    sys.path.insert(0, ADDON_DIR)

from slopengine_exporter.export_multiple import export_multiple_asset  # noqa: E402
from slopengine_exporter.format_utils import descendant_bone_names, trim_mesh_to_bones  # noqa: E402
from slopengine_exporter.format_utils import armature_from_mesh  # noqa: E402


def parse_args():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    else:
        argv = []

    parser = argparse.ArgumentParser(description="Export a slopengine asset headlessly")
    parser.add_argument("--input", required=True, help="Source .blend, .glb/.gltf, or .fbx")
    parser.add_argument("--root", required=True, help="Package root directory")
    parser.add_argument("--asset", required=True, help="Asset name shared by skel/geo/anim")
    parser.add_argument("--skeleton", default="", help="Skeleton id (defaults to asset name)")
    parser.add_argument("--fps", type=float, default=30.0, help="Animation sample rate")
    parser.add_argument("--clips", default="", help="Comma-separated action names to export")
    parser.add_argument("--armature", default="", help="Armature object name to export")
    parser.add_argument("--meshes", default="", help="Comma-separated mesh object names")
    parser.add_argument(
        "--bones",
        default="",
        help="Keep only geometry weighted to these bones and their children, e.g. a hands rig",
    )
    parser.add_argument(
        "--only",
        default="",
        help="Comma-separated subset of skeleton, geometry, animation to write",
    )
    parser.add_argument("--geo-name", default="", help="File name for the .geo (defaults to the asset name)")
    parser.add_argument(
        "--rename-material",
        action="append",
        default=[],
        metavar="OLD=NEW",
        help="Rename a Blender material before export so it maps to a package material path",
    )
    return parser.parse_args(argv)


def load_source(path):
    ext = os.path.splitext(path)[1].lower()
    if ext == ".blend":
        bpy.ops.wm.open_mainfile(filepath=path)
        return
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if ext in (".glb", ".gltf"):
        bpy.ops.import_scene.gltf(filepath=path)
    elif ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=path)
    else:
        raise SystemExit(f"Unsupported input '{path}'")


def pick_armature(name):
    if name:
        obj = bpy.data.objects.get(name)
        if obj is None or obj.type != "ARMATURE":
            raise SystemExit(f"Armature '{name}' not found")
        return obj
    for obj in bpy.data.objects:
        if obj.type == "ARMATURE":
            return obj
    return None


def pick_meshes(names, armature_obj):
    if names:
        meshes = []
        for name in names:
            obj = bpy.data.objects.get(name)
            if obj is None or obj.type != "MESH":
                raise SystemExit(f"Mesh '{name}' not found")
            meshes.append(obj)
        return meshes
    meshes = [
        obj
        for obj in bpy.data.objects
        if obj.type == "MESH" and armature_obj is not None and armature_from_mesh(obj) == armature_obj
    ]
    if not meshes:
        meshes = [obj for obj in bpy.data.objects if obj.type == "MESH"]
    return meshes


def rename_materials(specs):
    for spec in specs:
        if "=" not in spec:
            raise SystemExit(f"--rename-material expects OLD=NEW, got '{spec}'")
        old, new = spec.split("=", 1)
        material = bpy.data.materials.get(old)
        if material is None:
            print(f"WARNING: material '{old}' not found")
            continue
        material.name = new


def main():
    args = parse_args()
    load_source(os.path.abspath(args.input))
    rename_materials(args.rename_material)

    armature_obj = pick_armature(args.armature)
    mesh_names = [name.strip() for name in args.meshes.split(",") if name.strip()]
    meshes = pick_meshes(mesh_names, armature_obj)
    if not meshes:
        raise SystemExit("No mesh objects to export")

    bone_names = [name.strip() for name in args.bones.split(",") if name.strip()]
    if bone_names:
        if armature_obj is None:
            raise SystemExit("--bones needs an armature")
        keep = descendant_bone_names(armature_obj.data, bone_names)
        for obj in meshes:
            removed = trim_mesh_to_bones(obj, keep)
            print(f"INFO: {obj.name}: kept {len(obj.data.vertices)} vertices on {len(keep)} bones, dropped {removed}")

    view_layer = bpy.context.view_layer
    for obj in bpy.data.objects:
        obj.select_set(False)
    for obj in meshes:
        obj.select_set(True)
    if armature_obj is not None:
        armature_obj.select_set(True)
        view_layer.objects.active = armature_obj
    else:
        view_layer.objects.active = meshes[0]
    bpy.ops.object.mode_set(mode="OBJECT")

    clip_names = [name.strip() for name in args.clips.split(",") if name.strip()] or None
    result = export_multiple_asset(
        bpy.context,
        args.root,
        args.asset,
        args.skeleton or args.asset,
        fps=args.fps,
        use_selection=True,
        clip_names=clip_names,
        only=[name.strip() for name in args.only.split(",") if name.strip()] or None,
        geo_name=args.geo_name or None,
    )
    for warning in result["warnings"]:
        print(f"WARNING: {warning}")
    for info in result["info"]:
        print(f"INFO: {info}")
    if result["status"] != "FINISHED":
        print(f"ERROR: {result['error']}")
        sys.exit(1)


if __name__ == "__main__":
    main()
