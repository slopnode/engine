# Lights

Lighting is a bake-first pipeline with a small runtime dynamic overlay. Map surfaces get offline lightmaps; moving or toggled lights are `DynamicLight` entities ranked each frame and added on top. Thing light forms in `things.s7` feed the bake (point / spot) and editor gizmos; they are not the same as the runtime dynamic light path.

Related: [Maps](maps.md), [BSP and radiosity](bsp-rad.md), [Materials](materials.md), [Things](things.md), [Player](player.md).

## Layers

| Layer | Source | When it applies | What it lights |
|-------|--------|-----------------|----------------|
| Baked lightmaps | Material emission + `point-light` / `spot-light` things via `sloprad` | Offline → `rad/` atlases | Map brush meshes (lightmap shader) |
| Thing light entities | `(point-light …)`, `(spot-light …)`, `(area-light …)`, `(sun …)` | Map load → flecs components | Bake (point/spot); authoring / gizmos at runtime |
| Dynamic lights | `DynamicLight` component (e.g. FP flashlight) | Each frame, ranked near the camera | Map shader add-on; FP rad tint / probe |

There is no runtime PBR stack. Props and characters are not lightmapped; sprites can sample map light at their feet when lightmaps exist. Viewmodels use optional rad tint and faux shading; see [Player](player.md).

## Baked light

`sloprad` builds lightmap atlases from:

1. Surface emission: brush materials with `emission-color` / `emission-power` (and emission textures at bake time). See [Materials](materials.md).
2. Placed point and spot lights: collected from `maps/<name>/things.s7` (and prefab sidecars) by `collectRadiosityLights`. Area and sun things are not bake emitters today.

Direct + bounce irradiance lands in `rad/atlasN.png` and `rad/static.rad`. At runtime the lightmap fragment path is:

```text
final = albedo * (baked + dynamic) + emit
```

`baked` comes from the atlas (`useLightmap = 1`) or white (`0` / unlit debug). `dynamic` is the ranked runtime overlay below. `emit` is the flat material emission color when power is set.

Re-bake after emission or light thing edits that should change static lighting. Moving a thing light in the editor without re-running `sloprad` does not update the atlases.

## Thing lights

Engine forms in `things.s7` (also editable in `slopmap`):

| Form | Flecs component | Bake | Runtime dynamic overlay |
|------|-----------------|------|-------------------------|
| `(point-light …)` | `PointLight` | Yes | No (not gathered as `DynamicLight`) |
| `(spot-light …)` | `SpotLight` | Yes | No |
| `(area-light …)` | `AreaLight` | No | No |
| `(sun …)` | `SunLight` | No | No |

Shared optional fields: `(color r g b)` (default `1 1 1`), `(intensity N)` (default `1`).

| Form | Required | Extra fields |
|------|----------|--------------|
| `(point-light …)` | `id`, `at` | `(range N)` default `8` |
| `(spot-light …)` | `id`, `at` | `(yaw …)` or `(angles …)`, `(range N)`, `(cone radians)` default `0.7` |
| `(area-light …)` | `id`, `at` | `(angles …)`, `(size width height)` default `1 1` |
| `(sun …)` | `id` | Direction from `(angles …)` or `(yaw …)`; optional `(at …)` for editor gizmo only |

Example:

```text
(point-light
  (id "lamp-a")
  (at 0.0 2.0 0.0)
  (color 1.0 0.95 0.9)
  (intensity 1.0)
  (range 8.0))

(spot-light
  (id "desk-spot")
  (at 1.0 2.2 0.5)
  (yaw 0.0)
  (color 1.0 0.98 0.92)
  (intensity 1.2)
  (range 6.0)
  (cone 0.55))
```

Use things for static or bake-time lighting in the level. Use `DynamicLight` (below) for lights that must change at runtime (flashlights, temporary glows, script-toggled sources).

## Dynamic lights

A dynamic light is a flecs entity with `DynamicLight` plus a transform (`LocalTransformation` / `GlobalTransformation`). Optional space tag:

- `WorldSpace`: position and direction already in world meters.
- `ViewSpace`: eye-relative; at gather time the player `Lens` converts position and direction into world space so a flashlight on the FP stage lights the map without rotating the weapon root.

### Component

| Field | Meaning |
|-------|---------|
| `kind` | `Point` or `Spot` |
| `color` | Stored as RGB, HSV, or HSL; evaluated to linear RGB for shading |
| `intensity` | Scale; `≤ 0` skips the light in gather |
| `range` | Falloff distance (meters) |
| `coneAngle` | Spot outer cone (radians); ignored for point |
| `castShadows` | Eligible for a shadow slot when ranking (shadow maps exist; map draw currently uploads lights without binding shadow maps) |

Helpers: `spawnDynamicLight`, color setters / modulators in `src/render/dynamic_light.hpp`. First-person Scheme uses `(fp-spawn-light …)` / `(fp-set-light-enabled …)`; see [Player](player.md).

### Gather and rank

Each frame (player `Lens` world draw), the engine:

1. Collects every entity with `DynamicLight` + `GlobalTransformation` and `intensity > 0`.
2. Converts `ViewSpace` lights through the lens.
3. Scores candidates near the camera (`intensity * range / (1 + distance)`).
4. Keeps up to 8 lights (`kMaxDynamicLights`); up to 2 may take shadow slots if `castShadows` is set.

Ranked lights live in `DynamicLightFrameState` for the frame. The map lightmap shader receives them as `dynLight*` uniforms. The same list feeds first-person rad tint sampling at the player feet.

Unlit debug clears dynamic contribution (and can force white baked light).

### Shader add-on

In `default/lightmap_frag`, each ranked light applies range-squared attenuation; spots also apply a smooth cone. Contribution is added to the baked sample before multiplying albedo. Dynamic lights do not replace lightmaps.

### First-person lights

Packages may attach a spot under the `emission` socket and toggle it from Scheme. That entity is a normal `DynamicLight` in `ViewSpace`: when enabled it lights both the world (via gather → lightmap shader) and the viewmodel probe when rad tint is on. The FP stage itself does not own flashlight rules; package scripts do. See [Player: First-person scene](player.md#first-person-scene).

## What belongs where

| Goal | Prefer |
|------|--------|
| Static room lighting | Emission materials and/or point/spot things + `sloprad` |
| Editor-visible light markers | Thing forms (always spawn components + gizmos) |
| Runtime toggle / move / player-held | `DynamicLight` (Scheme FP API or C++ `spawnDynamicLight`) |
| Viewmodel look (tint / faux shade) | `(fp-set-rad-tint)` / `(fp-set-shading)` (presentation only) |

Do not treat thing `PointLight` / `SpotLight` components as the runtime overlay: only `DynamicLight` is gathered for the map shader and FP probe.
