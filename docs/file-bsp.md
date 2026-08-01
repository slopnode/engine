@page bsp (.bsp) Binary space partition

Magic 0x32505342 (BSP2), version 3. 

# Header

| Field | Type | Notes |
|-------|------|-------|
| magic | u32 | 0x32505342 |
| version | u32 | 3 |
| root | i32 | Root node or leaf index |
| boundsMins | Vector3 | Tree AABB |
| boundsMaxs | Vector3 | Tree AABB |

# Nodes

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

# Leaves

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

# Portals

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

# Surface faces

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

# String table

| Field | Type | Notes |
|-------|------|-------|
| stringCount | u32 | Index 0 is always the empty string |
| strings[stringCount] | string | Length-prefixed |