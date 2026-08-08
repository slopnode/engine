@page fac (.fac) Faces

Magic 0x31434146 (FAC1), version 2.

# Header {#fac-header}

| Field | Type | Notes |
|-------|------|-------|
| magic | u32 | 0x31434146 |
| version | u32 | 2 (or 1) |
| faceCount | u32 |  |

# Faces {#faces}

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

# String table {#fac-string-table}

| Field | Type | Notes |
|-------|------|-------|
| stringCount | u32 | Index 0 is always the empty string |
| strings[stringCount] | string | Length-prefixed |