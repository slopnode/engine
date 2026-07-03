import re
import struct

import bpy
import bmesh
from mathutils import Matrix, Vector

MAGIC_VERTS = b"DLKV"
MAGIC_WEIGHTS = b"DLKW"
MAGIC_TRACKS = b"DLKT"
MAGIC_BIND = b"DLKB"
FORMAT_VERSION = 1
TRACKS_FORMAT_VERSION = 2
BIND_FORMAT_VERSION = 1

FLAG_NORMALS = 1
FLAG_UVS = 2

BLENDER_TO_ENGINE_BASIS = Matrix(
    (
        (1.0, 0.0, 0.0, 0.0),
        (0.0, 0.0, 1.0, 0.0),
        (0.0, -1.0, 0.0, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )
)


def convert_position(vec):
    converted = BLENDER_TO_ENGINE_BASIS @ Vector((vec.x, vec.y, vec.z, 1.0))
    return converted.to_3d()


def convert_direction(vec):
    converted = BLENDER_TO_ENGINE_BASIS @ Vector((vec.x, vec.y, vec.z, 0.0))
    return converted.to_3d()


def convert_matrix(matrix):
    return BLENDER_TO_ENGINE_BASIS @ matrix @ BLENDER_TO_ENGINE_BASIS.inverted()


def decompose_engine_transform(matrix):
    return convert_matrix(matrix).decompose()


def matrix_to_raylib_floats(matrix):
    converted = convert_matrix(matrix)
    values = []
    for col in range(4):
        for row in range(4):
            values.append(float(converted[row][col]))
    return values


def matrix_from_raylib_floats(values):
    matrix = Matrix.Identity(4)
    index = 0
    for col in range(4):
        for row in range(4):
            matrix[row][col] = values[index]
            index += 1
    return matrix


def pose_bone_matrix_global(pose_bone):
    return pose_bone.matrix.copy()


class bind_pose_context:
    def __init__(self, context, armature_obj):
        self.context = context
        self.armature_obj = armature_obj
        self.scene = context.scene
        self.previous_frame = self.scene.frame_current
        self.previous_mode = armature_obj.mode
        self.animation_data = armature_obj.animation_data
        self.previous_action = (
            self.animation_data.action if self.animation_data is not None else None
        )

    def __enter__(self):
        if self.armature_obj.mode != "POSE":
            bpy.ops.object.mode_set(mode="POSE")
        if self.animation_data is not None:
            self.animation_data.action = None
        self.scene.frame_set(0)
        self.context.view_layer.update()
        return self

    def __exit__(self, exc_type, exc, tb):
        if self.animation_data is not None:
            self.animation_data.action = self.previous_action
        self.scene.frame_set(self.previous_frame)
        if self.previous_mode != "POSE":
            bpy.ops.object.mode_set(mode=self.previous_mode)
        return False


def sanitize_name(name):
    cleaned = re.sub(r"[^\w\-./]", "_", name.strip())
    return cleaned or "unnamed"


def material_path(material_name):
    path = material_name.replace("\\", "/").strip()
    if path.startswith("materials/"):
        path = path[len("materials/") :]
    for ext in (".mat", ".s7", ".geo", ".skel", ".anim", ".vert", ".weights"):
        if path.endswith(ext):
            path = path[: -len(ext)]
    if "/" not in path and "." in path:
        path = path.replace(".", "/")
    return path or "default/unassigned"


def format_float(value):
    text = f"{value:.6g}"
    if text == "-0":
        return "0"
    return text


def format_vec3(vec):
    return f"(t {format_float(vec.x)} {format_float(vec.y)} {format_float(vec.z)})"


def format_quat(quat):
    return (
        f"(r {format_float(quat.x)} {format_float(quat.y)} "
        f"{format_float(quat.z)} {format_float(quat.w)})"
    )


def format_scale(vec):
    return f"(s {format_float(vec.x)} {format_float(vec.y)} {format_float(vec.z)})"


def write_floats(file_obj, values):
    for index in range(0, len(values), 1024):
        chunk = values[index : index + 1024]
        file_obj.write(struct.pack(f"<{len(chunk)}f", *chunk))


def write_vert_file(path, positions, normals, uvs, indices):
    vertex_count = len(positions) // 3
    index_count = len(indices)
    flags = 0
    if normals:
        flags |= FLAG_NORMALS
    if uvs:
        flags |= FLAG_UVS

    with open(path, "wb") as file_obj:
        file_obj.write(MAGIC_VERTS)
        file_obj.write(struct.pack("<H", FORMAT_VERSION))
        file_obj.write(struct.pack("<IIH", vertex_count, index_count, flags))
        write_floats(file_obj, positions)
        if normals:
            write_floats(file_obj, normals)
        if uvs:
            write_floats(file_obj, uvs)
        if index_count:
            for index in range(0, index_count, 1024):
                chunk = indices[index : index + 1024]
                file_obj.write(struct.pack(f"<{len(chunk)}I", *chunk))


def write_weights_file(path, weights):
    vertex_count = len(weights)
    with open(path, "wb") as file_obj:
        file_obj.write(MAGIC_WEIGHTS)
        file_obj.write(struct.pack("<H", FORMAT_VERSION))
        file_obj.write(struct.pack("<I", vertex_count))
        for joint_indices, joint_weights in weights:
            file_obj.write(struct.pack("<4B", *joint_indices))
            file_obj.write(struct.pack("<4f", *joint_weights))


def merge_geo_parts(parts):
    combined_positions = []
    combined_normals = []
    combined_uvs = []
    combined_indices = []
    combined_weights = []
    merged_primitives = []
    has_weights = False

    vertex_offset = 0
    index_offset = 0
    primitive_index = 0

    for part in parts:
        vertex_count = len(part["positions"]) // 3
        index_count = len(part["indices"])
        if vertex_count == 0 or index_count == 0:
            continue

        base_vertex = vertex_offset
        combined_positions.extend(part["positions"])
        combined_normals.extend(part["normals"])
        combined_uvs.extend(part["uvs"])
        combined_indices.extend(index + base_vertex for index in part["indices"])

        if part["weights"] is not None:
            has_weights = True
            combined_weights.extend(part["weights"])

        merged_primitives.append(
            {
                "id": str(primitive_index),
                "material": part["material"],
                "vertex_offset": base_vertex,
                "vertex_count": vertex_count,
                "index_offset": index_offset,
                "index_count": index_count,
            }
        )

        vertex_offset += vertex_count
        index_offset += index_count
        primitive_index += 1

    return {
        "positions": combined_positions,
        "normals": combined_normals,
        "uvs": combined_uvs,
        "indices": combined_indices,
        "weights": combined_weights if has_weights else None,
        "primitives": merged_primitives,
    }


def write_tracks_file(path, frames):
    frame_count = len(frames)
    bone_count = len(frames[0]) if frame_count else 0
    uses_matrices = frame_count > 0 and isinstance(frames[0][0], list)

    with open(path, "wb") as file_obj:
        file_obj.write(MAGIC_TRACKS)
        version = TRACKS_FORMAT_VERSION if uses_matrices else FORMAT_VERSION
        file_obj.write(struct.pack("<H", version))
        file_obj.write(struct.pack("<II", bone_count, frame_count))
        for frame in frames:
            for entry in frame:
                if uses_matrices:
                    file_obj.write(struct.pack("<16f", *entry))
                else:
                    translation, rotation, scale = entry
                    file_obj.write(
                        struct.pack(
                            "<10f",
                            translation.x,
                            translation.y,
                            translation.z,
                            rotation.x,
                            rotation.y,
                            rotation.z,
                            rotation.w,
                            scale.x,
                            scale.y,
                            scale.z,
                        )
                    )


def write_bind_file(path, armature_obj, order):
    with open(path, "wb") as file_obj:
        file_obj.write(MAGIC_BIND)
        file_obj.write(struct.pack("<HI", BIND_FORMAT_VERSION, len(order)))
        for bone in order:
            pose_bone = armature_obj.pose.bones.get(bone.name)
            if pose_bone is None:
                matrix = Matrix.Identity(4)
            else:
                matrix = pose_bone_matrix_global(pose_bone)
            file_obj.write(struct.pack("<16f", *matrix_to_raylib_floats(matrix)))


def collect_armature_actions(armature_obj):
    actions = []
    seen = set()
    animation_data = armature_obj.animation_data
    if animation_data is None:
        return actions

    def add_action(action):
        if action is None:
            return
        key = action.as_pointer()
        if key in seen:
            return
        seen.add(key)
        actions.append(action)

    add_action(animation_data.action)

    for track in animation_data.nla_tracks:
        for strip in track.strips:
            if strip.type == "CLIP":
                add_action(strip.action)

    return actions


def sample_pose_matrices(armature_obj, order):
    pose_frame = []
    for bone in order:
        pose_bone = armature_obj.pose.bones.get(bone.name)
        if pose_bone is None:
            matrix = Matrix.Identity(4)
        else:
            matrix = pose_bone_matrix_global(pose_bone)
        pose_frame.append(matrix_to_raylib_floats(matrix))
    return pose_frame


def sample_action_frames(context, armature_obj, order, action, fps):
    frame_start = int(action.frame_range[0])
    frame_end = int(action.frame_range[1])
    if frame_end < frame_start:
        return None

    scene = context.scene
    animation_data = armature_obj.animation_data
    action_duration = (frame_end - frame_start) / scene.render.fps
    sample_count = max(1, int(action_duration * fps) + 1)
    frames = []

    if animation_data is not None:
        animation_data.action = action
    for sample_index in range(sample_count):
        time = sample_index / fps
        frame = frame_start + time * scene.render.fps
        scene.frame_set(int(round(frame)))
        context.view_layer.update()
        frames.append(sample_pose_matrices(armature_obj, order))

    return {
        "duration": (len(frames) - 1) / fps,
        "frames": frames,
    }


def bone_order(armature_data):
    order = []

    def walk(bone):
        order.append(bone)
        for child in bone.children:
            walk(child)

    for bone in armature_data.bones:
        if bone.parent is None:
            walk(bone)

    return order


def bone_parent_index(bone, order):
    if bone.parent is None:
        return -1
    for index, entry in enumerate(order):
        if entry == bone.parent:
            return index
    return -1


def bind_pose_transform(armature_obj, bone):
    pose_bone = armature_obj.pose.bones.get(bone.name)
    if pose_bone is not None:
        return decompose_engine_transform(pose_bone_matrix_local(pose_bone))
    return decompose_engine_transform(bone.matrix_local.copy())


def pose_bone_matrix_local(pose_bone):
    if hasattr(pose_bone, "matrix_local"):
        return pose_bone.matrix_local.copy()
    matrix = pose_bone.matrix.copy()
    parent = pose_bone.parent
    if parent is not None:
        matrix = parent.matrix.inverted() @ matrix
    return matrix


def is_identity_transform(matrix, tolerance=1e-6):
    identity = Matrix.Identity(4)
    for row in range(4):
        for col in range(4):
            if abs(matrix[row][col] - identity[row][col]) > tolerance:
                return False
    return True


def apply_world_transform_to_mesh(mesh, matrix):
    if is_identity_transform(matrix):
        return
    mesh.transform(matrix)


def mesh_objects_from_selection(context):
    return [obj for obj in context.selected_objects if obj.type == "MESH"]


def active_armature(context):
    obj = context.active_object
    if obj and obj.type == "ARMATURE":
        return obj
    for selected in context.selected_objects:
        if selected.type == "ARMATURE":
            return selected
    return None


def armature_from_mesh(obj):
    for modifier in obj.modifiers:
        if modifier.type == "ARMATURE" and modifier.object is not None:
            return modifier.object
    return None


def triangulate_mesh(mesh):
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()


def build_vertex_weights(obj, bone_order_list):
    bone_index = {bone.name: index for index, bone in enumerate(bone_order_list)}
    mesh = obj.data
    weights = []
    unmatched_groups = set()
    zero_weight_vertices = 0

    for vertex in mesh.vertices:
        influences = []
        for group in vertex.groups:
            group_name = obj.vertex_groups[group.group].name
            if group_name not in bone_index:
                unmatched_groups.add(group_name)
                continue
            if group.weight <= 0.0:
                continue
            influences.append((bone_index[group_name], group.weight))

        influences.sort(key=lambda item: item[1], reverse=True)
        influences = influences[:4]
        total = sum(weight for _, weight in influences)
        if total > 0.0:
            influences = [(index, weight / total) for index, weight in influences]
        else:
            zero_weight_vertices += 1

        joint_indices = [0, 0, 0, 0]
        joint_weights = [0.0, 0.0, 0.0, 0.0]
        for slot, (index, weight) in enumerate(influences):
            joint_indices[slot] = index
            joint_weights[slot] = weight

        weights.append((joint_indices, joint_weights))

    return weights, {
        "unmatched_groups": sorted(unmatched_groups),
        "zero_weight_vertices": zero_weight_vertices,
    }


def material_name_for_slot(obj, slot_index):
    if slot_index >= len(obj.material_slots):
        return "default/unassigned"
    material = obj.material_slots[slot_index].material
    if material is None:
        return "default/unassigned"
    return material_path(material.name)


def extract_material_parts(mesh, obj, vertex_weights=None):
    if not mesh.uv_layers.active:
        mesh.uv_layers.new(name="ExportUV")
    uv_layer = mesh.uv_layers.active.data

    material_indices = sorted({face.material_index for face in mesh.polygons})
    if not material_indices:
        material_indices = [0]

    parts = []
    for material_index in material_indices:
        positions = []
        normals = []
        uvs = []
        indices = []
        weights = []
        loop_to_export = {}

        for face in mesh.polygons:
            if face.material_index != material_index:
                continue
            if len(face.vertices) != 3:
                continue

            face_indices = []
            for loop_index in face.loop_indices:
                vertex_index = mesh.loops[loop_index].vertex_index
                export_key = (vertex_index, loop_index)
                if export_key not in loop_to_export:
                    loop_to_export[export_key] = len(positions) // 3
                    vertex = mesh.vertices[vertex_index]
                    position = convert_position(vertex.co)
                    positions.extend((position.x, position.y, position.z))
                    if vertex.normal.length_squared > 0.0:
                        normal = convert_direction(vertex.normal)
                    else:
                        normal = convert_direction(Vector((0.0, 0.0, 1.0)))
                    normals.extend((normal.x, normal.y, normal.z))
                    uv = uv_layer[loop_index].uv
                    uvs.extend((uv.x, uv.y))
                    if vertex_weights is not None:
                        weights.append(vertex_weights[vertex_index])
                face_indices.append(loop_to_export[export_key])
            indices.extend(face_indices)

        if not indices:
            continue

        parts.append(
            {
                "material": material_name_for_slot(obj, material_index),
                "positions": positions,
                "normals": normals,
                "uvs": uvs,
                "indices": indices,
                "weights": weights if vertex_weights is not None else None,
            }
        )

    return parts
