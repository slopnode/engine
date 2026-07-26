# Things

Placed content in a level (static props, usables, pickups, actors, lights) is authored in maps/{name}/things.s7. A thing is the authored record (id, pose, kind, presentation or light params). At load, the engine spawns a flecs entity from each thing. The map file is composition: ids, poses, and which presentation or handler to use. Behavior and shared helpers live in package Scheme under scripts/. The player is separate; see [Player](player.md).

World solids stay in CSG / BSP ([Maps](maps.md)). Thing presentation uses sprites ([Sprites](sprites.md)) or .geo ([Geometry](geometry.md)).

## Kinds

A static prop ((prop ...)) is visual only: a sprite or mesh at a pose, with no interact and no AI. A usable ((usable ...)) uses the same presentation, then adds an interact prompt and optional Scheme on-use handler. A pickup ((pickup ...)) is presented item content that reacts on touch (`on-enter`) and/or use (`on-use`); packages own collect once / inventory / ammo. A mover ((mover ...)) is a presented rigid leaf with kinematic collision and A/B open/close motion (doors, hatches). An actor ((actor ...)) uses the same presentation plus a character capsule motor and opaque tags; packages own brains, health, and factions. Light things cover bake and editor work: (point-light ...) and (spot-light ...) feed radiosity and gizmos; (sun ...) is the bake directional sun (angles/yaw + color/intensity; optional at for the gizmo); (ambient-light ...) sets soft ambient fill (omit for black); (area-light ...) is authoring / gizmo only. See [Lights](lights.md).

(player-start ...) is spawn pose only -- it does not create a prop entity; the engine builds Player from that pose after map load.

(marker ...) is a named pose-only point (door centers, script hints). It spawns a flecs entity with a transform and no presentation, physics, or handlers. Scripts read it with (thing-pos id) / (thing-yaw id).

These forms are engine Scheme bindings, always available regardless of which package is mounted. Package scripts may wrap them; they do not define the primitives.

Debug entity list labels match this split: prop, usable, mover, actor, point-light, spot-light, area-light, sun, player (plus map for MapStatic). Presented pickups may still show as prop/usable in the debug list depending on components.

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

(pickup
  (id "clip-1")
  (at -2.0 0.0 6.0)
  (yaw 0.0)
  (sprite "items/clip")
  (on-enter "on-enter-ammo" (ammo "clip"))
  (trigger-size 1.0 1.5 1.0))

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
| on-use | no | Handler id (and optional typed arg clauses). See [Map handlers](scripting.md#map-handlers). |

Adds:

```text
Interactable { prompt, onUse binding, maxDistance = 5 }
```

Interact casts a ray from the player Lens. Closest hit among usables with Model3D (mesh) or SpriteInstance (billboard / hit mask) within maxDistance becomes the current target. On Interact:

1. If on-use names a procedure, call it (catalog handlers get `(handler thing-id args)`; legacy get `(handler thing-id)`).
2. Otherwise open the inspect Interact UI.

Usables are the content-facing wrapper around the engine Interactable primitive. Panels and terminals stay as usables. Ordinary doors use [brush doors](#doors-brush-door); elevators / platforms / custom A/B leaves use [Movers](#movers-mover).

## Pickups (pickup)

A pickup is presented item content (sprite or geo) that the player collects by walking into a volume and/or pressing Interact. Prefer pickup over a bare prop with `on-enter`, or a usable that only exists to be taken once. Collect-once, inventory, ammo, and keys stay in package Scheme.

| Field | Required | Notes |
|-------|----------|-------|
| (all prop fields) | same rules | Exactly one of sprite / geo. |
| on-enter and/or on-use | at least one | Touch volume and/or use interact. Both may be set. |
| trigger-size | no | AABB for touch (`on-enter`); default 1 1 1 when size is authored. |
| on-use | no | Handler id (+ optional typed args). See [Map handlers](scripting.md#map-handlers). |

Spawn:

- Always applies presentation.
- Non-empty `on-enter` (or exit) adds `TriggerVolume` like other presented things.
- Non-empty `on-use` adds `Interactable` (no authored prompt).

```text
(pickup
  (id "key-blue")
  (at 3.0 0.0 1.0)
  (sprite "items/key-blue")
  (on-enter "on-enter-key" (key "blue"))
  (trigger-size 1 1.5 1))

(pickup
  (id "medkit-use")
  (at 0.0 0.0 2.0)
  (sprite "items/medkit")
  (on-use "on-use-medkit"))
```

Pure volume enter/exit without a mesh stays `(trigger …)`. Fixed world interactables that are not collectibles stay `(usable …)`.

## Doors (brush door)

Set brush role to **`door`** (Edit → Set as Door, role combo, or `H`) for raise / slide / swing doors. Nested `(door …)` holds motion params. No mover thing is required. The engine omits the leaf from static FAC / collision, spawns a `RigidMover` whose entity id is the brush id, and toggles on use. Optional `(can-use …)` is a package predicate that must return true before the toggle (keys, etc.).

```text
;; static.csg — door leaf in an already-sealed doorway opening
(brush-box
  (id "door-1")
  (role "door")
  (mins -1 0 -0.06)
  (maxs 1 2.2 0.06)
  (material "surfaces/door")
  (door
    (motion "raise")
    (duration 0.6)
    (auto-close 4)
    (prompt "Open")))
```

| Field | Notes |
|-------|-------|
| motion | `"raise"`, `"slide"`, or `"swing"` (default raise). |
| duration | Seconds closed↔open (default 0.6). |
| auto-close | Seconds fully open before closing (default 0). |
| travel | Distance for raise/slide; `0` / omit infers from AABB. |
| angle | Swing yaw radians (default π/2). |
| hinge | Thing id whose world `at` is the hinge; omit = brush center. |
| group | Shared id for double doors (same as mover group). |
| prompt | Interact prompt (default `"Open"`). |
| can-use | Optional map-handler predicate `(handler door-id args) -> bool`. |

A door-role brush must not also be claimed by a `(mover (brush …))`. Runtime control uses the same `(mover-open id)` / `(mover-toggle id)` APIs with the brush id. Remote switches keep face `(on-use …)` calling those APIs.

## Movers (mover)

A mover is a presented entity with a kinematic collision box and an A/B transform animation. Progress 0 is closed (spawn pose); 1 is open. Motion lerps a local **open-offset** and/or a single-axis rotation (**open-pitch** / **open-yaw** / **open-roll**) about a local **pivot**. The engine drives `LocalTransformation` and a Jolt kinematic box each frame.

Presentation is exactly one of `(brush …)`, `(geo …)`, or `(sprite …)`. Use movers for platforms, elevators, and other non-door A/B leaves. For ordinary doors prefer [brush doors](#doors-brush-door).

Geo form:

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
  (push "full")
  (carry #t)
  (group "armory-doors")
  (prompt "Open")
  (on-use "on-use-mover-toggle"))
```

| Field | Required | Notes |
|-------|----------|-------|
| brush, geo, or sprite | exactly one | Brush must be a **detail** brush id in the same map. |
| at | yes unless brush | Defaults to brush AABB center when `(brush …)` is set. |
| collide-size | yes unless brush | Full width/height/depth of the kinematic box; defaults to brush AABB extents. |
| open-offset and/or open-pitch\|yaw\|roll | at least one | Local-space slide and/or radians about the chosen axis. |
| pivot | no | Local hinge/reference (default origin). |
| collide-center | no | Box center in local space (default 0, half-height, 0 for geo/sprite; origin for brush). |
| duration | no | Seconds closed↔open (default 0.8). |
| block-mode | no | `"shove"` (default) or `"crush"`. |
| push | no | `"full"` (default), `"horizontal"`, or `"off"`. Depenetration shove axes. |
| carry | no | `#t` (default) or `#f`. When `#t`, characters inherit ground velocity while standing on the mover. Named `carry` (not `slide`) so it does not clash with motor `(move slide)`. |
| on-crush | no | Scheme proc `(on-crush mover-id victim-id)` when crush traps a character. |
| group | no | Shared id for double doors / multi-leaf. |
| prompt / on-use | no | If either is set, adds Interactable like usable. |

**block-mode shove** pushes CharacterVirtuals out of the moving box. **crush** still shoves, then calls `on-crush` when penetration remains (package owns damage/death). **push** controls that shove: `full` uses the least-penetration axis (including Y), `horizontal` nudges on XZ only, `off` skips position correction (crush can still fire). **carry** controls riding: rising doors usually want `(push "horizontal") (carry #f)`; elevators want `(carry #t)`. Geo/brush movers tint `Model3D.color` from a downward lightmap probe.

Runtime control: `(mover-open id)`, `(mover-close id)`, `(mover-toggle id)`, group variants, `(mover-set-locked id bool)`, `(mover-state id)` / `(mover-set-state …)` for save restore — see [Scripting](scripting.md#thing-runtime).

## Lights

Light things are engine forms that always exist. They spawn flecs entities with light components and a transform. Point and spot things contribute to the radiosity bake; at runtime the dynamic overlay uses a separate DynamicLight path (for example the FP flashlight). Full detail: [Lights](lights.md).

Shared optional fields: (color r g b) (default 1 1 1), (intensity N) (default 1).

| Form | Required | Extra fields |
|------|----------|--------------|
| (point-light ...) | id, at | (range N) default 8 |
| (spot-light ...) | id, at | (yaw ...) or (angles ...), (range N), (cone radians) default 0.7 |
| (area-light ...) | id, at | (angles ...), (size width height) default 1 1 |
| (sun ...) | id | Direction from (angles ...) or (yaw ...); optional (at ...) for gizmo. Bake directional sun (needs sky faces). |
| (ambient-light ...) | id | Optional at (gizmo), color, intensity. Bake/runtime ambient; omit for black. |

## Markers (marker)

A marker is a named world point with no mesh, interact, light, or collision. Use it for hinge/center references, camera aims, or any package hint that only needs a pose looked up by id.

```text
(marker
  (id "door-center")
  (at 0.0 1.0 0.0)
  (yaw 0.0))
```

| Field | Required | Notes |
|-------|----------|-------|
| id | yes | Flecs entity name; must be unique in the file. |
| at | yes | World position (x y z). |
| yaw | no | Radians around Y (default 0). |

Spawn adds `WorldSpace` and `LocalTransformation` only. Runtime pose: `(thing-pos id)` / `(thing-yaw id)` in [Scripting](scripting.md#thing-runtime).

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
| (health . N) / (idle-anim . "clip") / (behavior . "name") | no | Catalog fields for packages (`thing-def-health` / `thing-def-idle-anim` / `thing-def-behavior`). |
| (melee ...) | no | Optional package melee channel. Nested clauses below. Missing → melee getters return `#f`. |
| (sight ...) | no | Optional AI sight sensor. Nested clauses below. Missing → no `ActorSight` until `(actor-sight-set! …)`. |

Melee clauses (thing defs under `*package-things*`):

| Clause | Notes |
|--------|-------|
| (damage N) | Hit damage amount (default 0). |
| (range N) | Max horizontal distance to apply (default 1.2). |
| (cooldown N) | Seconds between swings (default 1.0). |
| (anim "clip") | Optional attack clip name for `(actor-play-anim …)`. |

Sight clauses (thing defs under `*package-things*` or map `(actor …)` / `(thing …)`):

| Clause | Notes |
|--------|-------|
| (range N) | Max eye-to-eye distance (default 32). |
| (fov N) | Horizontal cone width in degrees (default 180; 360 = omnidirectional). |
| (eye-lift N) | Fraction of (height+radius) for non-player eyes (default 0.75). |
| (see-tags …) | Target must have at least one; empty = any tagged character. |
| (ignore-tags …) | Target with any listed tag is ignored. |
| (filter "proc") | Optional Scheme `(proc observer-id target-id) -> bool`. |
| (enabled #t/#f) | Default #t when the block is present. |

hull box uses an axis-aligned box footprint (half-width/depth = radius). move try-move disables wall slide: horizontal motion either fully advances or fails, then stair step-up of step-height may still succeed. Use box + try-move for Doom-like cornering and package 8-dir chase; the player stays capsule + slide. Packages still drive intent with (actor-set-wish id wx wz).

Spawn adds Actor, CharacterMotor, and CollisionTags, then creates a Jolt CharacterVirtual. When `(sight …)` is present, spawn also sets `ActorSight`. The engine runs a budgeted sight scan (range → FOV → PVS → LOS) and fires `(on-sight observer target)` on newly acquired visibility; packages own reaction (alert/chase). Targets are any `CharacterMotor` + `CollisionTags` entity (including `Player`). Query helpers and sight APIs are in [Scripting](scripting.md). Nav pathfollowing is not shipped yet; graphs.s7 remains authoring data. Health, factions, and combat stay in package Scheme.

## Scripting

Package Scheme and map things.s7 share one s7 heap. Load order, hooks, and runtime APIs are covered in [Scripting](scripting.md). Map files stay thin: poses, presentation paths, and handler bindings. Behavior lives under scripts/.

### on-use handlers

Define a procedure in scripts/things.s7 (or anything loaded into the same environment). Prefer registering it in `*package-map-handlers*` so maps can pass typed args from the editor. See [Map handlers](scripting.md#map-handlers).

```text
(define (on-use-test thing-id)
  (format #t "used ~a~%" thing-id))

(define (toggle-light thing-id other-id args)
  ...)
```

Legacy (id not in the catalog): s7_name_to_value + `(handler thing-id)`. Catalog handlers receive an args alist last. Missing or non-procedure names fall back to the inspect UI.

### What belongs where

- Map things.s7: instance data (poses, presentation paths, handler binding + args, prompts, light params).
- Package scripts/: reusable behavior and constructors that wrap engine forms (usable, mover, lights, actor) so levels stay thin.
- Engine: spawn bindings, presentation, interact ray, player pawn, rigid movers, light thing forms. Not content catalogs.

### Extending without engine churn

Prefer new package procedures and map calls over new C++ thing kinds for each crate or NPC. When many packages need the same mechanic (trigger volume, motored body / rigid mover, light type), that is when a new engine primitive earns a binding; content still supplies sprites or .geo, prompts, and Scheme reactions.
