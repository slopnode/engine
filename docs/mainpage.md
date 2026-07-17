# slopengine

slopengine is a small hobby stack for first-person games, assembled from free libraries with classic folder-based content. See [Overview](overview.md).

## Guides

- [Overview](overview.md)
- [Package structure](package-structure.md)
- [Maps](maps.md)
- [Geometry](geometry.md)
- [Animation](animation.md)
- [Materials, textures, and shaders](materials.md)

Build the HTML documentation with:

```bash
cmake -S . -B build -DSLOPENGINE_BUILD_DOCS=ON
cmake --build build --target docs
```

The generated site is written to `build/docs/html/index.html`.
