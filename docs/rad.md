# Radiosity compilation

`sloprad` bakes lightmap atlases under `maps/<name>/rad/`. Requires [BSP](bsp.md) and [VIS](vis.md). Authoring stays on [Maps](maps.md); materials and emission on [Materials](materials.md); bake vs runtime dynamic lights on [Lights](lights.md).

CMake target `sloprad` (root `CMakeLists.txt`), linked against `sloplib`.

```bash
cmake --build build --target sloprad

./build/sloprad --base-game <package-path> [--mod <path>]... --map <name> \
  [--luxels-per-meter N] [--bounces N] [--samples N] [--gpu|--cpu]
```

Shared mount flags with the game: `--base-game` and repeated `--mod`. Tools also require `--map <name>` (folder under `maps/`). The game runtime takes `--map` only when the base package declares it in `data/cli.s7`.

Re-run after VIS face-id churn, material / albedo / emission / ambient edits, or `things.s7` point/spot changes that should affect static light. Missing `rad/` only skips lightmaps at load.

## Tool sequence

Entry point: `tools/sloprad/main.cpp`. Bake: `bakeRadiosity` in `src/map/radiosity.cpp`. Chart packing / `.rad` IO: `src/map/lightmap.cpp`. GPU direct and bounce: `src/map/radiosity_gpu.cpp` with compute shaders `shaders/tools/rad_direct_comp.glsl` and `shaders/tools/rad_bounce_comp.glsl`.

1. Parse CLI (map + bake settings). Defaults: 16 luxels/m, 2 bounces, 16 samples, atlas size 1024 (not CLI), `preferGpu` true unless `--cpu`.
2. Create a hidden OpenGL window (raylib) so GPU compute can run when available. Load direct and bounce shader sources when GPU is preferred.
3. Load map meta (ambient color), require `static.bsp`, load brushes from CSG.
4. Require readable `static.vis`.
5. `analyzeMapHull` for seal / detail warnings (bake still uses VIS faces even if leaky).
6. `collectLightmapFaces` from the VIS file (not raw brush faces), including each face's `interiorLeaf`.
7. Collect `point-light` / `spot-light` things from `things.s7` (and prefabs) as bake emitters.
8. Delete and recreate `maps/<name>/rad/`.
9. `bakeRadiosity` (passes the BSP tree and sealed flag for leaf reachability) -> `static.rad` + atlas PNGs named from the rad sidecar (`atlas0.png`, ...).

## Bake settings

| Setting | CLI | Default | Role |
|---------|-----|---------|------|
| `luxelsPerMeter` | `--luxels-per-meter` | 16 | Nominal chart resolution in world space |
| `bounces` | `--bounces` | 2 | Indirect passes; `0` = ambient + emission + direct only |
| `samples` | `--samples` | 16 | Stratified cosine-hemisphere samples per luxel per bounce |
| `atlasSize` | (none) | 1024 | Atlas edge length in luxels |
| `directWrap` | (none) | 0.35 | Softens direct N*L / N*V |
| `coplanarFill` | (none) | 0.15 | Extra fill between near-coplanar emitter/receiver pairs |
| `ambientScale` | (none) | 1.25 | Multiplies map meta ambient into receiver seed |
| `preferGpu` | `--gpu` / `--cpu` | GPU preferred | Direct and bounce compute paths |

## Bake stages

1. Leaf reachability. When the hull is sealed, BFS open-leaf adjacency into a bitmatrix. Luxels and emitter patches use VIS `interiorLeaf` (fallback: `pointLeaf` on position). Unreachable emitter<->receiver and light<->receiver pairs are skipped before occlusion. Unsealed maps or negative leaf indices disable the cull for that pair.

2. Pack charts. For each lightmap face, measure extent along the face UV axes (locked axes or world-axial basis from the normal). Effective luxels/m equals the setting when `max(extentU, extentV) <= 4`, otherwise half that (large flats get coarser charts). Luxel width/height = `ceil(extent * effectiveLpm) + 2`, clamped to `[2, atlasSize]`. Charts are sorted by descending height, then packed left-to-right in shelves, spilling to a new 1024^2 atlas when needed. The rad sidecar still stores the nominal `luxelsPerMeter`. Each chart records atlas index, pixel origin, luxel size, and normalized UV bounds inset by half a luxel.

3. Resolve materials. Albedo and emission textures load as CPU `Image`s. Sampling uses the same planar UV rules as the game (`texel-size` / pixels-per-meter, texture size, `uv-shift`, optional UV lock). Lighting directions use flat face normals only--no normal maps.

Emission at a world point: `emission-color * emission-power`, multiplied by the emission texel when a map is present. A bright emission texel alone can contribute even when power is zero. Albedo is `base-color` times albedo texel (or base color alone).

4. Emitter volumes. Brushes that contribute emission get a padded AABB. Receiving luxels whose world position falls inside a foreign emitter volume are marked covered (skipped for gather; filled later by inpaint). This reduces light bleeding into solid emitter volumes.

5. Acceleration. `buildLightmapFaceBvh` builds a quad BVH over lightmap face polygons. Direct occlusion and bounce ray hits use that BVH (`bspSegmentOccluded` / `raycastQuadBvh`), ignoring the self face (and the other endpoint face on segments).

6. Emitter patches. From each chart, stride over luxels so patch density stays near at most 8 patches per meter. Bright luxels become patches with position nudged along the normal, radiance from `emissionAt`, area covering the strided grid cells, and an interior leaf tag.

7. Receiving luxels. One sample per chart luxel: world position (cell center in UV, nudged along normal), normal, albedo, emission, irradiance seeded as `ambient * ambientScale + emission`. Luxels whose UV center lies outside the face polygon, or inside a foreign emitter volume, are marked covered and keep ambient only.

8. Direct lighting. For each uncovered luxel and each emitter patch (after leaf cull): reject by wrapped `N*L` / `N*V` (and coplanar-fill alignment) before the segment occlusion test; if clear, accumulate a form-factor term `N*L * N*V * area / (dist^2 pi)` with wrap cosine, plus optional coplanar fill. Entity lights: range and `N*L` (and spot cone) before occlusion. GPU path packs dense luxels/emitters/lights, the BVH, and the reachability bitmatrix into `rad_direct_comp.glsl` when available; otherwise CPU threads over luxels. Failure falls back to CPU.

9. Inpaint covered luxels. Up to 64 passes of 4-neighbor average from uncovered neighbors on the same face grid, so covered holes do not leave black islands.

10. Bounce loop. Build a "shoot" buffer: `(irradiance - emission) * albedo` (zero for covered). For each bounce, each uncovered luxel fires `samples` stratified cosine-weighted hemisphere rays (deterministic per-luxel seed). On hit, bilinear-sample the previous shoot radiance on the hit face (fallback: a fraction of raw ambient). Add the average to irradiance, then rebuild shoot from the newly gathered light times albedo, and inpaint again. GPU path uses `rad_bounce_comp.glsl` when preferred and available; failure falls back to the CPU bounce gather.

11. Denoise. After all bounces, a 3x3 bilateral filter on irradiance per face grid (spatial sigma ~ 1 luxel, range sigma on luminance). Covered luxels are not used as sources; only uncovered luxels are written.

12. Tonemap and write. Per-luxel Reinhard-style map `c / (1 + c)` into 8-bit RGB atlas images. Write `RAD1` version 2 sidecar (`luxelsPerMeter`, atlas list, chart list with face ids and UV rects) and export each atlas PNG under `rad/`.

## `RAD1` sidecar

Magic `RAD1` (`0x31444152`), version `2`. Atlas PNGs are separate files under `rad/`; face ids are VIS fragment ids. Field layout: [Binary formats — RAD1](binary-formats.md#rad1-rad).

## Runtime relationship

At map load, VIS faces (from `static.vis` or an in-memory rebuild) become the draw mesh. If `rad/` loads, chart UVs become mesh lightmap UV2 and atlas textures bind on the lightmap shader. Emission textures mattered at bake time; at runtime the lightmap shader adds a flat emit from material `emission-color` when `emission-power > 0` and does not re-sample the emission map. See [Materials](materials.md).

## Source map

| Concern | Location |
|---------|----------|
| Lightmap faces + pack + rad IO | `src/map/lightmap.cpp` |
| Radiosity bake | `src/map/radiosity.cpp` |
| GPU direct + bounce | `src/map/radiosity_gpu.cpp` |
| Direct compute shader | `packages/engine/shaders/tools/rad_direct_comp.glsl` |
| Bounce compute shader | `packages/engine/shaders/tools/rad_bounce_comp.glsl` |
| Surface / segment rays | `src/map/bsp_ray.cpp`, `src/map/quad_bvh.cpp` |
| CLI | `tools/sloprad/main.cpp` |
| CMake target | root `CMakeLists.txt` (`sloprad`) |
