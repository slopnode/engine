@page rad (.rad) Radiosity

Magic 0x31444152 (RAD1)

Atlas pixels are not embedded; they are separate PNGs under rad/ (for example atlas0.png). Face ids match FAC fragment ids. Strings in this file are inline length-prefixed (no trailing string table).

# Header

| Field | Type | Notes |
|-------|------|-------|
| magic | u32 | 0x31444152 |
| version | u32 | 2 |
| luxelsPerMeter | f32 | Nominal bake density |

# Atlases

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

# Charts

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