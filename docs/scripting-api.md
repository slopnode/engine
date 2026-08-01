@page scriptingapi Scripting API

Game logic and presentation hooks are written in s7 Scheme (.s7). One Scheme heap lives for the whole run. C++ binds primitives, loads package scripts, and calls optional procedures by name.

When a script is executed, it runs within a specific scope. The engine uses `ScriptScopeGuard` and `ScriptRoleGuard` to manage these contexts. The system prevents scripts from performing unauthorized operations based on their execution context. For example, a script running in the HUD scope cannot perform world mutations or save operations.

# Scopes, roles, and caps

This engine uses a combination of scopes & roles to what operations a script can actually perform.

1. Scope defines where the script is run, game init, map, hud, etc.
2. Roles define who executes the script, the engine, base-game, or mod

## Scopes

Scope|Description|Caps
-|-|-
boot|Initial package loading and setup|`package-load`
startup|After packages load and game starts|`startup-query`,`package-load`,`map-control`,`save-io`,`read-world`,`ui-draw`
world|Main gameplay loop|`world-mutate`,`fp-present`,`audio`,`input-query`,`read-world`,`map-control`,`save-io`
hud|HUD rendering|`hud-draw`,`input-query`,`real-world`
ui|UI (non-hud) rendering|`ui-draw`,`save-io`,`map-control`,`package-load`,`read-world`,`startup-query`
mapauthor|Loading map files|none

## Roles

Role|Description
-|-
Engine|Internal engine components
Base|Has full access to most capabilities
Mod|Limited to read-only and presentation capabilities

## Capabilities

Capability|Description
-|-
`hud-draw`| Drawing operations for HUD elements
`ui-draw`| Drawing operations for UI elements
`save-io`| Reading/writing save files
`map-control`| Loading/saving maps, controlling map flow
`world-mutate`| Modifying game world state
`fp-present`| First-person presentation (weapon attachments, etc.)
`audio`| Playing audio and music
`input-query`| Querying input state
`package-load`| Loading package data
`startup-query`| Querying startup arguments
`read-world`| Reading world state information

# Functions

## I/O

```scheme
(save-root)                                         ; Absolute path string for current mount's save context
(save-write rel form)                               ; Write an S-expression under the context root
(save-read rel)                                     ; Read one S-expression from a relative path
(save-exists? rel)                                  ; Check if a regular file exists at relative path
(save-delete rel)                                   ; Delete a regular file under the context root
(save-timestamp)                                    ; Local time stamp string for named save files
(save-list dir suffix)                              ; List of (rel-path . display) pairs under directory
(package-load-data package-id path)                 ; Load data/{path}.s7 from specific package
(package-load-script package-id path)               ; Load scripts/{path}.s7 from specific package
(current-package-id)                                ; Id of package file currently evaluating
(package-mounted? package-id)                       ; Check if package is mounted
(hook-add hook-symbol proc)                         ; Append a contrib for an engine-owned hook
(startup-arg name)                                  ; Get package CLI value
(startup-args)                                      ; Get all package CLI values
(request-map-load name)                             ; Queue a map change
(request-map-load name reason)                      ; Queue a map change with reason
(current-map)                                       ; Current map folder id string
```

## Player

```scheme
(player-pose)                                       ; Player position and look (x y z yaw pitch)
(player-set-pose x y z yaw pitch)                   ; Teleport player and set look
(player-eye-height)                                 ; Current eye height above feet
(player-set-eye-height h)                           ; Set CharacterMotor eye height
(player-set-control move? look?)                    ; Enable/disable move wish and mouse look
```

## UI (ImGui)

> NOTE: Scheme bindings directly to Imgui are temporary. This will change with a proper UI wrapper instead of direct Imgui calls.

```scheme
(playing?)                                          ; Check if in gameplay context
(main-menu?)                                        ; Check if in main menu context  
(pause-menu?)                                       ; Check if in pause menu context
(ui-menu-item label [enabled?])                     ; File/Pause menu row
(ui-button label)                                   ; Basic button widget
(ui-text str)                                       ; Text display
(ui-separator)                                      ; Separator line
(ui-same-line)                                      ; Same line positioning
(ui-begin title)                                    ; Begin window
(ui-end)                                            ; End window
(ui-begin-child id [height])                        ; Begin scroll region
(ui-end-child)                                      ; End scroll region
(ui-set-next-window-size w h)                       ; Set next window size
(ui-center-next-window)                             ; Center next window
(ui-input-text id initial)                          ; Text input field
(ui-input-text-set id str)                          ; Set text input value
(ui-begin-combo id preview)                         ; Begin combo box
(ui-combo-item label [selected?])                   ; Combo box item
(ui-end-combo)                                      ; End combo box
(ui-selectable label [selected?])                   ; Selectable list row
(ui-indent)                                         ; Increase indentation
(ui-unindent)                                       ; Decrease indentation
```

## First-person

```scheme
(fp-clear-socket socket-name)                       ; Clear a first-person socket
(fp-attach-geo socket-name geo-path x y z sx sy sz) ; Attach geometry to socket
(fp-attach-sprite socket-name sprite-path)          ; Attach sprite to socket
```

## HUD

```scheme
(hud-anchor symbol)                                 ; Set drawing origin (top-left, top-right, etc.)
(hud-font path)                                     ; Set font for HUD drawing
(hud-rect x y w h r g b [a])                        ; Draw rectangle
(hud-image tex-path x y [w h] [r g b a])            ; Draw image
(hud-text str x y size [r g b a])                   ; Draw text
```

## Audio

```scheme
(play-sound path [volume] [loop?])                  ; Play raw sound clip
(play-audio path [volume])                          ; Play audio definition
(play-music path [volume])                          ; Play music
(stop-music)                                        ; Stop music
(stop-sound handle)                                 ; Stop specific sound
(set-sound-volume handle vol)                       ; Set sound volume
(set-bus-volume bus vol)                            ; Set bus volume ("sfx" or "music")
```

## Things

```scheme
(thing-despawn id)                                  ; Queue despawn of thing by entity name
(thing-type id)                                     ; Get catalog type id for thing
(thing-def-health type)                             ; Get health from catalog
(thing-def-idle-anim type)                          ; Get idle animation from catalog
(thing-def-behavior type)                           ; Get behavior from catalog
(thing-def-melee-damage type)                       ; Get melee damage from catalog
(thing-def-melee-range type)                        ; Get melee range from catalog
(thing-def-melee-cooldown type)                     ; Get melee cooldown from catalog
(thing-def-melee-anim type)                         ; Get melee animation from catalog
(thing-def-ranged-range type)                       ; Get ranged max range from catalog
(thing-def-ranged-min-range type)                   ; Get ranged min range from catalog
(thing-def-ranged-cooldown type)                    ; Get ranged cooldown from catalog
(thing-def-ranged-anim type)                        ; Get ranged animation from catalog
(thing-pos id)                                      ; Get thing position (x y z)
(thing-yaw id)                                      ; Get thing yaw radians
(mover-open id)                                     ; Open mover
(mover-close id)                                    ; Close mover  
(mover-toggle id)                                   ; Toggle mover
(mover-open-group g)                                ; Open all movers in group
(mover-close-group g)                               ; Close all movers in group
(mover-toggle-group g)                              ; Toggle all movers in group
(mover-set-locked id bool)                          ; Lock/unlock mover
(mover-locked? id)                                  ; Check if mover is locked
(mover-progress id)                                 ; Get mover progress (0..1)
(mover-state id)                                    ; Get mover state as alist
(mover-set-state id open? progress [locked?])       ; Set mover state
(motored-spawn id x y z vx vy vz kind path [radius gravity lifetime on-impact ignore]) ; Spawn motored body
(sprite-spawn id x y z path [clip] [lifetime])      ; Spawn world billboard
(particle-spawn id x y z path [yaw])                ; Spawn particle system
(particle-play id)                                  ; Restart particle system
(particle-stop id)                                  ; Stop particle system
(particle-despawn id)                               ; Destroy particle system
(actor-spawn id x y z yaw kind path [radius height speed gravity tags-list]) ; Spawn actor
(actor-pos id)                                      ; Get actor position (x y z)
(actor-yaw id)                                      ; Get actor yaw radians
(actor-set-wish id wx wz)                           ; Set actor movement wish
(actor-grounded? id)                                ; Check if actor is grounded
(actor-play-anim id clip [loop])                    ; Play animation on actor
(actor-tags id)                                     ; Get actor tags
(actors-with-tag tag)                               ; Get actors with specific tag
(actors-in-radius x y z r [tag])                    ; Get actors in radius
(los? x0 y0 z0 x1 y1 z1)                            ; Check line of sight
(actor-los? from-id to-id)                          ; Check actor line of sight
(actor-sight-set! id alist)                         ; Configure actor sight system
(actor-sight-get id)                                ; Get current sight configuration
(actor-can-see? from to)                            ; Check if actors can see each other
(sight-budget)                                      ; Get max LOS traces per frame
(sight-budget-set! n)                               ; Set max LOS traces per frame
```

## Hooks

The base package defines owner procedures with exact names. Mods extend them using `(hook-add 'name proc)`:

```scheme
(prepare-first-person player-id)                    ; After FP scene exists on map spawn
(on-map-ready map-id reason)                        ; After prepare-first-person on map spawn  
(on-startup)                                        ; Once after player/menus/contribs load
(draw-file-menu)                                    ; Inside File menu (before Quit)
(draw-pause-menu)                                   ; Inside Pause menu after Resume
(draw-modals)                                       ; Every UI frame for package-owned popups
(tick dt)                                           ; Each update frame (delta seconds)
(draw-hud)                                          ; When HUD pass runs
(draw-title)                                        ; Each menu frame for title chrome
(on-sprite-hint source name)                        ; When .spanim hold with (hint "name") is entered
(on-sight observer-id target-id)                    ; When enabled ActorSight acquires LOS on target
(sight-filter observer-id target-id)                ; Optional veto during sight scans
(on-action-{id})                                    ; When package action {id} is pressed
(on-use-{name} thing-id)                            ; From map (on-use "...")
(trigger enter/exit handlers)                       ; From map thing `(on-enter …)` / `(on-exit …)`
(use handlers)                                      ; From map usable/pickup/mover `(on-use …)`
(touch handlers)                                    ; From CSG face `(on-touch …)`
```
