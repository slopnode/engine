# Things

Placed content in a level (static props, usables, actors, lights) is authored in `maps/<name>/things.s7`. A thing is the authored record (id, pose, kind, presentation or light params). At load, the engine spawns a flecs entity from each thing. The map file is composition: ids, poses, and which presentation or handler to use. Behavior and shared helpers live in package Scheme under `scripts/`. The player is separate; see [Player](player.md).

World solids stay in CSG / BSP ([Maps](maps.md)). Thing presentation uses sprites ([Sprites](sprites.md)) or prop meshes ([Geometry](geometry.md)).

## Kinds

A static prop (`(prop ...)`) is visual only: a sprite or mesh at a pose, with no interact and no AI. A usable (`(usable ...)`) uses the same presentation, then adds an interact prompt and optional Scheme `on-use` handler. An actor (`(actor ...)`) uses the same presentation plus a character capsule motor and opaque tags; packages own brains, health, and factions. Light things cover bake and editor work: `(point-light ...)` and `(spot-light ...)` feed radiosity and gizmos; `(area-light ...)` and `(sun ...)` are authoring / gizmo forms today (sun may omit a meaningful world `at` and still use one for the editor handle). See [Lights](lights.md).

`(player-start ...)` is spawn pose only -- it does not create a prop entity; the engine builds `Player` from that pose after map load.

These forms are engine Scheme bindings, always available regardless of which package is mounted. Package scripts may wrap them; they do not define the primitives.

Debug entity list labels match this split: `prop`, `usable`, `actor`, `point-light`, `spot-light`, `area-light`, `sun`, `player` (plus `map` for `MapStatic`).

[slopmap](slopmap.md) edits things in the Things outliner and Library palette (load/save of `things.s7`). Viewport shows sprite/geo previews and light gizmos.

## Thing file

`maps/<name>/things.s7` is optional Scheme evaluated after map geometry. Engine bindings are active only during that load. Ids must be unique within the file. Missing file -> no placed props/usables/lights; player uses the default spawn.

```text
(prop
  (id "crate-a")
  (at 2.0 0.0 -1.0)
  (yaw 0.0)
  (geo "props/crate"))

(prop
  (id "guard-a")
  (at -2.0 0.0 -2.0)
  (yaw 0.0)
  (sprite "characters/guard")
  (anim "walk" #t))

(usable
  (id "use-test")
  (at 1.5 0.0 0.0)
  (yaw 0.0)
  (sprite "characters/guard")
  (frame "A")
  (prompt "Test use")
  (on-use "on-use-test"))

(point-light
  (id "lamp-a")
  (at 0.0 2.0 0.0)
  (color 1.0 0.95 0.9)
  (intensity 1.0)
  (range 8.0))
```

## Static props (`prop`)

A prop is a named flecs entity with world transform and exactly one presentation path: sprite billboard or `.geo` mesh. It does not get `Interactable`, a character motor, or a physics capsule. Use props for clutter, scenery characters that only idle or loop an anim, and anything that should appear in the level without use or AI.

| Field | Required | Notes |
|-------|----------|-------|
| `id` | yes | Flecs entity name; must be unique in the file. |
| `at` | yes | World position (`x y z`). |
| `sprite` or `geo` | exactly one | Virtual path (no extension). |
| `yaw` | no | Radians around Y (default `0`). Sets transform rotation and sprite `facingYaw`. |
| `frame` | no | Sprite frame id (default `"A"`). Ignored for `geo`. |
| `(anim clip [loop])` | no | Starts a `SpriteAnimator` on the sprite path. `loop` is boolean or `0`/`1` (default loop on). |

Runtime components for a sprite prop:

```text
WorldSpace
LocalTransformation { position, yaw rotation, scale 1 }
SpriteInstance      { sprite, frame, facingYaw }
SpriteAnimator      { optional; from (anim ...) }
```

For `geo`, the entity gets `Model3D` (cloned instance) instead of sprite components. Props are not lightmapped; they sample or draw like other placeable meshes/sprites.

Failed presentation (missing asset, both or neither of `sprite`/`geo`) destroys the entity and skips it.

## Usables (`usable`)

Same clauses as `prop`, plus interact fields. The entity is still a static thing with no navigation, but the player can aim at it and press Interact.

| Field | Required | Notes |
|-------|----------|-------|
| (all `prop` fields) | same rules | Presentation identical to `prop`. |
| `prompt` | no | HUD / UI prompt text (default `"Interact"`). |
| `on-use` | no | Name of a Scheme procedure in the package script environment. |

Adds:

```text
Interactable { prompt, eventName = on-use name, maxDistance = 5 }
```

Interact casts a ray from the player `Lens`. Closest hit among usables with `Model3D` (mesh) or `SpriteInstance` (billboard / hit mask) within `maxDistance` becomes the current target. On Interact:

1. If `on-use` names a procedure, call it with one argument: the entity id string.
2. Otherwise open the inspect Interact UI.

Usables are the content-facing wrapper around the engine `Interactable` primitive. Doors, switches, and terminals can be package-named helpers later that expand to `usable` (or other primitives) without new C++ per object.

## Lights

Light things are engine forms that always exist. They spawn flecs entities with light components and a transform. Point and spot things contribute to the radiosity bake; at runtime the dynamic overlay uses a separate `DynamicLight` path (for example the FP flashlight). Full detail: [Lights](lights.md).

Shared optional fields: `(color r g b)` (default `1 1 1`), `(intensity N)` (default `1`).

| Form | Required | Extra fields |
|------|----------|--------------|
| `(point-light ...)` | `id`, `at` | `(range N)` default `8` |
| `(spot-light ...)` | `id`, `at` | `(yaw ...)` or `(angles ...)`, `(range N)`, `(cone radians)` default `0.7` |
| `(area-light ...)` | `id`, `at` | `(angles ...)`, `(size width height)` default `1 1` |
| `(sun ...)` | `id` | Direction from `(angles ...)` or `(yaw ...)`; optional `(at ...)` for editor gizmo only |

## Motored bodies

A motored body is a runtime-spawned presented entity (`sprite` or `geo`) plus a `MotoredBody` motor: package-chosen velocity, gravity, sweep radius, and lifetime. The engine integrates motion each frame and sphere-casts against static brush hulls (the same solids the player walks on). The player capsule is a `CharacterVirtual`, not a rigid body, so world casts ignore the player.

Packages define recipes (rockets, arcing throws, bolts) with `(motored-spawn ...)` from [Scripting](scripting.md). Aim helpers `(player-eye)` / `(player-look-dir)` supply spawn origin and direction. On world hit, the engine calls an optional `on-impact` Scheme handler with the thing id, then despawns. Empty handler = silent despawn. This is motor-driven flight, not full dynamic rigid-body simulation.

## Actors

An actor is a presented world body with a character capsule motor and opaque `CollisionTags`. It is the engine primitive for walking entities packages may treat as enemies, NPCs, or neutrals. Distinct from:

- Prop: placed, may animate a sprite clip, does not move.
- Usable: static (or later movable) fixture the player uses.
- Motored body: runtime flyer with package velocity/gravity; see [Motored bodies](#motored-bodies).
- Light: illumination thing (bake for point/spot; runtime dynamic overlay is separate; see [Lights](lights.md)).
- Player: engine-owned first-person pawn; FP stage is presentation only ([Player](player.md)). Shares the same character-motor registry as actors.

```text
(actor
  (id "guard-a")
  (at -2.0 0.0 -2.0)
  (yaw 0.0)
  (sprite "characters/guard")
  (anim "idle" #t)
  (motor (radius 0.3) (height 1.1) (speed 3.5) (gravity 9.81))
  (tags "actor" "team:security"))
```

| Field | Required | Notes |
|-------|----------|-------|
| (all `prop` presentation fields) | same rules | Sprite or geo, pose, optional anim. |
| `(motor ...)` | no | Nested `(radius)`, `(height)`, `(speed)`, `(gravity)`. Defaults match the player capsule (`0.3`, `1.1`, `6`, `9.81`). |
| `(tags ...)` | no | Opaque strings copied to `CollisionTags`. Empty → `("actor")`. |

Spawn adds `Actor`, `CharacterMotor`, and `CollisionTags`, then creates a Jolt `CharacterVirtual`. Packages write wish via `(actor-set-wish id wx wz)`; the engine integrates against static hulls. Query helpers (`actors-with-tag`, `actors-in-radius`, `los?`) are in [Scripting](scripting.md). Nav pathfollowing is not shipped yet; `graphs.s7` remains authoring data. Health, factions, and combat stay in package Scheme.

## Scripting

Package Scheme and map `things.s7` share one s7 heap. Load order, hooks, and runtime APIs are covered in [Scripting](scripting.md). Map files stay thin: poses, presentation paths, and handler name strings. Behavior lives under `scripts/`.

### `on-use` handlers

Define a procedure in `scripts/things.s7` (or anything loaded into the same environment). The name in `(on-use "...")` must match.

```text
(define (on-use-test thing-id)
  (format #t "used ~a~%" thing-id))
```

The engine looks up the name with `s7_name_to_value`, checks it is a procedure, and calls it with the thing id string (the flecs entity name). There is no entity object API in Scheme yet, only that id, so handlers today are side-effect stubs (log, set game state you keep in Scheme, etc.). Missing or non-procedure names fall back to the inspect UI.

### What belongs where

- Map `things.s7`: instance data (where things are, which sprite/geo, which handler name, prompts, light params).
- Package `scripts/`: reusable behavior and, over time, constructors that wrap engine forms (`usable`, lights, future trigger / movable / actor) so levels stay thin.
- Engine: spawn bindings, presentation, interact ray, player pawn, light thing forms. Not content catalogs.

### Extending without engine churn

Prefer new package procedures and map calls over new C++ thing kinds for each crate or NPC. When many packages need the same mechanic (trigger volume, motored body / rigid mover, light type), that is when a new engine primitive earns a binding; content still supplies meshes, prompts, and Scheme reactions.
