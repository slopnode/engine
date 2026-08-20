@page rad (.rad) Radiosity

Magic 0x31444152 (RAD1), version 5 (readable back to version 2).

Atlas pixels are not embedded; they are separate PNGs under rad/ (for example atlas0.png). Face ids match authored brush face ids. Strings in this file are inline length-prefixed (no trailing string table).

# Header {#rad-header}

| Field | Type | Notes |
|-------|------|-------|
| magic | u32 | 0x31444152 |
| version | u32 | 5 (or 4, 3, 2) |
| luxelsPerMeter | f32 | Nominal bake density |

# Atlases {#atlases}

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
| encoding | u32 | 0 = Ldr (legacy Reinhard-baked RGB), 1 = Rgbe (HDR shared-exponent linear); present only when version >= 3, else Ldr |

# Charts {#charts}

| Field | Type | Notes |
|-------|------|-------|
| chartCount | u32 |  |
| charts[chartCount] | … |  |

Each chart:

| Field | Type | Notes |
|-------|------|-------|
| faceIndex | u32 | Index into the baked face list |
| faceId | string | Authored brush face id |
| atlasIndex | u32 |  |
| luxelWidth | u32 |  |
| luxelHeight | u32 |  |
| atlasX | u32 | Pixel origin in atlas |
| atlasY | u32 | Pixel origin in atlas |
| u0 | f32 | Normalized lightmap UV bounds |
| v0 | f32 |  |
| u1 | f32 |  |
| v1 | f32 |  |
| groupUMin | f32 | Bounds of the coplanar face group this chart belongs to; present only when version >= 4 |
| groupUMax | f32 | Present only when version >= 4 |
| groupVMin | f32 | Present only when version >= 4 |
| groupVMax | f32 | Present only when version >= 4 |

# Light probe grids {#light-probe-grids}

Present only when version >= 5. Two grids follow back to back, coarse then fine, each with the same layout:

| Field | Type | Notes |
|-------|------|-------|
| cellSize | f32 | World-space grid cell size in meters |
| probeCount | u32 |  |
| probes[probeCount] | … |  |

Each probe:

| Field | Type | Notes |
|-------|------|-------|
| cellX | i32 | Grid cell coordinate |
| cellY | i32 |  |
| cellZ | i32 |  |
| shRgbe | Color[4] | 4× RGBA8, spherical-harmonics L1 coefficients (DC + 3 directional) each RGBE8-encoded per [encodeRgbe](#encodergbe) |

## RGBE8 encoding {#encodergbe}

Ward-style shared-exponent encoding used for both HDR atlas texels (encoding = Rgbe) and probe SH coefficients: RGB channels are `round(clamp(component * 256 * 2^-exponent, 0, 255))`, and the alpha channel stores `exponent + 128`, where `exponent = floor(log2(max(r, g, b))) + 1`. Alpha 0 decodes to black.

# Transparent alpha occlusion (sloprad) {#transparent-alpha-occlusion-sloprad}

Transparent brush faces participate in baked light occlusion using albedo alpha:

- `effectiveAlpha = textureAlpha * material.baseColor.a`
- alpha >= 0.5 blocks light; lower alpha lets rays continue

CPU reference bake:

```bash
cmake --build build --target sloprad sloptests
./build/sloptests lightmap_transparent
./build/sloprad /path/to/maps/E1M1/static.bsp --cpu
```

GPU validation (requires OpenGL 4.3 + discrete GPU):

```bash
./build/sloprad /path/to/maps/E1M1/static.bsp --gpu --gpu-fast
```

Confirm sloprad logs `GPU direct lighting` without CPU fallback, then inspect `maps/E1M1/rad/atlas*.png` near fence geometry for post/grate sun shadows.