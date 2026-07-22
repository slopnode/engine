# Materials, textures, and shaders

Surfaces are described by `.mat` files. Materials are albedo + emission, not a PBR stack: there are no normal, roughness, or metallic maps. Detail comes from the diffuse texture and from offline lightmaps; special effects use emission fields and/or custom shaders.

Materials optionally reference textures and a shader name. At runtime the engine builds a raylib `Material`: albedo color/texture from the `.mat`, then the draw pipeline may replace the shader (maps with lightmaps do this today).

| Asset | Package dir | Extension | Virtual path example |
|-------|-------------|-----------|----------------------|
| Material | `materials/` | `.mat` | `surfaces/stone` -> `materials/surfaces/stone.mat` |
| Texture | `textures/` | `.png` | `surfaces/stone` -> `textures/surfaces/stone.png` |
| Shader | `shaders/` | `.glsl` | `default/lightmap_frag` -> `shaders/default/lightmap_frag.glsl` |

Geometry primitives and CSG faces name materials by virtual path (no extension). See [Geometry](geometry.md) and [Package structure](package-structure.md).

## Material files

`.mat` files are text with S-expression-style fields. The parser is line-oriented: wrappers like `(material` / `(params` are ignored; only known field lines matter.

```text
(material
  (shader "default")
  (texture "surfaces/stone")
  (texel-size 64)
  (base-color 1 1 1 1)
  (emission "lights/panel")
  (emission-color 1.0 0.95 0.8 1.0)
  (emission-power 8.0))
```

| Field | Maps to | Default | Role |
|-------|---------|---------|------|
| `(shader "...")` | `shader` | `"default"` | Stored on the asset; not used to select GLSL yet |
| `(texture "...")` | albedo texture path | none | Diffuse map under `textures/` |
| `(base-color R G B A)` | tint | white | Multiplies albedo (normalized floats -> 0-255) |
| `(texel-size N)` | `pixelsPerMeter` | `64` | World UV density for CSG faces (pixels per meter) |
| `(emission "...")` | emission texture path | none | Sampled during radiosity bake only |
| `(emission-color R G B A)` | emit tint | black | Bake and runtime emit color |
| `(emission-power N)` | emit strength | `0` | Scales emit; `> 0` enables runtime emit color |

Missing or unparsable materials fall back to defaults (white tint, no textures). An empty material path resolves to `default/unassigned`.

## Texture flow

A material `(texture "...")` or bake step requests a virtual path. The VFS resolves `textures/<path>.png` across mounted packages (later packages win). `AssetStore::getTexture` loads and caches the PNG. `createRaylibMaterial` binds a successful albedo texture to `MATERIAL_MAP_ALBEDO` with repeat wrap.

If the path is empty or load fails, the material keeps raylib's default 1x1 white diffuse.

Lightmap atlases are separate: PNG files under `maps/<name>/rad/`, loaded as map lightmap assets, not as material textures.

## Shader flow

Shaders are plain GLSL sources, one file per stage. Vert and frag are separate virtual paths. Stock pairs live under `default/`: `lightmap_*` for map geometry with radiosity, `viewmodel_*` for first-person geo faux shading (probe plus Lambert/rim; packages may override), and `skinning_*` as GPU skinning sources a package may ship (skinning is CPU-side today). `AssetStore::getShaderSource` reads the text; callers compile with raylib when needed.

### How a draw picks a shader

Materials do not currently drive shader selection from `(shader ...)`. Pipelines hardcode it. Prop and character `.geo` use the raylib default material shader. First-person `ViewSpace` geo uses `default/viewmodel_*` when `fp-set-shading` is on. Maps with baked lightmaps assign `default/lightmap_*` onto each map material; maps without lightmaps stay on the raylib default. Custom entity shaders are a separate render path and are not selected from `.mat` files.

### Lightmap shader

Compiled once at map load when `rad/` data is present. Bindings:

| Uniform / slot | Meaning |
|----------------|---------|
| `texture0` / albedo map | Diffuse texture |
| `texture1` / metalness map | Lightmap atlas (engine convention) |
| `colDiffuse` | Base color tint |
| `colSpecular` | Runtime emission color |
| `useLightmap` | `1` = sample atlas, `0` = unlit white light |

Fragment lighting:

```text
final = albedo * (baked + dynamic) + emit
```

where `baked` is the lightmap sample (or white when unlit), `dynamic` is the ranked runtime overlay (see [Lights](lights.md)), and `emit` comes from `colSpecular` (material emission color when `emission-power > 0`).

## End-to-end flows

### Prop / character mesh

```text
.geo primitive material "surfaces/stone"
  -> getMaterialAsset -> parse materials/surfaces/stone.mat
  -> createRaylibMaterial
       -> base-color -> albedo color
       -> texture -> textures/....png -> albedo map
       -> emission-power > 0 -> specular color = emission-color
  -> DrawModel with raylib default shader
```

### Map load

```text
VIS face material "surfaces/stone"
  -> resolveMaterialUv (texel-size + albedo size -> diffuse UVs)
  -> compile VIS faces -> meshes (+ lightmap UV2 from charts)
  -> resolveMaterial (same as props)
  -> override shader = lightmap program
  -> bind rad/atlasN.png on metalness slot per face chart
  -> DrawModel
```

### Radiosity bake (`sloprad`)

```text
getMaterialAsset + LoadImage albedo / emission PNGs
  -> sample albedo and emission with planar UVs (same texel-size rules)
  -> lighting directions use flat brush face normals (no normal maps)
  -> emission ~ emission-color * emission-power * emission-texel
  -> write atlases + static.rad
```

Emission textures matter at bake time. At runtime the lightmap shader adds a flat emit from `emission-color` when `emission-power > 0`; it does not sample the emission map again.

## Special effects

Glow and lit surfaces use the emission fields above. Other effects (scroll, pulse, fake specular, and so on) belong in custom shaders, not in additional material map types. Do not add normal/roughness/metallic maps for surface detail.

## Emission summary

`emission-color` plus `emission-power` contribute at bake time and again at runtime as `colSpecular` in the lightmap shader. The `emission` texture is sampled only during `sloprad`; it is not bound on the material for draw. Without the lightmap shader, specular color is still set on the raylib material but does not get the same additive emit path.

## Naming from Blender

The exporter does not write `.mat` files. Blender material names become material virtual paths on `.geo` primitives: a leading `materials/` prefix and known extensions are stripped; if there is no `/` but there is a `.`, the `.` is treated as `/` (`surfaces.stone` -> `surfaces/stone`); an empty name becomes `default/unassigned`. Blender material names should match paths under `materials/`.
