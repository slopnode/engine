# VIS compilation

`slopvis` builds the visible face fragment list `static.vis`. Requires a sealed [BSP](bsp.md). Authoring stays on [Maps](maps.md). Lightmaps consume VIS face ids: [Radiosity](rad.md).

CMake target `slopvis` (root `CMakeLists.txt`), linked against `sloplib`.

```bash
cmake --build build --target slopvis

./build/slopvis --base-game <package-path> [--mod <path>]... --map <name>
```

Shared flags with the game: `--base-game`, repeated `--mod`, and `--map <name>` (folder name under `maps/`, not a file path).

Re-run after BSP changes that affect sealing or face layout, detail brush edits that change visible fragments, face-id churn, or authored `(nodraw)`. Re-bake [radiosity](rad.md) when chart keys (VIS ids) change.

## What VIS is for

`static.vis` is the **visible face fragment list** used for the draw mesh, lightmap charts, and Steam Audio occlusion. Despite the Quake-style name, it is **not** a leaf<->leaf PVS bitset and does not drive runtime portal culling.

- Interior clip. Hull and detail faces are clipped against sealed interior empty leaf polyhedra so large or buried faces keep only the visible polygon(s).
- Brush occlusion. Fragments are subtracted against other VIS-emitting brushes so detail-detail contacts (and hull under detail) drop buried area while abutment remainders (e.g. stair risers) stay.
- Inferred nodraw. Faces with zero remaining visible area (after clip, occlusion, and sliver cull) are treated as nodraw. Authored `(nodraw)` is never cleared.
- Cleanup. After clip and occlusion: T-junction weld, vertex snap weld, sliver/degenerate cull, coplanar merge (retains non-colinear shared-edge endpoints so concave T-junction corners survive; spike cleanup strips out-and-back A->B->A folds; merges that stay non-simple, inflate area beyond the inputs, or pinch a keyhole with non-adjacent duplicate verts are refused so punched openings stay hollow), then material-major sort. Draw/BVH triangulation uses ear clipping so concave simple rings stay correct.
- Stable face ids. Fragments use ids such as `wall/north#0`; coplanar merges across sources use `merge/...` ids. Charts and mesh UV2 key off those ids.
- Interior leaf hint. Each fragment stores an `interiorLeaf` index used by radiosity leaf-reachability culling.

## Tool sequence

Entry point: `tools/slopvis/main.cpp`. Core build: `buildVisibleFaces` in `src/map/vis_build.cpp`. On-disk format: `VIS1` via `src/map/vis_io.cpp`.

1. Parse CLI (`AppConfig`); require `--map`.
2. Require readable `static.bsp`; load CSG brushes.
3. `analyzeMapHull`. If not sealed: log leak path, exit 1 (no `.vis` written).
4. `buildVisibleFaces` -> clip, weld, cull, merge, sort.
5. Write sibling `static.vis` with `writeVisFile`; log face and inferred-nodraw counts.

## Face visibility algorithm

For each non-authored-nodraw face on a VIS-emitting brush (`hull`, `detail`, `water`, `window`; not `hint` / `trigger`) when the hull is sealed:

1. Clip the face polygon against every sealed interior empty leaf polyhedron (Sutherland-Hodgman against outward leaf planes).
2. Keep fragments whose outward-nudged centroid sample lands in interior empty.
3. Discard zero-area scraps; micro-merge coplanar adjacent scraps from the same source face.
4. Occlude against other VIS-emitting brushes: clip the fragment into each occluder, drop fully covered scraps, otherwise subtract the covered hole (splitting into multiple remainders when needed); remainder rings are spike-cleaned.
5. Emit fragments with provisional ids `source#N`. If no fragments remain -> inferred nodraw for that source face id.

On an unsealed fallback path (loader only), faces pass through with their authored ids.

After fragments are collected:

1. T-junction weld -- insert vertices where another face's vertex lies on an edge.
2. Vertex snap weld -- snap near-coincident verts (grid + epsilon) and collapse consecutive duplicates.
3. Sliver / degenerate cull -- drop faces below minimum area, with needle altitude, or with a tiny area/perimeter^2 ratio; sources that lose all fragments become inferred nodraw.
4. Coplanar merge -- merge adjacent faces that share an edge and the same material / UV frame; shared-edge endpoints that are not colinear with the new neighbors are kept so concave T-junction corners (e.g. stair side silhouettes) are not chorded away; after each merge, spike cleanup runs and merges that stay non-simple, lose too much area, inflate beyond the input areas, or pinch a keyhole (non-adjacent duplicate verts) are refused (scraps stay separate) so punched openings stay hollow; assign stable ids (`source#N` or `merge/...`).
5. Material sort -- order faces by material, then id, for denser draw batches.

## `VIS1` file contents

Magic `VIS1` (little-endian `0x31534956`), version `2`. Payload is a list of visible faces (polygon, normal, UV shift/scale/axes, uv-lock, interior leaf hint, string-table indices for id / source face id / material) plus a string table. Exact field layout lives in `vis_io.cpp`.

## Source map

| Concern | Location |
|---------|----------|
| Visible faces + weld/merge | `src/map/vis.hpp`, `src/map/vis_build.cpp` |
| VIS file IO | `src/map/vis_io.cpp` |
| CLI | `tools/slopvis/main.cpp` |
| CMake target | root `CMakeLists.txt` (`slopvis`) |
