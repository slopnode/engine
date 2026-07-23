# VIS compilation (PVS)

slopvis builds the leaf↔leaf potentially visible set `static.vis` (PVS1). Requires a sealed [BSP](bsp.md) and an existing [FAC](fac.md) file (pipeline gate; FAC is not read as input). Authoring stays on [Maps](maps.md). Runtime uses PVS for object culling and visibility queries, not for subsetting world FAC geometry: [View frustum culling](frustum.md).

CMake target slopvis (root CMakeLists.txt), linked against sloplib.

```bash
cmake --build build --target slopvis

./build/slopvis --base-game {package-path} [--mod {path}]... --map {name}
```

Shared mount flags with the game: --base-game and repeated --mod. Tools also require --map {name}. Re-run after BSP portal / leaf changes. [Radiosity](rad.md) requires VIS to have been run even though bake still uses its own leaf-reachability cull.

## What VIS is for

`static.vis` is a **leaf↔leaf PVS bitmatrix**. From the camera leaf (or any point's leaf), it answers which other leaves might be visible through the portal graph. It does **not** replace [FAC](fac.md); world draw still uses the full FAC mesh.

- One cluster per open leaf in v1 (no auto room merge).
- Portal-flow visibility with winding clipping through `BspPortal` polygons.
- Symmetric bits (if A might see B, B might see A).
- Solid / non-open leaves are unused as sources.

## Tool sequence

Entry point: tools/slopvis/main.cpp. Core build: buildPvs in src/map/pvs_build.cpp. On-disk format: PVS1 via src/map/pvs_io.cpp.

1. Parse CLI (AppConfig); require --map.
2. Require readable static.bsp and static.fac.
3. analyzeMapHull. If not sealed: log leak path, exit 1 (no .vis written).
4. buildPvs from BSP portals.
5. Write sibling static.vis with writePvsFile.

## PVS1 file contents

Magic PVS1 (0x31535650), version 1. Field layout: [Binary formats — PVS1](binary-formats.md#pvs1-vis).

## Source map

| Concern | Location |
|---------|----------|
| PVS build | src/map/pvs.hpp, src/map/pvs_build.cpp |
| PVS file IO | src/map/pvs_io.cpp |
| CLI | tools/slopvis/main.cpp |
| CMake target | root CMakeLists.txt (slopvis) |
