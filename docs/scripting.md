# Scripting

Game logic and presentation hooks are written in s7 Scheme (.s7). One Scheme heap lives for the whole run. C++ binds primitives, loads package scripts, and calls optional procedures by name.

Related: [Writing s7](s7.md), [Package structure](package-structure.md), [Persistence](persistence.md), [Player](player.md), [Things](things.md), [Maps](maps.md), [Audio](audio.md).

## Getting started

Language basics: [Writing s7](s7.md).

1. Add or override package scripts under scripts/ and data under data/ (virtual paths omit the extension and scripts/ / data/ prefix).
2. Declare input actions in data/actions.s7 (*package-actions*).
3. Implement hooks in scripts/player.s7, at least (prepare-first-person player-id) for first-person presentation.
4. Put reusable interact handlers in scripts/things.s7; place instances in maps/{name}/things.s7.
5. Optional: (tick dt), (draw-hud), (on-action-{id}) for per-frame and input-driven behavior.

Minimal player hook:

```text
(define (prepare-first-person player-id)
  (fp-clear-socket "weapon")
  (fp-attach-geo "weapon" "fp/stub" 0.22 -0.18 0.42 0.08 0.08 0.35))
```

## Load order

| Step | Source | When |
|------|--------|------|
| 1 | scripts/init.s7 | App start |
| 2 | data/actions.s7 | App start -> registers *package-actions* |
| 3 | data/items.s7 | App start -> *item-catalog* (if present) |
| 4 | data/view.s7 | App start -> *view-canvas* / *hud-canvas* |
| 5 | data/cli.s7 | App start -> *package-cli* parsed for extra argv flags |
| 6 | scripts/things.s7 | App start |
| 7 | Module API binds | Render / audio / input / thing-runtime / save / UI |
| 8 | scripts/player.s7 | After those binds |
| 9 | scripts/menus.s7 | After player (optional; File/Pause/modals / on-startup) |
| 10 | (on-startup) | Once after menus load |
| 11 | Map CSG | Map load (maps/{name}/static.csg) |
| 12 | maps/{name}/things.s7 | Map load (thing spawn forms) |
| 13 | maps/{name}/graphs.s7 | Map load if present (nav graphs) |

Later packages override earlier ones at the same virtual path. Map thing / CSG / graph bindings are active only while that file evaluates.

## Package data conventions

| Symbol / file | Role |
|---------------|------|
| *package-actions* | Extra gameplay actions (data/actions.s7); see [Player](player.md) |
| *item-catalog* | Item definitions (data/items.s7) when used by the package |
| *view-canvas* | View resolution pair, e.g. (320 200) |
| *hud-canvas* | HUD resolution pair |
| *package-cli* | Extra CLI flags (data/cli.s7) |
| *package-campaign* | Package-owned table for menus (optional; loaded by package Scheme, not the engine) |

```text
(define *view-canvas* '(320 200))
(define *hud-canvas* '(320 200))
```

### Package CLI

data/cli.s7 defines *package-cli* after packages are mounted. The engine only parses --base-game and --mod up front; remaining argv must match this schema or startup fails with combined usage text.

```text
(define *package-cli*
  '((flags
     ((name "map") (value "string") (help "Initial map folder under maps/"))
     ((name "mission") (value "string") (help "Mission id for a fresh run")))))
```

| Field | Meaning |
|-------|---------|
| name | Flag without -- |
| value | "string" (requires an argument) or "flag" (presence → "#t") |
| help | Usage line text |

Read values with (startup-arg "map") → string or #f, or (startup-args) → alist. Handle them in (on-startup) (for example (request-map-load …) when map is set).

### Package menus

The F1 menu bar (File / Config / Debug) and Pause window stay engine chrome. Packages opt in by defining draw hooks that use Scheme ImGui bindings. No hooks → no New/Load/Save entries. Structure and presentation (labels, fields, mission lists, modal layout) live entirely in package Scheme, often scripts/menus.s7 plus optional data such as data/campaign.s7 loaded via (package-load-data "campaign").

Save context layout is in [Persistence](persistence.md). Listing files for a Load UI uses (save-list dir suffix).

## Engine hooks

Call these by defining a procedure with the exact name. Missing procedures are skipped.

| Procedure | When |
|-----------|------|
| (prepare-first-person player-id) | After the FP scene exists on map / free-camera spawn; presentation only |
| (on-map-ready map-id reason) | After prepare-first-person on map spawn; reason is a string from request-map-load (default "fresh"). Packages apply carry/reset/restore policy here |
| (on-startup) | Once after player / menus load; read (startup-arg …) and start a map or leave the menu |
| (draw-file-menu) | Inside File menu (before Quit); use (ui-menu-item …) |
| (draw-pause-menu) | Inside Pause after Resume; typically (ui-button …) |
| (draw-modals) | Every UI frame; package-owned popup open flags |
| (tick dt) | Each update frame (delta seconds), if defined |
| (draw-hud) | When the HUD pass runs, if defined |
| (on-action-{id}) | When package action {id} is pressed |
| (on-sprite-hint source name) | When a .spanim hold with (hint "name") is entered; source is the entity name or FP socket (weapon / emission). See [Sprites](sprites.md#logic-hints). |
| (on-use-{name} thing-id) / named handlers | From map (on-use "..."); see [Things](things.md) |
| Trigger enter/exit handlers | From map (on-enter ...) / (on-exit ...) by handler name |

Handlers receive string ids (flecs entity names). There is no entity object API in Scheme yet. Keep game state in Scheme variables or other package-owned structures.

## Runtime APIs

### Save I/O and map flow

Player progress is written under the user config directory (not inside packages), scoped by the mounted stack engine → base game → mods. Path layout, mount.s7, and ownership are in [Persistence](persistence.md). The engine does not define campaigns, checkpoints, or save policies; packages own those and call these primitives when something in the game triggers a save or map change.

| Binding | Meaning |
|---------|---------|
| (save-root) | Absolute path string for the current mount's save context |
| (save-write rel form) | Write an S-expression under the context root; creates dirs and mount.s7; #t / #f |
| (save-read rel) | Read one S-expression from a relative path, or #f |
| (save-exists? rel) | #t if a regular file exists at the relative path |
| (save-delete rel) | Delete a regular file under the context root; #t / #f |
| (save-timestamp) | Local time stamp string YYYYMMDD_HHMMSS for named save files |
| (save-list dir suffix) | List of (rel-path . display) pairs under a relative directory |
| (package-load-data path) / (package-load-script path) | Load another package data/ or scripts/ virtual path |
| (startup-arg name) / (startup-args) | Package CLI values from data/cli.s7 |
| (request-map-load name) / (request-map-load name reason) | Queue a map change; reason is delivered to on-map-ready (default "fresh") |
| (current-map) | Current map folder id string, or #f |
| (player-pose) | (x y z yaw pitch) feet position and look, or #f |
| (player-set-pose x y z yaw pitch) | Teleport player and set look |

Relative save paths must stay under the context root (.. and absolute paths are rejected). Suggested envelope for package blobs: (save (version N) (package "id") ...). Body fields are package-defined.

### Package UI (ImGui)

Used from (draw-file-menu), (draw-pause-menu), and (draw-modals).

| Binding | Meaning |
|---------|---------|
| (playing?) / (main-menu?) / (pause-menu?) | Context predicates |
| (ui-menu-item label [enabled?]) | File/Pause menu row; #t if clicked |
| (ui-button label) / (ui-text str) / (ui-separator) / (ui-same-line) | Basic widgets |
| (ui-begin title) / (ui-end) | Window; always call ui-end |
| (ui-begin-child id [height]) / (ui-end-child) | Scroll region |
| (ui-set-next-window-size w h) / (ui-center-next-window) | Next window placement |
| (ui-input-text id initial) / (ui-input-text-set id str) | Text field; returns current string |
| (ui-begin-combo id preview) / (ui-combo-item label [selected?]) / (ui-end-combo) | Combo |
| (ui-selectable label [selected?]) | List row |
| (ui-indent) / (ui-unindent) | Indentation |

### First-person

Socket attach, view sprites, lights, rad tint, motion sensors. Full table: [Player Scheme API](player.md#scheme-api-engine-primitives).

### Input

| Binding | Meaning |
|---------|---------|
| (action-down? id) | #t while the bound action is held (gameplay context) |
| (action-pressed? id) | #t on the press edge this frame |

Works for package and core action ids.

### HUD

Drawn from (draw-hud) into the HUD canvas. Coordinates are in canvas space; (hud-anchor ...) sets the origin for following draws.

| Binding | Signature |
|---------|-----------|
| hud-anchor | (hud-anchor symbol) with top-left, top-right, bottom-left, bottom-right, center, or bottom-center |
| hud-font | (hud-font path) (virtual font path under fonts/) |
| hud-rect | (hud-rect x y w h r g b [a]) |
| hud-image | (hud-image tex-path x y [w h] [r g b a]) |
| hud-text | (hud-text str x y size [r g b a]) |

### Audio

| Binding | Role |
|---------|------|
| (play-sound path [volume] [loop?]) | Raw clip from sound/ |
| (play-audio path [volume]) | Def from audio/ |
| (play-music path [volume]) / (stop-music) | Music bus stream |
| (stop-sound handle) / (set-sound-volume handle vol) | Voice control |
| (set-bus-volume bus vol) | "sfx" or "music" |

Full formats, buses, filters, and frame sounds: [Audio](audio.md).

### Thing runtime

| Binding | Meaning |
|---------|---------|
| (thing-despawn id) | Queue despawn of a spawned thing by entity name string (also destroys an actor character capsule) |
| (motored-spawn id x y z vx vy vz kind path [radius gravity lifetime on-impact]) | Spawn a motored body at runtime (kind is "sprite" or "geo"). Defaults: radius 0.12, gravity 0, lifetime 8, on-impact "". Integrates velocity against static brush hulls; positive gravity pulls down; empty on-impact silently despawns on hit. See [Things](things.md#motored-bodies). |
| (actor-spawn id x y z yaw kind path [radius height speed gravity tags-list]) | Runtime actor (kind "sprite" or "geo"). Defaults match player motor; empty tags → ("actor"). |
| (actor-pos id) | Feet (x y z) or #f |
| (actor-yaw id) | Yaw radians or #f |
| (actor-set-wish id wx wz) | Write horizontal wish on the actor motor |
| (actor-grounded? id) | #t when the character is supported |
| (actor-play-anim id clip [loop]) | Play a world sprite clip on the actor |
| (actor-tags id) | Tag string list or #f |
| (actors-with-tag tag) | List of actor id strings with that tag |
| (actors-in-radius x y z r [tag]) | Actor ids whose feet are within r (optional tag filter) |
| (los? x0 y0 z0 x1 y1 z1) | #t if the segment is clear of static brush hulls |
| (actor-los? from-id to-id) | LOS between approximate eye heights of two actors |

Player aim helpers for spawn recipes: (player-eye) and (player-look-dir) — see [Player](player.md#scheme-api-engine-primitives).

### Map authoring DSLs

Bound only while the matching map file loads, not for general gameplay scripts:

- Things: prop, usable, actor, trigger, lights, prefab, ... -> [Things](things.md), [Maps](maps.md)
- CSG brushes -> [Maps](maps.md)
- Nav graphs -> maps/{name}/graphs.s7 (graph, node, edge, ...)

## What belongs where

maps/{name}/things.s7 holds instance data: poses, sprite or geo paths, handler names, light params -- composition for this level, not shared logic. Reusable behavior, constructors, first-person presentation, HUD, and New/Load/Save menus live under scripts/. Package catalogs such as actions, items, CLI flags, and canvas sizes live under data/. The engine supplies save I/O under the config saves tree, map/pose hooks, F1/Pause chrome, and ImGui bindings (see [Persistence](persistence.md)); it does not own per-game menu structure or content rules.

Prefer new package procedures and thin map calls over new C++ thing kinds for each content type.
