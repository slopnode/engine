# slopengine

slopengine is a small hobby stack for first-person games, assembled from free libraries with classic folder-based content. See [Overview](overview.md).

## Guides

- [Overview](overview.md)
- [Package structure](package-structure.md)
- [Scripting](scripting.md)

### Maps and world

- [Maps](maps.md)
- [BSP and radiosity compilation](bsp-rad.md)
- [Lights](lights.md)
- [Things](things.md)

### Player

- [Player](player.md)

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

The generated site is written to `build/docs/html/index.html`.
