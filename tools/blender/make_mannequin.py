"""Headless rigid mannequin generator.

    blender -b --python tools/blender/make_mannequin.py -- \
        --name grunt --preset HEAVY [--set limb_thickness=1.6 ...] \
        [--sides 8] [--no-hitboxes] [--no-uv] [--tight-pack] \
        [--fused] \
        [--skeleton id] [--geo-name name] [--material path] [--face-material path] \
        [--output grunt.blend] [--root path/to/package]

Builds the same mannequin as Add > Slopengine Mannequin, segmented and
rigidly weighted or with --fused as one closed skin, saves it to --output,
and with --root also exports the skeleton and geometry into that package.
"""

import argparse
import os
import sys

import bpy

ADDON_DIR = os.path.dirname(os.path.abspath(__file__))
if ADDON_DIR not in sys.path:
    sys.path.insert(0, ADDON_DIR)

from slopengine_exporter.export_multiple import export_multiple_asset  # noqa: E402
from slopengine_exporter.mannequin import BUILD_FIELDS, PRESETS, create_mannequin, preset_values  # noqa: E402


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description="Generate a rigid slopengine mannequin")
    parser.add_argument("--name", default="mannequin", help="Armature name, skeleton id and asset name")
    parser.add_argument("--preset", default="AVERAGE", choices=sorted(PRESETS))
    parser.add_argument(
        "--set",
        action="append",
        default=[],
        metavar="FIELD=VALUE",
        help=f"Override a build value: {', '.join(field[0] for field in BUILD_FIELDS)}",
    )
    parser.add_argument("--sides", type=int, default=8)
    parser.add_argument("--no-hitboxes", action="store_true")
    parser.add_argument("--no-uv", action="store_true")
    parser.add_argument("--tight-pack", action="store_true", help="Pack UV charts by exact shape; slower")
    parser.add_argument("--fused", action="store_true", help="One closed quad skin with blended joint weights")
    parser.add_argument("--skeleton", default="", help="Existing skeleton id and folder to export the geometry into; skips the skeleton export")
    parser.add_argument("--geo-name", default="", help="Name of the exported .geo, defaults to --name")
    parser.add_argument("--material", default="", help="Material path of the body, defaults to <name>/skin")
    parser.add_argument("--face-material", default="", help="Give the fused skin's neck and head their own material and UV space")
    parser.add_argument("--output", default="", help="Save the generated scene as this .blend")
    parser.add_argument("--root", default="", help="Package root to export skeletons/ and geometry/ into")
    return parser.parse_args(argv)


def main():
    args = parse_args()
    values = preset_values(args.preset)
    for spec in args.set:
        key, _, text = spec.partition("=")
        if key not in values or not text:
            raise SystemExit(f"--set expects FIELD=VALUE with a known field, got '{spec}'")
        values[key] = float(text)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    armature_obj, mesh_obj, hitboxes = create_mannequin(
        bpy.context,
        args.name,
        values,
        sides=args.sides,
        hitboxes=not args.no_hitboxes,
        uv_unwrap=not args.no_uv,
        tight_uv=args.tight_pack,
        fused=args.fused,
        material_name=args.material,
        face_material_name=args.face_material,
    )
    print(
        f"INFO: built {armature_obj.name}: {len(armature_obj.data.bones)} bones, "
        f"{len(mesh_obj.data.vertices)} vertices, {len(mesh_obj.data.polygons)} faces, {len(hitboxes)} hitboxes"
    )

    if args.output:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.output))
        print(f"INFO: saved {args.output}")

    if args.root:
        for obj in bpy.data.objects:
            obj.select_set(False)
        mesh_obj.select_set(True)
        armature_obj.select_set(True)
        bpy.context.view_layer.objects.active = armature_obj
        result = export_multiple_asset(
            bpy.context,
            os.path.abspath(args.root),
            args.skeleton or args.name,
            args.skeleton or args.name,
            use_selection=True,
            only=["geometry"] if args.skeleton else ["skeleton", "geometry"],
            geo_name=args.geo_name or args.name,
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
