# Player

The first-person avatar is a single flecs entity named `Player`. Maps do not place a visible character mesh for it; they only set spawn pose. Movement, look, and camera follow from components on that entity plus a physics character capsule. Props, usables, lights, and other placements are separate—see [Placements](entities.md) and [Maps](maps.md).

## Spawn (`player-start`)

Optional Scheme in `maps/<name>/entities.s7` supplies feet position and facing. The form does not create a prop entity; after the map loads, the game builds `Player` from this pose (or from defaults).

```text
(player-start
  (id "start")
  (at 0.0 0.1 0.0)
  (yaw 3.14159))
```

| Field | Required | Notes |
|-------|----------|-------|
| `id` | yes | Unique among entity ids in the file. |
| `at` | yes | Feet position in world meters (`x y z`). |
| `yaw` | no | Facing in radians around Y. Omit → `0`. |

Only the first `player-start` is used; later ones log a warning and are ignored. If the file is missing, or has no `player-start`, the spawn is `(0, 0.1, 0)` facing yaw `π`.

`at` is the character feet / capsule origin. The view is placed at feet plus `eyeHeight` (default `1.7`).

## Runtime entity

Systems look up `world.lookup("Player")`. On a map load the entity carries:

| Component | Role |
|-----------|------|
| `PlayerCamera` | Tag: this entity is the first-person view. |
| `WorldSpace` | In the world transform set (no `LocalTransformation`; pose comes from `Lens` / physics). |
| `Lens` | raylib `Camera3D`: position, target, up, `fovy` `75`, perspective. |
| `FirstPersonController` | Yaw / pitch look, move and look rates, eye height used when not physics-driven. |
| `FirstPersonScene` | Handles for the view-space stage root and sockets (`weapon`, `emission`). |
| `CharacterMotor` | Capsule motor params and wish velocity; present on map scenes. |

Demo / non-map scenes may spawn `Player` without `CharacterMotor`; look still runs, and movement then slides the camera on a flat plane at `eyeHeight`.

## First-person scene

The engine owns an empty **eye-space stage** rooted at `PlayerFp` (`ViewSpace`), with child sockets `weapon` and `emission`. The stage stays fixed in view space: looking around does **not** move or rotate it. Packages fill the sockets; the engine does not define weapons, inventory, or flashlight recipes.

**View axes:** `+X` screen-right, `+Y` up, `+Z` forward. Author viewmodels and offsets in that space.

After `Player` spawns, the engine calls `(prepare-first-person "Player")` if that procedure exists (loaded from `scripts/player.s7`). View models draw in a separate fixed-eye pass after the world. Dynamic lights under `ViewSpace` are converted to world space at light-gather time via the player `Lens` (so a flashlight still lights the map without rotating the weapon stage).

### Scheme API (engine primitives)

| Binding | Purpose |
|---------|---------|
| `(fp-clear-socket name)` | Destroy children of `weapon` or `emission`. |
| `(fp-attach-geo socket geo [x y z sx sy sz])` | Attach a geo viewmodel under a socket. |
| `(fp-spawn-light socket kind [intensity range cone r g b x y z])` | Spawn a dynamic light under a socket (starts off). |
| `(fp-set-light-enabled socket enabled)` | Toggle light intensity using the spawn-time on-intensity. |
| `(fp-set-rad-tint enabled)` | Tint viewmodels from a baked rad probe at the feet (plus dynamic lights). Off by default. |
| `(fp-set-shading enabled)` | Use package `default/viewmodel_*` faux lighting (Lambert + rim) with the probe. Off by default. |

### Package hooks

| Procedure | When |
|-----------|------|
| `(prepare-first-person player-id)` | After FP scene exists on map / free-camera spawn. |
| `(on-action-flashlight)` | When the Flashlight action is pressed (default **F**). |

Base package `scripts/player.s7` attaches a stub cube “gun”, a warm spot light, and enables rad tint + viewmodel shading. Other base-games override virtual path `player` to redefine presentation. Inventory and loadouts stay package-only and optional.

When rad tint is on, the FP pass samples baked light from the player feet (average of a few downward probes, ambient fallback), adds ranked dynamic lights, temporally smooths the color, then feeds that as `probeRgb` (or multiplies draw color when shading is off). Viewmodels are not lightmapped. Faux shading is GLSL under `shaders/default/viewmodel_*` (packages may override).

### `FirstPersonController`

| Field | Default | Meaning |
|-------|---------|---------|
| `yaw` | from `player-start` | Heading in radians. |
| `pitch` | `-0.05` at spawn | Look pitch; clamped to about ±`1.4`. |
| `moveSpeed` | `6` | Used when there is no `CharacterMotor`. |
| `lookSensitivity` | `0.003` | Scales mouse delta into yaw / pitch. |
| `eyeHeight` | `1.7` | Camera height when not physics-driven. |

### `CharacterMotor`

| Field | Default | Meaning |
|-------|---------|---------|
| `radius` | `0.3` | Capsule radius (meters). |
| `height` | `1.1` | Cylinder segment between the capsule hemispheres. |
| `moveSpeed` | `6` | Horizontal wish speed. |
| `gravity` | `9.81` | Applied when grounded / falling (skipped in noclip). |
| `eyeHeight` | `1.7` | Camera offset above physics feet. |
| `wishX` / `wishZ` | `0` | Filled each frame from move input. |

The physics character is a capsule built from `radius` and `height`, created at the `player-start` feet position. Each physics step writes feet position back into `Lens.camera.position` (plus `eyeHeight`) and aims `camera.target` from controller yaw / pitch.

## Input

While gameplay input is allowed:

- Move actions set `CharacterMotor` wish (forward / back / strafe relative to yaw).
- Mouse delta updates yaw and pitch on `FirstPersonController`.
- Interact uses the player `Lens` as the aim ray against `usable` entities (see [Maps](maps.md)).
- Flashlight action calls package `(on-action-flashlight)` when defined.

Debug **Noclip** (main menu → Debug) flies the capsule with move wish and no gravity; look still applies.

## Example

```text
Player
  PlayerCamera
  WorldSpace
  Lens                 { position = feet + eyeHeight, fovy = 75, … }
  FirstPersonController { yaw from player-start, pitch ≈ -0.05 }
  FirstPersonScene      { root = PlayerFp, sockets weapon / emission }
  CharacterMotor        { radius 0.3, height 1.1, eyeHeight 1.7, … }
```

```text
(player-start
  (id "start")
  (at 0.0 0.1 0.0)
  (yaw 3.14159))
```
