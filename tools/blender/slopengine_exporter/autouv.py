import heapq
import math

import bmesh
import bpy
from mathutils import Vector

FRONT = Vector((0.0, -1.0, 0.0))
UP = Vector((0.0, 0.0, 1.0))


def face_charts(bm):
    label = {}
    charts = []
    for face in bm.faces:
        if face in label:
            continue
        label[face] = len(charts)
        stack = [face]
        members = []
        while stack:
            current = stack.pop()
            members.append(current)
            for edge in current.edges:
                if edge.seam:
                    continue
                for other in edge.link_faces:
                    if other not in label:
                        label[other] = len(charts)
                        stack.append(other)
        charts.append(members)
    return charts


def chart_boundary(chart):
    edges = set()
    for face in chart:
        for edge in face.edges:
            if edge.seam or sum(1 for other in edge.link_faces if other in chart) == 1:
                edges.add(edge)
    return edges


def vertex_components(edges):
    parent = {}

    def find(vert):
        while parent[vert] is not vert:
            parent[vert] = parent[parent[vert]]
            vert = parent[vert]
        return vert

    for edge in edges:
        for vert in edge.verts:
            parent.setdefault(vert, vert)
        a, b = find(edge.verts[0]), find(edge.verts[1])
        if a is not b:
            parent[a] = b
    groups = {}
    for vert in parent:
        groups.setdefault(find(vert), set()).add(vert)
    return list(groups.values())


def visibility(normal, center):
    side = normal.x * math.copysign(1.0, center.x) if abs(center.x) > 1e-3 else 0.0
    return normal.dot(FRONT) + 0.5 * normal.dot(UP) + 0.5 * side


def hidden_path(chart, center, sources, targets):
    best = {vert: 0.0 for vert in sources}
    previous = {}
    queue = [(0.0, vert.index, vert) for vert in sources]
    heapq.heapify(queue)
    while queue:
        cost, _, vert = heapq.heappop(queue)
        if cost > best.get(vert, math.inf):
            continue
        if vert in targets:
            path = []
            while vert in previous:
                edge = previous[vert]
                path.append(edge)
                vert = edge.other_vert(vert)
            return path
        for edge in vert.link_edges:
            inside = [face for face in edge.link_faces if face in chart]
            if edge.seam or len(inside) != 2:
                continue
            normal = (inside[0].normal + inside[1].normal).normalized()
            step = edge.calc_length() * math.exp(2.0 * visibility(normal, center)) + 1e-6
            other = edge.other_vert(vert)
            if cost + step < best.get(other, math.inf):
                best[other] = cost + step
                previous[other] = edge
                heapq.heappush(queue, (cost + step, other.index, other))
    return []


def open_chart(chart_faces, limit=16):
    chart = set(chart_faces)
    verts = {vert for face in chart_faces for vert in face.verts}
    center = sum((vert.co for vert in verts), Vector()) / len(verts)
    for _ in range(limit):
        loops = vertex_components(chart_boundary(chart))
        if len(loops) == 1:
            return
        if loops:
            sources, targets = loops[0], set().union(*loops[1:])
        else:
            low, high = min(verts, key=lambda v: v.co.z), max(verts, key=lambda v: v.co.z)
            sources, targets = {low}, {high}
        path = hidden_path(chart, center, sources, targets)
        if not path:
            return
        for edge in path:
            edge.seam = True


def face_region(bm, obj, face_group, threshold):
    group = obj.vertex_groups.get(face_group) if face_group else None
    if group is None:
        return set()
    deform = bm.verts.layers.deform.active
    if deform is None:
        return set()

    def in_group(face):
        return all(vert[deform].get(group.index, 0.0) > 0.5 for vert in face.verts)

    head = {face for face in bm.faces if in_group(face)}
    region = {face for face in head if face.normal.dot(FRONT) > threshold}
    for face in region:
        for edge in face.edges:
            if any(other in head and other not in region for other in edge.link_faces):
                edge.seam = True
    return region


def mark_seams(bm, obj, hard_angle, face_group, face_threshold, keep_seams):
    if not keep_seams:
        for edge in bm.edges:
            edge.seam = False
    for edge in bm.edges:
        if len(edge.link_faces) == 2 and edge.calc_face_angle(0.0) > hard_angle:
            edge.seam = True
    region = face_region(bm, obj, face_group, face_threshold)
    for chart in face_charts(bm):
        if region and chart[0] in region:
            continue
        open_chart(chart)
    return region


def uv_area(face, uv_layer):
    points = [loop[uv_layer].uv for loop in face.loops]
    area = 0.0
    for a, b in zip(points, points[1:] + points[:1]):
        area += a.x * b.y - b.x * a.y
    return abs(area) * 0.5


def chart_is_even(chart, uv_layer, tolerance):
    ratios = [(uv_area(face, uv_layer) / face.calc_area(), face.calc_area()) for face in chart if face.calc_area() > 1e-12]
    total = sum(area for _, area in ratios)
    if total <= 0.0:
        return True
    mean = sum(ratio * area for ratio, area in ratios) / total
    if mean <= 0.0:
        return False
    even = sum(area for ratio, area in ratios if 1.0 / (1.0 + tolerance) <= ratio / mean <= 1.0 + tolerance)
    return even / total >= 0.8


def split_chart(chart, direction):
    members = set(chart)
    center = sum((face.calc_center_median() for face in chart), Vector()) / len(chart)
    side = {face: face.normal.dot(direction) >= 0.0 for face in chart}
    if len(set(side.values())) < 2:
        side = {face: (face.calc_center_median() - center).dot(direction) >= 0.0 for face in chart}
    if len(set(side.values())) < 2:
        return False
    for face in chart:
        for edge in face.edges:
            if any(other in members and side[other] != side[face] for other in edge.link_faces):
                edge.seam = True
    for part in (True, False):
        for piece in face_charts_within([face for face in chart if side[face] == part]):
            open_chart(piece)
    return True


def face_charts_within(faces):
    members = set(faces)
    seen = set()
    charts = []
    for face in faces:
        if face in seen:
            continue
        seen.add(face)
        stack = [face]
        chart = []
        while stack:
            current = stack.pop()
            chart.append(current)
            for edge in current.edges:
                if edge.seam:
                    continue
                for other in edge.link_faces:
                    if other in members and other not in seen:
                        seen.add(other)
                        stack.append(other)
        charts.append(chart)
    return charts


def island_stats(mesh):
    uv_data = mesh.uv_layers.active.data
    ratios = []
    for poly in mesh.polygons:
        points = [uv_data[index].uv for index in poly.loop_indices]
        area = 0.0
        for a, b in zip(points, points[1:] + points[:1]):
            area += a.x * b.y - b.x * a.y
        if poly.area > 1e-12:
            ratios.append((abs(area) * 0.5 / poly.area, poly.area))
    total_uv = sum(r * a for r, a in ratios)
    total_3d = sum(a for _, a in ratios)
    mean = total_uv / total_3d
    within = sum(a for r, a in ratios if 0.8 <= r / mean <= 1.25) / total_3d
    return {"uv_coverage": total_uv, "density_within_25": within}


def auto_unwrap(
    context,
    obj,
    hard_angle=math.radians(60.0),
    face_group="",
    face_threshold=0.35,
    face_scale=2.0,
    margin=0.006,
    keep_seams=False,
    distortion=0.25,
    tight=False,
):
    view_layer = context.view_layer
    previous_active = view_layer.objects.active
    previous_selection = list(context.selected_objects)
    previous_mode = obj.mode
    tool_settings = context.scene.tool_settings
    previous_sync = tool_settings.use_uv_select_sync
    for selected in previous_selection:
        selected.select_set(False)
    obj.select_set(True)
    view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    tool_settings.use_uv_select_sync = True
    try:
        bm = bmesh.from_edit_mesh(obj.data)
        bm.faces.ensure_lookup_table()
        region = mark_seams(bm, obj, hard_angle, face_group, face_threshold, keep_seams)
        bm.faces.index_update()
        region_indices = {face.index for face in region}
        for face in bm.faces:
            face.select = True
        bmesh.update_edit_mesh(obj.data)
        for direction in (FRONT, UP, Vector((1.0, 0.0, 0.0)), None):
            bpy.ops.uv.unwrap(method="MINIMUM_STRETCH", iterations=40, margin=0.0, no_flip=True)
            if direction is None:
                break
            bm = bmesh.from_edit_mesh(obj.data)
            uv_layer = bm.loops.layers.uv.verify()
            uneven = [
                chart
                for chart in face_charts(bm)
                if chart[0].index not in region_indices and not chart_is_even(chart, uv_layer, distortion)
            ]
            if not uneven:
                break
            split = [split_chart(chart, direction) for chart in uneven]
            bmesh.update_edit_mesh(obj.data)
            if not any(split):
                break

        bm = bmesh.from_edit_mesh(obj.data)
        uv_layer = bm.loops.layers.uv.verify()
        region = [face for face in bm.faces if face.index in region_indices]
        for face in region:
            for loop in face.loops:
                loop[uv_layer].uv = (loop.vert.co.x, loop.vert.co.z)
        bmesh.update_edit_mesh(obj.data)
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.select_all(action="SELECT")
        bpy.ops.uv.average_islands_scale()

        bm = bmesh.from_edit_mesh(obj.data)
        uv_layer = bm.loops.layers.uv.verify()
        region = [face for face in bm.faces if face.index in region_indices]
        if region:
            face_loops = [loop for face in region for loop in face.loops]
            middle = sum((loop[uv_layer].uv for loop in face_loops), Vector((0.0, 0.0))) / len(face_loops)
            for loop in face_loops:
                loop[uv_layer].uv = middle + (loop[uv_layer].uv - middle) * face_scale
                loop[uv_layer].pin_uv = True
        bmesh.update_edit_mesh(obj.data)
        bpy.ops.uv.pack_islands(
            rotate=True,
            rotate_method="AXIS_ALIGNED",
            scale=True,
            margin_method="FRACTION",
            margin=margin,
            pin=bool(region),
            pin_method="ROTATION",
            shape_method="CONCAVE" if tight else "AABB",
        )
        bm = bmesh.from_edit_mesh(obj.data)
        uv_layer = bm.loops.layers.uv.verify()
        for face in bm.faces:
            for loop in face.loops:
                loop[uv_layer].pin_uv = False
        bmesh.update_edit_mesh(obj.data)
    finally:
        bpy.ops.object.mode_set(mode="OBJECT")
        tool_settings.use_uv_select_sync = previous_sync
        obj.select_set(False)
        for selected in previous_selection:
            selected.select_set(True)
        view_layer.objects.active = previous_active
        if previous_mode != "OBJECT" and previous_active is obj:
            bpy.ops.object.mode_set(mode=previous_mode)
    return island_stats(obj.data)


class OBJECT_OT_slopengine_auto_uv(bpy.types.Operator):
    bl_idname = "object.slopengine_auto_uv"
    bl_label = "Auto UV Unwrap"
    bl_description = (
        "Cut seams on hard edges and the least visible side of every closed shell, flatten with minimum stretch, "
        "even out texel density and pack; optionally give the face its own front-projected chart"
    )
    bl_options = {"REGISTER", "UNDO"}

    hard_angle: bpy.props.FloatProperty(
        name="Hard Angle",
        description="Edges sharper than this become seams",
        default=math.radians(60.0),
        min=math.radians(10.0),
        max=math.radians(180.0),
        subtype="ANGLE",
    )
    face_group: bpy.props.StringProperty(
        name="Face Group",
        description="Vertex group of the head; its front becomes a chart projected straight from the front, for photographs",
        default="head",
    )
    face_scale: bpy.props.FloatProperty(
        name="Face Detail",
        description="Texel density of the face chart relative to the rest",
        default=2.0,
        min=0.5,
        max=8.0,
    )
    margin: bpy.props.FloatProperty(name="Margin", default=0.006, min=0.0, max=0.05)
    keep_seams: bpy.props.BoolProperty(
        name="Keep Existing Seams",
        description="Add to seams already marked instead of starting over",
        default=False,
    )
    tight: bpy.props.BoolProperty(
        name="Tight Packing",
        description="Pack by exact chart shape for more texture use; slower, a few seconds to tens of seconds",
        default=True,
    )

    def execute(self, context):
        meshes = [obj for obj in context.selected_objects if obj.type == "MESH"]
        if not meshes:
            self.report({"ERROR"}, "Select at least one mesh")
            return {"CANCELLED"}
        if context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        for obj in meshes:
            stats = auto_unwrap(
                context,
                obj,
                hard_angle=self.hard_angle,
                face_group=self.face_group.strip(),
                face_scale=self.face_scale,
                margin=self.margin,
                keep_seams=self.keep_seams,
                tight=self.tight,
            )
            self.report(
                {"INFO"},
                f"{obj.name}: {stats['uv_coverage']:.0%} of the texture used, "
                f"{stats['density_within_25']:.0%} of the surface within 25% of the mean texel density",
            )
        return {"FINISHED"}


classes = (OBJECT_OT_slopengine_auto_uv,)
