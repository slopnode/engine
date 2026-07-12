# engine

A small C++ game built with raylib, flecs, and Scheme (s7).

## Requirements

- CMake 3.22+
- C++20 compiler (MSVC, Clang, or GCC)
- Git (for submodules)

## Build

```bash
git submodule update --init --recursive
cmake -S . -B build
cmake --build build
```

On Windows with Visual Studio, open the generated solution in `build/` or use the commands above from a developer shell.

## Run

```bash
build/slopengine --base-game packages/base
```

Optional mods load on top of the base package:

```bash
build/slopengine --base-game packages/base --mod path/to/my-mod
```

## Packages

A package is a folder of game content. The engine looks up assets by type:

```
packages/base/
  animations/   .anim, .tracks
  geometry/     .geo, .vert, .weights
  materials/    .mat
  scripts/      .s7
  shaders/      .glsl
  skeletons/    .skel, .bind
  textures/     .png
```

Paths inside a package are virtual (e.g. `human01/human01` for `geometry/human01/human01.geo`). Later mods override earlier packages for the same path.

## Blender exporter

Source: `tools/blender/slopengine_exporter/`

Requires Blender 4.2+. Package it as a zip:

```powershell
tools/blender/package_extension.ps1
```

In Blender: **Edit → Preferences → Get Extensions → (menu) → Install from Disk** and select `tools/blender/slopengine_exporter.zip`.

Exports are under **File → Export → Slopengine**:

- **Multiple** — skeleton, geometry, and animation into a package folder
- **Geometry**, **Animation**, **Skeleton** — individual exports

Point exports at your package directory (e.g. `packages/base`). The **Multiple** exporter writes into `geometry/`, `skeletons/`, and `animations/` automatically.
