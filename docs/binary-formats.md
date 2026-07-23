# Binary formats

Custom little-endian binary schemas used by package and map assets. Domain guides cover build/load semantics; this page is the field layout reference for tool authors.

Related: [Package structure](package-structure.md), [Maps](maps.md), [BSP](bsp.md), [VIS](vis.md), [RAD](rad.md), [Geometry](geometry.md), [Skeletal animation](animation.md).

PNG, OGG, and TTF are external formats and are not specified here. Prop meshes use .geo / .vert (this page covers the binary companions). Text / S-expression assets (.geo, .skel, .anim, .mat, .csg, .s7, …) stay on their own pages.

## Conventions

| Concept | Encoding |
|---------|----------|
| Endianness | Little-endian throughout |
| f32 / u8 / u16 / u32 / i32 | Native IEEE / unsigned / two’s-complement sizes |
| Vector2 | 2×f32 (x, y) |
| Vector3 | 3×f32 (x, y, z) |
| Length-prefixed string | u32 byte length, then that many bytes (no trailing NUL) |
| Polygon | u32 vertex count, then that many Vector3 |

Map sidecars (BSP2, VIS1, RAD1) store magic as a u32 fourCC in little-endian (ASCII when viewed on a LE host). Geometry / skeleton / track files (DLK*) store magic as **four ASCII bytes**, then a u16 version.

Writers are authoritative: src/map/bsp_io.cpp, vis_io.cpp, lightmap.cpp, and tools/blender/slopengine_exporter/format_utils.py with matching loaders under src/assets/.

## BSP2 (.bsp)

Path: maps/{name}/static.bsp. Magic 0x32505342 (BSP2), version 3. Build and contents bits: [BSP](bsp.md).

### Header

| Field | Type | Notes |
|-------|------|-------|
| magic | u32 | 0x32505342 |
| version | u32 | 3 |
| root | i32 | Root node or leaf index |
| boundsMins | Vector3 | Tree AABB |
| boundsMaxs | Vector3 | Tree AABB |

### Nodes

| Field | Type | Notes |
|-------|------|-------|
| nodeCount | u32 |  |
| nodes[nodeCount] | … |  |

Each node:

| Field | Type | Notes |
|-------|------|-------|
| plane.normal | Vector3 | Split plane |
| plane.distance | f32 |  |
| front | i32 | Child index |
| back | i32 | Child index |

### Leaves

| Field | Type | Notes |
|-------|------|-------|
| leafCount | u32 |  |
| leaves[leafCount] | … |  |

Each leaf:

| Field | Type | Notes |
|-------|------|-------|
| contents | u32 | Bit flags: Solid 1<<0, Glass 1<<1, Water 1<<2, Trigger 1<<3 |
| mins | Vector3 | Leaf AABB |
| maxs | Vector3 | Leaf AABB |
| faceCount | u32 |  |
| faces[faceCount] | Polygon | Leaf polyhedron faces |
| neighborCount | u32 |  |
| neighbors[neighborCount] | i32 | Neighbor leaf indices |

### Portals

| Field | Type | Notes |
|-------|------|-------|
| portalCount | u32 |  |
| portals[portalCount] | … |  |

Each portal:

| Field | Type | Notes |
|-------|------|-------|
| leafA | i32 |  |
| leafB | i32 |  |
| vertices | Polygon | Portal polygon |

### Surface faces

| Field | Type | Notes |
|-------|------|-------|
| surfaceFaceCount | u32 |  |
| surfaceFaces[surfaceFaceCount] | … |  |

Each surface face:

| Field | Type | Notes |
|-------|------|-------|
| vertices | Polygon |  |
| normal | Vector3 |  |
| emptyLeaf | i32 |  |
| uvShiftPixels | Vector2 |  |
| idIndex | u32 | Index into string table |
| materialIndex | u32 | Index into string table |

### String table

| Field | Type | Notes |
|-------|------|-------|
| stringCount | u32 | Index 0 is always the empty string |
| strings[stringCount] | string | Length-prefixed |

## VIS1 (.vis)

Path: maps/{name}/static.vis. Magic 0x31534956 (VIS1), version 2. Semantics: [VIS](vis.md).

The reader also accepts version 1 (same layout without uvScale; defaults to (1, 1)).

### Header

| Field | Type | Notes |
|-------|------|-------|
| magic | u32 | 0x31534956 |
| version | u32 | 2 (or 1) |
| faceCount | u32 |  |

### Faces

Each face (repeated faceCount times):

| Field | Type | Notes |
|-------|------|-------|
| vertices | Polygon |  |
| normal | Vector3 |  |
| uvShiftPixels | Vector2 |  |
| uvScale | Vector2 | Present only when version >= 2 |
| uvUAxis | Vector3 |  |
| uvVAxis | Vector3 |  |
| uvLock | u8 | 0 or 1 |
| interiorLeaf | i32 | Hint leaf index |
| idIndex | u32 | String table |
| sourceFaceIdIndex | u32 | String table |
| materialIndex | u32 | String table |

### String table

| Field | Type | Notes |
|-------|------|-------|
| stringCount | u32 | Index 0 is always the empty string |
| strings[stringCount] | string | Length-prefixed |

## RAD1 (.rad)

Path: maps/{name}/rad/static.rad. Magic 0x31444152 (RAD1), version 2. Semantics: [RAD](rad.md).

Atlas pixels are **not** embedded; they are separate PNGs under rad/ (for example atlas0.png). Face ids match VIS fragment ids. Strings in this file are inline length-prefixed (no trailing string table).

### Header

| Field | Type | Notes |
|-------|------|-------|
| magic | u32 | 0x31444152 |
| version | u32 | 2 |
| luxelsPerMeter | f32 | Nominal bake density |

### Atlases

| Field | Type | Notes |
|-------|------|-------|
| atlasCount | u32 |  |
| atlases[atlasCount] | … |  |

Each atlas:

| Field | Type | Notes |
|-------|------|-------|
| texturePath | string | Virtual path stem for the PNG |
| width | u32 |  |
| height | u32 |  |

### Charts

| Field | Type | Notes |
|-------|------|-------|
| chartCount | u32 |  |
| charts[chartCount] | … |  |

Each chart:

| Field | Type | Notes |
|-------|------|-------|
| faceIndex | u32 | Index into the VIS face list |
| faceId | string | VIS fragment id |
| atlasIndex | u32 |  |
| luxelWidth | u32 |  |
| luxelHeight | u32 |  |
| atlasX | u32 | Pixel origin in atlas |
| atlasY | u32 | Pixel origin in atlas |
| u0 | f32 | Normalized lightmap UV bounds |
| v0 | f32 |  |
| u1 | f32 |  |
| v1 | f32 |  |

## DLKV (.vert)

Path: geometry/{path}.vert (sibling of .geo). Magic ASCII DLKV, version 1. Text companion: [Geometry](geometry.md).

| Field | Type | Notes |
|-------|------|-------|
| magic | char[4] | D L K V |
| version | u16 | 1 |
| vertexCount | u32 |  |
| indexCount | u32 |  |
| flags | u16 | 1 = normals present, 2 = UVs present (bits may combine) |
| positions | f32[vertexCount×3] | Interleaved x y z |
| normals | f32[vertexCount×3] | Only if flags & 1 |
| uvs | f32[vertexCount×2] | Only if flags & 2 |
| indices | u32[indexCount] | Triangle indices into the shared vertex buffer |

Primitives in .geo select ranges via vertex/index offsets and counts.

## DLKW (.weights)

Path: geometry/{path}.weights. Magic ASCII DLKW, version 1. Written only for skinned meshes.

| Field | Type | Notes |
|-------|------|-------|
| magic | char[4] | D L K W |
| version | u16 | 1 |
| vertexCount | u32 | Must match .vert |
| verts[vertexCount] | … |  |

Each vertex:

| Field | Type | Notes |
|-------|------|-------|
| jointIndices | u8[4] | Bone indices |
| jointWeights | f32[4] | Corresponding weights |

## DLKB (.bind)

Path: skeletons/{path}.bind (same stem as .skel). Magic ASCII DLKB, version 1.

| Field | Type | Notes |
|-------|------|-------|
| magic | char[4] | D L K B |
| version | u16 | 1 |
| boneCount | u32 | Must match .skel bone order |
| matrices[boneCount] | f32[16] | Column-major 4×4, engine Z-up / raylib float order |

## DLKT (.tracks)

Path: animations/{path}.tracks (named from an .anim clip). Magic ASCII DLKT.

| Field | Type | Notes |
|-------|------|-------|
| magic | char[4] | D L K T |
| version | u16 | 1 = TRS, 2 = matrix |
| boneCount | u32 | Must match skeleton |
| frameCount | u32 |  |
| poses | … | frameCount × boneCount entries |

Pose entry by version:

| Version | Per-bone payload | Notes |
|---------|------------------|-------|
| 1 | 10×f32 | Translation xyz, quaternion xyzw, scale xyz |
| 2 | 16×f32 | Column-major 4×4 matrix (same float order as .bind) |

The Blender exporter writes version 2. Clip metadata (fps, duration, track path) lives in the text .anim file; see [Skeletal animation](animation.md).
