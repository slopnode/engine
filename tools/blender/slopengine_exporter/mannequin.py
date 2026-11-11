import math

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.kdtree import KDTree

from .autouv import OBJECT_OT_slopengine_auto_uv, auto_unwrap

BUILD_FIELDS = (
    ("height", "Height", "Standing height in meters", 1.8, 0.3, 5.0),
    ("head_size", "Head Size", "Head scale", 1.0, 0.5, 2.0),
    ("neck_length", "Neck Length", "Neck length scale", 1.0, 0.3, 3.0),
    ("torso_length", "Torso Length", "Pelvis to neck length scale", 1.0, 0.5, 2.0),
    ("shoulder_width", "Shoulder Width", "Shoulder joint spacing scale", 1.0, 0.5, 2.0),
    ("chest_width", "Chest Width", "Chest bulk scale", 1.0, 0.5, 2.5),
    ("belly_width", "Belly Width", "Abdomen bulk scale", 1.0, 0.5, 2.5),
    ("hip_width", "Hip Width", "Hip joint spacing and pelvis bulk scale", 1.0, 0.5, 2.0),
    ("torso_depth", "Torso Depth", "Front to back thickness of the torso", 1.0, 0.5, 2.0),
    ("arm_length", "Arm Length", "Arm length scale", 1.0, 0.5, 2.0),
    ("leg_length", "Leg Length", "Leg length scale", 1.0, 0.5, 2.0),
    ("limb_thickness", "Limb Thickness", "Arm, leg and neck girth scale", 1.0, 0.4, 2.5),
    ("hand_size", "Hand Size", "Hand scale", 1.0, 0.5, 2.0),
    ("foot_size", "Foot Size", "Foot scale", 1.0, 0.5, 2.0),
)

PRESETS = {
    "AVERAGE": ("Average", "Plain adult build", {}),
    "HEAVY": (
        "Heavy",
        "Broad, thick-limbed bruiser",
        {
            "height": 1.9,
            "neck_length": 0.6,
            "shoulder_width": 1.25,
            "chest_width": 1.35,
            "belly_width": 1.3,
            "hip_width": 1.15,
            "torso_depth": 1.3,
            "limb_thickness": 1.45,
            "hand_size": 1.15,
            "foot_size": 1.1,
        },
    ),
    "SLIM": (
        "Slim",
        "Narrow, light build",
        {
            "height": 1.75,
            "shoulder_width": 0.9,
            "chest_width": 0.85,
            "belly_width": 0.8,
            "hip_width": 0.95,
            "torso_depth": 0.85,
            "limb_thickness": 0.8,
        },
    ),
    "SHORT": (
        "Short",
        "Short, stocky build with a big head",
        {
            "height": 1.3,
            "head_size": 1.25,
            "neck_length": 0.6,
            "torso_length": 1.05,
            "shoulder_width": 1.15,
            "chest_width": 1.25,
            "belly_width": 1.3,
            "hip_width": 1.15,
            "torso_depth": 1.2,
            "arm_length": 0.85,
            "leg_length": 0.75,
            "limb_thickness": 1.35,
            "hand_size": 1.15,
            "foot_size": 1.1,
        },
    ),
    "TALL": (
        "Tall",
        "Long-limbed, lanky build",
        {
            "height": 2.2,
            "head_size": 0.85,
            "neck_length": 1.6,
            "torso_length": 1.1,
            "chest_width": 0.85,
            "belly_width": 0.75,
            "hip_width": 0.85,
            "torso_depth": 0.8,
            "arm_length": 1.25,
            "leg_length": 1.2,
            "limb_thickness": 0.7,
            "hand_size": 1.2,
            "foot_size": 1.05,
        },
    ),
}

BONE_PARENTS = (
    ("root", None),
    ("pelvis", "root"),
    ("spine", "pelvis"),
    ("chest", "spine"),
    ("neck", "chest"),
    ("head", "neck"),
    ("clavicle.L", "chest"),
    ("upperarm.L", "clavicle.L"),
    ("forearm.L", "upperarm.L"),
    ("hand.L", "forearm.L"),
    ("clavicle.R", "chest"),
    ("upperarm.R", "clavicle.R"),
    ("forearm.R", "upperarm.R"),
    ("hand.R", "forearm.R"),
    ("thigh.L", "pelvis"),
    ("calf.L", "thigh.L"),
    ("foot.L", "calf.L"),
    ("thigh.R", "pelvis"),
    ("calf.R", "thigh.R"),
    ("foot.R", "calf.R"),
)

HITBOX_RULES = {
    "head": ("head", 3.0, True),
    "neck": ("head", 3.0, False),
    "chest": ("chest", 1.0, False),
    "spine": ("stomach", 1.0, False),
    "pelvis": ("stomach", 1.0, False),
    "upperarm": ("arm", 1.0, True),
    "forearm": ("arm", 1.0, True),
    "hand": ("arm", 1.0, True),
    "thigh": ("leg", 1.0, True),
    "calf": ("leg", 1.0, True),
    "foot": ("leg", 1.0, True),
}

HITBOX_PREFIX = "hbox."


def build_values(overrides=None):
    values = {field[0]: field[3] for field in BUILD_FIELDS}
    values.update(overrides or {})
    return values


def preset_values(preset):
    return build_values(PRESETS[preset][2])


def hitbox_rule(bone_name):
    base, _, side = bone_name.partition(".")
    rule = HITBOX_RULES.get(base)
    if rule is None:
        return "generic", 1.0, False
    group, damage, severable = rule
    if group in ("arm", "leg"):
        group = f"{'left' if side == 'L' else 'right'}_{group}"
    return group, damage, severable


IK_PREBEND = 0.04


def layout(values):
    v = values
    tl = v["torso_length"]
    ankle_z = 0.045 * v["foot_size"]
    hip_z = 0.50 * v["leg_length"]
    thigh_len = (hip_z - ankle_z) * 0.52
    knee_z = hip_z - thigh_len
    pelvis_top = hip_z + 0.05 * tl
    chest_z = pelvis_top + 0.10 * tl
    neck_z = chest_z + 0.15 * tl
    head_z = neck_z + 0.05 * v["neck_length"]
    top_z = head_z + 0.13 * v["head_size"]
    shoulder_z = neck_z - 0.025
    shoulder_x = 0.115 * v["shoulder_width"]
    hip_x = 0.055 * v["hip_width"]
    upper_len = 0.165 * v["arm_length"]
    fore_len = 0.145 * v["arm_length"]
    hand_len = 0.10 * v["hand_size"]
    heel_y = 0.035 * v["foot_size"]
    toe_y = -0.115 * v["foot_size"]

    joints = {
        "root": (Vector((0, 0, 0)), Vector((0, 0.1, 0))),
        "pelvis": (Vector((0, 0, hip_z)), Vector((0, 0, pelvis_top))),
        "spine": (Vector((0, 0, pelvis_top)), Vector((0, 0, chest_z))),
        "chest": (Vector((0, 0, chest_z)), Vector((0, 0, neck_z))),
        "neck": (Vector((0, 0, neck_z)), Vector((0, 0, head_z))),
        "head": (Vector((0, 0, head_z)), Vector((0, 0, top_z))),
    }
    for side, sx in (("L", 1.0), ("R", -1.0)):
        shoulder = Vector((sx * shoulder_x, 0, shoulder_z))
        elbow = shoulder + Vector((sx * upper_len, IK_PREBEND * upper_len, 0))
        wrist = shoulder + Vector((sx * (upper_len + fore_len), 0, 0))
        fingertips = wrist + Vector((sx * hand_len, 0, 0))
        joints[f"clavicle.{side}"] = (Vector((sx * 0.02, 0, shoulder_z)), shoulder)
        joints[f"upperarm.{side}"] = (shoulder, elbow)
        joints[f"forearm.{side}"] = (elbow, wrist)
        joints[f"hand.{side}"] = (wrist, fingertips)
        hip = Vector((sx * hip_x, 0, hip_z))
        knee = Vector((sx * hip_x, -IK_PREBEND * thigh_len, knee_z))
        ankle = Vector((sx * hip_x, 0, ankle_z))
        joints[f"thigh.{side}"] = (hip, knee)
        joints[f"calf.{side}"] = (knee, ankle)
        joints[f"foot.{side}"] = (ankle, Vector((sx * hip_x, toe_y, 0.015)))
    return joints, top_z, heel_y, toe_y


def segment_specs(values, joints, heel_y, toe_y):
    v = values
    t = v["limb_thickness"]
    depth = v["torso_depth"]
    up = Vector((0, 0, 1))
    down = Vector((0, 0, -1))
    x_axis = Vector((1, 0, 0))
    y_axis = Vector((0, 1, 0))
    z_axis = Vector((0, 0, 1))
    back = Vector((0, 1, 0))

    def length(bone):
        head, tail = joints[bone]
        return (tail - head).length

    specs = []

    def add(bone, start, axis, u, w, rings, seam, box=False):
        specs.append(
            {
                "bone": bone,
                "start": start,
                "axis": axis,
                "u": u,
                "v": w,
                "rings": rings,
                "seam": seam,
                "box": box,
            }
        )

    hip_bulk = 0.1 * v["hip_width"]
    belly = 0.082 * v["belly_width"]
    chest_w = 0.10 * v["chest_width"]
    shoulder_x = joints["upperarm.L"][0].x

    lp = length("pelvis")
    add(
        "pelvis",
        joints["pelvis"][0],
        up,
        x_axis,
        y_axis,
        [
            (-0.06, hip_bulk * 0.62, 0.05 * depth, 0, 0),
            (-0.02, hip_bulk, 0.068 * depth, 0, 0),
            (lp, belly * 1.04, 0.062 * depth, 0, 0),
        ],
        back,
    )
    ls = length("spine")
    add(
        "spine",
        joints["spine"][0],
        up,
        x_axis,
        y_axis,
        [
            (-0.004, belly, 0.058 * depth, 0, 0),
            (ls * 0.5, belly * 0.98, 0.06 * depth, 0, -0.002),
            (ls + 0.004, max(belly, chest_w * 0.9), 0.062 * depth, 0, 0),
        ],
        back,
    )
    lc = length("chest")
    add(
        "chest",
        joints["chest"][0],
        up,
        x_axis,
        y_axis,
        [
            (0.0, max(belly, chest_w * 0.9), 0.064 * depth, 0, 0),
            (lc * 0.5, chest_w * 1.05, 0.075 * depth, 0, -0.006 * depth),
            (lc * 0.85, max(chest_w * 1.1, shoulder_x * 0.95), 0.068 * depth, 0, -0.002),
            (lc + 0.004, 0.075 * v["chest_width"], 0.05 * depth, 0, 0.004),
        ],
        back,
    )
    ln = length("neck")
    nr = 0.03 * (0.5 + 0.5 * t)
    add(
        "neck",
        joints["neck"][0],
        up,
        x_axis,
        y_axis,
        [(-0.015, nr * 1.1, nr * 1.1, 0, 0.004), (ln + 0.015, nr, nr, 0, 0.002)],
        back,
    )
    lh = length("head")
    hs = v["head_size"]
    add(
        "head",
        joints["head"][0],
        up,
        x_axis,
        y_axis,
        [
            (lh * -0.04, 0.032 * hs, 0.034 * hs, 0, -0.006 * hs),
            (lh * 0.12, 0.052 * hs, 0.06 * hs, 0, -0.012 * hs),
            (lh * 0.45, 0.066 * hs, 0.076 * hs, 0, -0.004 * hs),
            (lh * 0.78, 0.064 * hs, 0.074 * hs, 0, 0.002 * hs),
            (lh * 0.95, 0.044 * hs, 0.052 * hs, 0, 0.004 * hs),
            (lh, 0.018 * hs, 0.022 * hs, 0, 0.004 * hs),
        ],
        back,
    )

    for side, sx in (("L", 1.0), ("R", -1.0)):
        out = Vector((sx, 0, 0))
        lu = length(f"upperarm.{side}")
        add(
            f"upperarm.{side}",
            joints[f"upperarm.{side}"][0],
            out,
            y_axis,
            z_axis,
            [
                (-0.03, 0.036 * t, 0.038 * t, 0, 0.002),
                (lu * 0.3, 0.033 * t, 0.034 * t, 0, 0),
                (lu + 0.006, 0.025 * t, 0.025 * t, 0, 0),
            ],
            down,
        )
        lf = length(f"forearm.{side}")
        add(
            f"forearm.{side}",
            joints[f"forearm.{side}"][0],
            out,
            y_axis,
            z_axis,
            [
                (-0.022, 0.025 * t, 0.025 * t, 0, 0),
                (lf * 0.25, 0.028 * t, 0.027 * t, 0, 0),
                (lf + 0.004, 0.019 * t, 0.017 * t, 0, 0),
            ],
            down,
        )
        lw = length(f"hand.{side}")
        hz = v["hand_size"]
        add(
            f"hand.{side}",
            joints[f"hand.{side}"][0],
            out,
            y_axis,
            z_axis,
            [
                (-0.008, 0.02 * hz, 0.013 * hz, 0, 0),
                (lw * 0.5, 0.026 * hz, 0.012 * hz, 0, 0),
                (lw, 0.021 * hz, 0.007 * hz, 0, -0.002 * hz),
            ],
            y_axis,
            box=True,
        )
        lt = length(f"thigh.{side}")
        add(
            f"thigh.{side}",
            joints[f"thigh.{side}"][0],
            down,
            x_axis,
            y_axis,
            [
                (-0.035, 0.046 * t, 0.058 * t, -sx * 0.004, 0),
                (lt * 0.3, 0.047 * t, 0.054 * t, 0, -0.003 * t),
                (lt + 0.005, 0.036 * t, 0.038 * t, 0, 0),
            ],
            Vector((-sx, 0, 0)),
        )
        lk = length(f"calf.{side}")
        add(
            f"calf.{side}",
            joints[f"calf.{side}"][0],
            down,
            x_axis,
            y_axis,
            [
                (-0.012, 0.037 * t, 0.038 * t, 0, 0),
                (lk * 0.3, 0.04 * t, 0.043 * t, 0, 0.008 * t),
                (lk + 0.012, 0.025 * t, 0.026 * t, 0, 0.002),
            ],
            Vector((-sx, 0, 0)),
        )
        fs = v["foot_size"]
        ankle = joints[f"foot.{side}"][0]
        heel = Vector((ankle.x, heel_y, 0.0))
        foot_len = heel_y - toe_y
        fz = ankle.z + 0.01
        add(
            f"foot.{side}",
            heel,
            Vector((0, -1, 0)),
            x_axis,
            z_axis,
            [
                (0.0, 0.027 * fs * t ** 0.5, fz * 0.45, 0, fz * 0.45),
                (foot_len * 0.3, 0.031 * fs * t ** 0.5, fz * 0.55, 0, fz * 0.55),
                (foot_len * 0.75, 0.036 * fs * t ** 0.5, fz * 0.28, 0, fz * 0.28),
                (foot_len, 0.03 * fs * t ** 0.5, fz * 0.16, 0, fz * 0.16),
            ],
            down,
            box=True,
        )
    return specs


HINGE_BONES = ("forearm.L", "forearm.R", "calf.L", "calf.R")
HINGE_MAX_DEGREES = 150.0

ROLL_HINTS = {
    "root": Vector((0, 0, 1)),
    "clavicle": Vector((0, 0, 1)),
    "hand": Vector((0, 0, -1)),
    "calf": Vector((0, 1, 0)),
    "foot": Vector((0, 0, 1)),
}

def ring_point(a, ru, rv, box, stretch):
    c, s = math.cos(a), math.sin(a)
    if box:
        k = 1.0 / max(abs(c), abs(s))
        return c * k * ru, s * k * rv
    return c * ru * stretch, s * rv * stretch


def build_segment(bm, spec, scale, deform_layer, group_index):
    n = spec["sides"]
    box = spec["box"]
    stretch = 1.0 / math.cos(math.pi / n)
    axis = spec["axis"].normalized()
    u = spec["u"].normalized()
    w = spec["v"].normalized()
    seam = spec["seam"].normalized()
    start_angle = math.atan2(seam.dot(w), seam.dot(u))
    rings = []
    for d, ru, rv, du, dv in spec["rings"]:
        center = spec["start"] + axis * d + u * du + w * dv
        ring = []
        for k in range(n):
            pu, pv = ring_point(start_angle + 2 * math.pi * k / n, ru, rv, box, stretch)
            vert = bm.verts.new((center + u * pu + w * pv) * scale)
            vert[deform_layer][group_index] = 1.0
            ring.append(vert)
        rings.append(ring)
    faces = []
    for lower, upper in zip(rings, rings[1:]):
        for k in range(n):
            k1 = (k + 1) % n
            face = bm.faces.new((lower[k], lower[k1], upper[k1], upper[k]))
            face.smooth = True
            faces.append(face)
    for ring in (rings[0], list(reversed(rings[-1]))):
        cap = bm.faces.new(ring)
        cap.smooth = False
        faces.append(cap)
        for edge in cap.edges:
            edge.smooth = False
    bmesh.ops.recalc_face_normals(bm, faces=faces)


IK_CHAINS = (
    ("forearm", "upperarm", "hand", "ik_hand", "pole_elbow", Vector((0, 1, 0))),
    ("calf", "thigh", "foot", "ik_foot", "pole_knee", Vector((0, -1, 0))),
)
IK_COLLECTION = "IK"
DEFORM_COLLECTION = "Deform"


def roll_hint(name):
    return ROLL_HINTS.get(name.partition(".")[0], Vector((0, -1, 0)))


def ensure_collection(context, name, parent=None):
    collection = bpy.data.collections.get(name)
    if collection is None:
        collection = bpy.data.collections.new(name)
        (parent or context.scene.collection).children.link(collection)
    return collection


def add_hinges(armature_obj):
    for bone_name in HINGE_BONES:
        pose_bone = armature_obj.pose.bones.get(bone_name)
        if pose_bone is None:
            continue
        pose_bone.rotation_mode = "XYZ"
        pose_bone.lock_rotation = (False, True, True)
        limit = pose_bone.constraints.new("LIMIT_ROTATION")
        limit.name = "Hinge"
        limit.owner_space = "LOCAL"
        limit.use_limit_x = limit.use_limit_y = limit.use_limit_z = True
        limit.min_x = 0.0
        limit.max_x = math.radians(HINGE_MAX_DEGREES)
        limit.use_transform_limit = True


def build_armature(context, name, joints, scale, collection):
    data = bpy.data.armatures.new(f"{name}_rig")
    data.display_type = "STICK"
    obj = bpy.data.objects.new(name, data)
    obj.show_in_front = True
    collection.objects.link(obj)
    previous_active = context.view_layer.objects.active
    context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    for bone_name, parent_name in BONE_PARENTS:
        head, tail = joints[bone_name]
        bone = data.edit_bones.new(bone_name)
        bone.head = head * scale
        bone.tail = tail * scale
        bone.align_roll(roll_hint(bone_name))
        bone.use_deform = True
        if parent_name:
            bone.parent = data.edit_bones[parent_name]
            bone.use_connect = (data.edit_bones[parent_name].tail - bone.head).length < 1e-6
    add_ik_bones(data)
    bpy.ops.object.mode_set(mode="OBJECT")
    context.view_layer.objects.active = previous_active
    add_ik_constraints(obj)
    add_hinges(obj)
    calibrate_poles(context, obj)
    return obj


def add_ik_bones(data):
    deform = data.collections.new(DEFORM_COLLECTION)
    controls = data.collections.new(IK_COLLECTION)
    for bone in data.edit_bones:
        deform.assign(bone)
    root = data.edit_bones["root"]
    for side in ("L", "R"):
        for lower, upper, end, target, pole, direction in IK_CHAINS:
            end_bone = data.edit_bones[f"{end}.{side}"]
            upper_bone = data.edit_bones[f"{upper}.{side}"]
            lower_bone = data.edit_bones[f"{lower}.{side}"]
            reach = upper_bone.length + lower_bone.length
            target_bone = data.edit_bones.new(f"{target}.{side}")
            target_bone.head = end_bone.head.copy()
            target_bone.tail = end_bone.tail.copy()
            target_bone.roll = end_bone.roll
            target_bone.use_deform = False
            target_bone.parent = root
            controls.assign(target_bone)
            pole_bone = data.edit_bones.new(f"{pole}.{side}")
            pole_bone.head = lower_bone.head + direction * reach * 0.6
            pole_bone.tail = pole_bone.head + direction * reach * 0.15
            pole_bone.use_deform = False
            pole_bone.parent = root
            controls.assign(pole_bone)


def add_ik_constraints(armature_obj):
    pose = armature_obj.pose.bones
    for side in ("L", "R"):
        for lower, upper, end, target, pole, direction in IK_CHAINS:
            ik = pose[f"{lower}.{side}"].constraints.new("IK")
            ik.name = "IK"
            ik.target = armature_obj
            ik.subtarget = f"{target}.{side}"
            ik.pole_target = armature_obj
            ik.pole_subtarget = f"{pole}.{side}"
            ik.chain_count = 2
            ik.use_stretch = False
            follow = pose[f"{end}.{side}"].constraints.new("COPY_ROTATION")
            follow.name = "IK Rotation"
            follow.target = armature_obj
            follow.subtarget = f"{target}.{side}"
            hinge = pose[f"{lower}.{side}"]
            hinge.lock_ik_y = hinge.lock_ik_z = True
            hinge.use_ik_limit_x = True
            hinge.ik_min_x = 0.0
            hinge.ik_max_x = math.radians(HINGE_MAX_DEGREES)


def chain_error(context, armature_obj, names):
    context.view_layer.update()
    error = 0.0
    for name in names:
        posed = armature_obj.pose.bones[name].matrix.to_3x3()
        rest = armature_obj.data.bones[name].matrix_local.to_3x3()
        error += sum((posed[r][c] - rest[r][c]) ** 2 for r in range(3) for c in range(3))
    return error


def calibrate_poles(context, armature_obj):
    for side in ("L", "R"):
        for lower, upper, *_ in IK_CHAINS:
            ik = armature_obj.pose.bones[f"{lower}.{side}"].constraints["IK"]
            names = (f"{upper}.{side}", f"{lower}.{side}")

            def error_at(degrees):
                ik.pole_angle = math.radians(degrees)
                return chain_error(context, armature_obj, names)

            best = min(range(-180, 180, 15), key=error_at)
            best = min((best + step * 0.5 for step in range(-30, 31)), key=error_at)
            ik.pole_angle = math.radians(best)
    context.view_layer.update()


def ensure_material(name):
    material = bpy.data.materials.get(name)
    if material is None:
        material = bpy.data.materials.new(name)
        material.diffuse_color = (0.62, 0.6, 0.56, 1.0)
    return material


def build_mesh(
    context,
    name,
    armature_obj,
    specs,
    scale,
    collection,
    material_name,
    uv_unwrap=True,
    tight_uv=False,
    skin=None,
    face_material_name="",
):
    mesh = bpy.data.meshes.new(f"{name}_body")
    obj = bpy.data.objects.new(f"{name}_body", mesh)
    collection.objects.link(obj)
    group_names = [bone.name for bone in armature_obj.data.bones]
    for group_name in group_names:
        obj.vertex_groups.new(name=group_name)
    bm = bmesh.new()
    deform_layer = bm.verts.layers.deform.verify()
    if skin is None:
        for spec in specs:
            build_segment(bm, spec, scale, deform_layer, group_names.index(spec["bone"]))
    else:
        skin.emit(bm, scale, deform_layer, group_names)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(ensure_material(material_name))
    if skin is not None and face_material_name:
        mesh.materials.append(ensure_material(face_material_name))
    obj.parent = armature_obj
    modifier = obj.modifiers.new("Armature", "ARMATURE")
    modifier.object = armature_obj
    if uv_unwrap and skin is None:
        auto_unwrap(context, obj, face_group="head", tight=tight_uv)
    return obj


class Skin:
    def __init__(self):
        self.points = []
        self.weights = []
        self.faces = []
        self.places = []
        self.charts = []
        self.uvs = []
        self.materials = []

    def vertex(self, point, weights):
        self.points.append(Vector(point))
        self.weights.append(weights)
        return len(self.points) - 1

    def ring(self, center, u, v, ru, rv, angles, weights, box=False):
        stretch = 1.0 / math.cos(math.pi / len(angles))
        ring = []
        for angle in angles:
            pu, pv = ring_point(angle, ru, rv, box, stretch)
            ring.append(self.vertex(center + u * pu + v * pv, weights))
        return ring

    def chart(self, rings, seam, head=False):
        self.charts.append({"rings": rings, "seam": seam, "head": head})
        return len(self.charts) - 1

    def quad(self, chart, row, k):
        rings = self.charts[chart]["rings"]
        lower, upper = rings[row], rings[row + 1]
        n = len(lower)
        self.faces.append((lower[k], lower[(k + 1) % n], upper[(k + 1) % n], upper[k]))
        self.places.append((chart, row, k))

    def bridge(self, chart, row):
        for k in range(len(self.charts[chart]["rings"][row])):
            self.quad(chart, row, k)

    def cap(self, chart, tip, weights):
        ring = self.charts[chart]["rings"][-1]
        center = self.vertex(tip, weights)
        n = len(ring)
        for k in range(0, n, 2):
            self.faces.append((center, ring[k], ring[(k + 1) % n], ring[(k + 2) % n]))
            self.places.append((chart, None, k))

    def unwrap(self, points, split_head):
        layouts = [chart_layout(chart, points) for chart in self.charts]
        corners = []
        for face, (chart, row, k) in zip(self.faces, self.places):
            us, vs, seam, cap = layouts[chart]
            m = len(us[0]) - 2
            j = (k - seam) % m
            if row is None:
                last = len(vs) - 1
                corners.append(
                    [
                        (us[last][j + 1], vs[last] + cap),
                        (us[last][j], vs[last]),
                        (us[last][j + 1], vs[last]),
                        (us[last][j + 2], vs[last]),
                    ]
                )
            else:
                corners.append(
                    [
                        (us[row][j], vs[row]),
                        (us[row][j + 1], vs[row]),
                        (us[row + 1][j + 1], vs[row + 1]),
                        (us[row + 1][j], vs[row + 1]),
                    ]
                )
        groups = {}
        for index, chart in enumerate(self.charts):
            groups.setdefault(1 if split_head and chart["head"] else 0, []).append(index)
        placement = {}
        for charts in groups.values():
            bounds = []
            for chart in charts:
                points = [uv for corner, (owner, _, _) in zip(corners, self.places) if owner == chart for uv in corner]
                lo = (min(p[0] for p in points), min(p[1] for p in points))
                hi = (max(p[0] for p in points), max(p[1] for p in points))
                bounds.append((lo, hi))
            sizes = [(hi[0] - lo[0], hi[1] - lo[1]) for lo, hi in bounds]
            if len(charts) == 1:
                fill = 1.0 - 2.0 * UV_MARGIN
                scale = (fill / sizes[0][0], fill / sizes[0][1])
                placement[charts[0]] = (bounds[0][0], scale, (UV_MARGIN, UV_MARGIN))
                continue
            scale, offsets = pack_charts(sizes)
            for chart, (lo, _), offset in zip(charts, bounds, offsets):
                placement[chart] = (lo, (scale, scale), offset)
        self.uvs = []
        self.materials = []
        for corner, (chart, _, _) in zip(corners, self.places):
            lo, scale, offset = placement[chart]
            self.uvs.append([((u - lo[0]) * scale[0] + offset[0], (v - lo[1]) * scale[1] + offset[1]) for u, v in corner])
            self.materials.append(1 if split_head and self.charts[chart]["head"] else 0)

    def emit(self, bm, scale, deform_layer, group_names):
        verts = [bm.verts.new(point * scale) for point in self.points]
        for vert, weights in zip(verts, self.weights):
            for bone, weight in weights.items():
                vert[deform_layer][group_names.index(bone)] = weight
        uv_layer = bm.loops.layers.uv.verify()
        faces = []
        for index, face in enumerate(self.faces):
            bm_face = bm.faces.new([verts[i] for i in face])
            bm_face.smooth = True
            if self.uvs:
                bm_face.material_index = self.materials[index]
                for loop, uv in zip(bm_face.loops, self.uvs[index]):
                    loop[uv_layer].uv = uv
            faces.append(bm_face)
        loose = [vert for vert in verts if not vert.link_faces]
        bmesh.ops.delete(bm, geom=loose, context="VERTS")
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])


def chart_layout(chart, points):
    rings = chart["rings"]
    seam = chart["seam"]
    m = len(rings[0])
    us = []
    for ring in rings:
        order = [points[ring[(seam + j) % m]] for j in range(m + 2)]
        steps = [(order[j + 1] - order[j]).length for j in range(m + 1)]
        around = sum(steps[:m])
        u = [-0.5 * around]
        for step in steps:
            u.append(u[-1] + step)
        us.append(u)
    vs = [0.0]
    for lower, upper in zip(rings, rings[1:]):
        vs.append(vs[-1] + sum((points[b] - points[a]).length for a, b in zip(lower, upper)) / m)
    cap = sum(us[-1][j + 1] - us[-1][j] for j in range(m)) / (2.0 * math.pi)
    return us, vs, seam, cap


UV_MARGIN = 0.012


def shelf_pack(sizes, scale):
    order = sorted(range(len(sizes)), key=lambda i: -sizes[i][1])
    offsets = [None] * len(sizes)
    x = y = shelf = UV_MARGIN
    for i in order:
        w, h = sizes[i][0] * scale, sizes[i][1] * scale
        if x + w + UV_MARGIN > 1.0:
            x = UV_MARGIN
            y += shelf + UV_MARGIN
            shelf = 0.0
        if x + w + UV_MARGIN > 1.0 or y + h + UV_MARGIN > 1.0:
            return None
        offsets[i] = (x, y)
        x += w + UV_MARGIN
        shelf = max(shelf, h)
    return offsets


def pack_charts(sizes):
    low, high = 0.0, 1.0 / max(max(w, h) for w, h in sizes)
    best = shelf_pack(sizes, low)
    for _ in range(40):
        mid = 0.5 * (low + high)
        offsets = shelf_pack(sizes, mid)
        if offsets is None:
            high = mid
        else:
            low, best = mid, offsets
    return low, best


def loop_angles(points, center, u, v):
    angles = []
    for point in points:
        d = point - center
        angle = math.atan2(d.dot(v), d.dot(u))
        if angles:
            while angle - angles[-1] > math.pi:
                angle -= 2 * math.pi
            while angle - angles[-1] < -math.pi:
                angle += 2 * math.pi
        angles.append(angle)
    direction = 1.0 if angles[-1] > angles[0] else -1.0
    n = len(angles)
    even = [angles[0] + direction * 2 * math.pi * j / n for j in range(n)]
    offset = sum(a - e for a, e in zip(angles, even)) / n
    return angles, [e + offset for e in even]


def blended(start, end, f):
    return [a + (b - a) * f for a, b in zip(start, end)]


def mix(*pairs):
    total = sum(weight for _, weight in pairs)
    return {bone: weight / total for bone, weight in pairs}


def skin_layout(values, joints, top_z, heel_y, toe_y, sides):
    v = values
    n = max(6, sides + (sides % 2))
    nt = 2 * n - 4
    t = v["limb_thickness"]
    depth = v["torso_depth"]
    x_axis = Vector((1, 0, 0))
    y_axis = Vector((0, 1, 0))
    z_axis = Vector((0, 0, 1))
    skin = Skin()

    hip_z = joints["pelvis"][0].z
    pelvis_top = joints["spine"][0].z
    chest_z = joints["chest"][0].z
    neck_z = joints["neck"][0].z
    head_z = joints["head"][0].z
    lh = top_z - head_z
    hs = v["head_size"]
    shoulder = joints["upperarm.L"][0]
    shoulder_x, shoulder_z = shoulder.x, shoulder.z
    hip_x = joints["thigh.L"][0].x
    thigh_len = hip_z - joints["calf.L"][0].z
    hip_out = hip_x + thigh_shift(hip_x, 0.048 * t, n)
    belly = 0.082 * v["belly_width"]
    chest_w = 0.10 * v["chest_width"]
    nr = 0.03 * (0.5 + 0.5 * t)
    arm_r = 0.037 * t

    head_rings = [
        (-0.04, 0.034, 0.036, -0.006, mix(("neck", 0.45), ("head", 0.55))),
        (0.12, 0.052, 0.06, -0.012, {"head": 1.0}),
        (0.45, 0.066, 0.076, -0.004, {"head": 1.0}),
        (0.78, 0.064, 0.074, 0.002, {"head": 1.0}),
        (0.95, 0.044, 0.052, 0.004, {"head": 1.0}),
    ]
    head_start = head_z + head_rings[0][0] * lh
    half = min(arm_r, 0.5 * (head_start - shoulder_z))
    gap = head_start - (shoulder_z + half)
    a = 2 * max(1, round(n / 8))
    b = n // 2 - a

    torso = [
        (hip_z - 0.15 * thigh_len, hip_out + 0.047 * t, 0.5 * (0.066 * depth + 0.056 * t), 0.0, {"pelvis": 1.0}),
        (hip_z, hip_out + 0.049 * t, 0.066 * depth, 0.0, {"pelvis": 1.0}),
        (pelvis_top, 0.5 * (belly * 1.02 + hip_out + 0.049 * t), 0.064 * depth, 0.0, mix(("pelvis", 0.55), ("spine", 0.45))),
        (0.5 * (pelvis_top + chest_z), belly * 0.98, 0.06 * depth, -0.002, {"spine": 1.0}),
        (chest_z, max(belly, chest_w * 0.9), 0.064 * depth, 0.0, mix(("spine", 0.45), ("chest", 0.55))),
    ]
    mid_chest = chest_z + 0.5 * (neck_z - chest_z)
    if mid_chest < shoulder_z - half - 0.015:
        torso.append((mid_chest, chest_w * 1.05, 0.075 * depth, -0.006 * depth, {"chest": 1.0}))
    armpit = len(torso)
    for row in range(b + 1):
        f = row / b
        z = shoulder_z - half + 2 * half * f
        torso.append((z, shoulder_x * (1.02 - 0.12 * f), (0.068 - 0.016 * f) * depth, 0.0, {"chest": 1.0}))
    torso.append((shoulder_z + half + 0.4 * gap, nr * 2.0, nr * 1.6, 0.002, mix(("chest", 0.6), ("neck", 0.4))))
    torso.append((shoulder_z + half + 0.75 * gap, nr, nr, 0.003, {"neck": 1.0}))
    for d, ru, rv, dv, weights in head_rings:
        torso.append((head_z + d * lh, ru * hs, rv * hs, dv * hs, weights))

    angles = [2 * math.pi * k / nt for k in range(nt)]
    rings = [
        skin.ring(Vector((0, dy, z)), x_axis, y_axis, ru, rv, angles, weights)
        for z, ru, rv, dy, weights in torso
    ]
    collar = armpit + b + 1
    trunk = skin.chart(rings[: collar + 1], nt // 4)
    head = skin.chart(rings[collar:], nt // 4, head=True)

    def column(side, k):
        return k % nt if side > 0 else (nt // 2 - k) % nt

    holes = set()
    for side in (1, -1):
        for row in range(armpit, armpit + b):
            for k in range(-a // 2, a // 2):
                holes.add((row, column(side, k if side > 0 else k + 1)))
    for row in range(len(rings) - 1):
        for k in range(nt):
            if (row, k) not in holes:
                if row < collar:
                    skin.quad(trunk, row, k)
                else:
                    skin.quad(head, row - collar, k)
    skin.cap(head, Vector((0, head_rings[-1][3] * hs, top_z)), {"head": 1.0})

    crotch = skin.vertex((0, 0, hip_z - 0.25 * thigh_len), {"pelvis": 1.0})
    for index in rings[0]:
        x = skin.points[index].x
        if abs(x) > 1e-6:
            skin.weights[index] = mix(("pelvis", 0.55), ("thigh.L" if x > 0 else "thigh.R", 0.45))
    for side in (1, -1):
        sx = float(side)
        hole = [rings[armpit][column(side, k)] for k in range(-a // 2, a // 2 + 1)]
        hole += [rings[armpit + row][column(side, a // 2)] for row in range(1, b + 1)]
        hole += [rings[armpit + b][column(side, k)] for k in range(a // 2 - 1, -a // 2 - 1, -1)]
        hole += [rings[armpit + row][column(side, -a // 2)] for row in range(b - 1, 0, -1)]
        build_arm(skin, values, joints, side, hole, shoulder_z, arm_r)
        front, back = 3 * nt // 4, nt // 4
        loop = [rings[0][column(side, k)] for k in range(front - nt, back + 1)] + [crotch]
        build_leg(skin, values, joints, side, loop, hip_x * sx, heel_y, toe_y)
    symmetrize(skin)
    return skin


def build_arm(skin, values, joints, side, hole, shoulder_z, arm_r):
    t = values["limb_thickness"]
    hz = values["hand_size"]
    sx = float(side)
    tag = "L" if side > 0 else "R"
    y_axis = Vector((0, 1, 0))
    z_axis = Vector((0, 0, 1))
    shoulder = joints[f"upperarm.{tag}"][0]
    elbow = joints[f"forearm.{tag}"][0]
    wrist = joints[f"hand.{tag}"][0]
    tip = joints[f"hand.{tag}"][1]
    points = [skin.points[i] for i in hole]
    start, even = loop_angles(points, Vector((0, 0, shoulder_z)), y_axis, z_axis)

    def along(p, q, f):
        return p + (q - p) * f

    upper = f"upperarm.{tag}"
    fore = f"forearm.{tag}"
    hand = f"hand.{tag}"
    outer = max(abs(p.x) for p in points)
    spec = [
        (Vector((sx * (outer + 0.012), 0, shoulder_z)), arm_r, arm_r * 1.03, 0.5, mix(("chest", 0.3), (upper, 0.7)), False),
        (along(shoulder, elbow, 0.35), 0.033 * t, 0.034 * t, 1.0, {upper: 1.0}, False),
        (along(shoulder, elbow, 0.8), 0.027 * t, 0.027 * t, 1.0, {upper: 1.0}, False),
        (elbow, 0.025 * t, 0.025 * t, 1.0, mix((upper, 0.55), (fore, 0.45)), False),
        (along(elbow, wrist, 0.2), 0.028 * t, 0.027 * t, 1.0, {fore: 1.0}, False),
        (along(elbow, wrist, 0.85), 0.02 * t, 0.018 * t, 1.0, {fore: 1.0}, False),
        (wrist, 0.019 * t, 0.016 * t, 1.0, mix((fore, 0.55), (hand, 0.45)), False),
        (along(wrist, tip, 0.15), 0.02 * hz, 0.013 * hz, 1.0, {hand: 1.0}, True),
        (along(wrist, tip, 0.55), 0.026 * hz, 0.012 * hz, 1.0, {hand: 1.0}, True),
        (along(wrist, tip, 0.92), 0.021 * hz, 0.007 * hz, 1.0, {hand: 1.0}, True),
    ]
    seam = min(range(len(hole)), key=lambda j: (round(points[j].z, 6), round(points[j].y, 6)))
    chart = skin.chart([hole], seam)
    for row, (center, ru, rv, f, weights, box) in enumerate(spec):
        skin.charts[chart]["rings"].append(skin.ring(center, y_axis, z_axis, ru, rv, blended(start, even, f), weights, box))
        skin.bridge(chart, row)
    skin.cap(chart, tip, {hand: 1.0})


THIGH_GAP = 0.004


def thigh_shift(hip_x, ru, sides):
    return max(0.0, ru / math.cos(math.pi / sides) + THIGH_GAP - hip_x)


def build_leg(skin, values, joints, side, loop, cx, heel_y, toe_y):
    t = values["limb_thickness"]
    fs = values["foot_size"]
    tag = "L" if side > 0 else "R"
    x_axis = Vector((1, 0, 0))
    y_axis = Vector((0, 1, 0))
    hip = joints[f"thigh.{tag}"][0]
    knee = joints[f"calf.{tag}"][0]
    ankle = joints[f"foot.{tag}"][0]
    points = [skin.points[i] for i in loop]
    start, even = loop_angles(points, Vector((cx, 0, 0)), x_axis, y_axis)

    def along(p, q, f):
        return p + (q - p) * f

    def tilt(degrees):
        rotation = Matrix.Rotation(math.radians(degrees), 3, "X")
        return rotation @ y_axis

    thigh = f"thigh.{tag}"
    calf = f"calf.{tag}"
    foot = f"foot.{tag}"
    fz = ankle.z + 0.01
    sole = fz * 0.5
    spec = [
        (along(hip, knee, 0.33), 0.048 * t, 0.056 * t, 0.0, 0.5, mix(("pelvis", 0.2), (thigh, 0.8))),
        (along(hip, knee, 0.55), 0.046 * t, 0.053 * t, 0.0, 1.0, {thigh: 1.0}),
        (along(hip, knee, 0.85), 0.038 * t, 0.04 * t, 0.0, 1.0, {thigh: 1.0}),
        (knee, 0.037 * t, 0.038 * t, 0.0, 1.0, mix((thigh, 0.55), (calf, 0.45))),
        (along(knee, ankle, 0.3) + Vector((0, 0.008 * t, 0)), 0.04 * t, 0.043 * t, 0.0, 1.0, {calf: 1.0}),
        (along(knee, ankle, 0.75), 0.03 * t, 0.031 * t, 0.0, 1.0, {calf: 1.0}),
        (Vector((cx, 0.0, ankle.z + 0.035)), 0.026 * t, 0.028 * t, 0.0, 1.0, mix((calf, 0.55), (foot, 0.45))),
        (Vector((cx, 0.006, fz * 0.62)), 0.029 * fs, 0.036 * fs, -45.0, 1.0, {foot: 1.0}),
        (Vector((cx, toe_y * 0.15, sole)), 0.031 * fs, sole, -90.0, 1.0, {foot: 1.0}),
        (Vector((cx, toe_y * 0.55, sole * 0.8)), 0.035 * fs, sole * 0.8, -90.0, 1.0, {foot: 1.0}),
        (Vector((cx, toe_y * 0.88, sole * 0.55)), 0.03 * fs, sole * 0.55, -90.0, 1.0, {foot: 1.0}),
    ]
    chart = skin.chart([loop], len(loop) - 1)
    for row, (center, ru, rv, degrees, f, weights) in enumerate(spec):
        center = center + x_axis * (side * thigh_shift(abs(cx), ru, len(loop)))
        skin.charts[chart]["rings"].append(skin.ring(center, x_axis, tilt(degrees), ru, rv, blended(start, even, f), weights))
        skin.bridge(chart, row)
    skin.cap(chart, Vector((cx, toe_y, sole * 0.5)), {foot: 1.0})


GIRTH_FIELDS = ("chest_width", "belly_width", "torso_depth", "limb_thickness")


def fused_skin(values, joints, top_z, heel_y, toe_y, sides, split_head):
    skin = skin_layout(values, joints, top_z, heel_y, toe_y, sides)
    reference = dict(values)
    for key in GIRTH_FIELDS:
        reference[key] = build_values()[key]
    plain = skin_layout(reference, joints, top_z, heel_y, toe_y, sides)
    skin.unwrap(plain.points, split_head)
    return skin


def symmetrize(skin):
    tree = KDTree(len(skin.points))
    for index, point in enumerate(skin.points):
        tree.insert(point, index)
    tree.balance()
    for index, point in enumerate(skin.points):
        if abs(point.x) < 1e-6:
            point.x = 0.0
        elif point.x > 0.0:
            mirror = Vector((-point.x, point.y, point.z))
            _, other, distance = tree.find(mirror)
            if distance > 1e-4:
                raise RuntimeError(f"skin vertex {index} has no mirror")
            skin.points[other] = mirror


def bone_vertex_bounds(mesh_obj, armature_obj):
    group_names = {group.index: group.name for group in mesh_obj.vertex_groups}
    to_armature = armature_obj.matrix_world.inverted() @ mesh_obj.matrix_world
    bounds = {}
    for vertex in mesh_obj.data.vertices:
        best = max(vertex.groups, key=lambda entry: entry.weight, default=None)
        if best is None or best.weight <= 0.0:
            continue
        bone = armature_obj.data.bones.get(group_names.get(best.group, ""))
        if bone is None:
            continue
        local = bone.matrix_local.inverted() @ (to_armature @ vertex.co)
        lo, hi = bounds.setdefault(bone.name, (local.copy(), local.copy()))
        for axis in range(3):
            lo[axis] = min(lo[axis], local[axis])
            hi[axis] = max(hi[axis], local[axis])
    return bounds


def hitbox_objects(armature_obj):
    return {
        obj.parent_bone: obj
        for obj in bpy.data.objects
        if obj.parent == armature_obj and obj.parent_type == "BONE" and obj.get("slop_hitbox")
    }


def fit_hitboxes(context, armature_obj, mesh_obj, collection=None, padding=0.005):
    bounds = bone_vertex_bounds(mesh_obj, armature_obj)
    existing = hitbox_objects(armature_obj)
    if collection is None:
        collection = next(iter(existing.values())).users_collection[0] if existing else ensure_collection(
            context, f"{armature_obj.name}_hitboxes", armature_obj.users_collection[0]
        )
    fitted = []
    for bone_name, (lo, hi) in bounds.items():
        obj = existing.get(bone_name)
        if obj is None:
            obj = bpy.data.objects.new(f"{HITBOX_PREFIX}{bone_name}", None)
            obj.empty_display_type = "CUBE"
            obj.empty_display_size = 1.0
            obj.hide_render = True
            collection.objects.link(obj)
            group, damage, severable = hitbox_rule(bone_name)
            obj["slop_hitbox"] = True
            obj["slop_hitgroup"] = group
            obj["slop_damage_scale"] = damage
            obj["slop_severable"] = severable
        obj.parent = armature_obj
        obj.parent_type = "BONE"
        obj.parent_bone = bone_name
        obj.matrix_parent_inverse = Matrix.Identity(4)
        context.view_layer.update()
        bone = armature_obj.data.bones[bone_name]
        center = (lo + hi) * 0.5
        half = (hi - lo) * 0.5 + Vector((padding, padding, padding))
        obj.matrix_world = (
            armature_obj.matrix_world
            @ bone.matrix_local
            @ Matrix.Translation(center)
            @ Matrix.Diagonal(half.to_4d())
        )
        fitted.append(obj)
    context.view_layer.update()
    return fitted


def loose_parts(mesh):
    parent = list(range(len(mesh.vertices)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for edge in mesh.edges:
        a, b = find(edge.vertices[0]), find(edge.vertices[1])
        if a != b:
            parent[a] = b
    parts = {}
    for index in range(len(mesh.vertices)):
        parts.setdefault(find(index), []).append(index)
    return list(parts.values())


def nearest_bone(point, armature_obj):
    best, best_distance = None, None
    for bone in armature_obj.data.bones:
        if not bone.use_deform:
            continue
        head, tail = bone.head_local, bone.tail_local
        segment = tail - head
        t = 0.0 if segment.length_squared == 0 else max(0.0, min(1.0, (point - head).dot(segment) / segment.length_squared))
        distance = (head + segment * t - point).length
        if best_distance is None or distance < best_distance:
            best, best_distance = bone.name, distance
    return best


def rigidify_weights(mesh_obj, armature_obj, mode="PARTS"):
    mesh = mesh_obj.data
    group_names = {group.index: group.name for group in mesh_obj.vertex_groups}
    bone_names = {bone.name for bone in armature_obj.data.bones if bone.use_deform}
    to_armature = armature_obj.matrix_world.inverted() @ mesh_obj.matrix_world

    def heaviest(indices):
        totals = {}
        for index in indices:
            for entry in mesh.vertices[index].groups:
                name = group_names.get(entry.group)
                if name in bone_names and entry.weight > 0.0:
                    totals[name] = totals.get(name, 0.0) + entry.weight
        if totals:
            return max(totals, key=totals.get)
        centroid = sum((to_armature @ mesh.vertices[i].co for i in indices), Vector()) / len(indices)
        return nearest_bone(centroid, armature_obj)

    groups = [[i] for i in range(len(mesh.vertices))] if mode == "VERTICES" else loose_parts(mesh)
    assignment = {}
    for indices in groups:
        bone_name = heaviest(indices)
        for index in indices:
            assignment[index] = bone_name

    for group in list(mesh_obj.vertex_groups):
        if group.name in bone_names:
            mesh_obj.vertex_groups.remove(group)
    targets = {}
    for index, bone_name in assignment.items():
        if bone_name is None:
            continue
        targets.setdefault(bone_name, []).append(index)
    for bone_name, indices in targets.items():
        mesh_obj.vertex_groups.new(name=bone_name).add(indices, 1.0, "REPLACE")
    if not any(m.type == "ARMATURE" and m.object == armature_obj for m in mesh_obj.modifiers):
        mesh_obj.modifiers.new("Armature", "ARMATURE").object = armature_obj
    return len(targets)


def create_mannequin(
    context,
    name,
    values,
    sides=8,
    hitboxes=True,
    uv_unwrap=True,
    tight_uv=False,
    fused=False,
    material_name="",
    face_material_name="",
):
    joints, top_z, heel_y, toe_y = layout(values)
    scale = values["height"] / top_z
    specs = segment_specs(values, joints, heel_y, toe_y)
    for spec in specs:
        spec["sides"] = sides
    collection = ensure_collection(context, name)
    armature_obj = build_armature(context, name, joints, scale, collection)
    mesh_obj = build_mesh(
        context,
        name,
        armature_obj,
        specs,
        scale,
        collection,
        material_name or f"{name}/skin",
        uv_unwrap,
        tight_uv,
        skin=fused_skin(values, joints, top_z, heel_y, toe_y, sides, bool(face_material_name)) if fused else None,
        face_material_name=face_material_name,
    )
    fitted = []
    if hitboxes:
        fitted = fit_hitboxes(context, armature_obj, mesh_obj, ensure_collection(context, f"{name}_hitboxes", collection))
    armature_obj["slop_mannequin"] = {key: float(value) for key, value in values.items()}
    return armature_obj, mesh_obj, fitted


def apply_preset(self, context):
    for key, value in preset_values(self.preset).items():
        setattr(self, key, value)


def build_properties():
    annotations = {
        "asset_name": bpy.props.StringProperty(
            name="Name",
            description="Armature, collection and skeleton id; the material is <name>/skin",
            default="mannequin",
        ),
        "preset": bpy.props.EnumProperty(
            name="Build",
            items=[(key, label, description) for key, (label, description, _) in PRESETS.items()],
            default="AVERAGE",
            update=apply_preset,
        ),
        "sides": bpy.props.IntProperty(
            name="Sides",
            description="Facets around each limb and torso piece",
            default=8,
            min=4,
            max=24,
        ),
        "fused": bpy.props.BoolProperty(
            name="Single Skin",
            description="One closed quad mesh with the limbs grown out of the torso and weights blended across the joints, so it bends and can be cut apart",
            default=False,
        ),
        "hitboxes": bpy.props.BoolProperty(name="Hitboxes", default=True),
        "uv_unwrap": bpy.props.BoolProperty(name="UV Unwrap", default=True),
    }
    for key, label, description, default, low, high in BUILD_FIELDS:
        annotations[key] = bpy.props.FloatProperty(
            name=label,
            description=description,
            default=default,
            min=low,
            max=high,
            unit="LENGTH" if key == "height" else "NONE",
        )
    return annotations


class OBJECT_OT_slopengine_add_mannequin(bpy.types.Operator):
    bl_idname = "object.slopengine_add_mannequin"
    bl_label = "Slopengine Mannequin"
    bl_description = "Add a segmented or single-skin mannequin with an armature and per-limb hitboxes"
    bl_options = {"REGISTER", "UNDO"}

    __annotations__ = build_properties()

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "asset_name")
        layout.prop(self, "preset")
        column = layout.column(align=True)
        for key, *_ in BUILD_FIELDS:
            column.prop(self, key)
        layout.prop(self, "sides")
        layout.prop(self, "fused")
        layout.prop(self, "hitboxes")
        layout.prop(self, "uv_unwrap")

    def execute(self, context):
        if context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        values = {key: getattr(self, key) for key, *_ in BUILD_FIELDS}
        armature_obj, mesh_obj, fitted = create_mannequin(
            context,
            self.asset_name.strip() or "mannequin",
            values,
            sides=self.sides,
            hitboxes=self.hitboxes,
            uv_unwrap=self.uv_unwrap,
            fused=self.fused,
        )
        armature_obj.location = context.scene.cursor.location
        for obj in context.selected_objects:
            obj.select_set(False)
        armature_obj.select_set(True)
        mesh_obj.select_set(True)
        context.view_layer.objects.active = armature_obj
        self.report({"INFO"}, f"Added {armature_obj.name} with {len(fitted)} hitboxes")
        return {"FINISHED"}


def selected_rig(context):
    armature_obj = None
    meshes = []
    for obj in context.selected_objects:
        if obj.type == "ARMATURE":
            armature_obj = obj
        elif obj.type == "MESH":
            meshes.append(obj)
    if armature_obj is None:
        for obj in meshes:
            for modifier in obj.modifiers:
                if modifier.type == "ARMATURE" and modifier.object is not None:
                    armature_obj = modifier.object
                    break
            if armature_obj is None and obj.parent is not None and obj.parent.type == "ARMATURE":
                armature_obj = obj.parent
    return armature_obj, meshes


class OBJECT_OT_slopengine_rigidify_weights(bpy.types.Operator):
    bl_idname = "object.slopengine_rigidify_weights"
    bl_label = "Rigidify Weights"
    bl_description = "Weight every loose part (or vertex) of the selected meshes 100% to a single bone"
    bl_options = {"REGISTER", "UNDO"}

    mode: bpy.props.EnumProperty(
        name="Per",
        items=[
            ("PARTS", "Loose Part", "Each connected piece goes to the bone it is mostly weighted to, or the nearest bone"),
            ("VERTICES", "Vertex", "Each vertex goes to its heaviest bone"),
        ],
        default="PARTS",
    )

    def execute(self, context):
        if context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        armature_obj, meshes = selected_rig(context)
        if armature_obj is None or not meshes:
            self.report({"ERROR"}, "Select the meshes and their armature")
            return {"CANCELLED"}
        for mesh_obj in meshes:
            count = rigidify_weights(mesh_obj, armature_obj, self.mode)
            self.report({"INFO"}, f"{mesh_obj.name}: rigid on {count} bones")
        return {"FINISHED"}


class OBJECT_OT_slopengine_fit_hitboxes(bpy.types.Operator):
    bl_idname = "object.slopengine_fit_hitboxes"
    bl_label = "Fit Hitboxes"
    bl_description = "Create or refit one bone-parented hitbox box per bone around the vertices weighted to it"
    bl_options = {"REGISTER", "UNDO"}

    padding: bpy.props.FloatProperty(name="Padding", default=0.005, min=0.0, max=0.2, unit="LENGTH")

    def execute(self, context):
        if context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        armature_obj, meshes = selected_rig(context)
        if armature_obj is None or not meshes:
            self.report({"ERROR"}, "Select the skinned mesh and its armature")
            return {"CANCELLED"}
        fitted = []
        for mesh_obj in meshes:
            fitted.extend(fit_hitboxes(context, armature_obj, mesh_obj, padding=self.padding))
        self.report({"INFO"}, f"Fitted {len(fitted)} hitboxes")
        return {"FINISHED"}


class VIEW3D_MT_slopengine_object(bpy.types.Menu):
    bl_idname = "VIEW3D_MT_slopengine_object"
    bl_label = "Slopengine"

    def draw(self, context):
        self.layout.operator(OBJECT_OT_slopengine_rigidify_weights.bl_idname, icon="MOD_VERTEX_WEIGHT")
        self.layout.operator(OBJECT_OT_slopengine_fit_hitboxes.bl_idname, icon="CUBE")
        self.layout.operator(OBJECT_OT_slopengine_auto_uv.bl_idname, icon="UV")


def menu_func_add(self, context):
    self.layout.operator(OBJECT_OT_slopengine_add_mannequin.bl_idname, icon="OUTLINER_OB_ARMATURE")


def menu_func_object(self, context):
    self.layout.menu(VIEW3D_MT_slopengine_object.bl_idname)


classes = (
    OBJECT_OT_slopengine_add_mannequin,
    OBJECT_OT_slopengine_rigidify_weights,
    OBJECT_OT_slopengine_fit_hitboxes,
    VIEW3D_MT_slopengine_object,
)
