# Package structure

A package is a directory of game content mounted by the engine. The base game is required; additional mod packages can be layered on top. Every package must contain a package.meta file at its root.

## Mounting

Packages are mounted from the command line. The engine owns only mount flags; other flags come from the base game's data/cli.s7 (see [Scripting](scripting.md#package-cli)):

```bash
./build/slopengine --base-game {package-path} [--mod {mod-path}]... [package-flags...]
```

- --base-game: path to the base package directory (required)
- --mod: additional package directory; can be repeated

Compile tools (slopbsp, slopfac, sloprad) and editors still take --map as a tool flag. The game runtime treats --map as a package flag when the base game declares it in data/cli.s7.

Later packages override earlier ones when the same **media** virtual path exists in more than one package (textures, sprites, sound, maps, geometry, …). Boot **scripts/** and **data/** catalogs are not flatten-overridable: the engine always loads base-game entrypoints by package role, then each mod's `scripts/contrib.s7`. Loading another package's script or data from Scheme always requires that package's id. Package ids must be unique across the mount set, and every depends entry in package.meta must resolve to a mounted package.

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
| id | Unique package id (required) |
| name | Display name |
| version | Package version string |
| depends | Space-separated quoted package ids that must already be mounted |

## Layout

Asset lookup is by category subdirectory plus a virtual path. The engine appends the expected file extension; callers never include it in the virtual path.

```text
my-package/
  package.meta
  animations/     # .anim, .tracks
  audio/          # .saudio, .s7 audio defs
  data/           # .s7 (actions, map-handlers, items, view, cli, campaign, ...)
  fonts/          # .ttf (ImGui / UI)
  geometry/       # .geo, .vert, .weights
  icons/          # .png atlas + .iconmap (+ source folders)
  maps/           # map folders
  materials/      # .mat
  particles/      # .prt
  prefabs/        # brush assemblies (+ optional entity sidecars)
  scripts/        # .s7 (base: init, things, player, menus; mods: contrib.s7)
  shaders/        # .glsl
  skeletons/      # .skel, .bind
  sound/          # .ogg
  sprites/        # .spr, .spanim
  textures/       # .png
```

Example: material virtual path surfaces/stone resolves to materials/surfaces/stone.mat.

| Directory | Extensions | Virtual path example |
|-----------|------------|----------------------|
| textures/ | .png | surfaces/stone -> textures/surfaces/stone.png |
| materials/ | .mat | surfaces/stone -> materials/surfaces/stone.mat |
| shaders/ | .glsl | default/lightmap_vert -> shaders/default/lightmap_vert.glsl |
| scripts/ | .s7 | init -> scripts/init.s7 |
| data/ | .s7 | actions -> data/actions.s7 |
| skeletons/ | .skel, .bind | character -> skeletons/character.skel |
| geometry/ | .geo, .vert, .weights | props/crate -> geometry/props/crate.geo |
| animations/ | .anim, .tracks | character -> animations/character/character.anim |
| sprites/ | .spr, .spanim | characters/guard -> sprites/characters/guard.spr |
| particles/ | .prt | fx/generic-smoke -> particles/fx/generic-smoke.prt |
| sound/ | .ogg | weapons/fire -> sound/weapons/fire.ogg |
| audio/ | .saudio, .s7 | ui/pickup -> audio/ui/pickup.saudio |
| icons/ | .png, .iconmap | silk -> icons/silk.png / icons/silk.iconmap |
| fonts/ | .ttf | FiraSans/FiraSans-Regular -> fonts/FiraSans/FiraSans-Regular.ttf |
| prefabs/ | .csg, .s7 | furniture/desk -> prefabs/furniture/desk.csg (optional sibling .s7) |
| maps/ | see below | {name}/static -> .csg / .bsp / .fac / .vis; {name}/things -> things.s7; {name}/graphs -> graphs.s7; {name}/rad/static -> rad/static.rad |

Nested folders under each category are allowed. Related assets are often grouped by shared name under geometry/, skeletons/, and animations/.

## Asset kinds

### Textures

PNG images under textures/. Materials reference them by virtual path without the extension. See [Materials, textures, and shaders](materials.md).

### Materials

.mat files under materials/. Albedo tint/texture, UV density, and emission (no normal or PBR maps). See [Materials, textures, and shaders](materials.md).

```text
(material
  (shader "default")
  (params
    (base-color 0.8 0.8 0.8 1.0)))
```

### Shaders

GLSL sources under shaders/. Vertex and fragment programs are separate virtual paths. Map lightmaps use default/lightmap_*; see [Materials, textures, and shaders](materials.md).

### Scripts

Scheme (s7) sources under scripts/ and package data under data/. The base game owns boot entrypoints (init, actions, map-handlers, items, view, cli, things, player, menus). Mods expand via scripts/contrib.s7 and `(hook-add ...)`, and may append data/actions.s7 and data/map-handlers.s7. Cross-package loads use `(package-load-script package-id path)` / `(package-load-data package-id path)`. Map things are a separate maps/{name}/things.s7. See [Scripting](scripting.md) and [Things](things.md).

### Sound and audio

Raw clips live under sound/ as .ogg. Audio definitions live under audio/ (.saudio procedural Sfxr, or .s7 sample wrappers). See [Audio](audio.md).

### Skeletons

.skel describes the bone hierarchy. An optional sibling .bind holds bind-pose data used with skinned geometry (same virtual path, different asset kind / extension).

### Geometry

Prop and character meshes use .geo / .vert / optional .weights under geometry/. Level solids use CSG under maps/ instead. See [Geometry](geometry.md).

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

Skeletal clips for skinned meshes: .anim plus .tracks, always tied to a skeleton id. The Multiple Blender export writes animations/{asset}/{asset}.anim (and tracks beside it); flatter paths under animations/ are also valid virtual paths. See [Skeletal animation](animation.md). Rigid object motion uses component animator systems on entities, not this export path.

### Sprites

.spr files under sprites/ describe named billboard sprites with per-frame rotations, pose channels, and optional mirroring. Sibling .spanim files define named clips (loop, hold durations, optional tween and frame sounds) for the same virtual path. Source PNGs live under textures/. Optional hit-mask textures and (hit-part ...) entries configure multi-part hits. See [Sprites](sprites.md) and [Audio](audio.md) for frame sounds.

### Icons

UI icon atlases under icons/. Each set is a packed .png plus a sibling .iconmap that names rectangles inside the atlas. Source PNGs for packing live in icons/{set}/. See [Icons](icons.md).

### Prefabs

Reusable brush assemblies under prefabs/. A .csg file uses the same brush-box / brush-convex forms as maps. An optional sibling .s7 holds entity attachments for that prefab. Maps instance them with (prefab ...); see [Maps](maps.md).

### Maps

Each map is a folder under maps/{name}/ with authored map.meta / static.csg, optional things.s7 / graphs.s7, and compiled static.bsp / static.fac plus optional rad/. The map belongs to whichever package directory contains it; map.meta (depends ...) lists other packages only when the map uses their assets. See [Maps](maps.md) for authoring and things, [slopmap](slopmap.md) for the editor, and [BSP](bsp.md) / [VIS](vis.md) / [Radiosity](rad.md) for the compile tools.

A package is created by adding a package.meta with a unique id, the category folders you need, and mounting it with --base-game or --mod. Dependencies listed in (depends ...) must also be mounted.

## Saves

Player save data is not stored in packages. It lives under the user config directory (same root as settings.cfg), scoped by the mounted stack engine → base game → mods. Packages choose relative layout and S-expr body fields under that context, draw New/Load/Save UI in Scheme, and decide when to write. Full path rules and ownership: [Persistence](persistence.md). Menus, CLI, and save API: [Scripting](scripting.md#package-menus), [Scripting](scripting.md#save-io-and-map-flow).
