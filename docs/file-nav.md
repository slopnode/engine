@page nav (.nav) Baked leaf navigation graph

Magic 0x3156414E (NAV1), version 3.

Written/read by `writeNavFile`/`readNavFile` (`src/map/nav_io.cpp`). Stores a `MapNavigation` leaf portal graph — either the BSP-leaf graph or a Recast-baked navmesh graph, both share this same layout. Only `adjacency` is stored; `reverseAdjacency` is rebuilt after load by mirroring each link, so the file doesn't pay to store both directions.

# Header {#nav-header}

| Field | Type | Notes |
|-------|------|-------|
| magic | u32 | 0x3156414E |
| version | u32 | 3 |
| leafCount | u32 |  |

# Leaves {#nav-leaves}

`leafCount` entries, immediately following the header:

| Field | Type | Notes |
|-------|------|-------|
| walkable | u8 | 0 or 1 |
| leafIsWater | u8 | 0 or 1; true if the leaf's BSP contents include Water |
| centroid | Vector3 |  |
| floorY | f32 |  |
| ceilingY | f32 |  |
| boundaryCount | u32 | 0 for a BSP-leaf graph (no single flat boundary polygon) |
| boundary[boundaryCount] | Vector3 | Outward-wound XZ boundary polygon; navmesh graphs only |

# Adjacency {#nav-adjacency}

`leafCount` entries, one per leaf, immediately following the leaf list:

| Field | Type | Notes |
|-------|------|-------|
| linkCount | u32 |  |
| links[linkCount] | … |  |

Each link (`NavPortalLink`):

| Field | Type | Notes |
|-------|------|-------|
| neighborLeaf | i32 |  |
| portalCenter | Vector3 |  |
| portalTangent | Vector3 | Unit horizontal direction along the portal's widest span |
| portalHalfWidth | f32 | 0 for portals too narrow to spread agents across |
| cost | f32 |  |
| doorBrushIdIndex | u32 | Index into string table; brush id of the gating Door, or the empty string (index 0) if ungated |
| climbHeight | f32 | Vertical rise from this leaf to neighborLeaf, owner -> neighbor (negative is a drop); always 0 for a navmesh graph, since Recast only ever links polygons whose real step is already within the bake's walkableClimb (v3+) |

# String table {#nav-string-table}

| Field | Type | Notes |
|-------|------|-------|
| stringCount | u32 | Index 0 is always the empty string |
| strings[stringCount] | string | Length-prefixed |
