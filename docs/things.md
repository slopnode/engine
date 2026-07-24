# Things

Placed content in a level (static props, usables, actors, lights) is authored in maps/{name}/things.s7. A thing is the authored record (id, pose, kind, presentation or light params). At load, the engine spawns a flecs entity from each thing. The map file is composition: ids, poses, and which presentation or handler to use. Behavior and shared helpers live in package Scheme under scripts/. The player is separate; see [Player](player.md).

World solids stay in CSG / BSP ([Maps](maps.md)). Thing presentation uses sprites ([Sprites](sprites.md)) or .geo ([Geometry](geometry.md)).

## Kinds

A static prop ((prop ...)) is visual only: a sprite or mesh at a pose, with no interact and no AI. A usable ((usable ...)) uses the same presentation, then adds an interact prompt and optional Scheme on-use handler. A mover ((mover ...)) is a presented rigid leaf with kinematic collision and A/B open/close motion (doors, hatches). An actor ((actor ...)) uses the same presentation plus a character capsule motor and opaque tags; packages own brains, health, and factions. Light things cover bake and editor work: (point-light ...) and (spot-light ...) feed radiosity and gizmos; (area-light ...) and (sun ...) are authoring / gizmo forms today (sun may omit a meaningful world at and still use one for the editor handle). See [Lights](lights.md).

(player-start ...) is spawn pose only -- it does not create a prop entity; the engine builds Player from that pose after map load.

These forms are engine Scheme bindings, always available regardless of which package is mounted. Package scripts may wrap them; they do not define the primitives.

Debug entity list labels match this split: prop, usable, mover, actor, point-light, spot-light, area-light, sun, player (plus map for MapStatic).

[slopmap](slopmap.md) edits things in the Things outliner and Library palette (load/save of things.s7). Viewport shows sprite/geo previews and light gizmos.

## Thing file

maps/{name}/things.s7 is optional Scheme evaluated after map geometry. Engine bindings are active only during that load. Ids must be unique within the file. Missing file -> no placed props/usables/lights; player uses the default spawn.

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

## Static props (prop)

A prop is a named flecs entity with world transform and exactly one presentation path: sprite billboard or .geo. It does not get Interactable, a character motor, or a physics capsule. Use props for clutter, scenery characters that only idle or loop an anim, and anything that should appear in the level without use or AI.

| Field | Required | Notes |
|-------|----------|-------|
| id | yes | Flecs entity name; must be unique in the file. |
| at | yes | World position (x y z). |
| sprite or geo | exactly one | Virtual path (no extension). |
| yaw | no | Radians around Y (default 0). Sets transform rotation and sprite facingYaw. |
| frame | no | Sprite frame id (default "A"). Ignored for geo. |
| (anim clip [loop]) | no | Starts a SpriteAnimator on the sprite path. loop is boolean or 0/1 (default loop on). |

Runtime components for a sprite prop:

```text
WorldSpace
LocalTransformation { position, yaw rotation, scale 1 }
SpriteInstance      { sprite, frame, facingYaw }
SpriteAnimator      { optional; from (anim ...) }
```

For geo, the entity gets Model3D (cloned instance) instead of sprite components. Props are not lightmapped; they sample or draw like other placeable .geo / sprites.

Failed presentation (missing asset, both or neither of sprite/geo) destroys the entity and skips it.

## Usables (usable)

Same clauses as prop, plus interact fields. The entity is still a static thing with no navigation, but the player can aim at it and press Interact.

| Field | Required | Notes |
|-------|----------|-------|
| (all prop fields) | same rules | Presentation identical to prop. |
| prompt | no | HUD / UI prompt text (default "Interact"). |
| on-use | no | Name of a Scheme procedure in the package script environment. |

Adds:

```text
Interactable { prompt, eventName = on-use name, maxDistance = 5 }
```

Interact casts a ray from the player Lens. Closest hit among usables with Model3D (mesh) or SpriteInstance (billboard / hit mask) within maxDistance becomes the current target. On Interact:

1. If on-use names a procedure, call it with one argument: the entity id string.
2. Otherwise open the inspect Interact UI.

Usables are the content-facing wrapper around the engine Interactable primitive. Panels and terminals stay as usables; sliding/swinging doors use [Movers](#movers-mover).

## Movers (mover)

A mover is a presented entity (sprite or geo) with a kinematic collision box and an A/B transform animation. Progress 0 is closed (spawn pose); 1 is open. Motion lerps a local **open-offset** and/or a single-axis rotation (**open-pitch** / **open-yaw** / **open-roll**) about a local **pivot**. The engine drives `LocalTransformation` and a Jolt kinematic box each frame.

```text
(mover
  (id "door-armory-l")
  (at 4.12 1.1 0.0)
  (yaw 0.0)
  (geo "default/cube")
  (pivot 0.0 0.0 0.0)
  (open-offset 0.0 0.0 -2.6)
  (duration 0.7)
  (collide-size 2.5 2.2 0.12)
  (collide-center 0.0 0.0 0.0)
  (block-mode "shove")
  (group "armory-doors")
  (prompt "Open")
  (on-use "on-use-mover-toggle"))
```

| Field | Required | Notes |
|-------|----------|-------|
| (all prop presentation fields) | same rules | Geo recommended for solid doors. |
| collide-size | yes | Full width/height/depth of the kinematic box. |
| open-offset and/or open-pitch\|yaw\|roll | at least one | Local-space slide and/or radians about the chosen axis. |
| pivot | no | Local hinge/reference (default origin). |
| collide-center | no | Box center in local space (default 0, half-height, 0). |
| duration | no | Seconds closed↔open (default 0.8). |
| block-mode | no | `"shove"` (default) or `"crush"`. |
| on-crush | no | Scheme proc `(on-crush mover-id victim-id)` when crush traps a character. |
| group | no | Shared id for double doors / multi-leaf. |
| prompt / on-use | no | If either is set, adds Interactable like usable. |

**block-mode shove** pushes CharacterVirtuals out of the moving box. **crush** still shoves, then calls `on-crush` when penetration remains (package owns damage/death). Geo movers tint `Model3D.color` from a downward lightmap probe.

Runtime control: `(mover-open id)`, `(mover-close id)`, `(mover-toggle id)`, group variants, `(mover-set-locked id bool)`, `(mover-state id)` / `(mover-set-state …)` for save restore — see [Scripting](scripting.md#thing-runtime).

## Lights

Light things are engine forms that always exist. They spawn flecs entities with light components and a transform. Point and spot things contribute to the radiosity bake; at runtime the dynamic overlay uses a separate DynamicLight path (for example the FP flashlight). Full detail: [Lights](lights.md).

Shared optional fields: (color r g b) (default 1 1 1), (intensity N) (default 1).

| Form | Required | Extra fields |
|------|----------|--------------|
| (point-light ...) | id, at | (range N) default 8 |
| (spot-light ...) | id, at | (yaw ...) or (angles ...), (range N), (cone radians) default 0.7 |
| (area-light ...) | id, at | (angles ...), (size width height) default 1 1 |
| (sun ...) | id | Direction from (angles ...) or (yaw ...); optional (at ...) for editor gizmo only |

## Motored bodies

A motored body is a runtime-spawned presented entity (sprite or geo) plus a MotoredBody motor: package-chosen velocity, gravity, sweep radius, and lifetime. The engine integrates motion each frame, sphere-casts against static brush hulls, and sweeps the same radius against actor character capsules. The nearer hit wins. The player capsule is a CharacterVirtual, not a rigid body, so world casts ignore the player; actor sweeps also skip the player (not an Actor).

Packages define recipes (rockets, arcing throws, bolts) with (motored-spawn ...) from [Scripting](scripting.md). Aim helpers (player-eye) / (player-look-dir) supply spawn origin and direction. On hit, the engine calls an optional on-impact Scheme handler as `(handler id x y z hit)` with the hit point and `hit` = actor id when an actor capsule won the sweep, or `#f` for a brush/world hit, then despawns. Empty handler = silent despawn. Direct bolts can damage `hit`; splash recipes can ignore it and query a radius. This is motor-driven flight, not full dynamic rigid-body simulation.

## Actors

An actor is a presented world body with a character motor and opaque CollisionTags. It is the engine primitive for walking entities packages may treat as enemies, NPCs, or neutrals. Distinct from:

- Prop: placed, may animate a sprite clip, does not move.
- Usable: static fixture the player uses.
- Mover: animated rigid leaf with kinematic collision; see [Movers](#movers-mover).
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
  (motor
    (radius 0.3)
    (height 1.1)
    (speed 3.5)
    (gravity 9.81)
    (step-height 0.4)
    (hull box)
    (move try-move))
  (tags "actor" "team:security"))
```

| Field | Required | Notes |
|-------|----------|-------|
| (all prop presentation fields) | same rules | Sprite or geo, pose, optional anim. |
| (motor ...) | no | Nested (radius), (height), (speed), (gravity), (step-height), (hull ...), (move ...). Numeric defaults match the player (0.3, 1.1, 6, 9.81, step 0.4). (hull capsule\|box) default capsule. (move slide\|try-move) default slide. |
| (tags ...) | no | Opaque strings copied to CollisionTags. Empty → ("actor"). |

hull box uses an axis-aligned box footprint (half-width/depth = radius). move try-move disables wall slide: horizontal motion either fully advances or fails, then stair step-up of step-height may still succeed. Use box + try-move for Doom-like cornering and package 8-dir chase; the player stays capsule + slide. Packages still drive intent with (actor-set-wish id wx wz).

Spawn adds Actor, CharacterMotor, and CollisionTags, then creates a Jolt CharacterVirtual. Query helpers (actors-with-tag, actors-in-radius, los?) are in [Scripting](scripting.md). Nav pathfollowing is not shipped yet; graphs.s7 remains authoring data. Health, factions, and combat stay in package Scheme.

## Scripting

Package Scheme and map things.s7 share one s7 heap. Load order, hooks, and runtime APIs are covered in [Scripting](scripting.md). Map files stay thin: poses, presentation paths, and handler name strings. Behavior lives under scripts/.

### on-use handlers

Define a procedure in scripts/things.s7 (or anything loaded into the same environment). The name in (on-use "...") must match.

```text
(define (on-use-test thing-id)
  (format #t "used ~a~%" thing-id))
```

The engine looks up the name with s7_name_to_value, checks it is a procedure, and calls it with the thing id string (the flecs entity name). There is no entity object API in Scheme yet, only that id, so handlers today are side-effect stubs (log, set game state you keep in Scheme, etc.). Missing or non-procedure names fall back to the inspect UI.

### What belongs where

- Map things.s7: instance data (where things are, which sprite/geo, which handler name, prompts, light params).
- Package scripts/: reusable behavior and constructors that wrap engine forms (usable, mover, lights, actor) so levels stay thin.
- Engine: spawn bindings, presentation, interact ray, player pawn, rigid movers, light thing forms. Not content catalogs.

### Extending without engine churn

Prefer new package procedures and map calls over new C++ thing kinds for each crate or NPC. When many packages need the same mechanic (trigger volume, motored body / rigid mover, light type), that is when a new engine primitive earns a binding; content still supplies sprites or .geo, prompts, and Scheme reactions.
