# Player

Related: [Scripting](scripting.md), [Sprites](sprites.md), [Audio](audio.md), [Lights](lights.md), [Things](things.md).

The first-person avatar is a single flecs entity named `Player`. Maps do not place a visible character mesh for it; they only set spawn pose. Movement, look, and camera follow from components on that entity plus a physics character capsule. Props, usables, lights, and other things are separate; see [Things](things.md), [Lights](lights.md), and [Maps](maps.md).

## Spawn (`player-start`)

Optional Scheme in `maps/<name>/things.s7` supplies feet position and facing. The form does not create a prop entity; after the map loads, the game builds `Player` from this pose (or from defaults).

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
| `yaw` | no | Facing in radians around Y. Omit -> `0`. |

Only the first `player-start` is used; later ones log a warning and are ignored. If the file is missing, or has no `player-start`, the spawn is `(0, 0.1, 0)` facing yaw `pi`.

`at` is the character feet / capsule origin. The view is placed at feet plus `eyeHeight` (default `1.7`).

## Runtime entity

Systems look up `world.lookup("Player")`. On a map load the entity carries:

| Component | Role |
|-----------|------|
| `PlayerCamera` | Tag: this entity is the first-person view. |
| `WorldSpace` | In the world transform set (no `LocalTransformation`; pose comes from `Lens` / physics). |
| `Lens` | raylib `Camera3D`: position, target, up, `fovy` `75`, perspective. |
| `FirstPersonController` | Yaw / pitch look, move and look rates, eye height used when not physics-driven. |
| `ViewEyeOffset` | View-space presentation offset; default zero. Does not affect aim. |
| `FirstPersonScene` | Handles for the view-space stage root and sockets (`weapon`, `emission`). |
| `CharacterMotor` | Capsule motor params and wish velocity; present on map scenes. |

Demo / non-map scenes may spawn `Player` without `CharacterMotor`; look still runs, and movement then slides the camera on a flat plane at `eyeHeight`.

## First-person scene

The first-person scene is a presentation layer: it shows what the package decided is currently equipped, lit, or held. It is not the owner of gameplay rules, inventory, weapon logic, or flashlight state. Those live in package Scheme (or other game systems); the FP stage only attaches meshes and lights so the player can see that state.

### Ownership

The engine owns the empty eye-space stage (`PlayerFp`) and its sockets, the draw of `ViewSpace` geo and view sprites after the world, and the primitives to attach meshes or sprites, mutate sprite pose, set eye offset, and spawn or toggle a `DynamicLight`. It also owns the raw `Lens` used for aim and interact, the presentation camera for world draw, and optional rad tint / faux shading look.

It does not own inventory, ammo, loadouts, or weapon switch rules. Hit detection, fire, reload, and usability stay in game logic. Whether the flashlight "should" be on, and raise/lower, bob, or holster policy, are package decisions -- the stage only shows the result. Map lighting design (bake plus dynamic overlay) is separate; see [Lights](lights.md).

Input still reaches the package as hooks (for example flashlight -> `(on-action-flashlight)`). The engine does not infer weapons from the stage contents.

### Stage layout

The engine owns an empty eye-space stage rooted at `PlayerFp` (`ViewSpace`), with child sockets `weapon` and `emission`. The stage stays fixed in view space: looking around does not move or rotate it. Packages fill the sockets; the engine does not define weapons, inventory, or flashlight recipes.

View axes: `+X` screen-right, `+Y` up, `+Z` forward. Author viewmodels and offsets in that space.

Raw vs presentation eye: Physics and look write the authoritative `Lens` (feet + `eyeHeight` + yaw/pitch). Aim and interact always use that raw `Lens`. Packages may set a view-space `ViewEyeOffset` via `(fp-set-eye-offset x y z)`; world draw and `ViewSpace` light lifts use a presentation camera (raw eye + offset). The offset is never written back into `Lens`. Bob / punch / settle formulas stay package Scheme.

After `Player` spawns, the engine calls `(prepare-first-person "Player")` if that procedure exists (loaded from `scripts/player.s7`). That hook is the usual place to sync sockets to initial game state (clear, attach geo, spawn lights, set tint flags). View models draw in a separate fixed-eye pass after the world. Dynamic lights under `ViewSpace` are converted to world space at light-gather time via the presentation camera (so a flashlight still lights the map without rotating the weapon stage). Details: [Lights](lights.md).

### Scheme API (engine primitives)

These bindings mutate presentation only (or read motion sensors). Keep authoritative gameplay state in package variables or entities elsewhere, then call into this API when that state changes.

| Binding | Purpose |
|---------|---------|
| `(fp-clear-socket name)` | Destroy children of `weapon` or `emission`. |
| `(fp-attach-geo socket geo [x y z sx sy sz])` | Attach a geo viewmodel under a socket. |
| `(fp-attach-sprite socket sprite [canvas-x canvas-y])` | Attach a screen-space sprite under a socket. Optional canvas position places the sprite origin (default bottom-center) on the view canvas. Formats and pose/tween: [Sprites](sprites.md). |
| `(fp-set-sprite-frame socket frame-id)` | Set the current sprite frame id. |
| `(fp-play-sprite-anim socket clip [loop])` | Play a `.spanim` clip on the socket sprite (tween / frame sounds apply; see [Sprites](sprites.md), [Audio](audio.md)). |
| `(fp-set-sprite-pos socket x y)` | Move the view-sprite origin on the view canvas. |
| `(fp-set-sprite-scale socket sx sy)` | Independent X/Y scale multipliers (default `1 1`). |
| `(fp-set-sprite-rotation socket degrees)` | Rotation in degrees around the sprite origin. |
| `(fp-set-sprite-origin socket ox oy)` | Normalized pivot in sprite space (default `0.5 1.0` = bottom-center). Canvas position places this pivot. |
| `(fp-set-eye-offset x y z)` | View-space eye offset in meters for the presentation camera only. Does not affect aim/interact. |
| `(player-speed)` | Horizontal character speed (m/s); `0` if no physics player. |
| `(player-grounded?)` | `#t` when the character is supported; `#f` if unsupported or no body. |
| `(player-wish-speed)` | `hypot(wishX, wishZ)` from `CharacterMotor` (move intent). |
| `(fp-spawn-light socket kind [intensity range cone r g b x y z])` | Spawn a dynamic light under a socket (starts off). |
| `(fp-set-light-enabled socket enabled)` | Toggle light intensity using the spawn-time on-intensity. |
| `(fp-set-rad-tint enabled)` | Tint viewmodels from a baked rad probe at the feet (plus dynamic lights). Off by default. |
| `(fp-set-shading enabled)` | Use package `default/viewmodel_*` faux lighting (Lambert + rim) with the probe. Off by default. |

Raise/lower, bob, kick, and similar presentation policies stay in package Scheme. The engine only exposes pose/eye mutators, motion sensors, and an optional `(tick dt)` heartbeat.

### Package hooks

| Procedure | When |
|-----------|------|
| `(prepare-first-person player-id)` | After FP scene exists on map / free-camera spawn; build the initial view from game state. |
| `(tick dt)` | Each update frame with frame delta seconds, if defined. Use for package-owned pose stepping (raise/lower, bob, etc.). |
| `(on-action-<id>)` | When a package action with that id is pressed (see Package actions below). |
| `(action-down? id)` | `#t` while the bound action is held (gameplay context only). Works for package and core action ids. |
| `(action-pressed? id)` | `#t` on the press edge this frame (gameplay context only). Works for package and core action ids. |

Packages override virtual path `player` (`scripts/player.s7`) for presentation: attach geo or view sprites, spawn socket lights, enable rad tint / shading, and react to `(on-action-*)`. Inventory and loadouts stay package-only and optional. See [Scripting](scripting.md).

### Package actions

Core binds (move, jump, pause, interact, console, main menu) are owned by the engine. Packages declare extra gameplay actions in `data/actions.s7`:

```text
(define *package-actions*
  (list
    (cons "flashlight" '((label . "Flashlight") (default . "f")))
    (cons "attack" '((label . "Attack") (default . "mouse1")))
    (cons "weapon-1" '((label . "Weapon 1") (default . "1")))))
```

| Field | Notes |
|-------|--------|
| id | Stable string used in settings and for the hook name `on-action-<id>`. |
| `label` | Controls UI display name. |
| `default` | Bind token: letter/digit keys (`f`, `1`), named keys (`space`, `grave`), or mouse (`mouse1`...`mouse5`). |

Mods override `data/actions.s7` like other package data. On press (gameplay context), the engine calls `(on-action-<id>)` if that procedure exists. Base ships flashlight, attack, and weapon-1/2; only flashlight has a handler today. Attack and slots are stubs for games to implement.

Binds support keyboard and mouse buttons. User overrides live in `settings.cfg` under `[controls]` by action id.

When rad tint is on, the FP pass samples baked light from the player feet (average of a few downward probes, ambient fallback), adds ranked dynamic lights, temporally smooths the color, then feeds that as `probeRgb` (or multiplies draw color when shading is off). Viewmodels are not lightmapped. Faux shading is GLSL under `shaders/default/viewmodel_*` (packages may override).

### `FirstPersonController`

| Field | Default | Meaning |
|-------|---------|---------|
| `yaw` | from `player-start` | Heading in radians. |
| `pitch` | `-0.05` at spawn | Look pitch; clamped to about +/-`1.4`. |
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
- Package actions (from `data/actions.s7`) call `(on-action-<id>)` when pressed.
- Scheme can poll `(action-down? id)` / `(action-pressed? id)` for held vs edge state (e.g. autofire in `(tick)`). Both return `#f` when gameplay input is blocked or the id is unknown.

Debug Noclip (main menu -> Debug) flies the capsule with move wish and no gravity; look still applies.

## Example

```text
Player
  PlayerCamera
  WorldSpace
  Lens                 { position = feet + eyeHeight, fovy = 75, ... }
  FirstPersonController { yaw from player-start, pitch ~ -0.05 }
  FirstPersonScene      { root = PlayerFp, sockets weapon / emission }
  CharacterMotor        { radius 0.3, height 1.1, eyeHeight 1.7, ... }
```

```text
(player-start
  (id "start")
  (at 0.0 0.1 0.0)
  (yaw 3.14159))
```
