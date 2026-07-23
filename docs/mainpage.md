# slopengine

slopengine is a small hobby stack for first-person games, assembled from free libraries with classic folder-based content. See [Overview](overview.md).

API reference for flecs types is under **Modules**: @ref components "Components" and @ref systems "Systems" (grouped by subsystem).

## Guides

- [Overview](overview.md)
- [Package structure](package-structure.md)
- [Persistence](persistence.md)
- [Writing s7](s7.md)
- [Scripting](scripting.md)

### Maps and world

- [Maps](maps.md)
- [BSP compilation](bsp.md)
- [VIS compilation](vis.md)
- [Radiosity compilation](rad.md)
- [Lights](lights.md)
- [Things](things.md)
- [Binary formats](binary-formats.md)

### Player

- [Player](player.md)

### Editor tools

- [slopmap](slopmap.md)
- [slopsprite](slopsprite.md)

### Assets

- [Geometry](geometry.md)
- [Skeletal animation](animation.md)
- [Sprites](sprites.md)
- [Materials, textures, and shaders](materials.md)
- [Audio](audio.md)
- [Icons](icons.md)

Build the HTML documentation with:

```bash
cmake -S . -B build -DSLOPENGINE_BUILD_DOCS=ON
cmake --build build --target docs
```

The generated site is written to build/docs/html/index.html.
