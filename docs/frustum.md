# View frustum culling

Each frame the world pass builds a camera frustum from the presentation camera and skips objects that fall entirely outside it. Object culling also consults the compiled leaf [PVS](vis.md) (`static.vis`): an object must pass **frustum ∩ PVS**. This is runtime draw filtering only; it does not change bake data. The map FAC mesh is never culled by frustum or PVS.

Related: [Player](player.md), [Sprites](sprites.md), [Lights](lights.md), [Geometry](geometry.md), [FAC](fac.md), [VIS](vis.md).

## What a frustum is

The frustum is the space the camera can see: left, right, bottom, top, near, and far planes. `makeFrustumFromCamera` builds those six planes from the presentation `Camera3D` (same view used for `BeginMode3D`) and the current render aspect.

Near and far distances match raylib’s cull defaults (`RL_CULL_DISTANCE_NEAR` / `FAR`). Aspect should match the active framebuffer (`GetRenderWidth` / `GetRenderHeight` in the game loop).

Tests live in `sloptests frustum` ([test/test_frustum.cpp](../test/test_frustum.cpp)).

## Frame order

In the world lens pass:

1. Build the frustum from the presentation camera.
2. Gather dynamic lights (sphere cull by range, then PVS, then rank).
3. Upload ranked lights to the map lightmap shader once.
4. Draw world models (map always; props if AABB in frustum and PVS-visible).
5. Collect and draw world sprites (sphere cull, PVS, then back-to-front sort).
6. First-person viewmodels and screen-space view sprites are not frustum- or PVS-culled.

## What is culled

| Target | Test | Notes |
|--------|------|--------|
| Map brush model (`MapLightmapState`) | Never culled | Full FAC mesh always draws. |
| World props (`Model3D` + `WorldSpace`, no map lightmaps) | AABB ∩ frustum, then PVS on AABB center | Leaf via `pvsSampleLeaf` (nudges up into open space). |
| Skeleton debug overlays | Same as props | Skipped when the prop is out of view. |
| World sprites | Sphere ∩ frustum, then PVS at feet | Feet often sit in solid floor; sample nudges up. Fail-open if no open leaf. |
| Dynamic lights | Sphere ∩ frustum, then PVS | `ViewSpace` lights (e.g. flashlight) skip PVS. Still capped by `kMaxDynamicLights` after score. |
| FP viewmodels / view sprites / HUD | Not culled | Eye-space or screen-space; intended on screen. |

Scheme: `(pvs-can-see x0 y0 z0 x1 y1 z1)` answers leaf visibility between two points (separate from physics `(los? …)`).

## Helpers

| API | Role |
|-----|------|
| `makeFrustumFromCamera` | Extract six inward-facing planes from view × projection. |
| `aabbInFrustum` | True if the box is not fully outside any plane. |
| `sphereInFrustum` | True if the sphere is not fully outside any plane. |
| `transformAabb` | World AABB from a local box and a matrix (eight corners). |
| `pvsCanSee` | Leaf↔leaf bit test from `MapPvs` / PVS1. |

Raylib’s `MatrixMultiply(A, B)` combines matrices in the order the engine already uses for MVP-style products; frustum construction follows that convention so planes match what `BeginMode3D` draws.

## Practical notes

- Large props with tight authored bounds can pop at the screen edge if the AABB underestimates the mesh; prefer accurate geo bounds.
- Sprite spheres are conservative approximations; a tall billboard uses an elevated center so feet-only origins are not culled while the art is still on screen.
- A dynamic light behind the camera with a large range can still pass if its sphere intersects the frustum (correct for lighting surfaces you can see).
- Turning away from props, sprites, or lights should stop drawing / ranking them; the map and FP weapon should stay visible.
- Missing or mismatched `static.vis` fails map load; re-run `slopvis` after BSP changes.

## Source map

| Concern | Location |
|---------|----------|
| Plane extract + tests | `src/render/render_frustum.hpp`, `render_frustum.cpp` |
| Frame wiring | `src/render/render_module.cpp` (`LensWorld`) |
| Prop / sprite / light cull | `src/render/render_pass_world.cpp` |
| PVS bake / IO | `src/map/pvs_build.cpp`, `pvs_io.cpp` |
| Unit tests | `test/test_frustum.cpp` (`sloptests frustum`), `test/test_pvs_build.cpp` |
