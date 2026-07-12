# slopengine

slopengine is a small C++ game engine built with raylib, flecs, and Scheme (s7).

Build the HTML documentation with:

```bash
cmake -S . -B build -DSLOPENGINE_BUILD_DOCS=ON
cmake --build build --target docs
```

The generated site is written to `build/docs/html/index.html`.
