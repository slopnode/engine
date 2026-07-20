# Placements

Placed content in a level—static props, usables, lights, and (later) actors—are authored in `maps/<name>/entities.s7`. A **placement** is the authored record (id, pose, kind, presentation or light params). At load, the engine spawns a flecs **entity** from each placement. The map file is composition: ids, poses, and which presentation or handler to use. Behavior and shared helpers live in package Scheme under `scripts/`. The player is separate; see [Player](player.md).

World solids stay in CSG / BSP ([Maps](maps.md)). Placement presentation uses sprites ([Sprites](sprites.md)) or prop meshes ([Geometry](geometry.md)).

## Kinds

| Kind | Map form | Role today |
|------|----------|------------|
| Static prop | `(prop …)` | Visual only: sprite or mesh at a pose. No interact, no AI. |
| Usable | `(usable …)` | Same presentation as a prop, plus an interact prompt and optional Scheme `on-use`. |
| Point light | `(point-light …)` | Local omnidirectional light placement (bake + gizmos; see [Lights](lights.md)). |
| Spot light | `(spot-light …)` | Directed cone light placement (bake + gizmos). |
| Area light | `(area-light …)` | Rectangular area light placement (authoring / gizmo). |
| Sun | `(sun …)` | Directional sun placement (authoring / gizmo; optional `at` for editor). |
| Actor | — | Intended: AI nav agents (enemies, NPCs). Not a map form yet. |
| Player | `(player-start …)` | Spawn pose only; the `Player` entity is built by the engine. |

These forms are **engine Scheme bindings**—always available regardless of which package is mounted. Package scripts may wrap them; they do not define the primitives.

Debug entity list labels match this split: `prop`, `usable`, `point-light`, `spot-light`, `area-light`, `sun`, `player` (plus `map` for `MapStatic`).

`slopmap` edits placements in the **Placements** outliner and Library palette (load/save of `entities.s7`). Viewport shows sprite/geo previews and light gizmos.

## Placement file

`maps/<name>/entities.s7` is optional Scheme evaluated after map geometry. Engine bindings are active only during that load. Ids must be unique within the file. Missing file → no placed props/usables/lights; player uses the default spawn.

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
  (sprite "usmc/umca")
  (anim "walk" #t))

(usable
  (id "use-test")
  (at 1.5 0.0 0.0)
  (yaw 0.0)
  (sprite "usmc/umca")
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
SpriteAnimator      { optional; from (anim …) }
```

For `geo`, the entity gets `Model3D` (cloned instance) instead of sprite components. Props are not lightmapped; they sample or draw like other placeable meshes/sprites.

Failed presentation (missing asset, both or neither of `sprite`/`geo`) destroys the entity and skips it.

## Usables (`usable`)

Same clauses as `prop`, plus interact fields. The entity is still a static placement—no navigation—but the player can aim at it and press Interact.

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

Light placements are engine forms that always exist. They spawn flecs entities with light components and a transform. Point and spot placements contribute to the radiosity bake; at runtime the dynamic overlay uses a separate `DynamicLight` path (for example the FP flashlight). Full detail: [Lights](lights.md).

Shared optional fields: `(color r g b)` (default `1 1 1`), `(intensity N)` (default `1`).

| Form | Required | Extra fields |
|------|----------|--------------|
| `(point-light …)` | `id`, `at` | `(range N)` default `8` |
| `(spot-light …)` | `id`, `at` | `(yaw …)` or `(angles …)`, `(range N)`, `(cone radians)` default `0.7` |
| `(area-light …)` | `id`, `at` | `(angles …)`, `(size width height)` default `1 1` |
| `(sun …)` | `id` | Direction from `(angles …)` or `(yaw …)`; optional `(at …)` for editor gizmo only |

## Actors

**Actor** means an AI-capable agent that can navigate and interact: enemies, NPCs, and similar. That is distinct from:

- **Prop** — placed, may animate a sprite clip, does not think or pathfind.
- **Usable** — static (or later movable) fixture the player uses.
- **Light** — illumination placement (bake for point/spot; runtime dynamic overlay is separate—[Lights](lights.md)).
- **Player** — engine-owned first-person pawn; FP stage is presentation only ([Player](player.md)).

There is no `(actor …)` form yet and no nav / AI stack in the engine. Until that exists, decorative or ambient characters belong as `(prop …)` (optionally with `(anim …)`). When actors land, expect a map form or package constructor that still uses the same presentation clauses (`sprite` / `geo`, pose) and adds motor / brain / nav data on top—behavior owned by package scripts where possible, with engine primitives for movement and sensing.

## Scripting

Two Scheme layers share one s7 heap for the run:

| Source | When | Purpose |
|--------|------|---------|
| `scripts/init.s7` | App start | Package bootstrap (version, shared defs). |
| `scripts/entities.s7` | App start (after `init`) | Handlers and helpers for placed content (`on-use`, later prefabs). |
| `scripts/player.s7` | After FP Scheme API bind (before map spawn) | First-person **presentation**: `(prepare-first-person)`, `(on-action-flashlight)`, etc. Game rules stay in package state; FP API only mirrors them. |
| `maps/<name>/entities.s7` | Map load | Placement only: `player-start`, `prop`, `usable`, lights, `prefab`. |

Virtual paths omit the extension: `init`, `entities`, `player`, `<map>/entities`. Later packages override earlier ones at the same path.

### `on-use` handlers

Define a procedure in `scripts/entities.s7` (or anything loaded into the same environment). The name in `(on-use "…")` must match.

```text
(define (on-use-test entity-id)
  (format #t "used ~a~%" entity-id))
```

The engine looks up the name with `s7_name_to_value`, checks it is a procedure, and calls it with the entity id string. There is no entity object API in Scheme yet—only that id—so handlers today are side-effect stubs (log, set game state you keep in Scheme, etc.). Missing or non-procedure names fall back to the inspect UI.

### What belongs where

- **Map `entities.s7`** — instance data: where placements are, which sprite/geo, which handler name, prompts, light params.
- **Package `scripts/`** — reusable behavior and, over time, constructors that wrap engine forms (`usable`, lights, future trigger / movable / actor) so levels stay thin.
- **Engine** — spawn bindings, presentation, interact ray, player pawn, light placement forms. Not content catalogs (no built-in “USMC guard” type).

### Extending without engine churn

Prefer new package procedures and map calls over new C++ placement kinds for each crate or NPC. When many packages need the same mechanic (trigger volume, rigid mover, light type), that is when a new engine primitive earns a map binding; content still supplies meshes, prompts, and Scheme reactions.
