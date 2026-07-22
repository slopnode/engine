# Scripting

Game logic and presentation hooks are written in s7 Scheme (`.s7`). One Scheme heap lives for the whole run. C++ binds primitives, loads package scripts, and calls optional procedures by name.

Related: [Package structure](package-structure.md), [Player](player.md), [Things](things.md), [Maps](maps.md), [Audio](audio.md).

## Getting started

1. Add or override package scripts under `scripts/` and data under `data/` (virtual paths omit the extension and `scripts/` / `data/` prefix).
2. Declare input actions in `data/actions.s7` (`*package-actions*`).
3. Implement hooks in `scripts/player.s7`, at least `(prepare-first-person player-id)` for first-person presentation.
4. Put reusable interact handlers in `scripts/things.s7`; place instances in `maps/<name>/things.s7`.
5. Optional: `(tick dt)`, `(draw-hud)`, `(on-action-<id>)` for per-frame and input-driven behavior.

Minimal player hook:

```text
(define (prepare-first-person player-id)
  (fp-clear-socket "weapon")
  (fp-attach-geo "weapon" "fp/stub" 0.22 -0.18 0.42 0.08 0.08 0.35))
```

## Load order

| Step | Source | When |
|------|--------|------|
| 1 | `scripts/init.s7` | App start |
| 2 | `data/actions.s7` | App start -> registers `*package-actions*` |
| 3 | `data/items.s7` | App start -> `*item-catalog*` (if present) |
| 4 | `data/view.s7` | App start -> `*view-canvas*` / `*hud-canvas*` |
| 5 | `scripts/things.s7` | App start |
| 6 | Module API binds | Render / audio / input / thing-runtime |
| 7 | `scripts/player.s7` | After FP / HUD / input / thing-runtime binds |
| 8 | Map CSG | Map load (`maps/<name>/static.csg`) |
| 9 | `maps/<name>/things.s7` | Map load (thing spawn forms) |
| 10 | `maps/<name>/graphs.s7` | Map load if present (nav graphs) |

Later packages override earlier ones at the same virtual path. Map thing / CSG / graph bindings are active only while that file evaluates.

## Package data conventions

| Symbol / file | Role |
|---------------|------|
| `*package-actions*` | Extra gameplay actions (`data/actions.s7`); see [Player](player.md) |
| `*item-catalog*` | Item definitions (`data/items.s7`) when used by the package |
| `*view-canvas*` | View resolution pair, e.g. `(320 200)` |
| `*hud-canvas*` | HUD resolution pair |

```text
(define *view-canvas* '(320 200))
(define *hud-canvas* '(320 200))
```

## Engine hooks

Call these by defining a procedure with the exact name. Missing procedures are skipped.

| Procedure | When |
|-----------|------|
| `(prepare-first-person player-id)` | After the FP scene exists on map / free-camera spawn |
| `(tick dt)` | Each update frame (delta seconds), if defined |
| `(draw-hud)` | When the HUD pass runs, if defined |
| `(on-action-<id>)` | When package action `<id>` is pressed |
| `(on-use-<name> thing-id)` / named handlers | From map `(on-use "...")`; see [Things](things.md) |
| Trigger enter/exit handlers | From map `(on-enter ...)` / `(on-exit ...)` by handler name |

Handlers receive string ids (flecs entity names). There is no entity object API in Scheme yet. Keep game state in Scheme variables or other package-owned structures.

## Runtime APIs

### First-person

Socket attach, view sprites, lights, rad tint, motion sensors. Full table: [Player Scheme API](player.md#scheme-api-engine-primitives).

### Input

| Binding | Meaning |
|---------|---------|
| `(action-down? id)` | `#t` while the bound action is held (gameplay context) |
| `(action-pressed? id)` | `#t` on the press edge this frame |

Works for package and core action ids.

### HUD

Drawn from `(draw-hud)` into the HUD canvas. Coordinates are in canvas space; `(hud-anchor ...)` sets the origin for following draws.

| Binding | Signature |
|---------|-----------|
| `hud-anchor` | `(hud-anchor symbol)` with `top-left`, `top-right`, `bottom-left`, `bottom-right`, `center`, or `bottom-center` |
| `hud-font` | `(hud-font path)` (virtual font path under `fonts/`) |
| `hud-rect` | `(hud-rect x y w h r g b [a])` |
| `hud-image` | `(hud-image tex-path x y [w h] [r g b a])` |
| `hud-text` | `(hud-text str x y size [r g b a])` |

### Audio

| Binding | Role |
|---------|------|
| `(play-sound path [volume] [loop?])` | Raw clip from `sound/` |
| `(play-audio path [volume])` | Def from `audio/` |
| `(play-music path [volume])` / `(stop-music)` | Music bus stream |
| `(stop-sound handle)` / `(set-sound-volume handle vol)` | Voice control |
| `(set-bus-volume bus vol)` | `"sfx"` or `"music"` |

Full formats, buses, filters, and frame sounds: [Audio](audio.md).

### Thing runtime

| Binding | Meaning |
|---------|---------|
| `(thing-despawn id)` | Queue despawn of a spawned thing by entity name string |

### Map authoring DSLs

Bound only while the matching map file loads, not for general gameplay scripts:

- Things: `prop`, `usable`, `trigger`, lights, `prefab`, ... -> [Things](things.md), [Maps](maps.md)
- CSG brushes -> [Maps](maps.md)
- Nav graphs -> `maps/<name>/graphs.s7` (`graph`, `node`, `edge`, ...)

## What belongs where

| Place | Content |
|-------|---------|
| `maps/<name>/things.s7` | Instance data: poses, sprite/geo paths, handler names, light params |
| `scripts/` | Reusable behavior, constructors, FP presentation, HUD |
| `data/` | Actions, items, canvas sizes |
| Engine | Spawn bindings, presentation primitives, interact, player pawn |

Prefer new package procedures and thin map calls over new C++ thing kinds for each content type.
