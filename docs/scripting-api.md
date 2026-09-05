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

Each entry shows the call signature, the type expected for every argument, and what the call returns. Optional arguments are shown in `[brackets]`; `= value` marks a default used when the argument is omitted.

## I/O {#i-o}

### save-root {#save-root}
Returns the absolute filesystem path for the current mount's save context.
<pre><code class="language-scheme">(save-root) ↵ string?
</code></pre>

### save-write {#save-write}
Writes a Scheme value as an S-expression to a file under the save context root.
<pre><code class="language-scheme">(save-write
  rel : string?
  form : any?)
  ↵ boolean?
</code></pre>

### save-read {#save-read}
Reads one S-expression from a file relative to the save context root.
<pre><code class="language-scheme">(save-read
  rel : string?) ↵ any?
</code></pre>

### save-exists? {#save-exists}
Checks whether a regular file exists at the given relative save path.
<pre><code class="language-scheme">(save-exists?
  rel : string?) ↵ boolean?
</code></pre>

### save-delete {#save-delete}
Deletes a regular file under the save context root.
<pre><code class="language-scheme">(save-delete
  rel : string?) ↵ boolean?
</code></pre>

### save-timestamp {#save-timestamp}
Returns a local timestamp string, suitable for naming save files.
<pre><code class="language-scheme">(save-timestamp) ↵ string?
</code></pre>

### save-list {#save-list}
Lists `(rel-path . display)` pairs for files with the given suffix under a save directory.
<pre><code class="language-scheme">(save-list
  dir : string?
  suffix : string?)
  ↵ (listof pair?)
</code></pre>

### package-load-data {#package-load-data}
Loads `data/{path}.s7` from a specific package.
<pre><code class="language-scheme">(package-load-data
  package-id : string?
  path : string?)
  ↵ boolean?
</code></pre>

### package-load-script {#package-load-script}
Loads `scripts/{path}.s7` from a specific package.
<pre><code class="language-scheme">(package-load-script
  package-id : string?
  path : string?)
  ↵ boolean?
</code></pre>

### current-package-id {#current-package-id}
Returns the id of the package file currently being evaluated.
<pre><code class="language-scheme">(current-package-id) ↵ string?
</code></pre>

### package-mounted? {#package-mounted}
Checks whether a package is currently mounted.
<pre><code class="language-scheme">(package-mounted?
  package-id : string?) ↵ boolean?
</code></pre>

### hook-add {#hook-add}
Appends a contributor procedure for an engine-owned hook (see the Hooks section below).
<pre><code class="language-scheme">(hook-add
  hook-symbol : symbol?
  proc : procedure?)
  ↵ boolean?
</code></pre>

### startup-arg {#startup-arg}
Returns a package CLI value by name.
<pre><code class="language-scheme">(startup-arg
  name : string?) ↵ (or string? \#f)
</code></pre>

### startup-args {#startup-args}
Returns all package CLI values.
<pre><code class="language-scheme">(startup-args) ↵ (listof pair?)
</code></pre>

### request-map-load {#request-map-load}
Queues a map change, optionally tagged with a reason string (e.g. `"fresh"`).
<pre><code class="language-scheme">(request-map-load
  name : string?
  [reason] : string?)
  ↵ boolean?
</code></pre>

### current-map {#current-map}
Returns the current map's folder id.
<pre><code class="language-scheme">(current-map) ↵ string?
</code></pre>

### list-maps {#list-maps}
Lists the available map folder ids.
<pre><code class="language-scheme">(list-maps) ↵ (listof string?)
</code></pre>

### return-to-menu {#return-to-menu}
Unloads the current map and returns to the main menu.
<pre><code class="language-scheme">(return-to-menu) ↵ boolean?
</code></pre>

## Player {#player}

### player-pose {#player-pose}
Returns the player's position and look as `(x y z yaw pitch)`.
<pre><code class="language-scheme">(player-pose) ↵ (list real? real? real? real? real?)
</code></pre>

### player-set-pose {#player-set-pose}
Teleports the player and sets their look angles.
<pre><code class="language-scheme">(player-set-pose
  x, y, z : real?
  yaw, pitch : real?)
  ↵ boolean?
</code></pre>

### player-eye-height {#player-eye-height}
Returns the player's current eye height above their feet.
<pre><code class="language-scheme">(player-eye-height) ↵ real?
</code></pre>

### player-set-eye-height {#player-set-eye-height}
Sets the CharacterMotor eye height.
<pre><code class="language-scheme">(player-set-eye-height
  h : real?) ↵ boolean?
</code></pre>

### player-collider {#player-collider}
Returns the player's current collider as `(radius height)`.
<pre><code class="language-scheme">(player-collider) ↵ (list real? real?)
</code></pre>

### player-set-collider {#player-set-collider}
Sets the player's collider radius and height.
<pre><code class="language-scheme">(player-set-collider
  radius, height : real?) ↵ boolean?
</code></pre>

### player-set-control {#player-set-control}
Enables or disables player move wish and mouse look independently.
<pre><code class="language-scheme">(player-set-control
  move? : boolean?
  look? : boolean?)
  ↵ boolean?
</code></pre>

### player-set-pitch-locked {#player-set-pitch-locked}
When locked, mouse-look stays yaw-only and pitch is pinned to 0.
<pre><code class="language-scheme">(player-set-pitch-locked
  locked? : boolean?) ↵ boolean?
</code></pre>

### player-speed {#player-speed}
Returns the player's current horizontal speed, in world units per second.
<pre><code class="language-scheme">(player-speed) ↵ real?
</code></pre>

### player-grounded? {#player-grounded}
Checks whether the player's character motor is on the ground.
<pre><code class="language-scheme">(player-grounded?) ↵ boolean?
</code></pre>

### player-wish-speed {#player-wish-speed}
Returns the current wish-vector speed, before physics resolves collisions.
<pre><code class="language-scheme">(player-wish-speed) ↵ real?
</code></pre>

### player-eye {#player-eye}
Returns the player's eye-space world position as `(x y z)`.
<pre><code class="language-scheme">(player-eye) ↵ (list real? real? real?)
</code></pre>

### player-look-dir {#player-look-dir}
Returns the player's normalized look direction as `(x y z)`.
<pre><code class="language-scheme">(player-look-dir) ↵ (list real? real? real?)
</code></pre>

## Input {#input}

### action-down? {#action-down}
Checks whether a package-defined input action is currently held down.
<pre><code class="language-scheme">(action-down?
  id : string?) ↵ boolean?
</code></pre>

### action-pressed? {#action-pressed}
Checks whether a package-defined input action was pressed this frame.
<pre><code class="language-scheme">(action-pressed?
  id : string?) ↵ boolean?
</code></pre>

## UI (ImGui) {#ui-imgui}

These bind directly to ImGui calls. Most menu and dialog code should build on the higher-level @ref ui-screens instead. Reach for these directly only for a one-off widget or when writing a new node type for that wrapper.

### playing? {#playing}
Checks whether the game is currently in gameplay context.
<pre><code class="language-scheme">(playing?) ↵ boolean?
</code></pre>

### main-menu? {#main-menu}
Checks whether the game is currently showing the main menu.
<pre><code class="language-scheme">(main-menu?) ↵ boolean?
</code></pre>

### pause-menu? {#pause-menu}
Checks whether the game is currently showing the pause menu.
<pre><code class="language-scheme">(pause-menu?) ↵ boolean?
</code></pre>

### ui-menu-item {#ui-menu-item}
Draws a File/Pause menu row; returns \#t when clicked.
<pre><code class="language-scheme">(ui-menu-item
  label : string?
  [enabled?] : boolean? = \#t)
  ↵ boolean?
</code></pre>

### ui-menu-check {#ui-menu-check}
Draws a File/Pause menu row with a checkmark; returns \#t when clicked.
<pre><code class="language-scheme">(ui-menu-check
  label : string?
  selected? : boolean?)
  ↵ boolean?
</code></pre>

### ui-button {#ui-button}
Draws a basic button widget; returns \#t when clicked.
<pre><code class="language-scheme">(ui-button
  label : string?) ↵ boolean?
</code></pre>

### ui-text {#ui-text}
Draws a line of text.
<pre><code class="language-scheme">(ui-text
  str : string?) ↵ boolean?
</code></pre>

### ui-separator {#ui-separator}
Draws a horizontal separator line.
<pre><code class="language-scheme">(ui-separator) ↵ boolean?
</code></pre>

### ui-same-line {#ui-same-line}
Keeps the next widget on the same line as the previous one.
<pre><code class="language-scheme">(ui-same-line) ↵ boolean?
</code></pre>

### ui-begin {#ui-begin}
Begins a new ImGui window.
<pre><code class="language-scheme">(ui-begin
  title : string?) ↵ boolean?
</code></pre>

### ui-end {#ui-end}
Ends the current ImGui window.
<pre><code class="language-scheme">(ui-end) ↵ boolean?
</code></pre>

### ui-begin-child {#ui-begin-child}
Begins a scrolling child region.
<pre><code class="language-scheme">(ui-begin-child
  id : string?
  [height] : real?)
  ↵ boolean?
</code></pre>

### ui-end-child {#ui-end-child}
Ends the current scrolling child region.
<pre><code class="language-scheme">(ui-end-child) ↵ boolean?
</code></pre>

### ui-set-next-window-size {#ui-set-next-window-size}
Sets the size of the next window to be drawn.
<pre><code class="language-scheme">(ui-set-next-window-size
  w, h : real?) ↵ boolean?
</code></pre>

### ui-center-next-window {#ui-center-next-window}
Centers the next window on screen.
<pre><code class="language-scheme">(ui-center-next-window) ↵ boolean?
</code></pre>

### ui-input-text {#ui-input-text}
Draws a text input field; returns its current contents.
<pre><code class="language-scheme">(ui-input-text
  id : string?
  initial : string?)
  ↵ string?
</code></pre>

### ui-input-text-set {#ui-input-text-set}
Overwrites the contents of a text input field.
<pre><code class="language-scheme">(ui-input-text-set
  id : string?
  str : string?)
  ↵ boolean?
</code></pre>

### ui-begin-combo {#ui-begin-combo}
Begins a combo box, showing `preview` as the current selection.
<pre><code class="language-scheme">(ui-begin-combo
  id : string?
  preview : string?)
  ↵ boolean?
</code></pre>

### ui-combo-item {#ui-combo-item}
Draws a combo box item; returns \#t when clicked.
<pre><code class="language-scheme">(ui-combo-item
  label : string?
  [selected?] : boolean?)
  ↵ boolean?
</code></pre>

### ui-end-combo {#ui-end-combo}
Ends the current combo box.
<pre><code class="language-scheme">(ui-end-combo) ↵ boolean?
</code></pre>

### ui-selectable {#ui-selectable}
Draws a selectable list row; returns \#t when clicked.
<pre><code class="language-scheme">(ui-selectable
  label : string?
  [selected?] : boolean?)
  ↵ boolean?
</code></pre>

### ui-indent {#ui-indent}
Increases indentation for subsequent widgets.
<pre><code class="language-scheme">(ui-indent) ↵ boolean?
</code></pre>

### ui-unindent {#ui-unindent}
Decreases indentation for subsequent widgets.
<pre><code class="language-scheme">(ui-unindent) ↵ boolean?
</code></pre>

## UI Screens (Scheme wrapper) {#ui-screens}

`packages/engine/scripts/ui.s7` builds a declarative node tree on top of the raw @ref ui-imgui bindings above, so menu and dialog scripts compose widgets instead of calling ImGui directly. It's loaded automatically as part of `lang.s7`, so every package has it without an explicit `require`. See @ref ui_hud for the full architecture and worked examples.

Each constructor below returns a node, an opaque value (`node?`) that only makes sense passed as a child to another constructor, given to `define-screen`, or wrapped in `when-visible?`. A trailing `. children` accepts zero or more child nodes, Scheme's usual rest-argument notation.

A screen is declared once with `define-screen`, then rendered every frame from a hook (`draw-file-menu`, `draw-pause-menu`, `draw-debug-menu`, `draw-modals`) with `ui-screen-render`. The tree is built lazily and memoized on first render, not rebuilt every frame. Only `dynamic` and `dynamic-text` subtrees re-evaluate their content each call. `ui-screen-node`, `ui-set!`, and `ui-get` reach into an already-built screen by a child's `id` to read or patch it from outside the builder. `ui-screen-append!` adds a new child to the root afterward.

### define-screen {#define-screen}
Registers a named screen backed by a builder thunk that returns its root node.
<pre><code class="language-scheme">(define-screen
  name : symbol?
  builder : procedure?)
  ↵ boolean?
</code></pre>

### ui-screen-render {#ui-screen-render}
Builds a registered screen on first use, then renders its node tree; call this from a `draw-*` hook.
<pre><code class="language-scheme">(ui-screen-render
  name : symbol?)
  ↵ boolean?
</code></pre>

### ui-screen-append! {#ui-screen-append}
Appends a child node to an already-built screen's root and reindexes it.
<pre><code class="language-scheme">(ui-screen-append!
  name : symbol?
  node : node?)
  ↵ boolean?
</code></pre>

### ui-screen-node {#ui-screen-node}
Looks up a node by id within a built screen; raises an error if the id is unknown.
<pre><code class="language-scheme">(ui-screen-node
  name : symbol?
  id : string?)
  ↵ node?
</code></pre>

### ui-set! {#ui-set}
Sets a property on a node found by id within a screen.
<pre><code class="language-scheme">(ui-set!
  name : symbol?
  id : string?
  key : symbol?
  value : any?)
  ↵ boolean?
</code></pre>

### ui-get {#ui-get}
Reads a property from a node found by id within a screen.
<pre><code class="language-scheme">(ui-get
  name : symbol?
  id : string?
  key : symbol?)
  ↵ any?
</code></pre>

### window {#ui-node-window}
Declares an ImGui window; renders its children only while the window is open.
<pre><code class="language-scheme">(window
  title : string?
  . children : node?)
  ↵ node?
</code></pre>

### modal {#ui-node-modal}
Declares a fixed-size window centered on screen, for dialogs.
<pre><code class="language-scheme">(modal
  title : string?
  width, height : real?
  . children : node?)
  ↵ node?
</code></pre>

### vbox {#ui-node-vbox}
Declares children stacked top to bottom in order.
<pre><code class="language-scheme">(vbox
  . children : node?)
  ↵ node?
</code></pre>

### hbox {#ui-node-hbox}
Declares children laid out left to right on one line.
<pre><code class="language-scheme">(hbox
  . children : node?)
  ↵ node?
</code></pre>

### indent {#ui-node-indent}
Declares children rendered with one extra indent level.
<pre><code class="language-scheme">(indent
  . children : node?)
  ↵ node?
</code></pre>

### ui-child {#ui-node-child}
Declares a scrolling child region.
<pre><code class="language-scheme">(ui-child
  id : string?
  [height] : real?
  . children : node?)
  ↵ node?
</code></pre>

### text {#ui-node-text}
Declares a static line of text.
<pre><code class="language-scheme">(text
  label : string?)
  ↵ node?
</code></pre>

### dynamic-text {#ui-node-dynamic-text}
Declares a line of text recomputed every frame by calling `label-getter`.
<pre><code class="language-scheme">(dynamic-text
  label-getter : procedure?)
  ↵ node?
</code></pre>

### separator {#ui-node-separator}
Declares a horizontal separator line.
<pre><code class="language-scheme">(separator) ↵ node?
</code></pre>

### dynamic {#ui-node-dynamic}
Declares a subtree rebuilt every frame by calling `builder`, which returns a list of nodes.
<pre><code class="language-scheme">(dynamic
  builder : procedure?)
  ↵ node?
</code></pre>

### button {#ui-node-button}
Declares a button; calls `on-click` with no arguments when clicked.
<pre><code class="language-scheme">(button
  id : string?
  label : string?
  on-click : procedure?)
  ↵ node?
</code></pre>

### menu-item {#ui-node-menu-item}
Declares a File/Pause/Debug menu row; `enabled-getter` is called to decide enabled state, `on-click` fires on click.
<pre><code class="language-scheme">(menu-item
  id : string?
  label : string?
  enabled-getter : procedure?
  on-click : procedure?)
  ↵ node?
</code></pre>

### menu-check {#ui-node-menu-check}
Declares a menu row with a checkmark; calls `on-toggle` with the new state when toggled.
<pre><code class="language-scheme">(menu-check
  id : string?
  label : string?
  checked-getter : procedure?
  on-toggle : procedure?)
  ↵ node?
</code></pre>

### selectable {#ui-node-selectable}
Declares a selectable list row.
<pre><code class="language-scheme">(selectable
  id : string?
  label : string?
  selected-getter : procedure?
  on-click : procedure?)
  ↵ node?
</code></pre>

### input-text {#ui-node-input-text}
Declares a text input field; calls `on-change` with the new contents when edited.
<pre><code class="language-scheme">(input-text
  id : string?
  initial : string?
  on-change : procedure?)
  ↵ node?
</code></pre>

### combo {#ui-node-combo}
Declares a combo box; `items` should be `combo-item` nodes.
<pre><code class="language-scheme">(combo
  id : string?
  preview-getter : procedure?
  . items : node?)
  ↵ node?
</code></pre>

### combo-item {#ui-node-combo-item}
Declares one row of a combo box.
<pre><code class="language-scheme">(combo-item
  label : string?
  selected-getter : procedure?
  on-click : procedure?)
  ↵ node?
</code></pre>

### when-visible? {#ui-node-when-visible}
Wraps a node so it, and its children, are skipped when `getter` returns \#f.
<pre><code class="language-scheme">(when-visible?
  getter : procedure?
  node : node?)
  ↵ node?
</code></pre>

## First-person {#first-person}

### fp-clear-socket {#fp-clear-socket}
Clears whatever is currently attached to a first-person socket.
<pre><code class="language-scheme">(fp-clear-socket
  socket : string?) ↵ boolean?
</code></pre>

### fp-attach-geo {#fp-attach-geo}
Attaches a geometry asset to a first-person socket, with an optional local offset and scale.
<pre><code class="language-scheme">(fp-attach-geo
  socket : string?
  geo : string?
  [x, y, z] : real? = 0
  [sx, sy, sz] : real? = 1)
  ↵ boolean?
</code></pre>

### fp-attach-sprite {#fp-attach-sprite}
Attaches a sprite to a first-person socket, with an optional starting clip and an explicit canvas position.
<pre><code class="language-scheme">(fp-attach-sprite
  socket : string?
  sprite : string?
  [clip] : string? = "idle"
  [canvas-x, canvas-y] : real?)
  ↵ boolean?
</code></pre>

### fp-set-sprite-frame {#fp-set-sprite-frame}
Forces a socket's sprite to a specific frame id.
<pre><code class="language-scheme">(fp-set-sprite-frame
  socket : string?
  frame-id : string?)
  ↵ boolean?
</code></pre>

### fp-play-sprite-anim {#fp-play-sprite-anim}
Plays a `.spanim` clip on a socket's sprite.
<pre><code class="language-scheme">(fp-play-sprite-anim
  socket : string?
  clip : string?
  [loop?] : boolean? = \#t)
  ↵ boolean?
</code></pre>

### fp-sprite-anim-busy? {#fp-sprite-anim-busy}
Checks whether a socket's sprite is still mid-way through a non-looping clip.
<pre><code class="language-scheme">(fp-sprite-anim-busy?
  socket : string?) ↵ boolean?
</code></pre>

### fp-set-sprite-pos {#fp-set-sprite-pos}
Sets a socket sprite's canvas position.
<pre><code class="language-scheme">(fp-set-sprite-pos
  socket : string?
  x, y : real?)
  ↵ boolean?
</code></pre>

### fp-sprite-pos {#fp-sprite-pos}
Returns a socket sprite's canvas position as `(x y)`.
<pre><code class="language-scheme">(fp-sprite-pos
  socket : string?) ↵ (list real? real?)
</code></pre>

### fp-set-sprite-offset {#fp-set-sprite-offset}
Sets a socket sprite's presentation offset (raise/lower/bob), layered on top of its canvas position.
<pre><code class="language-scheme">(fp-set-sprite-offset
  socket : string?
  x, y : real?)
  ↵ boolean?
</code></pre>

### fp-sprite-offset {#fp-sprite-offset}
Returns a socket sprite's presentation offset as `(x y)`.
<pre><code class="language-scheme">(fp-sprite-offset
  socket : string?) ↵ (list real? real?)
</code></pre>

### fp-set-sprite-scale {#fp-set-sprite-scale}
Sets a socket sprite's scale.
<pre><code class="language-scheme">(fp-set-sprite-scale
  socket : string?
  sx, sy : real?)
  ↵ boolean?
</code></pre>

### fp-set-sprite-rotation {#fp-set-sprite-rotation}
Sets a socket sprite's rotation.
<pre><code class="language-scheme">(fp-set-sprite-rotation
  socket : string?
  degrees : real?)
  ↵ boolean?
</code></pre>

### fp-set-sprite-origin {#fp-set-sprite-origin}
Sets a socket sprite's normalized origin point.
<pre><code class="language-scheme">(fp-set-sprite-origin
  socket : string?
  ox, oy : real?)
  ↵ boolean?
</code></pre>

### fp-spawn-light {#fp-spawn-light}
Spawns a dynamic light as a child of a first-person socket.
<pre><code class="language-scheme">(fp-spawn-light
  socket : string?
  kind : symbol?
  [intensity, range] : real?
  [cone] : real?
  [r, g, b] : real?
  [x, y, z] : real?)
  ↵ boolean?
</code></pre>

### fp-set-light-enabled {#fp-set-light-enabled}
Enables or disables a light previously spawned under a socket.
<pre><code class="language-scheme">(fp-set-light-enabled
  socket : string?
  enabled : boolean?)
  ↵ boolean?
</code></pre>

### fp-set-rad-tint {#fp-set-rad-tint}
Toggles whether viewmodels are tinted by the map's baked light probe.
<pre><code class="language-scheme">(fp-set-rad-tint
  enabled : boolean?) ↵ boolean?
</code></pre>

### fp-set-shading {#fp-set-shading}
Toggles the viewmodel faux-lighting shader.
<pre><code class="language-scheme">(fp-set-shading
  enabled : boolean?) ↵ boolean?
</code></pre>

### fp-set-eye-offset {#fp-set-eye-offset}
Sets the package's view-space eye offset, in meters.
<pre><code class="language-scheme">(fp-set-eye-offset
  x, y, z : real?) ↵ boolean?
</code></pre>

### fp-set-weapon-hidden! {#fp-set-weapon-hidden}
Hides or shows the viewmodel weapon.
<pre><code class="language-scheme">(fp-set-weapon-hidden!
  hidden : boolean?) ↵ boolean?
</code></pre>

### extra-eye-configure! {#extra-eye-configure}
Configures the resolution and vertical field of view of the player's secondary render eye.
<pre><code class="language-scheme">(extra-eye-configure!
  width, height : integer?
  fovy : real?)
  ↵ boolean?
</code></pre>

### extra-eye-set-active! {#extra-eye-set-active}
Enables or disables rendering of the player's secondary render eye.
<pre><code class="language-scheme">(extra-eye-set-active!
  active : boolean?) ↵ boolean?
</code></pre>

## HUD {#hud}

### hud-anchor {#hud-anchor}
Sets the HUD drawing origin (e.g. `'top-left`, `'top-right`, `'center`).
<pre><code class="language-scheme">(hud-anchor
  symbol : symbol?) ↵ boolean?
</code></pre>

### hud-font {#hud-font}
Sets the font used for subsequent HUD text drawing.
<pre><code class="language-scheme">(hud-font
  path : string?) ↵ boolean?
</code></pre>

### hud-rect {#hud-rect}
Draws a filled rectangle on the HUD canvas.
<pre><code class="language-scheme">(hud-rect
  x, y, w, h : real?
  r, g, b : real?
  [a] : real? = 255)
  ↵ boolean?
</code></pre>

### hud-image {#hud-image}
Draws an image on the HUD canvas.
<pre><code class="language-scheme">(hud-image
  tex-path : string?
  x, y : real?
  [w, h] : real?
  [r, g, b, a] : real? = 255)
  ↵ boolean?
</code></pre>

### hud-text {#hud-text}
Draws text on the HUD canvas.
<pre><code class="language-scheme">(hud-text
  str : string?
  x, y : real?
  size : real?
  [r, g, b, a] : real? = 255)
  ↵ boolean?
</code></pre>

### hud-draw-eye {#hud-draw-eye}
Draws the player's secondary render eye's output onto the HUD canvas, optionally masked by a texture.
<pre><code class="language-scheme">(hud-draw-eye
  x, y, w, h : real?
  [mask-path] : string?)
  ↵ boolean?
</code></pre>

## Post-processing {#post-processing}

Post-process shaders run as an ordered stack per target, so any number of
effects can be layered — each pushed shader is a full extra fullscreen pass,
so stacking many has a real GPU cost. `target` is one of three symbols:

- `'scene` — runs on the rendered 3D/first-person scene only, before the HUD
  is drawn.
- `'hud` — runs on the HUD layer only (in isolation, on a transparent
  background), before it's composited over the scene.
- `'both` — runs once on the final composited scene+HUD image, after
  compositing. Use this for full-screen effects (CRT, vignette, chromatic
  aberration) that should visibly affect the UI too.

Every push returns an integer handle (`0` on failure) used to address that
specific pass — enable/disable it, update its uniforms, or remove it.
Whenever `'hud` or `'both` has at least one active pass, the engine captures
the HUD to its own texture and composites it with the shaded scene; with only
`'scene` passes active (the common case), rendering stays on the original
direct-to-backbuffer fast path.

### post-push-shader {#post-push-shader}
Compiles `path` against the engine's fullscreen vertex shader and pushes it
onto `target`'s stack, enabled by default. Returns a handle, or `0` on
failure (bad path, compile error).
<pre><code class="language-scheme">(post-push-shader
  path : string?
  target : symbol? = 'scene | 'hud | 'both)
  ↵ integer?
</code></pre>

### post-remove-shader {#post-remove-shader}
Removes a single pass from whichever stack it's in.
<pre><code class="language-scheme">(post-remove-shader
  handle : integer?) ↵ boolean?
</code></pre>

### post-clear-shaders {#post-clear-shaders}
Removes every pass on the given target's stack.
<pre><code class="language-scheme">(post-clear-shaders
  target : symbol? = 'scene | 'hud | 'both) ↵ boolean?
</code></pre>

### post-set-enabled {#post-set-enabled}
Enables or disables a specific pass without removing it.
<pre><code class="language-scheme">(post-set-enabled
  handle : integer?
  enabled : boolean?)
  ↵ boolean?
</code></pre>

### post-set-float {#post-set-float}
Sets a `float` uniform on a specific pass's shader.
<pre><code class="language-scheme">(post-set-float
  handle : integer?
  name : string?
  x : real?)
  ↵ boolean?
</code></pre>

### post-set-vec2 {#post-set-vec2}
Sets a `vec2` uniform on a specific pass's shader.
<pre><code class="language-scheme">(post-set-vec2
  handle : integer?
  name : string?
  x, y : real?)
  ↵ boolean?
</code></pre>

### post-set-vec3 {#post-set-vec3}
Sets a `vec3` uniform on a specific pass's shader.
<pre><code class="language-scheme">(post-set-vec3
  handle : integer?
  name : string?
  x, y, z : real?)
  ↵ boolean?
</code></pre>

### post-set-vec4 {#post-set-vec4}
Sets a `vec4` uniform on a specific pass's shader.
<pre><code class="language-scheme">(post-set-vec4
  handle : integer?
  name : string?
  x, y, z, w : real?)
  ↵ boolean?
</code></pre>

## Audio {#audio}

### play-sound {#play-sound}
Plays a raw sound clip; returns a handle for `stop-sound`/`set-sound-volume`.
<pre><code class="language-scheme">(play-sound
  path : string?
  [volume] : real? = 1.0
  [loop?] : boolean? = \#f)
  ↵ integer?
</code></pre>

### play-audio {#play-audio}
Plays a package audio definition (`audio/*.s7`).
<pre><code class="language-scheme">(play-audio
  path : string?
  [volume] : real? = 1.0)
  ↵ boolean?
</code></pre>

### play-audio-3d {#play-audio-3d}
Plays a package audio definition at a fixed 3D position.
<pre><code class="language-scheme">(play-audio-3d
  path : string?
  x, y, z : real?
  [volume] : real? = 1.0)
  ↵ boolean?
</code></pre>

### audio-attach {#audio-attach}
Attaches a positional audio definition to an existing thing.
<pre><code class="language-scheme">(audio-attach
  id : string?
  path : string?)
  ↵ boolean?
</code></pre>

### play-music {#play-music}
Plays streaming music, replacing whatever is currently playing.
<pre><code class="language-scheme">(play-music
  path : string?
  [volume] : real? = 1.0)
  ↵ boolean?
</code></pre>

### stop-music {#stop-music}
Stops the currently playing music.
<pre><code class="language-scheme">(stop-music) ↵ boolean?
</code></pre>

### stop-sound {#stop-sound}
Stops a specific sound voice by handle.
<pre><code class="language-scheme">(stop-sound
  handle : integer?) ↵ boolean?
</code></pre>

### set-sound-volume {#set-sound-volume}
Sets the volume of a specific sound voice by handle.
<pre><code class="language-scheme">(set-sound-volume
  handle : integer?
  vol : real?)
  ↵ boolean?
</code></pre>

### set-bus-volume {#set-bus-volume}
Sets the volume of an audio bus (`"sfx"` or `"music"`).
<pre><code class="language-scheme">(set-bus-volume
  bus : string?
  vol : real?)
  ↵ boolean?
</code></pre>

### audio-filter-attach {#audio-filter-attach}
Attaches a registered DSP filter to a source or bus.
<pre><code class="language-scheme">(audio-filter-attach
  target : string?
  filter : string?
  [slot] : integer?)
  ↵ boolean?
</code></pre>

### register-audio-filter {#register-audio-filter}
Registers a package-defined DSP filter procedure by name.
<pre><code class="language-scheme">(register-audio-filter
  name : string?
  proc : procedure?)
  ↵ boolean?
</code></pre>

## Things {#script-things}

### thing-despawn {#thing-despawn}
Queues despawn of a thing by its entity name.
<pre><code class="language-scheme">(thing-despawn
  id : string?) ↵ boolean?
</code></pre>

### thing-type {#thing-type}
Returns the catalog type id for a thing.
<pre><code class="language-scheme">(thing-type
  id : string?) ↵ (or string? \#f)
</code></pre>

### thing-def-health {#thing-def-health}
Looks up a thing type's health value from its catalog definition.
<pre><code class="language-scheme">(thing-def-health
  type : string?) ↵ (or integer? \#f)
</code></pre>

### thing-def-idle-anim {#thing-def-idle-anim}
Looks up a thing type's idle animation clip from its catalog definition.
<pre><code class="language-scheme">(thing-def-idle-anim
  type : string?) ↵ (or string? \#f)
</code></pre>

### thing-def-behavior {#thing-def-behavior}
Looks up a thing type's behavior name from its catalog definition.
<pre><code class="language-scheme">(thing-def-behavior
  type : string?) ↵ (or string? \#f)
</code></pre>

### thing-def-move-mode {#thing-def-move-mode}
Looks up a thing type's motor move mode (`'slide`, `'try-move`, or `'fly`) from its catalog definition.
<pre><code class="language-scheme">(thing-def-move-mode
  type : string?) ↵ (or symbol? \#f)
</code></pre>

### thing-def-gravity {#thing-def-gravity}
Looks up a thing type's motor gravity from its catalog definition.
<pre><code class="language-scheme">(thing-def-gravity
  type : string?) ↵ (or real? \#f)
</code></pre>

### thing-def-vertical-speed {#thing-def-vertical-speed}
Looks up a thing type's max climb/dive speed (flight mode) from its catalog definition.
<pre><code class="language-scheme">(thing-def-vertical-speed
  type : string?) ↵ (or real? \#f)
</code></pre>

### thing-def-hover-height {#thing-def-hover-height}
Looks up a thing type's default hover height from its catalog definition.
<pre><code class="language-scheme">(thing-def-hover-height
  type : string?) ↵ (or real? \#f)
</code></pre>

### thing-def-radius {#thing-def-radius}
Looks up a thing type's motor collision radius from its catalog definition.
<pre><code class="language-scheme">(thing-def-radius
  type : string?) ↵ (or real? \#f)
</code></pre>

### thing-def-behavior-names {#thing-def-behavior-names}
Returns the names of every behavior attached to a thing type's catalog definition.
<pre><code class="language-scheme">(thing-def-behavior-names
  type : string?) ↵ (listof string?)
</code></pre>

### thing-def-behavior-has? {#thing-def-behavior-has}
Checks whether a thing type's catalog definition has a behavior with the given name.
<pre><code class="language-scheme">(thing-def-behavior-has?
  type : string?
  name : string?)
  ↵ boolean?
</code></pre>

### thing-def-behavior-params {#thing-def-behavior-params}
Returns a named behavior's parameters as an association list, or \#f if the type or behavior isn't found.
<pre><code class="language-scheme">(thing-def-behavior-params
  type : string?
  name : string?)
  ↵ (or alist? \#f)
</code></pre>

### thing-def-speed {#thing-def-speed}
Looks up a thing type's motor move speed from its catalog definition.
<pre><code class="language-scheme">(thing-def-speed
  type : string?) ↵ (or real? \#f)
</code></pre>

### thing-def-pain-chance {#thing-def-pain-chance}
Looks up a thing type's chance to enter a pain state from its catalog definition.
<pre><code class="language-scheme">(thing-def-pain-chance
  type : string?) ↵ (or real? \#f)
</code></pre>

### thing-def-pain-threshold {#thing-def-pain-threshold}
Looks up a thing type's pain-state damage threshold from its catalog definition.
<pre><code class="language-scheme">(thing-def-pain-threshold
  type : string?) ↵ (or real? \#f)
</code></pre>

### thing-pos {#thing-pos}
Returns a thing's world position as `(x y z)`.
<pre><code class="language-scheme">(thing-pos
  id : string?) ↵ (list real? real? real?)
</code></pre>

### thing-yaw {#thing-yaw}
Returns a thing's yaw, in radians.
<pre><code class="language-scheme">(thing-yaw
  id : string?) ↵ real?
</code></pre>

## Effects {#script-effects}

### motored-spawn {#motored-spawn}
Spawns a motor-driven projectile (rocket, fireball, etc.) that integrates against static geometry and actors.
<pre><code class="language-scheme">(motored-spawn
  id : string?
  x, y, z : real?
  vx, vy, vz : real?
  kind : symbol?
  path : string?
  [radius, gravity] : real?
  [lifetime] : real? = 8.0
  [on-impact] : string?
  [ignore] : string?)
  ↵ boolean?
</code></pre>

### sprite-spawn {#sprite-spawn}
Spawns a world-space billboard sprite.
<pre><code class="language-scheme">(sprite-spawn
  id : string?
  x, y, z : real?
  path : string?
  [clip] : string?
  [lifetime] : real?)
  ↵ boolean?
</code></pre>

### particle-spawn {#particle-spawn}
Spawns a particle system, aimed by either a yaw angle or an explicit direction vector.
<pre><code class="language-scheme">(particle-spawn
  id : string?
  x, y, z : real?
  path : string?
  [yaw] : real?
  [dx, dy, dz] : real?)
  ↵ boolean?
</code></pre>

### particle-spawn-fp {#particle-spawn-fp}
Spawns a view-space particle system pinned to a first-person socket.
<pre><code class="language-scheme">(particle-spawn-fp
  id : string?
  socket : string?
  path : string?
  attach : string?
  [depth] : real? = 0.35)
  ↵ boolean?
</code></pre>

### particle-spawn-attach {#particle-spawn-attach}
Spawns a particle system attached to an existing thing.
<pre><code class="language-scheme">(particle-spawn-attach
  id : string?
  target : string?
  path : string?
  attach : string?)
  ↵ boolean?
</code></pre>

### trail-spawn-fp {#trail-spawn-fp}
Spawns a trail effect anchored to a first-person socket, stretching toward a world-space endpoint.
<pre><code class="language-scheme">(trail-spawn-fp
  id : string?
  socket : string?
  path : string?
  attach : string?
  ex, ey, ez : real?
  lifetime : real?
  [width, depth] : real?)
  ↵ boolean?
</code></pre>

### trail-spawn {#trail-spawn}
Spawns a world-space trail effect between two points.
<pre><code class="language-scheme">(trail-spawn
  id : string?
  sx, sy, sz : real?
  ex, ey, ez : real?
  path : string?
  lifetime : real?
  [width] : real?)
  ↵ boolean?
</code></pre>

### particle-play {#particle-play}
Restarts a particle system.
<pre><code class="language-scheme">(particle-play
  id : string?) ↵ boolean?
</code></pre>

### particle-stop {#particle-stop}
Stops a particle system.
<pre><code class="language-scheme">(particle-stop
  id : string?) ↵ boolean?
</code></pre>

### particle-despawn {#particle-despawn}
Destroys a particle system entity.
<pre><code class="language-scheme">(particle-despawn
  id : string?) ↵ boolean?
</code></pre>

### fx-light-spawn {#fx-light-spawn}
Spawns a receiver-only FX light (tints dynamic receivers, not the baked map lightmap).
<pre><code class="language-scheme">(fx-light-spawn
  id : string?
  x, y, z : real?
  r, g, b : real?
  intensity, range : real?
  [lifetime] : real?)
  ↵ boolean?
</code></pre>

### fx-light-attach {#fx-light-attach}
Attaches an FX light to an existing thing.
<pre><code class="language-scheme">(fx-light-attach
  id : string?
  r, g, b : real?
  intensity, range : real?)
  ↵ boolean?
</code></pre>

### dyn-light-spawn {#dyn-light-spawn}
Spawns a shadow-casting dynamic light, composited on top of baked lightmaps.
<pre><code class="language-scheme">(dyn-light-spawn
  id : string?
  x, y, z : real?
  r, g, b : real?
  intensity, range : real?
  [lifetime] : real?)
  ↵ boolean?
</code></pre>

### dyn-light-attach {#dyn-light-attach}
Attaches a dynamic light to an existing thing.
<pre><code class="language-scheme">(dyn-light-attach
  id : string?
  r, g, b : real?
  intensity, range : real?)
  ↵ boolean?
</code></pre>

### dyn-light-set-pos! {#dyn-light-set-pos}
Moves an existing dynamic light to a new world position.
<pre><code class="language-scheme">(dyn-light-set-pos!
  id : string?
  x, y, z : real?)
  ↵ boolean?
</code></pre>

### dyn-light-set-hsv! {#dyn-light-set-hsv}
Sets an existing dynamic light's color via hue/saturation/value.
<pre><code class="language-scheme">(dyn-light-set-hsv!
  id : string?
  h, s, v : real?)
  ↵ boolean?
</code></pre>

## Actors {#script-actors}

### actor-spawn {#actor-spawn}
Spawns a package-driven actor.
<pre><code class="language-scheme">(actor-spawn
  id : string?
  x, y, z : real?
  yaw : real?
  kind : symbol?
  path : string?
  [radius, height] : real?
  [speed, gravity] : real?
  [tags-list] : (listof string?))
  ↵ boolean?
</code></pre>

### actor-pos {#actor-pos}
Returns an actor's world position as `(x y z)`.
<pre><code class="language-scheme">(actor-pos
  id : string?) ↵ (list real? real? real?)
</code></pre>

### actor-yaw {#actor-yaw}
Returns an actor's yaw, in radians.
<pre><code class="language-scheme">(actor-yaw
  id : string?) ↵ real?
</code></pre>

### actor-set-wish {#actor-set-wish}
Sets an actor's horizontal movement wish vector.
<pre><code class="language-scheme">(actor-set-wish
  id : string?
  wx, wz : real?)
  ↵ boolean?
</code></pre>

### actor-set-wish-3d {#actor-set-wish-3d}
Sets an actor's movement wish vector, including the vertical component (for flying actors).
<pre><code class="language-scheme">(actor-set-wish-3d
  id : string?
  wx, wy, wz : real?)
  ↵ boolean?
</code></pre>

### actor-set-move-speed! {#actor-set-move-speed}
Overrides an actor's move speed.
<pre><code class="language-scheme">(actor-set-move-speed!
  id : string?
  speed : real?)
  ↵ boolean?
</code></pre>

### actor-set-corpse! {#actor-set-corpse}
Converts an actor into a non-colliding corpse, kept for visuals.
<pre><code class="language-scheme">(actor-set-corpse!
  id : string?) ↵ boolean?
</code></pre>

### sprite-hide-part! {#sprite-hide-part}
Hides a named part of an actor's multi-part sprite.
<pre><code class="language-scheme">(sprite-hide-part!
  id : string?
  part-name : string?)
  ↵ boolean?
</code></pre>

### actor-grounded? {#actor-grounded}
Checks whether an actor is currently grounded.
<pre><code class="language-scheme">(actor-grounded?
  id : string?) ↵ boolean?
</code></pre>

### actor-submerged? {#actor-submerged}
Checks whether an actor is currently submerged in water.
<pre><code class="language-scheme">(actor-submerged?
  id : string?) ↵ boolean?
</code></pre>

### actor-near-water-exit? {#actor-near-water-exit}
Checks whether a submerged actor is near a water surface it can climb out at.
<pre><code class="language-scheme">(actor-near-water-exit?
  id : string?) ↵ boolean?
</code></pre>

### actor-water-exit-dir {#actor-water-exit-dir}
Returns the direction toward the nearest water exit for a submerged actor, or \#f if none applies.
<pre><code class="language-scheme">(actor-water-exit-dir
  id : string?) ↵ (or (list real? real? real?) \#f)
</code></pre>

### actor-play-anim {#actor-play-anim}
Plays an animation clip on an actor.
<pre><code class="language-scheme">(actor-play-anim
  id : string?
  clip : string?
  [loop] : boolean? = \#t)
  ↵ boolean?
</code></pre>

### actor-tags {#actor-tags}
Returns an actor's tags.
<pre><code class="language-scheme">(actor-tags
  id : string?) ↵ (listof string?)
</code></pre>

### actors-with-tag {#actors-with-tag}
Returns the ids of all actors carrying a specific tag.
<pre><code class="language-scheme">(actors-with-tag
  tag : string?) ↵ (listof string?)
</code></pre>

### actors-in-radius {#actors-in-radius}
Returns the ids of all actors within a radius of a point, optionally filtered by tag.
<pre><code class="language-scheme">(actors-in-radius
  x, y, z : real?
  r : real?
  [tag] : string?)
  ↵ (listof string?)
</code></pre>

## World queries {#script-world-queries}

### los? {#los}
Checks unobstructed line of sight between two world points.
<pre><code class="language-scheme">(los?
  x0, y0, z0 : real?
  x1, y1, z1 : real?)
  ↵ boolean?
</code></pre>

### volume-floor-at {#volume-floor-at}
Probes downward for the nearest floor height below a point.
<pre><code class="language-scheme">(volume-floor-at
  x, y, z : real?
  [max-dist] : real?)
  ↵ (or real? \#f)
</code></pre>

### volume-ceiling-at {#volume-ceiling-at}
Probes upward for the nearest ceiling height above a point.
<pre><code class="language-scheme">(volume-ceiling-at
  x, y, z : real?
  [max-dist] : real?)
  ↵ (or real? \#f)
</code></pre>

### volume-clearance {#volume-clearance}
Measures the vertical clearance around a point.
<pre><code class="language-scheme">(volume-clearance
  x, y, z : real?
  half-height : real?
  [max-dist] : real?)
  ↵ (or real? \#f)
</code></pre>

### debug-line-add! {#debug-line-add}
Queues a colored debug line for the current frame.
<pre><code class="language-scheme">(debug-line-add!
  x0, y0, z0 : real?
  x1, y1, z1 : real?
  r, g, b : real?
  [a] : real? = 255)
  ↵ boolean?
</code></pre>

### debug-lines-clear! {#debug-lines-clear}
Clears all queued debug lines.
<pre><code class="language-scheme">(debug-lines-clear!) ↵ boolean?
</code></pre>

### pvs-can-see {#pvs-can-see}
Checks whether two points' BSP leaves are mutually visible in the map's precomputed PVS.
<pre><code class="language-scheme">(pvs-can-see
  x0, y0, z0 : real?
  x1, y1, z1 : real?)
  ↵ boolean?
</code></pre>

### sound-emit! {#sound-emit}
Floods a gameplay sound outward through the map's navigation graph; returns `(id perceived-loudness)` pairs for every actor that could hear it.
<pre><code class="language-scheme">(sound-emit!
  x, y, z : real?
  loudness : real?
  [falloff-per-unit] : real? = 1.0)
  ↵ (listof (list string? real?))
</code></pre>

### hitscan-actors {#hitscan-actors}
Linescans for the nearest actor hit along a ray; returns `(id part distance x y z)` or \#f.
<pre><code class="language-scheme">(hitscan-actors
  ox, oy, oz : real?
  dx, dy, dz : real?
  max-distance : real?
  [tag] : string?
  [xz-only?] : boolean?)
  ↵ (or (list string? string? real? real? real? real?) \#f)
</code></pre>

### linescan-world {#linescan-world}
Linescans world geometry along a ray; returns `(x y z nx ny nz distance)` or \#f.
<pre><code class="language-scheme">(linescan-world
  ox, oy, oz : real?
  dx, dy, dz : real?
  max-distance : real?)
  ↵ (or (list real? real? real? real? real? real? real?) \#f)
</code></pre>

## Sight {#script-sight}

### actor-los? {#actor-los}
Checks line of sight between two actors.
<pre><code class="language-scheme">(actor-los?
  from-id, to-id : string?) ↵ boolean?
</code></pre>

### actor-sight-set! {#actor-sight-set}
Configures an actor's `ActorSight` scanner (range, FOV, tags, filter procedure, etc.).
<pre><code class="language-scheme">(actor-sight-set!
  id : string?
  alist : alist?)
  ↵ boolean?
</code></pre>

### actor-sight-get {#actor-sight-get}
Returns an actor's current sight configuration as an association list.
<pre><code class="language-scheme">(actor-sight-get
  id : string?) ↵ alist?
</code></pre>

### actor-can-see? {#actor-can-see}
Checks whether one actor's sight scanner currently sees another actor.
<pre><code class="language-scheme">(actor-can-see?
  from, to : string?) ↵ boolean?
</code></pre>

### sight-budget {#sight-budget}
Returns the maximum number of line-of-sight traces run per frame.
<pre><code class="language-scheme">(sight-budget) ↵ integer?
</code></pre>

### sight-budget-set! {#sight-budget-set}
Sets the maximum number of line-of-sight traces run per frame.
<pre><code class="language-scheme">(sight-budget-set!
  n : integer?) ↵ boolean?
</code></pre>

## Movers {#script-movers}

### mover-open {#mover-open}
Opens a mover.
<pre><code class="language-scheme">(mover-open
  id : string?) ↵ boolean?
</code></pre>

### mover-close {#mover-close}
Closes a mover.
<pre><code class="language-scheme">(mover-close
  id : string?) ↵ boolean?
</code></pre>

### mover-toggle {#mover-toggle}
Toggles a mover between open and closed.
<pre><code class="language-scheme">(mover-toggle
  id : string?) ↵ boolean?
</code></pre>

### mover-open-group {#mover-open-group}
Opens every mover in a named group.
<pre><code class="language-scheme">(mover-open-group
  g : string?) ↵ boolean?
</code></pre>

### mover-close-group {#mover-close-group}
Closes every mover in a named group.
<pre><code class="language-scheme">(mover-close-group
  g : string?) ↵ boolean?
</code></pre>

### mover-toggle-group {#mover-toggle-group}
Toggles every mover in a named group.
<pre><code class="language-scheme">(mover-toggle-group
  g : string?) ↵ boolean?
</code></pre>

### mover-set-locked {#mover-set-locked}
Locks or unlocks a mover.
<pre><code class="language-scheme">(mover-set-locked
  id : string?
  bool : boolean?)
  ↵ boolean?
</code></pre>

### mover-locked? {#mover-locked}
Checks whether a mover is locked.
<pre><code class="language-scheme">(mover-locked?
  id : string?) ↵ boolean?
</code></pre>

### mover-progress {#mover-progress}
Returns a mover's progress between closed (`0`) and open (`1`).
<pre><code class="language-scheme">(mover-progress
  id : string?) ↵ real?
</code></pre>

### mover-state {#mover-state}
Returns a mover's full state as an association list.
<pre><code class="language-scheme">(mover-state
  id : string?) ↵ alist?
</code></pre>

### mover-set-state {#mover-set-state}
Sets a mover's open/closed target, progress, and optionally its locked state.
<pre><code class="language-scheme">(mover-set-state
  id : string?
  open? : boolean?
  progress : real?
  [locked?] : boolean?)
  ↵ boolean?
</code></pre>

## Navigation {#navigation}

### nav-set-goal-pos {#nav-set-goal-pos}
Sets an actor's pathfinding goal to a fixed ground position.
<pre><code class="language-scheme">(nav-set-goal-pos
  id : string?
  x, z : real?)
  ↵ boolean?
</code></pre>

### nav-set-goal-entity {#nav-set-goal-entity}
Sets an actor's pathfinding goal to follow another entity.
<pre><code class="language-scheme">(nav-set-goal-entity
  id, target-id : string?) ↵ boolean?
</code></pre>

### nav-clear-goal {#nav-clear-goal}
Clears an actor's pathfinding goal.
<pre><code class="language-scheme">(nav-clear-goal
  id : string?) ↵ boolean?
</code></pre>

### nav-waypoint {#nav-waypoint}
Returns an actor's current waypoint as `(x y z)`, or \#f if it has no path.
<pre><code class="language-scheme">(nav-waypoint
  id : string?) ↵ (or (list real? real? real?) \#f)
</code></pre>

### nav-has-path? {#nav-has-path}
Checks whether an actor currently has a planned path.
<pre><code class="language-scheme">(nav-has-path?
  id : string?) ↵ boolean?
</code></pre>

### nav-path-direction {#nav-path-direction}
Returns the normalized direction from an actor toward its current waypoint (or, once past the last waypoint, straight at the goal), or \#f if the actor has no path or its goal is known-unreachable.
<pre><code class="language-scheme">(nav-path-direction
  id : string?) ↵ (or (list real? real? real?) \#f)
</code></pre>

### nav-path-altitude {#nav-path-altitude}
Returns the vertical distance from an actor to its next waypoint.
<pre><code class="language-scheme">(nav-path-altitude
  id : string?) ↵ real?
</code></pre>

### nav-leaf-at {#nav-leaf-at}
Returns the navigation leaf index containing a world point.
<pre><code class="language-scheme">(nav-leaf-at
  x, y, z : real?) ↵ integer?
</code></pre>

### nav-leaf-bounds {#nav-leaf-bounds}
Returns a navigation leaf's AABB.
<pre><code class="language-scheme">(nav-leaf-bounds
  leaf : integer?) ↵ (list real? real? real? real? real? real?)
</code></pre>

### nav-leaf-floor {#nav-leaf-floor}
Returns a navigation leaf's floor height.
<pre><code class="language-scheme">(nav-leaf-floor
  leaf : integer?) ↵ real?
</code></pre>

### nav-leaf-ceiling {#nav-leaf-ceiling}
Returns a navigation leaf's ceiling height.
<pre><code class="language-scheme">(nav-leaf-ceiling
  leaf : integer?) ↵ real?
</code></pre>

### nav-at-goal? {#nav-at-goal}
Checks whether an actor has reached its pathfinding goal.
<pre><code class="language-scheme">(nav-at-goal?
  id : string?) ↵ boolean?
</code></pre>

## Hooks {#hooks}

The base package defines owner procedures with exact names below. Mods extend them using `(hook-add 'name proc)`.

### prepare-first-person {#prepare-first-person}
Called after the first-person scene exists, on map spawn.
<pre><code class="language-scheme">(prepare-first-person
  player-id : string?)
</code></pre>

### on-map-ready {#on-map-ready}
Called after `prepare-first-person`, on map spawn.
<pre><code class="language-scheme">(on-map-ready
  map-id, reason : string?)
</code></pre>

### on-startup {#on-startup}
Called once, after the player, menus, and mod contributions have loaded.
<pre><code class="language-scheme">(on-startup)
</code></pre>

### draw-file-menu {#draw-file-menu}
Called inside the File menu, before the Quit item.
<pre><code class="language-scheme">(draw-file-menu)
</code></pre>

### draw-pause-menu {#draw-pause-menu}
Called inside the Pause menu, after the Resume item.
<pre><code class="language-scheme">(draw-pause-menu)
</code></pre>

### draw-debug-menu {#draw-debug-menu}
Called inside the debug overlay menu.
<pre><code class="language-scheme">(draw-debug-menu)
</code></pre>

### draw-modals {#draw-modals}
Called every UI frame, for package-owned popups.
<pre><code class="language-scheme">(draw-modals)
</code></pre>

### tick {#tick}
Called every update frame, with the frame delta time in seconds.
<pre><code class="language-scheme">(tick
  dt : real?)
</code></pre>

### draw-hud {#draw-hud}
Called when the HUD render pass runs.
<pre><code class="language-scheme">(draw-hud)
</code></pre>

### draw-title {#draw-title}
Called every menu frame, for title screen chrome.
<pre><code class="language-scheme">(draw-title)
</code></pre>

### on-sprite-hint {#on-sprite-hint}
Called when a `.spanim` hold cue with `(hint "name")` is entered.
<pre><code class="language-scheme">(on-sprite-hint
  source, name : string?)
</code></pre>

### on-sight {#on-sight}
Called when an enabled `ActorSight` scanner acquires line of sight on a target.
<pre><code class="language-scheme">(on-sight
  observer-id, target-id : string?)
</code></pre>

### sight-filter {#sight-filter}
Optional veto called during sight scans; return \#f to reject a candidate target.
<pre><code class="language-scheme">(sight-filter
  observer-id, target-id : string?) ↵ boolean?
</code></pre>

### on-action-{id} {#on-action-id}
Called when the package action `{id}` is pressed.
<pre><code class="language-scheme">(on-action-{id})
</code></pre>

### on-use-{name} {#on-use-name}
Called from a map's `(on-use "...")` binding.
<pre><code class="language-scheme">(on-use-{name}
  thing-id : string?)
</code></pre>

### trigger enter/exit handlers {#trigger-enter-exit-handlers}
Bound per-thing via a map thing's `(on-enter …)` / `(on-exit …)` fields; called when something enters or exits a trigger volume.
<pre><code class="language-scheme">(handler-proc
  thing-id, other-id : string?)
</code></pre>

### use handlers {#use-handlers}
Bound per-thing via a map usable/pickup/mover's `(on-use …)` field; called when the thing is used.
<pre><code class="language-scheme">(handler-proc
  thing-id : string?)
</code></pre>

### touch handlers {#touch-handlers}
Bound per-face via a CSG face's `(on-touch …)` field; called when something touches the face.
<pre><code class="language-scheme">(handler-proc
  thing-id, other-id : string?)
</code></pre>
