# BSP, VIS, and radiosity compilation

This page is the detailed companion to the short compile outline in [Maps](maps.md). It describes what `slopbsp`, `slopvis`, and `sloprad` actually do, how the artifacts are structured, and when each step must be re-run. Authoring of `static.csg` / `map.meta` stays on the maps page; materials and emission on [Materials](materials.md); bake vs runtime dynamic lights on [Lights](lights.md).

The tools are ordinary CMake targets in the root `CMakeLists.txt`: `slopbsp`, `slopvis`, and `sloprad`, all linked against `sloplib`. Building the project produces the binaries; map compile is not an automatic CMake dependency of `slopengine`-you run the tools against package content yourself (or wire custom commands in a game project that consumes this engine).

```bash
cmake -S . -B build
cmake --build build --target slopbsp slopvis sloprad

./build/slopbsp --base-game <package-path> --map <name>
./build/slopvis --base-game <package-path> --map <name>
./build/sloprad --base-game <package-path> --map <name> \
  [--luxels-per-meter N] [--bounces N] [--samples N] [--gpu|--cpu]
```

## Pipeline contract

| Stage | Input | Output | Required to load map? |
|-------|--------|--------|------------------------|
| Author | - | `map.meta`, `static.csg` (+ optional `things.s7`) | meta + CSG yes |
| `slopbsp` | meta + CSG (hull brushes) | `static.bsp` | yes |
| `slopvis` | CSG + `static.bsp` | `static.vis` | preferred (in-memory fallback) |
| `sloprad` | meta + CSG + `static.bsp` + `static.vis` + materials/textures | `rad/static.rad`, `rad/atlasN.png` | no |

Order is strict: VIS refuses an unsealed hull or missing BSP; radiosity refuses a missing VIS. After hull or detail brush edits that affect face ids or sealing, rebuild BSP, then VIS, then rad. After material or emission-only edits, BSP and VIS can stay; re-bake rad. Missing `rad/` only skips lightmaps.

Shared flags with the game: `--base-game`, repeated `--mod`, and `--map <name>` (folder name under `maps/`, not a file path).

## What the BSP is for

`static.bsp` is structural data built from hull brushes only. It is not the visible mesh and not the physics mesh.

- Sealing / leaks. Exterior empty space is flooded from the padded world bounds through empty-leaf adjacency. If that flood reaches the playable interior, the hull is leaky.
- VIS prerequisite. `slopvis` uses sealed interior empty leaves to clip hull faces into drawable fragments.
- Runtime. The game requires BSP to load. It uses the tree for hull analysis, leaf-related debug, and as input when rebuilding VIS in memory if `static.vis` is missing. Collision comes from convex hulls per brush through the physics library.
- Radiosity prerequisite. `sloprad` loads the BSP for hull analysis / detail warnings. Bake-time ray occlusion itself is against a BVH of lightmap (VIS) faces, not by walking the BSP node tree as a mesh.

Detail brushes never contribute split planes and cannot seal a leak. Their centers must sit in sealed interior empty space or `slopbsp` / `slopvis` / `sloprad` warn.

## What VIS is for

`static.vis` is the **visible face fragment list** used for the draw mesh, lightmap charts, and Steam Audio occlusion. Despite the Quake-style name, it is **not** a leaf↔leaf PVS bitset and does not drive runtime portal culling.

- Interior clip. Hull faces are clipped against sealed interior empty leaf polyhedra so large walls that are only partly playable keep only the visible polygon(s).
- Inferred nodraw. Hull faces with zero remaining visible area are treated as nodraw (outer skins, buried sides). Authored `(nodraw)` is never cleared.
- Stable face ids. Fragments use ids such as `wall/north#0`; coplanar merges across sources use `merge/…` ids. Charts and mesh UV2 key off those ids.
- Detail. Detail faces pass through unchanged in v1 (they already sit in interior empty by policy).

## BSP compilation (`slopbsp`)

Entry point: `tools/slopbsp/main.cpp`. Core build: `buildBspFromHullBrushes` in `src/map/bsp_build.cpp`. Analysis: `analyzeMapHull` in `src/map/bsp_analyze.cpp`. On-disk format: `BSP2` via `src/map/bsp_io.cpp`.

### Tool sequence

1. Parse CLI (`AppConfig`); require `--map`.
2. Mount packages through `AssetStore`, init s7, load brushes with `loadMapBrushes`.
3. `buildBspFromHullBrushes` → in-memory `BspTree`.
4. Resolve `maps/<name>/static.csg` and write sibling `static.bsp` with `writeBspFile`.
5. `analyzeMapHull`. If not sealed: log a leaf-center leak path, exit 1 (file is still written for debugging). If sealed: log exterior/interior empty counts, preview visible-face / inferred-nodraw counts via `buildVisibleFaces` (does not write `.vis`), print detail-outside warnings; exit 0.

### Build algorithm

Constants used while building: plane epsilon `1e-4`, world bounds pad `0.5` world units, minimum face area `1e-6`.

1. Gather hulls. Copy brushes with `BrushRole::Hull`. Detail / box / nocollide counts are logged only. No hulls → empty tree warning and return.

2. World bounds. Axis-aligned bounds of all hull brushes, expanded by `kBoundsPad` on every side. Stored as `boundsMins` / `boundsMaxs`. That padding is what creates a ring of exterior empty leaves outside the solid shell.

3. Split plane list. Every unique face plane from hull faces (`normal` + distance through the first vertex). Planes match when normals align (`dot ≥ 0.999`) and distances differ by at most the plane epsilon. Order is first-seen; the recursive builder picks the first plane that still cuts the current cell.

4. Root polyhedron. An axis-aligned box polyhedron from the padded bounds (six quads).

5. Recursive partition (`buildNode`). For the current polyhedron:

- Find the first split plane that has vertices both in front of and behind it.
- If none (or the geometric split fails): emit a leaf. The leaf stores the polyhedron’s faces and AABB. It is solid if the polyhedron centroid lies inside any hull brush; otherwise empty. Child indices encode leaves as negative (`-leafIndex - 1`).
- If a plane cuts: clip every face against the plane (Sutherland–Hodgman style), build cap polygons on the cut from coplanar clip vertices (sorted around the plane), recurse front then back, store a `BspNode` with plane + front/back children.

There is no SAH or balancing heuristic. Plane order is the unique-face list order. That keeps the builder simple; maps with many distinct hull planes produce deeper trees.

6. Empty-leaf adjacency. Every pair of empty leaves is tested for a shared portal: opposing coplanar faces whose 2D projections overlap in the face plane. Matching pairs append each other to `neighbors`. Solid leaves are ignored. Adjacency is what the exterior flood walks.

7. Occlusion surfaces. For each hull face, probe a point slightly along the outward normal. If that point lands in an empty leaf, emit a `BspSurfaceFace` (vertices, normal, empty-leaf index, face id, material, UV shift) with winding oriented to the brush normal. Buried or exterior-only faces that do not face empty space are omitted here. Surfaces remain available for BSP surface raycasts; draw / bake / audio use VIS instead.

### Hull analysis (after write)

Exterior flood. Mark every empty leaf that touches the padded world AABB, then BFS through `neighbors`. Marked leaves are exterior empty; unmarked empty leaves are interior empty.

Sealed. The hull is sealed when at least one interior empty leaf exists. If every empty leaf is exterior-connected, the playable volume is open to the outside → leak.

Leak path. On failure, BFS from bound-touching empty leaves toward the empty leaf whose centroid is closest to the hull AABB center. The logged path is a chain of `leaf:N@(x,y,z)` strings for debugging in-world.

Detail warnings. Each detail brush center is classified with `pointLeaf`. If that leaf is not sealed interior empty, emit `detail '<id>' is outside sealed hull`.

Inferred nodraw is no longer decided by sparse whole-face probes in analysis. That decision belongs to `slopvis` (zero visible area after clip).

### `BSP2` file contents

Magic `BSP2` (little-endian `0x32505342`), version `2`. Payload includes root index, padded bounds, nodes (plane + front/back), leaves (solid flag, AABB, face polygons, neighbor indices), surface faces (geometry + string-table indices for id/material), and a string table. Exact field layout lives in `bsp_io.cpp`; treat the version as the compatibility gate.

## VIS compilation (`slopvis`)

Entry point: `tools/slopvis/main.cpp`. Core build: `buildVisibleFaces` in `src/map/vis_build.cpp`. On-disk format: `VIS1` via `src/map/vis_io.cpp`.

### Tool sequence

1. Parse CLI (`AppConfig`); require `--map`.
2. Require readable `static.bsp`; load CSG brushes.
3. `analyzeMapHull`. If not sealed: log leak path, exit 1 (no `.vis` written).
4. `buildVisibleFaces` → clip, weld, merge.
5. Write sibling `static.vis` with `writeVisFile`; log face and inferred-nodraw counts.

### Face visibility algorithm

For each non-authored-nodraw **hull** face:

1. Clip the face polygon against every sealed interior empty leaf polyhedron (Sutherland–Hodgman against outward leaf planes).
2. Keep fragments whose outward-nudged centroid sample lands in interior empty.
3. Discard zero-area scraps; micro-merge coplanar adjacent scraps from the same source face.
4. Assign stable fragment ids derived from the source face id (e.g. `wall/north#0`).
5. If no fragments remain → inferred nodraw for that source face id.

Detail faces and faces on an unsealed fallback path pass through with their authored ids.

After fragments are collected:

1. T-junction weld — insert vertices where another face’s vertex lies on an edge.
2. Coplanar merge — merge adjacent faces that share an edge and the same material / UV frame; multi-source merges get `merge/…` ids.

### `VIS1` file contents

Magic `VIS1` (little-endian `0x31534956`), version `1`. Payload is a list of visible faces (polygon, normal, UV shift/axes, uv-lock, interior leaf hint, string-table indices for id / source face id / material) plus a string table. Exact field layout lives in `vis_io.cpp`.

## Radiosity compilation (`sloprad`)

Entry point: `tools/sloprad/main.cpp`. Bake: `bakeRadiosity` in `src/map/radiosity.cpp`. Chart packing / `.rad` IO: `src/map/lightmap.cpp`. Optional GPU direct pass: `src/map/radiosity_gpu.cpp` with compute shader `shaders/tools/rad_direct_comp.glsl`.

### Tool sequence

1. Parse CLI (map + bake settings). Defaults: 16 luxels/m, 2 bounces, 32 samples, atlas size 512 (not CLI), `preferGpu` true unless `--cpu`.
2. Create a hidden OpenGL window (raylib) so GPU compute can run when available.
3. Load map meta (ambient color), require `static.bsp`, load brushes from CSG.
4. Require readable `static.vis`.
5. `analyzeMapHull` for seal / detail warnings (bake still uses VIS faces even if leaky).
6. `collectLightmapFaces` from the VIS file (not raw brush faces).
7. Collect `point-light` / `spot-light` things from `things.s7` (and prefabs) as bake emitters.
8. Delete and recreate `maps/<name>/rad/`.
9. `bakeRadiosity` → `static.rad` + atlas PNGs named from the rad sidecar (`atlas0.png`, …).

### Bake settings

| Setting | CLI | Default | Role |
|---------|-----|---------|------|
| `luxelsPerMeter` | `--luxels-per-meter` | 16 | Chart resolution in world space |
| `bounces` | `--bounces` | 2 | Indirect passes; `0` = ambient + emission + direct only |
| `samples` | `--samples` | 32 | Cosine-hemisphere samples per luxel per bounce |
| `atlasSize` | (none) | 512 | Atlas edge length in luxels |
| `directWrap` | (none) | 0.35 | Softens direct N·L / N·V |
| `coplanarFill` | (none) | 0.15 | Extra fill between near-coplanar emitter/receiver pairs |
| `ambientScale` | (none) | 1.25 | Multiplies map meta ambient into receiver seed |
| `preferGpu` | `--gpu` / `--cpu` | GPU preferred | Direct lighting path only |

### Bake stages

1. Pack charts. For each lightmap face, measure extent along the face UV axes (locked axes or world-axial basis from the normal). Luxel width/height = `ceil(extent * luxelsPerMeter) + 2`, clamped to `[2, atlasSize]`. Charts pack left-to-right, wrapping rows, spilling to a new 512² atlas when needed. Each chart records atlas index, pixel origin, luxel size, and normalized UV bounds inset by half a luxel.

2. Resolve materials. Albedo and emission textures load as CPU `Image`s. Sampling uses the same planar UV rules as the game (`texel-size` / pixels-per-meter, texture size, `uv-shift`, optional UV lock). Lighting directions use flat face normals only-no normal maps.

Emission at a world point: `emission-color * emission-power`, multiplied by the emission texel when a map is present. A bright emission texel alone can contribute even when power is zero. Albedo is `base-color` times albedo texel (or base color alone).

3. Emitter volumes. Brushes that contribute emission get a padded AABB. Receiving luxels whose world position falls inside a foreign emitter volume are marked covered (skipped for gather; filled later by inpaint). This reduces light bleeding into solid emitter volumes.

4. Acceleration. `buildLightmapFaceBvh` builds a quad BVH over lightmap face polygons. Direct occlusion and bounce ray hits use that BVH (`bspSegmentOccluded` / `raycastQuadBvh`), ignoring the self face (and the other endpoint face on segments).

5. Emitter patches. From each chart, stride over luxels so patch density stays near at most 8 patches per meter. Bright luxels become patches with position nudged along the normal, radiance from `emissionAt`, and area covering the strided grid cells.

6. Receiving luxels. One sample per chart luxel: world position (cell center in UV, nudged along normal), normal, albedo, emission, irradiance seeded as `ambient * ambientScale + emission`. Covered luxels keep ambient only.

7. Direct lighting. For each uncovered luxel and each emitter patch: segment occlusion test; if clear, accumulate a form-factor term `N·L * N·V * area / (dist² π)` with wrap cosine, plus optional coplanar fill. GPU path packs dense luxels/emitters and the BVH into a compute dispatch when the shader and GL compute are available; otherwise CPU threads over luxels. Failure falls back to CPU.

8. Inpaint covered luxels. Up to 64 passes of 4-neighbor average from uncovered neighbors on the same face grid, so covered holes do not leave black islands.

9. Bounce loop. Build a “shoot” buffer: `(irradiance - emission) * albedo` (zero for covered). For each bounce, each uncovered luxel fires `samples` cosine-weighted hemisphere rays. On hit, bilinear-sample the previous shoot radiance on the hit face (fallback: a fraction of raw ambient). Add the average to irradiance, then rebuild shoot from the newly gathered light times albedo, and inpaint again.

10. Tonemap and write. Per-luxel Reinhard-style map `c / (1 + c)` into 8-bit RGB atlas images. Write `RAD1` version 2 sidecar (`luxelsPerMeter`, atlas list, chart list with face ids and UV rects) and export each atlas PNG under `rad/`.

### `RAD1` sidecar

Magic `RAD1` (`0x31444152`), version `2`. Stores luxels-per-meter, atlas texture path stems + sizes, and charts keyed by face id / face index with atlas thing and UV bounds. Atlases are separate PNGs; the sidecar does not embed pixels. Face ids are VIS fragment ids.

### Runtime relationship

At map load, VIS faces (from `static.vis` or an in-memory rebuild) become the draw mesh. If `rad/` loads, chart UVs become mesh lightmap UV2 and atlas textures bind on the lightmap shader. Emission textures mattered at bake time; at runtime the lightmap shader adds a flat emit from material `emission-color` when `emission-power > 0` and does not re-sample the emission map. See [Materials](materials.md).

## Rebuild cheatsheet

| You changed… | Run |
|--------------|-----|
| Hull brushes, sealing, or hull face layout | `slopbsp`, then `slopvis`, then `sloprad` if you use lightmaps |
| Detail brushes only (no hull / face-id churn) | `slopvis`, then `sloprad`; run `slopbsp` if you care about detail-outside warnings or stale analysis |
| Face ids | `slopvis` then `sloprad` (charts key off VIS ids); `slopbsp` if hull planes changed |
| Materials, albedo, emission, ambient | `sloprad` |
| `things.s7` light thing change | Re-bake `sloprad` if point/spot should change static light; runtime `DynamicLight` is separate ([Lights](lights.md)) |
| Authored `(nodraw)` | `slopvis`, then `sloprad` |

## Source map

| Concern | Location |
|---------|----------|
| BSP types | `src/map/bsp.hpp` |
| Tree build | `src/map/bsp_build.cpp` |
| Leak / hull analysis | `src/map/bsp_analyze.cpp` |
| BSP file IO | `src/map/bsp_io.cpp` |
| Visible faces + weld/merge | `src/map/vis.hpp`, `src/map/vis_build.cpp` |
| VIS file IO | `src/map/vis_io.cpp` |
| Surface / segment rays | `src/map/bsp_ray.cpp`, `src/map/quad_bvh.cpp` |
| Lightmap faces + pack + rad IO | `src/map/lightmap.cpp` |
| Radiosity bake | `src/map/radiosity.cpp` |
| GPU direct | `src/map/radiosity_gpu.cpp` |
| CLI tools | `tools/slopbsp/main.cpp`, `tools/slopvis/main.cpp`, `tools/sloprad/main.cpp` |
| CMake targets | root `CMakeLists.txt` (`slopbsp`, `slopvis`, `sloprad`) |
