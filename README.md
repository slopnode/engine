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

### Optional: Steam Audio

Spatial audio can use Valve’s Steam Audio SDK. This is **opt-in** (`SLOPENGINE_USE_STEAM_AUDIO` defaults OFF). The SDK is **not** vendored under `lib/` or as a git submodule — download the binary SDK separately and point CMake at the extract:

```bash
cmake -S . -B build \
  -DSLOPENGINE_USE_STEAM_AUDIO=ON \
  -DSTEAM_AUDIO_ROOT=/path/to/steamaudio
```

Expected layout under `STEAM_AUDIO_ROOT`:

- `include/phonon.h`
- `lib/<platform>/` with `libphonon` (`linux-x64`, `windows-x64`, `osx`, …)

A source checkout may resolve headers but still needs a prebuilt `libphonon` unless you build it yourself. See [docs/audio.md](docs/audio.md) for runtime behavior when Steam Audio is enabled.

## Run

```bash
build/slopengine --base-game packages/base
```

Optional mods load on top of the base package:

```bash
build/slopengine --base-game packages/base --mod path/to/my-mod
```

## Packages

A package is a folder of game content with a root `package.meta`:

```
(package
  (id "slopengine.base")
  (name "Slopengine Base")
  (version "0.1.0")
  (depends))
```

The engine looks up assets by type:

```
my-package/
  package.meta
  animations/   .anim, .tracks
  audio/        .saudio, .s7
  data/         .s7
  fonts/        .ttf
  geometry/     .geo, .vert, .weights
  icons/        .png, .iconmap
  maps/         <map>/map.meta, <map>/static.csg
  materials/    .mat
  prefabs/      .csg, .s7
  scripts/      .s7
  shaders/      .glsl
  skeletons/    .skel, .bind
  sound/       .ogg
  sprites/      .spr, .spanim
  textures/     .png
```

Maps live under a package’s `maps/` folder (ownership is the package directory). `map.meta` only lists other packages when needed:

```
(map
  (id "empty-room")
  (name "Empty Room")
  (depends))
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
