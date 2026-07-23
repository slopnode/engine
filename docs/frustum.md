# View frustum culling

Each frame the world pass builds a camera frustum from the presentation camera and skips objects that fall entirely outside it. This is runtime draw filtering only: it does not change bake data, and it is not a PVS.

Related: [Player](player.md), [Sprites](sprites.md), [Lights](lights.md), [Geometry](geometry.md), [VIS](vis.md).

## What a frustum is

The frustum is the space the camera can see: left, right, bottom, top, near, and far planes. `makeFrustumFromCamera` builds those six planes from the presentation `Camera3D` (same view used for `BeginMode3D`) and the current render aspect.

Near and far distances match raylib’s cull defaults (`RL_CULL_DISTANCE_NEAR` / `FAR`). Aspect should match the active framebuffer (`GetRenderWidth` / `GetRenderHeight` in the game loop).

Tests live in `sloptests frustum` ([test/test_frustum.cpp](../test/test_frustum.cpp)).

## Frame order

In the world lens pass:

1. Build the frustum from the presentation camera.
2. Gather dynamic lights (sphere cull by range, then rank).
3. Upload ranked lights to the map lightmap shader once.
4. Draw world models (map always; props if AABB in frustum).
5. Collect and draw world sprites (sphere cull, then back-to-front sort).
6. First-person viewmodels and screen-space view sprites are not frustum-culled.

## What is culled

| Target | Test | Notes |
|--------|------|--------|
| Map brush model (`MapLightmapState`) | Never culled | The viewer stands inside the map AABB; frustum-culling the whole mesh would hide the level. |
| World props (`Model3D` + `WorldSpace`, no map lightmaps) | Transformed model AABB | Local `GetModelBoundingBox`, then `transformAabb` by `GlobalTransformation`. |
| Skeleton debug overlays | Same AABB test as props | Skipped when the prop is out of view. |
| World sprites | Sphere | Center lifted above the entity feet (billboards grow upward); radius from entity scale with a minimum. Culled before light sampling and sort. |
| Dynamic lights | Sphere at light position with `range` | Out-of-frustum candidates never enter ranking. Still capped by `kMaxDynamicLights` after score. |
| FP viewmodels / view sprites / HUD | Not culled | Eye-space or screen-space; intended on screen. |

Frustum culling only answers “is this roughly in the camera cone?” Walls do not occlude other rooms. Compile-time [VIS](vis.md) still builds the map face list; it does not drive this pass. True portal / leaf PVS is not implemented.

## Helpers

| API | Role |
|-----|------|
| `makeFrustumFromCamera` | Extract six inward-facing planes from view × projection. |
| `aabbInFrustum` | True if the box is not fully outside any plane. |
| `sphereInFrustum` | True if the sphere is not fully outside any plane. |
| `transformAabb` | World AABB from a local box and a matrix (eight corners). |

Raylib’s `MatrixMultiply(A, B)` combines matrices in the order the engine already uses for MVP-style products; frustum construction follows that convention so planes match what `BeginMode3D` draws.

## Practical notes

- Large props with tight authored bounds can pop at the screen edge if the AABB underestimates the mesh; prefer accurate geo bounds.
- Sprite spheres are conservative approximations; a tall billboard uses an elevated center so feet-only origins are not culled while the art is still on screen.
- A dynamic light behind the camera with a large range can still pass if its sphere intersects the frustum (correct for lighting surfaces you can see).
- Turning away from props, sprites, or lights should stop drawing / ranking them; the map and FP weapon should stay visible.

## Source map

| Concern | Location |
|---------|----------|
| Plane extract + tests | `src/render/render_frustum.hpp`, `render_frustum.cpp` |
| Frame wiring | `src/render/render_module.cpp` (`LensWorld`) |
| Prop / sprite / light cull | `src/render/render_pass_world.cpp` |
| Unit tests | `test/test_frustum.cpp` (`sloptests frustum`) |
