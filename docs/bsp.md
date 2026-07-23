# BSP compilation

`slopbsp` builds the structural hull tree `static.bsp`. Authoring of `static.csg` / `map.meta` stays on [Maps](maps.md). Visible faces: [VIS](vis.md). Lightmaps: [Radiosity](rad.md).

CMake target `slopbsp` (root `CMakeLists.txt`), linked against `sloplib`. Map compile is not an automatic dependency of `slopengine`--run the tool against package content yourself.

```bash
cmake --build build --target slopbsp

./build/slopbsp --base-game <package-path> [--mod <path>]... --map <name>
```

Shared mount flags with the game: `--base-game` and repeated `--mod`. Tools also require `--map <name>` (folder under `maps/`). The game runtime takes `--map` only when the base package declares it in `data/cli.s7`.

Re-run when hull brushes, sealing, or hull face layout change. Downstream [VIS](vis.md) (and [radiosity](rad.md) if you use lightmaps) must follow.

## What the BSP is for

`static.bsp` is structural data built from hull brushes only. It is not the visible mesh and not the physics mesh.

- Sealing / leaks. Exterior empty space is flooded from the padded world bounds through empty-leaf adjacency. If that flood reaches the playable interior, the hull is leaky.
- VIS prerequisite. `slopvis` uses sealed interior empty leaves to clip hull and detail faces into drawable fragments.
- Runtime. The game requires BSP to load. It uses the tree for hull analysis, leaf-related debug, and as input when rebuilding VIS in memory if `static.vis` is missing. Collision comes from convex hulls per brush through the physics library.
- Radiosity prerequisite. `sloprad` loads the BSP for hull analysis / detail warnings and open-leaf reachability culling. Bake-time ray occlusion itself is against a BVH of lightmap (VIS) faces, not by walking the BSP node tree as a mesh.

Detail brushes never contribute split planes and cannot seal a leak. Hint planes split without sealing. Window brushes seal like hull (`Glass` contents). Trigger / water / hint / detail centers must sit in sealed interior open space or `slopbsp` / `slopvis` / `sloprad` warn.

## Why this is not a modern mesh pipeline

Most current editors treat a level as an arbitrary triangle soup (or a collection of meshes) plus separate systems for collision, visibility, and lighting. You place geometry anywhere; a BVH or GPU raster path draws it; navmesh / probes / lightmaps are built from whatever faces the baker is told to include. There is usually no authored "inside" that must be watertight against the void.

This BSP path is older Quake-family thinking. A modern mesh level draws whatever is in the scene and often culls at runtime (portals, HZB, GPU occlusion). Here the draw mesh comes from VIS face fragments clipped to sealed interior empty space -- an offline visible-face set (`static.vis`), not leaf<->leaf PVS. Collision is also separate from the BSP: each brush keeps its own convex hull for physics; the tree is structural, not the physics mesh.

Modern tools usually have no global seal requirement. Here the hull must enclose playable empty space or compile fails a leak check. Detail and structure are not the same mesh kind either: brush **role** decides who splits the tree, who seals against the void, and who only decorates inside an already sealed volume.

The tree exists so empty space can be classified as exterior (connected to the padded world bounds) versus interior (playable). That classification drives leak detection, which faces are "inside," and what VIS / radiosity are allowed to treat as the level. A triangle mesh with a hole in the floor is usually fine in a modern editor; here it is a leak because exterior flood walks into the room.

## Why hull and non-hull

Brush roles are how authors mark structure versus content that lives *inside* structure. The full role matrix (splits / seals / VIS / physics) is on [Maps](maps.md#staticcsg).

**Hull** (and **window**, which seals as `Glass`) form the airtight shell. Their faces supply split planes and sealing contents (`Solid` / `Glass`). Without a closed hull, there is no sealed interior open leaf set, so VIS cannot decide what is buried versus playable and the map is rejected as leaky (the `.bsp` is still written for debug).

**Non-hull** roles do not seal, and that is the point. `detail` is for furniture, trim, and clutter that should still draw and lightmap but must not punch holes in the shell or explode the tree with extra split planes -- detail never splits and never seals. `hint` can split leaves without adding solid, when you want to reshape the tree on purpose. `trigger` and `water` mark open leaves for later gameplay; they must sit in already-sealed interior (`water` may split, `trigger` does not). `window` is the exception among "thin" pieces: it splits and seals as `Glass` so openings stay closed to exterior flood.

If every decorative crate were hull, each face would become a candidate split plane, the tree would fragment, and any gap between crate and floor could open a leak path from exterior into the room. Marking those brushes detail keeps them out of sealing: they still collide and still feed VIS faces when their surfaces see interior empty space, but they cannot define or break the shell.

Authoring rule of thumb: walls, floors, ceilings, and anything that must keep the void out -> `hull` (or `window` for sealed openings). Everything that only exists in the playable volume -> `detail` (or trigger / water / hint as needed). Prefab furniture should usually ship as detail; modular room pieces that form the shell should ship as hull.

## Tool sequence

Entry point: `tools/slopbsp/main.cpp`. Core build: `buildBspFromHullBrushes` in `src/map/bsp_build.cpp`. Analysis: `analyzeMapHull` in `src/map/bsp_analyze.cpp`. On-disk format: `BSP2` via `src/map/bsp_io.cpp`.

1. Parse CLI (`AppConfig`); require `--map`.
2. Mount packages through `AssetStore`, init s7, load brushes with `loadMapBrushes`.
3. `buildBspFromHullBrushes` -> in-memory `BspTree`.
4. Resolve `maps/<name>/static.csg` and write sibling `static.bsp` with `writeBspFile`.
5. `analyzeMapHull`. If not sealed: log a leaf-center leak path, exit 1 (file is still written for debugging). If sealed: log exterior/interior empty counts, preview visible-face / inferred-nodraw counts via `buildVisibleFaces` (does not write `.vis`), print detail-outside warnings; exit 0.

## Build algorithm

Constants used while building: plane epsilon `1e-4`, world bounds pad `0.5` world units, minimum face area `1e-6`.

1. Gather brushes. Sealing set = `hull` + `window`. Soft contents = `water` + `trigger`. Split planes from `hull`, `window`, `water`, and `hint`. Surface faces from sealing brushes. No sealing brushes -> empty tree warning and return.

2. World bounds. Axis-aligned bounds of sealing brushes, expanded by `kBoundsPad` on every side. Stored as `boundsMins` / `boundsMaxs`. That padding is what creates a ring of exterior open leaves outside the solid shell.

3. Split plane list. Unique face planes from split-contributing brushes. Planes match when normals align (`dot >= 0.999`) and distances differ by at most the plane epsilon. Hint-only planes are marked so scoring prefers structural splits.

4. Root polyhedron. An axis-aligned box polyhedron from the padded bounds (six quads).

5. Recursive partition (`buildNode`). For the current polyhedron:

- Classify the cell against sealing brushes as wholly solid, wholly open, or mixed (vertex + centroid samples).
- Wholly solid -> emit a leaf with `Solid` or `Glass` contents immediately (early-out; no further carving of thick walls).
- Otherwise score remaining unused planes that cut the cell: face-count balance - split penalty, plus axial alignment, plus a bonus for separating solid-vs-open samples; hint-only planes are penalized.
- If none (or the geometric split fails): emit an open leaf and OR in `Water` / `Trigger` when the centroid sits in those brushes.
- If a plane cuts: clip every face against the plane (Sutherland-Hodgman style), build cap polygons on the cut, recurse front then back, store a `BspNode` with plane + front/back children. Child indices encode leaves as negative (`-leafIndex - 1`).

6. Open-leaf adjacency / portals. Walk the tree; for each front/back open-leaf pair with overlapping AABBs, clip opposing coplanar faces into a portal polygon (`intersectPortal`). Store `BspPortal` entries and neighbor links. Adjacency is what the exterior flood walks. Debug overlays draw stored portals.

7. Occlusion surfaces. For each sealing (`hull` / `window`) face, probe a point slightly along the outward normal. If that point lands in an open leaf, emit a `BspSurfaceFace`. Buried or exterior-only faces that do not face open space are omitted. Surfaces remain available for BSP surface raycasts; draw / bake / audio use VIS instead.

## Hull analysis (after write)

Exterior flood. Mark every open leaf that touches the padded world AABB, then BFS through `neighbors`. Marked leaves are exterior open; unmarked open leaves are interior open. Leaves with `Solid` or `Glass` never participate.

Sealed. The hull is sealed when at least one interior open leaf exists. If every open leaf is exterior-connected, the playable volume is open to the outside -> leak.

Leak path. On failure, BFS from bound-touching open leaves toward the open leaf whose centroid is closest to the sealing AABB center. The logged path is a chain of `leaf:N@(x,y,z)` strings for debugging in-world.

Interior placement warnings. Each `detail` / `hint` / `trigger` / `water` brush center is classified with `pointLeaf`. If that leaf is not sealed interior open, emit `'<role> <id> is outside sealed hull'`.

Inferred nodraw is no longer decided by sparse whole-face probes in analysis. That decision belongs to `slopvis` (zero visible area after clip).

## `BSP2` file contents

Magic `BSP2` (little-endian `0x32505342`), version `3`. Payload includes root index, padded bounds, nodes (plane + front/back), leaves (`contents` u32 + AABB + face polygons + neighbor indices), portals (`leafA`, `leafB`, polygon), surface faces (geometry + string-table indices for id/material), and a string table. Contents bits: `Solid`, `Glass`, `Water`, `Trigger`. Exact field layout lives in `bsp_io.cpp`; treat the version as the compatibility gate.

## Source map

| Concern | Location |
|---------|----------|
| BSP types | `src/map/bsp.hpp` |
| Tree build | `src/map/bsp_build.cpp` |
| Leak / hull analysis | `src/map/bsp_analyze.cpp` |
| BSP file IO | `src/map/bsp_io.cpp` |
| CLI | `tools/slopbsp/main.cpp` |
| CMake target | root `CMakeLists.txt` (`slopbsp`) |
