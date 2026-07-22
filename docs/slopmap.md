# slopmap

CSG level / prefab / things editor (raylib + rlImGui). Writes map and prefab source under `--target`. File formats and compile algorithms stay on their own guides; this page covers how to build and run the editor.

Related: [Maps](maps.md), [Things](things.md), [Lights](lights.md), [Icons](icons.md), [BSP](bsp.md), [VIS](vis.md), [Radiosity](rad.md), [slopsprite](slopsprite.md).

## Build and CLI

CMake target `slopmap` (root `CMakeLists.txt`), linked against `sloplib` plus rlImGui.

```bash
cmake --build build --target slopmap

./build/slopmap --base-game <package-path> [--mod <path>]... --target <package-path> [--map <name>]
```

| Flag | Meaning |
|------|---------|
| `--base-game` | Base package to mount (required) |
| `--mod` | Additional package; repeatable |
| `--target` | Package that receives saves (required; must be one of the mounted packages) |
| `--map` | Map folder name under a mounted package (optional; omit for untitled) |

`--target` is always required. New / Save As pickers choose a write package among the mounted set; they do not invent a target from `--base-game` alone. UI chrome uses the silk icon atlas; see [Icons](icons.md).

## Scenes and modes

| Scene | Role |
|-------|------|
| Level | Edit `maps/<name>/static.csg` and `things.s7` |
| Prefab | Author `prefabs/<path>.csg` (+ optional `.s7` sidecar) without mutating the open level (level stays in memory) |

Switch scenes from the Prefab / Level menus. Prefab -> New / Open / Save As writes under `--target` (or the package chosen in the picker). The path you choose is the virtual path used when placing instances. Level save round-trips `(prefab ...)` forms; explode-to-brushes is not implemented yet.

| Mode | Role |
|------|------|
| Select | Pick and transform brushes, faces, or entities |
| Create | Draw new brush solids |
| Place | Drop prefab instances (Level only) or thing kinds |

Modes are chosen from the toolbar or Tools menu (no digit `1` / `2` / `3` shortcuts). Selection scope in Select mode: Brush, Face, or Entity.

## Authoring

Create primitives: box, cylinder, stairs; optional hollow. New brushes in the Prefab scene default to detail. `H` / Edit -> Toggle Brush Role cycles hull -> detail -> hint -> trigger -> water -> window. `L` / Edit -> Toggle UV Lock pins textures on the selected brush (or face in face scope).

Brush roles decide what participates in the structural BSP shell versus decoration. Hull (and window) seal the playable volume; detail and other non-hull roles must sit inside that sealed interior. See [BSP: hull vs non-hull](bsp.md#why-hull-and-non-hull).

Clip (`Shift+X` / Edit -> Clip) draws a 2-point cut on the construction grid, then `F` cycles keep Front / Back / Both, `Shift+F` flips the plane, and Enter commits (Esc cancels). Punch-out subtracts from brushes using selected faces.

In Level scene, select a prefab in the Prefabs panel and use Place mode to drop instances. Select mode moves (`G`) and rotates yaw by 90 deg (`R`). Things use the Things outliner and Library -> Things palette (props, usables, lights); Place mode drops the selected kind; Select moves (`G`) and rotates yaw (`R`). Prefab scene can load/save optional `prefabs/<path>.s7` sidecars the same way.

Material browser, texture panel, and brush panel edit face appearance and UV fields on the open document.

## View

Perspective / Top / Front / Side. Grid size `[` `]` `\`. Translate snap Offset / Absolute (`O` while translating). Ignore backfaces is a view toggle.

Viewport fill (`Z` cycles):

| Fill | Meaning |
|------|---------|
| Wireframe | Brush edges only |
| Solid | Faux-shaded CSG |
| Textures | CSG albedo |
| Unlit | Compiled VIS mesh without lightmaps |
| Lit | VIS + lightmaps |
| SolidLit | Solid CSG shaded with bake |

**X-Ray Overlay** (`Shift+Z`, toolbar **XRay**, or View -> X-Ray Overlay): Off, Visible (depth-tested edges on top of fill), or All (every brush edge through the fill, depth off).

## Compile and play

Compile menu spawns the same CLI tools as a hand-run bake: Run BSP, Run VIS, Run RAD, or **Run All** (`slopbsp` -> `slopvis` -> `sloprad`). Dirty markers show when source is ahead of compile data. Clean BSP / VIS / RAD / All removes generated files for the open map. RAD Options sets luxels, bounces, samples, and GPU/CPU. Play Map (`F5`) launches `slopengine` with the current package mount and map.

Compile order and rebuild rules: [Maps](maps.md). Tool details: [BSP](bsp.md), [VIS](vis.md), [Radiosity](rad.md).

## Shortcuts

| Key | Action |
|-----|--------|
| Ctrl+N / Ctrl+O / Ctrl+S | New / Open / Save |
| F5 | Play Map |
| G / R | Move / rotate yaw (Select) |
| H / L | Toggle brush role / UV lock |
| Shift+X | Clip tool |
| Tab | Cycle selection scope |
| Z / Shift+Z | Cycle fill / X-Ray |
| `[` `]` `\` | Grid |
| Home | Frame selection / origin |
| Delete | Delete selection |
