@page scriptingapi Scripting API

Game logic and presentation hooks are written in s7 Scheme (.s7). One Scheme heap lives for the whole run. C++ binds primitives, loads package scripts, and calls optional procedures by name.

When a script is executed, it runs within a specific scope. The engine uses `ScriptScopeGuard` and `ScriptRoleGuard` to manage these contexts. The system prevents scripts from performing unauthorized operations based on their execution context. For example, a script running in the HUD scope cannot perform world mutations or save operations.

# Scopes, roles, and caps {#scopes-roles-and-caps}

This engine uses a combination of scopes & roles to what operations a script can actually perform.

1. Scope defines where the script is run, game init, map, hud, etc.
2. Roles define who executes the script, the engine, base-game, or mod

## Scopes {#scopes}

Scope|Description|Caps
-|-|-
boot|Initial package loading and setup|`package-load`
startup|After packages load and game starts|`startup-query`,`package-load`,`map-control`,`save-io`,`read-world`,`ui-draw`
world|Main gameplay loop|`world-mutate`,`fp-present`,`audio`,`input-query`,`read-world`,`map-control`,`save-io`
hud|HUD rendering|`hud-draw`,`input-query`,`read-world`
ui|UI (non-hud) rendering|`ui-draw`,`save-io`,`map-control`,`package-load`,`read-world`,`startup-query`
mapauthor|Loading map files|none

## Roles {#roles}

Role|Description
-|-
Engine|Internal engine components
Base|Has full access to most capabilities
Mod|Limited to read-only and presentation capabilities

## Capabilities {#capabilities}

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

# Functions {#functions}

Each entry shows the call signature, the type expected for every argument, and what the call returns, followed by a one-sentence description. Optional arguments are shown in `[brackets]`; `= value` marks a default used when the argument is omitted.

## I/O {#i-o}

### save-root {#save-root}
<pre><code class="language-scheme">(save-root) → string?
</code></pre>
Returns the absolute filesystem path for the current mount's save context.

### save-write {#save-write}
<pre><code class="language-scheme">(save-write rel form) → boolean?
  rel  : string?
  form : any?
</code></pre>
Writes a Scheme value as an S-expression to a file under the save context root.

### save-read {#save-read}
<pre><code class="language-scheme">(save-read rel) → any?
  rel : string?
</code></pre>
Reads one S-expression from a file relative to the save context root.

### save-exists? {#save-exists}
<pre><code class="language-scheme">(save-exists? rel) → boolean?
  rel : string?
</code></pre>
Checks whether a regular file exists at the given relative save path.

### save-delete {#save-delete}
<pre><code class="language-scheme">(save-delete rel) → boolean?
  rel : string?
</code></pre>
Deletes a regular file under the save context root.

### save-timestamp {#save-timestamp}
<pre><code class="language-scheme">(save-timestamp) → string?
</code></pre>
Returns a local timestamp string, suitable for naming save files.

### save-list {#save-list}
<pre><code class="language-scheme">(save-list dir suffix) → (listof pair?)
  dir    : string?
  suffix : string?
</code></pre>
Lists `(rel-path . display)` pairs for files with the given suffix under a save directory.

### package-load-data {#package-load-data}
<pre><code class="language-scheme">(package-load-data package-id path) → boolean?
  package-id : string?
  path       : string?
</code></pre>
Loads `data/{path}.s7` from a specific package.

### package-load-script {#package-load-script}
<pre><code class="language-scheme">(package-load-script package-id path) → boolean?
  package-id : string?
  path       : string?
</code></pre>
Loads `scripts/{path}.s7` from a specific package.

### current-package-id {#current-package-id}
<pre><code class="language-scheme">(current-package-id) → string?
</code></pre>
Returns the id of the package file currently being evaluated.

### package-mounted? {#package-mounted}
<pre><code class="language-scheme">(package-mounted? package-id) → boolean?
  package-id : string?
</code></pre>
Checks whether a package is currently mounted.

### hook-add {#hook-add}
<pre><code class="language-scheme">(hook-add hook-symbol proc) → boolean?
  hook-symbol : symbol?
  proc        : procedure?
</code></pre>
Appends a contributor procedure for an engine-owned hook (see the Hooks section below).

### startup-arg {#startup-arg}
<pre><code class="language-scheme">(startup-arg name) → (or string? \#f)
  name : string?
</code></pre>
Returns a package CLI value by name.

### startup-args {#startup-args}
<pre><code class="language-scheme">(startup-args) → (listof pair?)
</code></pre>
Returns all package CLI values.

### request-map-load {#request-map-load}
<pre><code class="language-scheme">(request-map-load name [reason]) → boolean?
  name   : string?
  reason : string?
</code></pre>
Queues a map change, optionally tagged with a reason string (e.g. `"fresh"`).

### current-map {#current-map}
<pre><code class="language-scheme">(current-map) → string?
</code></pre>
Returns the current map's folder id.

### list-maps {#list-maps}
<pre><code class="language-scheme">(list-maps) → (listof string?)
</code></pre>
Lists the available map folder ids.

### return-to-menu {#return-to-menu}
<pre><code class="language-scheme">(return-to-menu) → boolean?
</code></pre>
Unloads the current map and returns to the main menu.

## Player {#player}

### player-pose {#player-pose}
<pre><code class="language-scheme">(player-pose) → (list real? real? real? real? real?)
</code></pre>
Returns the player's position and look as `(x y z yaw pitch)`.

### player-set-pose {#player-set-pose}
<pre><code class="language-scheme">(player-set-pose x y z yaw pitch) → boolean?
  x, y, z    : real?
  yaw, pitch : real?
</code></pre>
Teleports the player and sets their look angles.

### player-eye-height {#player-eye-height}
<pre><code class="language-scheme">(player-eye-height) → real?
</code></pre>
Returns the player's current eye height above their feet.

### player-set-eye-height {#player-set-eye-height}
<pre><code class="language-scheme">(player-set-eye-height h) → boolean?
  h : real?
</code></pre>
Sets the CharacterMotor eye height.

### player-set-control {#player-set-control}
<pre><code class="language-scheme">(player-set-control move? look?) → boolean?
  move? : boolean?
  look? : boolean?
</code></pre>
Enables or disables player move wish and mouse look independently.

### player-set-pitch-locked {#player-set-pitch-locked}
<pre><code class="language-scheme">(player-set-pitch-locked locked?) → boolean?
  locked? : boolean?
</code></pre>
When locked, mouse-look stays yaw-only and pitch is pinned to 0.

### player-speed {#player-speed}
<pre><code class="language-scheme">(player-speed) → real?
</code></pre>
Returns the player's current horizontal speed, in world units per second.

### player-grounded? {#player-grounded}
<pre><code class="language-scheme">(player-grounded?) → boolean?
</code></pre>
Checks whether the player's character motor is on the ground.

### player-wish-speed {#player-wish-speed}
<pre><code class="language-scheme">(player-wish-speed) → real?
</code></pre>
Returns the current wish-vector speed, before physics resolves collisions.

### player-eye {#player-eye}
<pre><code class="language-scheme">(player-eye) → (list real? real? real?)
</code></pre>
Returns the player's eye-space world position as `(x y z)`.

### player-look-dir {#player-look-dir}
<pre><code class="language-scheme">(player-look-dir) → (list real? real? real?)
</code></pre>
Returns the player's normalized look direction as `(x y z)`.

## Input {#input}

### action-down? {#action-down}
<pre><code class="language-scheme">(action-down? id) → boolean?
  id : string?
</code></pre>
Checks whether a package-defined input action is currently held down.

### action-pressed? {#action-pressed}
<pre><code class="language-scheme">(action-pressed? id) → boolean?
  id : string?
</code></pre>
Checks whether a package-defined input action was pressed this frame.

## UI (ImGui) {#ui-imgui}

> NOTE: Scheme bindings directly to Imgui are temporary. This will change with a proper UI wrapper instead of direct Imgui calls.

### playing? {#playing}
<pre><code class="language-scheme">(playing?) → boolean?
</code></pre>
Checks whether the game is currently in gameplay context.

### main-menu? {#main-menu}
<pre><code class="language-scheme">(main-menu?) → boolean?
</code></pre>
Checks whether the game is currently showing the main menu.

### pause-menu? {#pause-menu}
<pre><code class="language-scheme">(pause-menu?) → boolean?
</code></pre>
Checks whether the game is currently showing the pause menu.

### ui-menu-item {#ui-menu-item}
<pre><code class="language-scheme">(ui-menu-item label [enabled?]) → boolean?
  label    : string?
  enabled? : boolean? = \#t
</code></pre>
Draws a File/Pause menu row; returns \#t when clicked.

### ui-menu-check {#ui-menu-check}
<pre><code class="language-scheme">(ui-menu-check label selected?) → boolean?
  label     : string?
  selected? : boolean?
</code></pre>
Draws a File/Pause menu row with a checkmark; returns \#t when clicked.

### ui-button {#ui-button}
<pre><code class="language-scheme">(ui-button label) → boolean?
  label : string?
</code></pre>
Draws a basic button widget; returns \#t when clicked.

### ui-text {#ui-text}
<pre><code class="language-scheme">(ui-text str) → boolean?
  str : string?
</code></pre>
Draws a line of text.

### ui-separator {#ui-separator}
<pre><code class="language-scheme">(ui-separator) → boolean?
</code></pre>
Draws a horizontal separator line.

### ui-same-line {#ui-same-line}
<pre><code class="language-scheme">(ui-same-line) → boolean?
</code></pre>
Keeps the next widget on the same line as the previous one.

### ui-begin {#ui-begin}
<pre><code class="language-scheme">(ui-begin title) → boolean?
  title : string?
</code></pre>
Begins a new ImGui window.

### ui-end {#ui-end}
<pre><code class="language-scheme">(ui-end) → boolean?
</code></pre>
Ends the current ImGui window.

### ui-begin-child {#ui-begin-child}
<pre><code class="language-scheme">(ui-begin-child id [height]) → boolean?
  id     : string?
  height : real?
</code></pre>
Begins a scrolling child region.

### ui-end-child {#ui-end-child}
<pre><code class="language-scheme">(ui-end-child) → boolean?
</code></pre>
Ends the current scrolling child region.

### ui-set-next-window-size {#ui-set-next-window-size}
<pre><code class="language-scheme">(ui-set-next-window-size w h) → boolean?
  w, h : real?
</code></pre>
Sets the size of the next window to be drawn.

### ui-center-next-window {#ui-center-next-window}
<pre><code class="language-scheme">(ui-center-next-window) → boolean?
</code></pre>
Centers the next window on screen.

### ui-input-text {#ui-input-text}
<pre><code class="language-scheme">(ui-input-text id initial) → string?
  id      : string?
  initial : string?
</code></pre>
Draws a text input field; returns its current contents.

### ui-input-text-set {#ui-input-text-set}
<pre><code class="language-scheme">(ui-input-text-set id str) → boolean?
  id  : string?
  str : string?
</code></pre>
Overwrites the contents of a text input field.

### ui-begin-combo {#ui-begin-combo}
<pre><code class="language-scheme">(ui-begin-combo id preview) → boolean?
  id      : string?
  preview : string?
</code></pre>
Begins a combo box, showing `preview` as the current selection.

### ui-combo-item {#ui-combo-item}
<pre><code class="language-scheme">(ui-combo-item label [selected?]) → boolean?
  label     : string?
  selected? : boolean?
</code></pre>
Draws a combo box item; returns \#t when clicked.

### ui-end-combo {#ui-end-combo}
<pre><code class="language-scheme">(ui-end-combo) → boolean?
</code></pre>
Ends the current combo box.

### ui-selectable {#ui-selectable}
<pre><code class="language-scheme">(ui-selectable label [selected?]) → boolean?
  label     : string?
  selected? : boolean?
</code></pre>
Draws a selectable list row; returns \#t when clicked.

### ui-indent {#ui-indent}
<pre><code class="language-scheme">(ui-indent) → boolean?
</code></pre>
Increases indentation for subsequent widgets.

### ui-unindent {#ui-unindent}
<pre><code class="language-scheme">(ui-unindent) → boolean?
</code></pre>
Decreases indentation for subsequent widgets.

## First-person {#first-person}

### fp-clear-socket {#fp-clear-socket}
<pre><code class="language-scheme">(fp-clear-socket socket) → boolean?
  socket : string?
</code></pre>
Clears whatever is currently attached to a first-person socket.

### fp-attach-geo {#fp-attach-geo}
<pre><code class="language-scheme">(fp-attach-geo socket geo [x y z sx sy sz]) → boolean?
  socket     : string?
  geo        : string?
  x, y, z    : real? = 0
  sx, sy, sz : real? = 1
</code></pre>
Attaches a geometry asset to a first-person socket, with an optional local offset and scale.

### fp-attach-sprite {#fp-attach-sprite}
<pre><code class="language-scheme">(fp-attach-sprite socket sprite [clip] [canvas-x canvas-y]) → boolean?
  socket             : string?
  sprite             : string?
  clip               : string? = "idle"
  canvas-x, canvas-y : real?
</code></pre>
Attaches a sprite to a first-person socket, with an optional starting clip and an explicit canvas position.

### fp-set-sprite-frame {#fp-set-sprite-frame}
<pre><code class="language-scheme">(fp-set-sprite-frame socket frame-id) → boolean?
  socket   : string?
  frame-id : string?
</code></pre>
Forces a socket's sprite to a specific frame id.

### fp-play-sprite-anim {#fp-play-sprite-anim}
<pre><code class="language-scheme">(fp-play-sprite-anim socket clip [loop?]) → boolean?
  socket : string?
  clip   : string?
  loop?  : boolean? = \#t
</code></pre>
Plays a `.spanim` clip on a socket's sprite.

### fp-sprite-anim-busy? {#fp-sprite-anim-busy}
<pre><code class="language-scheme">(fp-sprite-anim-busy? socket) → boolean?
  socket : string?
</code></pre>
Checks whether a socket's sprite is still mid-way through a non-looping clip.

### fp-set-sprite-pos {#fp-set-sprite-pos}
<pre><code class="language-scheme">(fp-set-sprite-pos socket x y) → boolean?
  socket : string?
  x, y   : real?
</code></pre>
Sets a socket sprite's canvas position.

### fp-sprite-pos {#fp-sprite-pos}
<pre><code class="language-scheme">(fp-sprite-pos socket) → (list real? real?)
  socket : string?
</code></pre>
Returns a socket sprite's canvas position as `(x y)`.

### fp-set-sprite-offset {#fp-set-sprite-offset}
<pre><code class="language-scheme">(fp-set-sprite-offset socket x y) → boolean?
  socket : string?
  x, y   : real?
</code></pre>
Sets a socket sprite's presentation offset (raise/lower/bob), layered on top of its canvas position.

### fp-sprite-offset {#fp-sprite-offset}
<pre><code class="language-scheme">(fp-sprite-offset socket) → (list real? real?)
  socket : string?
</code></pre>
Returns a socket sprite's presentation offset as `(x y)`.

### fp-set-sprite-scale {#fp-set-sprite-scale}
<pre><code class="language-scheme">(fp-set-sprite-scale socket sx sy) → boolean?
  socket : string?
  sx, sy : real?
</code></pre>
Sets a socket sprite's scale.

### fp-set-sprite-rotation {#fp-set-sprite-rotation}
<pre><code class="language-scheme">(fp-set-sprite-rotation socket degrees) → boolean?
  socket  : string?
  degrees : real?
</code></pre>
Sets a socket sprite's rotation.

### fp-set-sprite-origin {#fp-set-sprite-origin}
<pre><code class="language-scheme">(fp-set-sprite-origin socket ox oy) → boolean?
  socket : string?
  ox, oy : real?
</code></pre>
Sets a socket sprite's normalized origin point.

### fp-spawn-light {#fp-spawn-light}
<pre><code class="language-scheme">(fp-spawn-light socket kind [intensity range cone r g b x y z]) → boolean?
  socket            : string?
  kind              : symbol?
  intensity, range  : real?
  cone              : real?
  r, g, b           : real?
  x, y, z           : real?
</code></pre>
Spawns a dynamic light as a child of a first-person socket.

### fp-set-light-enabled {#fp-set-light-enabled}
<pre><code class="language-scheme">(fp-set-light-enabled socket enabled) → boolean?
  socket  : string?
  enabled : boolean?
</code></pre>
Enables or disables a light previously spawned under a socket.

### fp-set-rad-tint {#fp-set-rad-tint}
<pre><code class="language-scheme">(fp-set-rad-tint enabled) → boolean?
  enabled : boolean?
</code></pre>
Toggles whether viewmodels are tinted by the map's baked light probe.

### fp-set-shading {#fp-set-shading}
<pre><code class="language-scheme">(fp-set-shading enabled) → boolean?
  enabled : boolean?
</code></pre>
Toggles the viewmodel faux-lighting shader.

### fp-set-eye-offset {#fp-set-eye-offset}
<pre><code class="language-scheme">(fp-set-eye-offset x y z) → boolean?
  x, y, z : real?
</code></pre>
Sets the package's view-space eye offset, in meters.

## HUD {#hud}

### hud-anchor {#hud-anchor}
<pre><code class="language-scheme">(hud-anchor symbol) → boolean?
  symbol : symbol?
</code></pre>
Sets the HUD drawing origin (e.g. `'top-left`, `'top-right`, `'center`).

### hud-font {#hud-font}
<pre><code class="language-scheme">(hud-font path) → boolean?
  path : string?
</code></pre>
Sets the font used for subsequent HUD text drawing.

### hud-rect {#hud-rect}
<pre><code class="language-scheme">(hud-rect x y w h r g b [a]) → boolean?
  x, y, w, h : real?
  r, g, b    : real?
  a          : real? = 255
</code></pre>
Draws a filled rectangle on the HUD canvas.

### hud-image {#hud-image}
<pre><code class="language-scheme">(hud-image tex-path x y [w h] [r g b a]) → boolean?
  tex-path   : string?
  x, y       : real?
  w, h       : real?
  r, g, b, a : real? = 255
</code></pre>
Draws an image on the HUD canvas.

### hud-text {#hud-text}
<pre><code class="language-scheme">(hud-text str x y size [r g b a]) → boolean?
  str        : string?
  x, y       : real?
  size       : real?
  r, g, b, a : real? = 255
</code></pre>
Draws text on the HUD canvas.

## Post-processing {#post-processing}

### post-set-shader {#post-set-shader}
<pre><code class="language-scheme">(post-set-shader path) → boolean?
  path : string?
</code></pre>
Sets the active post-process shader.

### post-clear-shader {#post-clear-shader}
<pre><code class="language-scheme">(post-clear-shader) → boolean?
</code></pre>
Clears the active post-process shader.

### post-set-enabled {#post-set-enabled}
<pre><code class="language-scheme">(post-set-enabled enabled) → boolean?
  enabled : boolean?
</code></pre>
Enables or disables the post-process pass.

### post-set-float {#post-set-float}
<pre><code class="language-scheme">(post-set-float name x) → boolean?
  name : string?
  x    : real?
</code></pre>
Sets a `float` uniform on the active post-process shader.

### post-set-vec2 {#post-set-vec2}
<pre><code class="language-scheme">(post-set-vec2 name x y) → boolean?
  name : string?
  x, y : real?
</code></pre>
Sets a `vec2` uniform on the active post-process shader.

### post-set-vec3 {#post-set-vec3}
<pre><code class="language-scheme">(post-set-vec3 name x y z) → boolean?
  name    : string?
  x, y, z : real?
</code></pre>
Sets a `vec3` uniform on the active post-process shader.

### post-set-vec4 {#post-set-vec4}
<pre><code class="language-scheme">(post-set-vec4 name x y z w) → boolean?
  name       : string?
  x, y, z, w : real?
</code></pre>
Sets a `vec4` uniform on the active post-process shader.

## Audio {#audio}

### play-sound {#play-sound}
<pre><code class="language-scheme">(play-sound path [volume] [loop?]) → integer?
  path   : string?
  volume : real? = 1.0
  loop?  : boolean? = \#f
</code></pre>
Plays a raw sound clip; returns a handle for `stop-sound`/`set-sound-volume`.

### play-audio {#play-audio}
<pre><code class="language-scheme">(play-audio path [volume]) → boolean?
  path   : string?
  volume : real? = 1.0
</code></pre>
Plays a package audio definition (`audio/*.s7`).

### play-audio-3d {#play-audio-3d}
<pre><code class="language-scheme">(play-audio-3d path x y z [volume]) → boolean?
  path    : string?
  x, y, z : real?
  volume  : real? = 1.0
</code></pre>
Plays a package audio definition at a fixed 3D position.

### audio-attach {#audio-attach}
<pre><code class="language-scheme">(audio-attach id path) → boolean?
  id   : string?
  path : string?
</code></pre>
Attaches a positional audio definition to an existing thing.

### play-music {#play-music}
<pre><code class="language-scheme">(play-music path [volume]) → boolean?
  path   : string?
  volume : real? = 1.0
</code></pre>
Plays streaming music, replacing whatever is currently playing.

### stop-music {#stop-music}
<pre><code class="language-scheme">(stop-music) → boolean?
</code></pre>
Stops the currently playing music.

### stop-sound {#stop-sound}
<pre><code class="language-scheme">(stop-sound handle) → boolean?
  handle : integer?
</code></pre>
Stops a specific sound voice by handle.

### set-sound-volume {#set-sound-volume}
<pre><code class="language-scheme">(set-sound-volume handle vol) → boolean?
  handle : integer?
  vol    : real?
</code></pre>
Sets the volume of a specific sound voice by handle.

### set-bus-volume {#set-bus-volume}
<pre><code class="language-scheme">(set-bus-volume bus vol) → boolean?
  bus : string?
  vol : real?
</code></pre>
Sets the volume of an audio bus (`"sfx"` or `"music"`).

### audio-filter-attach {#audio-filter-attach}
<pre><code class="language-scheme">(audio-filter-attach target filter [slot]) → boolean?
  target : string?
  filter : string?
  slot   : integer?
</code></pre>
Attaches a registered DSP filter to a source or bus.

### register-audio-filter {#register-audio-filter}
<pre><code class="language-scheme">(register-audio-filter name proc) → boolean?
  name : string?
  proc : procedure?
</code></pre>
Registers a package-defined DSP filter procedure by name.

## Things {#things}

### thing-despawn {#thing-despawn}
<pre><code class="language-scheme">(thing-despawn id) → boolean?
  id : string?
</code></pre>
Queues despawn of a thing by its entity name.

### thing-type {#thing-type}
<pre><code class="language-scheme">(thing-type id) → (or string? \#f)
  id : string?
</code></pre>
Returns the catalog type id for a thing.

### thing-def-health {#thing-def-health}
<pre><code class="language-scheme">(thing-def-health type) → (or integer? \#f)
  type : string?
</code></pre>
Looks up a thing type's health value from its catalog definition.

### thing-def-idle-anim {#thing-def-idle-anim}
<pre><code class="language-scheme">(thing-def-idle-anim type) → (or string? \#f)
  type : string?
</code></pre>
Looks up a thing type's idle animation clip from its catalog definition.

### thing-def-behavior {#thing-def-behavior}
<pre><code class="language-scheme">(thing-def-behavior type) → (or string? \#f)
  type : string?
</code></pre>
Looks up a thing type's behavior name from its catalog definition.

### thing-def-move-mode {#thing-def-move-mode}
<pre><code class="language-scheme">(thing-def-move-mode type) → (or symbol? \#f)
  type : string?
</code></pre>
Looks up a thing type's motor move mode (`'slide`, `'try-move`, or `'fly`) from its catalog definition.

### thing-def-gravity {#thing-def-gravity}
<pre><code class="language-scheme">(thing-def-gravity type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's motor gravity from its catalog definition.

### thing-def-vertical-speed {#thing-def-vertical-speed}
<pre><code class="language-scheme">(thing-def-vertical-speed type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's max climb/dive speed (flight mode) from its catalog definition.

### thing-def-hover-height {#thing-def-hover-height}
<pre><code class="language-scheme">(thing-def-hover-height type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's default hover height from its catalog definition.

### thing-def-radius {#thing-def-radius}
<pre><code class="language-scheme">(thing-def-radius type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's motor collision radius from its catalog definition.

### thing-def-melee-damage {#thing-def-melee-damage}
<pre><code class="language-scheme">(thing-def-melee-damage type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's melee damage from its catalog definition.

### thing-def-melee-range {#thing-def-melee-range}
<pre><code class="language-scheme">(thing-def-melee-range type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's melee range from its catalog definition.

### thing-def-melee-cooldown {#thing-def-melee-cooldown}
<pre><code class="language-scheme">(thing-def-melee-cooldown type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's melee cooldown from its catalog definition.

### thing-def-melee-anim {#thing-def-melee-anim}
<pre><code class="language-scheme">(thing-def-melee-anim type) → (or string? \#f)
  type : string?
</code></pre>
Looks up a thing type's melee animation clip from its catalog definition.

### thing-def-ranged-range {#thing-def-ranged-range}
<pre><code class="language-scheme">(thing-def-ranged-range type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's max ranged attack distance from its catalog definition.

### thing-def-ranged-min-range {#thing-def-ranged-min-range}
<pre><code class="language-scheme">(thing-def-ranged-min-range type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's minimum ranged attack distance from its catalog definition.

### thing-def-ranged-cooldown {#thing-def-ranged-cooldown}
<pre><code class="language-scheme">(thing-def-ranged-cooldown type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's ranged attack cooldown from its catalog definition.

### thing-def-ranged-anim {#thing-def-ranged-anim}
<pre><code class="language-scheme">(thing-def-ranged-anim type) → (or string? \#f)
  type : string?
</code></pre>
Looks up a thing type's ranged attack animation clip from its catalog definition.

### thing-def-speed {#thing-def-speed}
<pre><code class="language-scheme">(thing-def-speed type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's motor move speed from its catalog definition.

### thing-def-lunge-range {#thing-def-lunge-range}
<pre><code class="language-scheme">(thing-def-lunge-range type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's lunge trigger range from its catalog definition.

### thing-def-lunge-speed {#thing-def-lunge-speed}
<pre><code class="language-scheme">(thing-def-lunge-speed type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's lunge speed from its catalog definition.

### thing-def-lunge-cooldown {#thing-def-lunge-cooldown}
<pre><code class="language-scheme">(thing-def-lunge-cooldown type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's lunge cooldown from its catalog definition.

### thing-def-lunge-duration {#thing-def-lunge-duration}
<pre><code class="language-scheme">(thing-def-lunge-duration type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's lunge duration from its catalog definition.

### thing-def-pain-chance {#thing-def-pain-chance}
<pre><code class="language-scheme">(thing-def-pain-chance type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's chance to enter a pain state from its catalog definition.

### thing-def-pain-threshold {#thing-def-pain-threshold}
<pre><code class="language-scheme">(thing-def-pain-threshold type) → (or real? \#f)
  type : string?
</code></pre>
Looks up a thing type's pain-state damage threshold from its catalog definition.

### thing-pos {#thing-pos}
<pre><code class="language-scheme">(thing-pos id) → (list real? real? real?)
  id : string?
</code></pre>
Returns a thing's world position as `(x y z)`.

### thing-yaw {#thing-yaw}
<pre><code class="language-scheme">(thing-yaw id) → real?
  id : string?
</code></pre>
Returns a thing's yaw, in radians.

### motored-spawn {#motored-spawn}
<pre><code class="language-scheme">(motored-spawn id x y z vx vy vz kind path [radius gravity lifetime on-impact ignore]) → boolean?
  id              : string?
  x, y, z         : real?
  vx, vy, vz      : real?
  kind            : symbol?
  path            : string?
  radius, gravity : real?
  lifetime        : real? = 8.0
  on-impact       : string?
  ignore          : string?
</code></pre>
Spawns a motor-driven projectile (rocket, fireball, etc.) that integrates against static geometry and actors.

### sprite-spawn {#sprite-spawn}
<pre><code class="language-scheme">(sprite-spawn id x y z path [clip] [lifetime]) → boolean?
  id       : string?
  x, y, z  : real?
  path     : string?
  clip     : string?
  lifetime : real?
</code></pre>
Spawns a world-space billboard sprite.

### particle-spawn {#particle-spawn}
<pre><code class="language-scheme">(particle-spawn id x y z path [yaw | dx dy dz]) → boolean?
  id       : string?
  x, y, z  : real?
  path     : string?
  yaw      : real?
  dx dy dz : real?
</code></pre>
Spawns a particle system, aimed by either a yaw angle or an explicit direction vector.

### particle-spawn-fp {#particle-spawn-fp}
<pre><code class="language-scheme">(particle-spawn-fp id socket path [depth]) → boolean?
  id     : string?
  socket : string?
  path   : string?
  depth  : real? = 0.35
</code></pre>
Spawns a view-space particle system pinned to a first-person socket.

### particle-play {#particle-play}
<pre><code class="language-scheme">(particle-play id) → boolean?
  id : string?
</code></pre>
Restarts a particle system.

### particle-stop {#particle-stop}
<pre><code class="language-scheme">(particle-stop id) → boolean?
  id : string?
</code></pre>
Stops a particle system.

### particle-despawn {#particle-despawn}
<pre><code class="language-scheme">(particle-despawn id) → boolean?
  id : string?
</code></pre>
Destroys a particle system entity.

### fx-light-spawn {#fx-light-spawn}
<pre><code class="language-scheme">(fx-light-spawn id x y z r g b intensity range [lifetime]) → boolean?
  id                : string?
  x, y, z           : real?
  r, g, b           : real?
  intensity, range  : real?
  lifetime          : real?
</code></pre>
Spawns a receiver-only FX light (tints dynamic receivers, not the baked map lightmap).

### fx-light-attach {#fx-light-attach}
<pre><code class="language-scheme">(fx-light-attach id r g b intensity range) → boolean?
  id               : string?
  r, g, b          : real?
  intensity, range : real?
</code></pre>
Attaches an FX light to an existing thing.

### dyn-light-spawn {#dyn-light-spawn}
<pre><code class="language-scheme">(dyn-light-spawn id x y z r g b intensity range [lifetime]) → boolean?
  id                : string?
  x, y, z           : real?
  r, g, b           : real?
  intensity, range  : real?
  lifetime          : real?
</code></pre>
Spawns a shadow-casting dynamic light, composited on top of baked lightmaps.

### dyn-light-attach {#dyn-light-attach}
<pre><code class="language-scheme">(dyn-light-attach id r g b intensity range) → boolean?
  id               : string?
  r, g, b          : real?
  intensity, range : real?
</code></pre>
Attaches a dynamic light to an existing thing.

### actor-spawn {#actor-spawn}
<pre><code class="language-scheme">(actor-spawn id x y z yaw kind path [radius height speed gravity tags-list]) → boolean?
  id             : string?
  x, y, z        : real?
  yaw            : real?
  kind           : symbol?
  path           : string?
  radius, height : real?
  speed, gravity : real?
  tags-list      : (listof string?)
</code></pre>
Spawns a package-driven actor.

### actor-pos {#actor-pos}
<pre><code class="language-scheme">(actor-pos id) → (list real? real? real?)
  id : string?
</code></pre>
Returns an actor's world position as `(x y z)`.

### actor-yaw {#actor-yaw}
<pre><code class="language-scheme">(actor-yaw id) → real?
  id : string?
</code></pre>
Returns an actor's yaw, in radians.

### actor-set-wish {#actor-set-wish}
<pre><code class="language-scheme">(actor-set-wish id wx wz) → boolean?
  id     : string?
  wx, wz : real?
</code></pre>
Sets an actor's horizontal movement wish vector.

### actor-set-wish-3d {#actor-set-wish-3d}
<pre><code class="language-scheme">(actor-set-wish-3d id wx wy wz) → boolean?
  id         : string?
  wx, wy, wz : real?
</code></pre>
Sets an actor's movement wish vector, including the vertical component (for flying actors).

### actor-set-move-speed! {#actor-set-move-speed}
<pre><code class="language-scheme">(actor-set-move-speed! id speed) → boolean?
  id    : string?
  speed : real?
</code></pre>
Overrides an actor's move speed.

### actor-set-corpse! {#actor-set-corpse}
<pre><code class="language-scheme">(actor-set-corpse! id) → boolean?
  id : string?
</code></pre>
Converts an actor into a non-colliding corpse, kept for visuals.

### actor-grounded? {#actor-grounded}
<pre><code class="language-scheme">(actor-grounded? id) → boolean?
  id : string?
</code></pre>
Checks whether an actor is currently grounded.

### actor-play-anim {#actor-play-anim}
<pre><code class="language-scheme">(actor-play-anim id clip [loop]) → boolean?
  id   : string?
  clip : string?
  loop : boolean? = \#t
</code></pre>
Plays an animation clip on an actor.

### actor-tags {#actor-tags}
<pre><code class="language-scheme">(actor-tags id) → (listof string?)
  id : string?
</code></pre>
Returns an actor's tags.

### actors-with-tag {#actors-with-tag}
<pre><code class="language-scheme">(actors-with-tag tag) → (listof string?)
  tag : string?
</code></pre>
Returns the ids of all actors carrying a specific tag.

### actors-in-radius {#actors-in-radius}
<pre><code class="language-scheme">(actors-in-radius x y z r [tag]) → (listof string?)
  x, y, z : real?
  r       : real?
  tag     : string?
</code></pre>
Returns the ids of all actors within a radius of a point, optionally filtered by tag.

### los? {#los}
<pre><code class="language-scheme">(los? x0 y0 z0 x1 y1 z1) → boolean?
  x0, y0, z0 : real?
  x1, y1, z1 : real?
</code></pre>
Checks unobstructed line of sight between two world points.

### volume-floor-at {#volume-floor-at}
<pre><code class="language-scheme">(volume-floor-at x y z [max-dist]) → (or real? \#f)
  x, y, z  : real?
  max-dist : real?
</code></pre>
Probes downward for the nearest floor height below a point.

### volume-ceiling-at {#volume-ceiling-at}
<pre><code class="language-scheme">(volume-ceiling-at x y z [max-dist]) → (or real? \#f)
  x, y, z  : real?
  max-dist : real?
</code></pre>
Probes upward for the nearest ceiling height above a point.

### volume-clearance {#volume-clearance}
<pre><code class="language-scheme">(volume-clearance x y z half-height [max-dist]) → (or real? \#f)
  x, y, z     : real?
  half-height : real?
  max-dist    : real?
</code></pre>
Measures the vertical clearance around a point.

### debug-line-add! {#debug-line-add}
<pre><code class="language-scheme">(debug-line-add! x0 y0 z0 x1 y1 z1 r g b [a]) → boolean?
  x0, y0, z0 : real?
  x1, y1, z1 : real?
  r, g, b    : real?
  a          : real? = 255
</code></pre>
Queues a colored debug line for the current frame.

### debug-lines-clear! {#debug-lines-clear}
<pre><code class="language-scheme">(debug-lines-clear!) → boolean?
</code></pre>
Clears all queued debug lines.

### pvs-can-see {#pvs-can-see}
<pre><code class="language-scheme">(pvs-can-see x0 y0 z0 x1 y1 z1) → boolean?
  x0, y0, z0 : real?
  x1, y1, z1 : real?
</code></pre>
Checks whether two points' BSP leaves are mutually visible in the map's precomputed PVS.

### actor-los? {#actor-los}
<pre><code class="language-scheme">(actor-los? from-id to-id) → boolean?
  from-id, to-id : string?
</code></pre>
Checks line of sight between two actors.

### actor-sight-set! {#actor-sight-set}
<pre><code class="language-scheme">(actor-sight-set! id alist) → boolean?
  id    : string?
  alist : alist?
</code></pre>
Configures an actor's `ActorSight` scanner (range, FOV, tags, filter procedure, etc.).

### actor-sight-get {#actor-sight-get}
<pre><code class="language-scheme">(actor-sight-get id) → alist?
  id : string?
</code></pre>
Returns an actor's current sight configuration as an association list.

### actor-can-see? {#actor-can-see}
<pre><code class="language-scheme">(actor-can-see? from to) → boolean?
  from, to : string?
</code></pre>
Checks whether one actor's sight scanner currently sees another actor.

### sight-budget {#sight-budget}
<pre><code class="language-scheme">(sight-budget) → integer?
</code></pre>
Returns the maximum number of line-of-sight traces run per frame.

### sight-budget-set! {#sight-budget-set}
<pre><code class="language-scheme">(sight-budget-set! n) → boolean?
  n : integer?
</code></pre>
Sets the maximum number of line-of-sight traces run per frame.

### hitscan-actors {#hitscan-actors}
<pre><code class="language-scheme">(hitscan-actors ox oy oz dx dy dz max-distance [tag] [xz-only?])
    → (or (list string? string? real? real? real? real?) \#f)
  ox, oy, oz   : real?
  dx, dy, dz   : real?
  max-distance : real?
  tag          : string?
  xz-only?     : boolean?
</code></pre>
Linescans for the nearest actor hit along a ray; returns `(id part distance x y z)` or \#f.

### linescan-world {#linescan-world}
<pre><code class="language-scheme">(linescan-world ox oy oz dx dy dz max-distance)
    → (or (list real? real? real? real? real? real? real?) \#f)
  ox, oy, oz   : real?
  dx, dy, dz   : real?
  max-distance : real?
</code></pre>
Linescans world geometry along a ray; returns `(x y z nx ny nz distance)` or \#f.

### mover-open {#mover-open}
<pre><code class="language-scheme">(mover-open id) → boolean?
  id : string?
</code></pre>
Opens a mover.

### mover-close {#mover-close}
<pre><code class="language-scheme">(mover-close id) → boolean?
  id : string?
</code></pre>
Closes a mover.

### mover-toggle {#mover-toggle}
<pre><code class="language-scheme">(mover-toggle id) → boolean?
  id : string?
</code></pre>
Toggles a mover between open and closed.

### mover-open-group {#mover-open-group}
<pre><code class="language-scheme">(mover-open-group g) → boolean?
  g : string?
</code></pre>
Opens every mover in a named group.

### mover-close-group {#mover-close-group}
<pre><code class="language-scheme">(mover-close-group g) → boolean?
  g : string?
</code></pre>
Closes every mover in a named group.

### mover-toggle-group {#mover-toggle-group}
<pre><code class="language-scheme">(mover-toggle-group g) → boolean?
  g : string?
</code></pre>
Toggles every mover in a named group.

### mover-set-locked {#mover-set-locked}
<pre><code class="language-scheme">(mover-set-locked id bool) → boolean?
  id   : string?
  bool : boolean?
</code></pre>
Locks or unlocks a mover.

### mover-locked? {#mover-locked}
<pre><code class="language-scheme">(mover-locked? id) → boolean?
  id : string?
</code></pre>
Checks whether a mover is locked.

### mover-progress {#mover-progress}
<pre><code class="language-scheme">(mover-progress id) → real?
  id : string?
</code></pre>
Returns a mover's progress between closed (`0`) and open (`1`).

### mover-state {#mover-state}
<pre><code class="language-scheme">(mover-state id) → alist?
  id : string?
</code></pre>
Returns a mover's full state as an association list.

### mover-set-state {#mover-set-state}
<pre><code class="language-scheme">(mover-set-state id open? progress [locked?]) → boolean?
  id       : string?
  open?    : boolean?
  progress : real?
  locked?  : boolean?
</code></pre>
Sets a mover's open/closed target, progress, and optionally its locked state.

## Navigation {#navigation}

### nav-set-goal-pos {#nav-set-goal-pos}
<pre><code class="language-scheme">(nav-set-goal-pos id x z) → boolean?
  id   : string?
  x, z : real?
</code></pre>
Sets an actor's pathfinding goal to a fixed ground position.

### nav-set-goal-entity {#nav-set-goal-entity}
<pre><code class="language-scheme">(nav-set-goal-entity id target-id) → boolean?
  id, target-id : string?
</code></pre>
Sets an actor's pathfinding goal to follow another entity.

### nav-clear-goal {#nav-clear-goal}
<pre><code class="language-scheme">(nav-clear-goal id) → boolean?
  id : string?
</code></pre>
Clears an actor's pathfinding goal.

### nav-waypoint {#nav-waypoint}
<pre><code class="language-scheme">(nav-waypoint id) → (or (list real? real? real?) \#f)
  id : string?
</code></pre>
Returns an actor's current waypoint as `(x y z)`, or \#f if it has no path.

### nav-has-path? {#nav-has-path}
<pre><code class="language-scheme">(nav-has-path? id) → boolean?
  id : string?
</code></pre>
Checks whether an actor currently has a planned path.

### nav-path-direction {#nav-path-direction}
<pre><code class="language-scheme">(nav-path-direction id) → (list real? real? real?)
  id : string?
</code></pre>
Returns the normalized direction from an actor toward its next waypoint.

### nav-path-altitude {#nav-path-altitude}
<pre><code class="language-scheme">(nav-path-altitude id) → real?
  id : string?
</code></pre>
Returns the vertical distance from an actor to its next waypoint.

### nav-leaf-at {#nav-leaf-at}
<pre><code class="language-scheme">(nav-leaf-at x y z) → integer?
  x, y, z : real?
</code></pre>
Returns the navigation leaf index containing a world point.

### nav-leaf-bounds {#nav-leaf-bounds}
<pre><code class="language-scheme">(nav-leaf-bounds leaf) → (list real? real? real? real? real? real?)
  leaf : integer?
</code></pre>
Returns a navigation leaf's AABB.

### nav-leaf-floor {#nav-leaf-floor}
<pre><code class="language-scheme">(nav-leaf-floor leaf) → real?
  leaf : integer?
</code></pre>
Returns a navigation leaf's floor height.

### nav-leaf-ceiling {#nav-leaf-ceiling}
<pre><code class="language-scheme">(nav-leaf-ceiling leaf) → real?
  leaf : integer?
</code></pre>
Returns a navigation leaf's ceiling height.

### nav-at-goal? {#nav-at-goal}
<pre><code class="language-scheme">(nav-at-goal? id) → boolean?
  id : string?
</code></pre>
Checks whether an actor has reached its pathfinding goal.

## Hooks {#hooks}

The base package defines owner procedures with exact names below. Mods extend them using `(hook-add 'name proc)`.

### prepare-first-person {#prepare-first-person}
<pre><code class="language-scheme">(prepare-first-person player-id)
  player-id : string?
</code></pre>
Called after the first-person scene exists, on map spawn.

### on-map-ready {#on-map-ready}
<pre><code class="language-scheme">(on-map-ready map-id reason)
  map-id, reason : string?
</code></pre>
Called after `prepare-first-person`, on map spawn.

### on-startup {#on-startup}
<pre><code class="language-scheme">(on-startup)
</code></pre>
Called once, after the player, menus, and mod contributions have loaded.

### draw-file-menu {#draw-file-menu}
<pre><code class="language-scheme">(draw-file-menu)
</code></pre>
Called inside the File menu, before the Quit item.

### draw-pause-menu {#draw-pause-menu}
<pre><code class="language-scheme">(draw-pause-menu)
</code></pre>
Called inside the Pause menu, after the Resume item.

### draw-debug-menu {#draw-debug-menu}
<pre><code class="language-scheme">(draw-debug-menu)
</code></pre>
Called inside the debug overlay menu.

### draw-modals {#draw-modals}
<pre><code class="language-scheme">(draw-modals)
</code></pre>
Called every UI frame, for package-owned popups.

### tick {#tick}
<pre><code class="language-scheme">(tick dt)
  dt : real?
</code></pre>
Called every update frame, with the frame delta time in seconds.

### draw-hud {#draw-hud}
<pre><code class="language-scheme">(draw-hud)
</code></pre>
Called when the HUD render pass runs.

### draw-title {#draw-title}
<pre><code class="language-scheme">(draw-title)
</code></pre>
Called every menu frame, for title screen chrome.

### on-sprite-hint {#on-sprite-hint}
<pre><code class="language-scheme">(on-sprite-hint source name)
  source, name : string?
</code></pre>
Called when a `.spanim` hold cue with `(hint "name")` is entered.

### on-sight {#on-sight}
<pre><code class="language-scheme">(on-sight observer-id target-id)
  observer-id, target-id : string?
</code></pre>
Called when an enabled `ActorSight` scanner acquires line of sight on a target.

### sight-filter {#sight-filter}
<pre><code class="language-scheme">(sight-filter observer-id target-id) → boolean?
  observer-id, target-id : string?
</code></pre>
Optional veto called during sight scans; return \#f to reject a candidate target.

### on-action-{id} {#on-action-id}
<pre><code class="language-scheme">(on-action-{id})
</code></pre>
Called when the package action `{id}` is pressed.

### on-use-{name} {#on-use-name}
<pre><code class="language-scheme">(on-use-{name} thing-id)
  thing-id : string?
</code></pre>
Called from a map's `(on-use "...")` binding.

### trigger enter/exit handlers {#trigger-enter-exit-handlers}
<pre><code class="language-scheme">(handler-proc thing-id other-id)
  thing-id, other-id : string?
</code></pre>
Bound per-thing via a map thing's `(on-enter …)` / `(on-exit …)` fields; called when something enters or exits a trigger volume.

### use handlers {#use-handlers}
<pre><code class="language-scheme">(handler-proc thing-id)
  thing-id : string?
</code></pre>
Bound per-thing via a map usable/pickup/mover's `(on-use …)` field; called when the thing is used.

### touch handlers {#touch-handlers}
<pre><code class="language-scheme">(handler-proc thing-id other-id)
  thing-id, other-id : string?
</code></pre>
Bound per-face via a CSG face's `(on-touch …)` field; called when something touches the face.
