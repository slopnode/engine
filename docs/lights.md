# Lights

Lighting is a bake-first pipeline with two runtime overlays. Map surfaces get offline lightmaps; a small ranked DynamicLight list can splash the map and dynamic receivers; a separate high-count FxLocalLight channel tints only sprites, models, and movers. Thing light forms in things.s7 feed the bake (point / spot) and editor gizmos; they are not the same as either runtime path.

Related: [Maps](maps.md), [Radiosity](rad.md), [Materials](materials.md), [Things](things.md), [Player](player.md), [slopmap](slopmap.md), [View frustum culling](frustum.md).

## Layers

Lighting is four layers that look related in the editor but do different jobs.

Baked lightmaps are the main look for map brushes. Material emission, optional ambient-light / sun things (sun through sky-material faces), and point/spot things go through sloprad offline into rad/ atlases, and the lightmap shader samples those atlases at runtime. Thing light entities (point-light, spot-light, area-light, sun, ambient-light) spawn flecs components and gizmos when the map loads; point, spot, sun, and ambient-light feed the bake — they are not gathered as runtime DynamicLights. Place and rotate a sun thing for directional bake sun; place an ambient-light thing for soft fill (absent ⇒ black). Sun still needs `(sky)` materials on open faces. Dynamic lights are a separate DynamicLight component (for example a first-person flashlight): each frame the engine ranks nearby ones and adds them on the map shader, and they also feed FP rad tint / probe sampling. FX local lights (FxLocalLight) are a high-count point-light channel for missiles, muzzle flashes, and similar effects: they tint sprites, 3D models, and movers only, and never upload to the map lightmap shader.

There is no runtime PBR stack. Props and characters are not lightmapped; they receive bake probes plus DynamicLight / FxLocalLight overlays via CPU tint. Sprites sample map light at their feet when lightmaps exist, then add the same overlays. Viewmodels use optional rad tint and faux shading; see [Player](player.md).

## Baked light

sloprad builds lightmap atlases from:

1. Soft ambient fill from an `ambient-light` thing (color × intensity). No ambient-light ⇒ black. Bake seed / runtime probe fallback only; not added again in the lightmap shader.
2. Optional directional sun from a `sun` thing (color, intensity, angles/yaw), admitted only through faces whose material has `(sky)`. A luxel receives sun when its ray toward the sun first hits a sky face. Omit the sun thing and/or sky materials for indoor or underground maps.
3. Surface emission: brush materials with emission-color / emission-power (and emission textures at bake time). See [Materials](materials.md). Sky materials are not emission emitters and are not lightmapped.
4. Placed point and spot lights: collected from maps/{name}/things.s7 (and prefab sidecars) by collectRadiosityLights. Area lights are not bake emitters.

Direct + bounce irradiance lands in rad/atlasN.png and rad/static.rad. At runtime the lightmap fragment path is:

```text
final = albedo * (baked + dynamic) + emit
```

baked comes from the atlas (useLightmap = 1) or white (0 / unlit debug). dynamic is the ranked runtime overlay below. emit is the flat material emission color when power is set.

Re-bake after emission or light thing edits that should change static lighting. Moving a thing light in the editor without re-running sloprad does not update the atlases.

## Thing lights

Engine forms in things.s7 (also editable in [slopmap](slopmap.md)):

| Form | Flecs component | Bake | Runtime dynamic overlay |
|------|-----------------|------|-------------------------|
| (point-light ...) | PointLight | Yes | No (not gathered as DynamicLight) |
| (spot-light ...) | SpotLight | Yes | No |
| (area-light ...) | AreaLight | No | No |
| (sun ...) | SunLight | Yes (directional; needs sky faces) | No |
| (ambient-light ...) | AmbientLight | Yes (soft fill) | No |

Shared optional fields: (color r g b) (default 1 1 1), (intensity N) (default 1).

| Form | Required | Extra fields |
|------|----------|--------------|
| (point-light ...) | id, at | (range N) default 8 |
| (spot-light ...) | id, at | (yaw ...) or (angles ...), (range N), (cone radians) default 0.7 |
| (area-light ...) | id, at | (angles ...), (size width height) default 1 1 |
| (sun ...) | id | Direction from (angles ...) or (yaw ...); optional (at ...) for gizmo; bake directional sun |
| (ambient-light ...) | id | Optional at (gizmo), color, intensity; bake/runtime ambient (omit ⇒ black) |

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

Use things for static or bake-time lighting in the level. Use DynamicLight (below) for lights that must change at runtime (flashlights, temporary glows, script-toggled sources).

## Dynamic lights

A dynamic light is a flecs entity with DynamicLight plus a transform (LocalTransformation / GlobalTransformation). Optional space tag:

- WorldSpace: position and direction already in world meters.
- ViewSpace: eye-relative; at gather time the player Lens converts position and direction into world space so a flashlight on the FP stage lights the map without rotating the weapon root.

### Component

| Field | Meaning |
|-------|---------|
| kind | Point or Spot |
| color | Stored as RGB, HSV, or HSL; evaluated to linear RGB for shading |
| intensity | Scale; <= 0 skips the light in gather |
| range | Falloff distance (meters) |
| coneAngle | Spot outer cone (radians); ignored for point |
| castShadows | Eligible for a shadow slot when ranking (up to 2 shadowed lights; map lightmap shader samples depth maps for hard occlusion) |

Helpers: spawnDynamicLight, color setters / modulators in src/render/dynamic_light.hpp. First-person Scheme uses (fp-spawn-light ...) / (fp-set-light-enabled ...); see [Player](player.md).

### Gather and rank

Each frame (player Lens world draw), the engine:

1. Collects every entity with DynamicLight + GlobalTransformation and intensity > 0.
2. Converts ViewSpace lights through the lens.
3. Scores candidates near the camera (intensity * range / (1 + distance)). Out-of-frustum lights (sphere at position with range) are dropped before ranking; see [View frustum culling](frustum.md).
4. Keeps up to 8 lights (kMaxDynamicLights); up to 2 may take shadow slots if castShadows is set and Graphics → Dynamic Light Shadows is on.

Ranked lights live in DynamicLightFrameState for the frame. Shadow maps are rendered for slotted lights, then the map lightmap shader receives dynLight* uniforms plus depth maps / light view-projections. The same ranked list feeds first-person rad tint sampling at the player feet.

Unlit debug clears dynamic contribution (and can force white baked light). Graphics → Dynamic Lights off skips gather/upload; Dynamic Light Shadows off keeps lights but assigns no shadow slots.

### Shader add-on

In default/lightmap_frag, each ranked light applies range-squared attenuation; spots also apply a smooth cone. Lights with a shadow slot multiply by a hard depth-map visibility test (point: cube face from light→fragment; spot: single face). Contribution is added to the baked sample before multiplying albedo. Dynamic lights do not replace lightmaps.

### First-person lights

Packages may attach a spot under the emission socket and toggle it from Scheme. That entity is a normal DynamicLight in ViewSpace: when enabled it lights both the world (via gather -> lightmap shader) and the viewmodel probe when rad tint is on. The FP stage itself does not own flashlight rules; package scripts do. See [Player: First-person scene](player.md#first-person-scene).

## FX local lights

FxLocalLight is for many short-lived point lights that should affect dynamic receivers without re-shading the baked map.

| | DynamicLight | FxLocalLight |
|--|--|--|
| Typical use | Flashlight, key scripted spots | Missiles, muzzle flashes, explosions |
| Count | Ranked top 8 | High count (spatial grid; per-receiver nearest 16) |
| Lights map brushes | Yes (lightmap shader uniforms) | No |
| Lights sprites / models / movers / FP tint | Yes | Yes |
| Shadows | Up to 2 GPU shadow-map slots (hard occlusion on map brushes) | None |
| Kind | Point or spot | Point only |

Each frame the engine gathers FxLocalLight entities with intensity > 0, frustum-culls by range, and builds FxLightFrameState (dense list + uniform grid). Receivers query nearby cells and evaluate up to 16 nearest lights with the same attenuation math as DynamicLight points. Unlit debug clears FX contribution with the other overlays.

### Receiver LOS occlusion

CPU overlays (sprites, models/movers, FP rad tint) skip a DynamicLight or FxLocalLight when `bspSegmentOccluded` on `MapLighting.surfaceBvh` reports a wall between the light and the probe (endpoints nudged a few centimeters to avoid contact false positives). This is the same bake-time segment test radiosity uses.

Map brushes lit by ranked DynamicLights use the GPU shadow-map path above (not this CPU segment test). FxLocalLight never hits map brushes.

### Component and spawn

| Field | Meaning |
|-------|---------|
| color | DynamicLightColor (RGB / HSV / HSL); linear RGB at gather |
| intensity | Scale; <= 0 skips the light |
| range | Falloff distance (meters) |

C++: spawnFxLocalLight in src/render/fx_local_light.hpp. Scheme:

```text
(fx-light-spawn id x y z r g b intensity range [lifetime])
(fx-light-attach id r g b intensity range)
(dyn-light-spawn id x y z r g b intensity range [lifetime])
(dyn-light-attach id r g b intensity range)
```

`fx-light-spawn` / `dyn-light-spawn` create a new MapOwned entity (optional TimedDespawn). `*-attach` adds the component to an existing entity so the light rides its transform (e.g. a motored plasma bolt). Scheme `dyn-light-*` and `fp-spawn-light` default `castShadows` on so they can take a shadow slot when Graphics → Dynamic Light Shadows is enabled.

## What belongs where

For static room lighting, prefer emission materials and/or point/spot things, then re-run sloprad. Thing forms are also the right place for editor-visible light markers: they always spawn components and gizmos even when a kind is not a bake emitter. Lights that must toggle, move, or ride with the player and splash the world belong on DynamicLight (Scheme FP API or C++ spawnDynamicLight). Ephemeral glows that only need to tint sprites and movers belong on FxLocalLight (fx-light-spawn). Viewmodel look -- rad tint and faux shade -- is presentation only via (fp-set-rad-tint) / (fp-set-shading).

Do not treat thing PointLight / SpotLight components as runtime lights: only DynamicLight feeds the map shader, and only DynamicLight / FxLocalLight feed receiver probes.
