# Package structure

A package is a directory of game content mounted by the engine. The base game is required; additional mod packages can be layered on top. Every package must contain a `package.meta` file at its root.

## Mounting

Packages are mounted from the command line:

```bash
./build/slopengine --base-game <package-path> [--mod <mod-path>]... [--map <name>]
```

- `--base-game` — path to the base package directory (required)
- `--mod` — additional package directory; can be repeated
- `--map` — map folder name under `maps/` (loads `maps/<name>/static.csg`)

Later packages override earlier ones when the same virtual asset path exists in more than one package. Package ids must be unique across the mount set, and every `depends` entry in `package.meta` must resolve to a mounted package.

## package.meta

```text
(package
  (id "com.example.game")
  (name "Example Game")
  (version "0.1.0")
  (depends))
```

| Field | Meaning |
|-------|---------|
| `id` | Unique package id (required) |
| `name` | Display name |
| `version` | Package version string |
| `depends` | Space-separated quoted package ids that must already be mounted |

## Layout

Asset lookup is by category subdirectory plus a virtual path. The engine appends the expected file extension; callers never include it in the virtual path.

```text
my-package/
  package.meta
  animations/     # .anim, .tracks
  fonts/          # .ttf (ImGui / UI)
  geometry/       # .geo, .vert, .weights
  icons/          # .png atlas + .iconmap (+ source folders)
  maps/           # map folders
  materials/      # .mat
  meshes/         # .glb (optional; supported by VFS)
  prefabs/        # brush assemblies (+ optional entity sidecars)
  scripts/        # .s7
  shaders/        # .glsl
  skeletons/      # .skel, .bind
  sprites/        # .spr, .spanim
  textures/       # .png
```

Example: material virtual path `surfaces/stone` resolves to `materials/surfaces/stone.mat`.

| Directory | Extensions | Virtual path example |
|-----------|------------|----------------------|
| `textures/` | `.png` | `surfaces/stone` → `textures/surfaces/stone.png` |
| `materials/` | `.mat` | `surfaces/stone` → `materials/surfaces/stone.mat` |
| `meshes/` | `.glb` | `props/crate` → `meshes/props/crate.glb` |
| `shaders/` | `.glsl` | `default/lightmap_vert` → `shaders/default/lightmap_vert.glsl` |
| `scripts/` | `.s7` | `init` → `scripts/init.s7` |
| `skeletons/` | `.skel`, `.bind` | `character` → `skeletons/character.skel` |
| `geometry/` | `.geo`, `.vert`, `.weights` | `props/crate` → `geometry/props/crate.geo` |
| `animations/` | `.anim`, `.tracks` | `character/walk` → `animations/character/walk.anim` |
| `sprites/` | `.spr`, `.spanim` | `usmc/umca` → `sprites/usmc/umca.spr` |
| `icons/` | `.png`, `.iconmap` | `silk` → `icons/silk.png` / `icons/silk.iconmap` |
| `fonts/` | `.ttf` | `FiraSans/FiraSans-Regular` → `fonts/FiraSans/FiraSans-Regular.ttf` |
| `prefabs/` | `.csg`, `.s7` | `furniture/desk` → `prefabs/furniture/desk.csg` (optional sibling `.s7`) |
| `maps/` | see below | `<name>/static` → `maps/<name>/static.csg`; `<name>/things` → `maps/<name>/things.s7` |

Nested folders under each category are allowed. Related assets are often grouped by shared name under `geometry/`, `skeletons/`, and `animations/`.

## Asset kinds

### Textures

PNG images under `textures/`. Materials reference them by virtual path without the extension. See [Materials, textures, and shaders](materials.md).

### Materials

`.mat` files under `materials/`. Albedo tint/texture, UV density, and emission (no normal or PBR maps). See [Materials, textures, and shaders](materials.md).

```text
(material
  (shader "default")
  (params
    (base-color 0.8 0.8 0.8 1.0)))
```

### Shaders

GLSL sources under `shaders/`. Vertex and fragment programs are separate virtual paths. Map lightmaps use `default/lightmap_*`; see [Materials, textures, and shaders](materials.md).

### Scripts

Scheme (s7) sources under `scripts/`. The game loads `init` then `things` at startup; map things are a separate `maps/<name>/things.s7`. See [Things](things.md).

### Skeletons

`.skel` describes the bone hierarchy. An optional sibling `.bind` holds bind-pose data used with skinned geometry (same virtual path, different asset kind / extension).

### Geometry

Prop and character meshes use `.geo` / `.vert` / optional `.weights` under `geometry/`. Level solids use CSG under `maps/` instead. See [Geometry](geometry.md).

```text
(geo
  (vertices implicit)
  (primitives
   (
    (name "0"
     material "surfaces/stone"
     ...))))
```

### Animations

Skeletal clips for skinned meshes: `.anim` plus `.tracks`, always tied to a skeleton id. See [Animation](animation.md). Rigid object motion uses component animator systems on entities, not this export path.

### Sprites

`.spr` files under `sprites/` describe named billboard sprites with per-frame rotations and optional mirroring. Sibling `.spanim` files define named clips (fps, loop, frame lists) for the same virtual path. Source PNGs live under `textures/`. Optional hit-mask textures and `(hit-part …)` entries configure multi-part hits. See [Sprites](sprites.md).

### Icons

UI icon atlases under `icons/`. Each set is a packed `.png` plus a sibling `.iconmap` that names rectangles inside the atlas. Source PNGs for packing live in `icons/<set>/`. See [Icons](icons.md).

### Prefabs

Reusable brush assemblies under `prefabs/`. A `.csg` file uses the same `brush-box` / `brush-convex` forms as maps. An optional sibling `.s7` holds entity attachments for that prefab. Maps instance them with `(prefab …)`; see [Maps](maps.md).

### Maps

Each map is a folder under `maps/<name>/` with authored `map.meta` / `static.csg`, optional `things.s7`, and compiled `static.bsp` plus optional `rad/`. The map belongs to whichever package directory contains it; `map.meta` `(depends …)` lists other packages only when the map uses their assets. See [Maps](maps.md) for authoring and things, and [BSP and radiosity compilation](bsp-rad.md) for the compile tools.

A package is created by adding a `package.meta` with a unique `id`, the category folders you need, and mounting it with `--base-game` or `--mod`. Dependencies listed in `(depends ...)` must also be mounted.
