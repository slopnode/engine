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
present|First-person presentation every frame, on clients too|`fp-present`,`audio`,`input-query`,`read-world`
ui|UI (non-hud) rendering|`ui-draw`,`save-io`,`map-control`,`package-load`,`read-world`,`startup-query`
mapauthor|Loading map files|none

## Roles {#roles}

Role|Description
-|-
Engine|Internal engine components
Base|Has full access to most capabilities
Mod|Limited to read-only and presentation capabilities

A procedure's role comes from the package whose script or data file defined it. Mod code can't take over a procedure that the engine, the base package, or s7 itself defined. If a mod redefines or `set!`s one, the engine restores the original as soon as that mod call returns, and before any non-mod code runs, then logs a warning naming the procedure. Mods extend hooks with `hook-add`. A procedure registered through `hook-add` while mod code is running gets the Mod role, whichever file registered it.

A base registry that stores procedures and calls them later passes each one through `as-caller-role` when it is registered. A procedure registered by mod code then runs with the Mod role whoever calls it. Base code calling a stored mod procedure that skipped `as-caller-role` runs it with Base rights.

The disabled file, eval, environment, and process primitives stay disabled when reached through `#_name` or `(unlet)`. `openlet`, `symbol-access`, and `procedure-setter` are disabled, so a value or variable can't carry code that runs when base code reads, sets, or does arithmetic on it. A mod's changes to s7's own hooks, such as `*error-hook*` and `*unbound-variable-hook*`, are undone the same way as rebound procedures.

Two s7 features can still carry mod code inside plain data, a hash table made with custom equality procedures and a closure used as an iterator. Base code that walks or looks up data a mod built can run that code with Base rights.

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

### as-caller-role {#as-caller-role}
Wraps a procedure so it runs with the Mod role when mod code is the caller of `as-caller-role`. Returns the procedure unchanged when called from engine or base code, or when given something that isn't a procedure.
<pre><code class="language-scheme">(as-caller-role
  proc : procedure?)
  ↵ procedure?
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

### players {#players}
Returns the ids of every player body, the map's `"Player"` and any a server spawned for clients. The `player-pose`, `player-velocity`, `player-grounded?`, `player-eye`, `player-look-dir`, `player-collider`, `player-set-collider` and `player-set-eye-height` functions take one of these ids.
<pre><code class="language-scheme">(players) ↵ (listof string?)
</code></pre>

### script-player {#script-player}
Returns the id of the player that `player-*` functions and `action-down?` act on when no id is given. It is `"Player"` unless a `with-script-player` call or an `on-action-*` hook for another body is running. First-person `fp-*` calls do nothing for any other player.
<pre><code class="language-scheme">(script-player) ↵ string?
</code></pre>

### with-script-player {#with-script-player}
Calls `thunk` with `script-player` set to `id`, then restores it, also on error. Returns `#f` without calling `thunk` when `id` names no player body.
<pre><code class="language-scheme">(with-script-player
  id : string?
  thunk : procedure?) ↵ any?
</code></pre>

### player-pose {#player-pose}
Returns the player's position and look as `(x y z yaw pitch)`.
<pre><code class="language-scheme">(player-pose
  [id : string? = (script-player)]) ↵ (list real? real? real? real? real?)
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
  h : real?
  [id : string? = (script-player)]) ↵ boolean?
</code></pre>

### player-collider {#player-collider}
Returns the player's current collider as `(radius height)`.
<pre><code class="language-scheme">(player-collider
  [id : string? = (script-player)]) ↵ (list real? real?)
</code></pre>

### player-set-collider {#player-set-collider}
Sets the player's collider radius and height.
<pre><code class="language-scheme">(player-set-collider
  radius, height : real?
  [id : string? = (script-player)]) ↵ boolean?
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

### player-velocity {#player-velocity}
Returns the player's physics velocity as `(vx vy vz)`, in world units per second. Physics runs on a fixed step, so this is steadier than differencing `player-pose` between frames.
<pre><code class="language-scheme">(player-velocity
  [id : string? = (script-player)]) ↵ (list real? real? real?)
</code></pre>

### player-grounded? {#player-grounded}
Checks whether the player's character motor is on the ground.
<pre><code class="language-scheme">(player-grounded?
  [id : string? = (script-player)]) ↵ boolean?
</code></pre>

### player-jump! {#player-jump}
Launches the player upward at `speed` meters per second if they are standing on something, keeping their horizontal velocity. Returns \#f when the player is airborne or has no physics body.
<pre><code class="language-scheme">(player-jump!
  speed : real?) ↵ boolean?
</code></pre>

### player-set-gravity! {#player-set-gravity}
Sets the downward acceleration of the player's character motor in meters per second squared. The default is 9.81. Raising it together with the `player-jump!` speed gives a jump of the same height with less hang time. Returns \#f when there is no player.
<pre><code class="language-scheme">(player-set-gravity!
  gravity : real?) ↵ boolean?
</code></pre>

### player-move-speed {#player-move-speed}
Returns the top horizontal speed of the player's character motor in meters per second, or \#f when there is no player.
<pre><code class="language-scheme">(player-move-speed) ↵ real?
</code></pre>

### player-set-move-speed! {#player-set-move-speed}
Sets the top horizontal speed of the player's character motor in meters per second. A map load resets it to the package or player-start value. Sprinting is this call with a larger speed while a key is held. Returns \#f when there is no player.
<pre><code class="language-scheme">(player-set-move-speed!
  speed : real?) ↵ boolean?
</code></pre>

### player-wish-speed {#player-wish-speed}
Returns the current wish-vector speed, before physics resolves collisions.
<pre><code class="language-scheme">(player-wish-speed) ↵ real?
</code></pre>

### player-eye {#player-eye}
Returns the player's eye-space world position as `(x y z)`.
<pre><code class="language-scheme">(player-eye
  [id : string? = (script-player)]) ↵ (list real? real? real?)
</code></pre>

### player-look-dir {#player-look-dir}
Returns the player's normalized look direction as `(x y z)`.
<pre><code class="language-scheme">(player-look-dir
  [id : string? = (script-player)]) ↵ (list real? real? real?)
</code></pre>

## Net state {#net-state-api}

Fields declared in `data/net.s7` (see @ref dataapi). An id is a player id, which is `"Player"` for the local player, a thing id, or the symbol `'session`.

### net-get {#net-get}
Returns a declared field's value. Raises a `net` error for a field the target doesn't declare, and returns `\#f` for an unknown target.
<pre><code class="language-scheme">(net-get
  id : (or string? 'session)
  field : symbol?) ↵ any?
</code></pre>

### net-set! {#net-set}
Stores a value in a declared field. Raises a `net` error when the value doesn't fit the field's type. Integer fields round real values, and real fields snap to their step.
<pre><code class="language-scheme">(net-set!
  id : (or string? 'session)
  field : symbol?
  value : any?) ↵ boolean?
</code></pre>

### net-reset! {#net-reset}
Resets every player and session field to its default, for a new game.
<pre><code class="language-scheme">(net-reset!) ↵ boolean?
</code></pre>

### net-save-state {#net-save-state}
Returns every field marked `save` as `((session (field . value) ...) (player "Player" (field . value) ...))`.
<pre><code class="language-scheme">(net-save-state) ↵ list?
</code></pre>

### net-restore-state! {#net-restore-state}
Applies a list from `net-save-state`. Unknown fields and values that no longer fit are skipped. Returns how many fields were applied.
<pre><code class="language-scheme">(net-restore-state!
  state : list?) ↵ integer?
</code></pre>

### net-event! {#net-event}
Raises a declared event. Runs `(on-event-NAME where arg ...)` here, and on a server also sends the event to the client its audience allows. Raises a `net` error for an undeclared event, a wrong `where`, or arguments that don't match the declaration.
<pre><code class="language-scheme">(net-event!
  event : symbol?
  where : (or (list real? real? real?) string? \#f)
  arg : any? ...) ↵ boolean?
</code></pre>

### net-schema-hash {#net-schema-hash}
Returns the declared schema's hash as a hex string. Two installs with the same packages return the same hash.
<pre><code class="language-scheme">(net-schema-hash) ↵ string?
</code></pre>

### session-max-players {#session-max-players}
Returns how many players the session takes at once, the host included, or `#f` when this process isn't hosting.
<pre><code class="language-scheme">(session-max-players) ↵ (or integer? #f)
</code></pre>

### session-set-max-players! {#session-set-max-players}
Sets how many players the session takes, clamped between 1 and the session kind's `players` maximum, or `--max-players` when that is lower, and returns the new value. Players already in stay. Returns `#f` when this process isn't hosting.
<pre><code class="language-scheme">(session-set-max-players!
  n : integer?) ↵ (or integer? #f)
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
Attaches a geometry asset to a first-person socket, with an optional local offset, scale, and rotation in radians. When the arguments are left out the placement comes from the geo's `(view ...)` block, which slopmodel edits (see @ref filegeo). A skinned geo gets an animation player over its skeleton's clip bank, so a hands rig attached here is driven with `fp-play-anim` and its clip events reach `on-anim-event` with the socket name as the source.
<pre><code class="language-scheme">(fp-attach-geo
  socket : string?
  geo : string?
  [x, y, z] : real? = 0
  [sx, sy, sz] : real? = 1
  [pitch, yaw, roll] : real? = 0)
  ↵ boolean?
</code></pre>

### fp-play-anim {#fp-play-anim}
Plays a clip on the skinned geo attached to a socket, or on geo hung from it. `path` is the socket name, optionally followed by attachment names such as `weapon/held`. Returns \#f when the path names no skinned geo or the bank has no such clip.
<pre><code class="language-scheme">(fp-play-anim
  path : string?
  clip : string?
  [loop?] : boolean? = \#f)
  ↵ boolean?
</code></pre>

### fp-anim-busy? {#fp-anim-busy}
Checks whether the skinned geo `path` names is still mid-way through a non-looping clip.
<pre><code class="language-scheme">(fp-anim-busy?
  path : string?) ↵ boolean?
</code></pre>

### fp-attach-held {#fp-attach-held}
Hangs a geo on a bone of the geo attached to a first-person socket, the held weapon on a hands rig. `path` is the socket name, or a path such as `weapon/held` to hang the geo on the held geo instead. `point` is a bone name or the name of an attachment declared in the host geo's `(attachments ...)` block, and the offset, rotation in radians and uniform scale apply relative to that point. A skinned geo gets an animation player over its skeleton's clip bank, driven with `fp-play-anim` on its path. A second call with the same `name` on the same host replaces the earlier one, along with anything hung on it.
<pre><code class="language-scheme">(fp-attach-held
  path : string?
  name : string?
  point : string?
  geo : string?
  [x y z] : real? = 0
  [pitch yaw roll] : real? = 0
  [scale] : real? = 1)
  ↵ boolean?
</code></pre>

### fp-detach-held {#fp-detach-held}
Removes a geo hung with `fp-attach-held`, along with anything hung on it. `path` names the host it hangs on.
<pre><code class="language-scheme">(fp-detach-held
  path : string?
  name : string?) ↵ boolean?
</code></pre>

### fp-point {#fp-point}
Returns where a point on a first-person geo is in the world, as `(x y z)`, or `#f` when `path` names nothing. `path` names a socket's geo or one hung under it, such as `"weapon/held"`, and the point is in that geo's own space, the origin when left out. A weapon's muzzle measured on its geo gives a world-space start for `tracer-spawn` that follows the gun through recoil and sway.
<pre><code class="language-scheme">(fp-point
  path : string?
  [x, y, z] : real?)
  ↵ list? or #f
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
Spawns a dynamic light as a child of a first-person socket, replacing anything already under it. The light starts off and casts shadows. View space has x right, y up and z forward, and a spot points along +z. The `"light"` socket sits on the eye and does not follow the weapon's bob or drop, which suits a flashlight.
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
Toggles whether viewmodels take the map's baked light. In a lit map, first-person geo is then lit per pixel like world models, from the probe grid and nearby static lights, and view sprites are tinted by the probe.
<pre><code class="language-scheme">(fp-set-rad-tint
  enabled : boolean?) ↵ boolean?
</code></pre>

### fp-set-shading {#fp-set-shading}
Toggles the viewmodel faux-lighting shader, a fixed key and rim light. First-person geo uses it only when the map has no baked lighting or rad tint is off.
<pre><code class="language-scheme">(fp-set-shading
  enabled : boolean?) ↵ boolean?
</code></pre>

### fp-set-eye-offset {#fp-set-eye-offset}
Sets the package's view-space eye offset, in meters.
<pre><code class="language-scheme">(fp-set-eye-offset
  x, y, z : real?) ↵ boolean?
</code></pre>

### fp-set-view-roll {#fp-set-view-roll}
Rolls the presentation camera about its view direction, in radians. A positive roll tilts the view toward the right. First-person geo is drawn from its own unrolled camera, so it stays put on screen while the world tilts, the way a Quake strafe roll looks.
<pre><code class="language-scheme">(fp-set-view-roll
  roll : real?) ↵ boolean?
</code></pre>

### fp-set-view-offset {#fp-set-view-offset}
Moves the whole first-person scene in view space, in meters, with +z pointing forward. Weapon bob is driven by setting it every frame.
<pre><code class="language-scheme">(fp-set-view-offset
  x, y, z : real?) ↵ boolean?
</code></pre>

### fp-recoil-tune! {#fp-recoil-tune}
Sets how the player's recoil springs behave, usually once per weapon when it is raised. Keys left out take their defaults. `aim-*` tunes the aim punch, `kick-*` the first-person scene sliding back, lifting and sideways, and `twist-*` its rise, turn and roll about `pivot`, a view-space point in meters. Frequencies are in hertz and dampings are ratios from 0.05 to 1. `climb-time` is how long in seconds climb takes to reach the look angles. `recovery` is the fraction of climb that returns once `recovery-delay` seconds pass without a kick, over about `recovery-time` seconds. `shake-angle` is the largest camera shake in degrees, `shake-decay` how much shake fades per second and `shake-frequency` how fast it wobbles.
<pre><code class="language-scheme">(fp-recoil-tune!
  alist : alist?) ↵ boolean?
</code></pre>

Defaults:

| Key | Default |
| --- | --- |
| `aim-frequency`, `aim-damping` | 9, 0.7 |
| `kick-frequency`, `kick-damping` | 12, 0.5 |
| `twist-frequency`, `twist-damping` | 9, 0.45 |
| `climb-time` | 0.04 |
| `recovery`, `recovery-delay`, `recovery-time` | 0, 0.12, 0.25 |
| `pivot` | 0 -0.08 0.25 |
| `shake-angle`, `shake-decay`, `shake-frequency` | 1.4, 3, 16 |

<pre><code class="language-scheme">(fp-recoil-tune!
  '((aim-frequency . 10) (kick-frequency . 14) (recovery . 0.45)
    (pivot 0.0 -0.1 0.2)))
</code></pre>

### fp-recoil-kick! {#fp-recoil-kick}
Adds one shot's recoil. Angles are degrees and lengths are meters, and each spring amount is roughly the peak a single kick reaches from rest. `punch-up` and `punch-right` jolt the aim and spring back. `climb-up` and `climb-right` move the aim for good, eased in over `climb-time`, for the player to pull back down. Both change where `player-look-dir` points, while mouse look keeps working on top. `back`, `lift` and `side` move the first-person scene, and `rise`, `turn` and `roll` swing it about the pivot. `shake` adds camera shake from 0 to 1, which moves only the world view, not the aim or the first-person scene.
<pre><code class="language-scheme">(fp-recoil-kick!
  alist : alist?) ↵ boolean?
</code></pre>

<pre><code class="language-scheme">(fp-recoil-kick!
  '((punch-up . 0.7) (climb-up . 0.55) (climb-right . 0.1)
    (back . 0.02) (rise . 2.2) (roll . -0.8) (shake . 0.06)))
</code></pre>

### fp-recoil-reset! {#fp-recoil-reset}
Settles every recoil spring, pending climb and shake at once, keeping the tuning. Aim already moved by punch or climb stays where it is.
<pre><code class="language-scheme">(fp-recoil-reset!) ↵ boolean?
</code></pre>

### fp-view-punch! {#fp-view-punch}
Snaps the presented camera by `up`, `right` and `roll` degrees, like Half-Life's `punchangle`, replacing any punch still in progress. The aim and the viewmodel stay put. The punch shrinks back to nothing along its own direction by 10 degrees a second plus half its size a second, times `recover`, so a big punch snaps back fast and the last few degrees settle more slowly. A `recover` of 1 is Half-Life's rate.
<pre><code class="language-scheme">(fp-view-punch!
  up, right, roll : real?
  [recover] : real? = 1) ↵ boolean?
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

### hud-text-align {#hud-text-align}
Sets how subsequent `hud-text` calls sit on their x position: `'left` starts the text there, `'right` ends it there and `'center` centers it. Resets to `'left` every frame.
<pre><code class="language-scheme">(hud-text-align
  symbol : symbol?) ↵ boolean?
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
Spawns a motor-driven projectile (rocket, fireball, etc.) that integrates against static geometry and actors. A `'geo` projectile turns its +Z along its velocity every step, upright with no roll.
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

### particle-spawn-bone {#particle-spawn-bone}
Spawns a particle system on a bone joint of a rigged thing, an actor or a severed part, and keeps it there as the bone moves. It points out of the limb, from the middle of the bone's hitbox through the joint, or out of the stump with `'stump`, from the middle of the parent's hitbox through the joint, which suits a joint whose bone was cut off. Emitters need `(orient #t)` to spray that way. The system stops when its host is removed.
<pre><code class="language-scheme">(particle-spawn-bone
  id, target, bone, path : string?
  [mode] : symbol? = 'limb)
  ↵ boolean?
</code></pre>

### particle-spawn-follow {#particle-spawn-follow}
Spawns a particle system `back` metres behind any thing, such as a projectile, and keeps it there, aimed out of the thing's -Z. Emitters need `(orient #t)` to spray that way. When the host is removed the system stops emitting, its live particles finish their lifetimes, and then it despawns, so a smoke trail outlives the rocket that left it.
<pre><code class="language-scheme">(particle-spawn-follow
  id, target, path : string?
  [back] : real? = 0)
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

### tracer-spawn {#tracer-spawn}
Fires a tracer streak from one world point to another. It is drawn additively, travels at `speed` meters per second, and is gone once its tail reaches the end point. It is only a visual and hits nothing, so a hitscan weapon traces its shot as usual and spawns a tracer from the muzzle to the hit point. Style keys left out take their defaults. `length` is the streak length and `width` the half width of its bright core in meters, with `glow-width` the half width of the softer glow around it. `min-pixels` keeps the core at least that many pixels wide however far away it is. The streak brightens from `tail-alpha` at its tail to full at its head. `lead` starts the head that many meters along the path. Colors are `r g b` with an optional alpha, from 0 to 255.
<pre><code class="language-scheme">(tracer-spawn
  sx, sy, sz : real?
  ex, ey, ez : real?
  [style] : alist?)
  ↵ boolean?
</code></pre>

| Key | Default |
| --- | --- |
| `speed` | 240 |
| `length` | 1.8 |
| `width`, `glow-width` | 0.02, 0.09 |
| `min-pixels` | 1.5 |
| `tail-alpha` | 0 |
| `color` | 255 240 190 255 |
| `glow-color` | 255 140 30 150 |
| `lead` | 0 |

<pre><code class="language-scheme">(let ((muzzle (fp-point "weapon/held" 0.0 0.076 0.692)))
  (tracer-spawn (car muzzle) (cadr muzzle) (caddr muzzle) hx hy hz
                '((speed . 280) (length . 2.2) (color 255 240 180))))
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
Spawns a package-driven actor. `kind` is `"sprite"` or `"geo"`.
<pre><code class="language-scheme">(actor-spawn
  id : string?
  x, y, z : real?
  yaw : real?
  kind : string?
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

### actor-set-yaw! {#actor-set-yaw}
Turns an actor to face a yaw, in radians. A sprite actor's facing and a geo actor's rotation are both set, and a geo actor otherwise keeps the yaw it was placed with.
<pre><code class="language-scheme">(actor-set-yaw!
  id : string?
  yaw : real?)
  ↵ boolean?
</code></pre>

### actor-set-bone-scale! {#actor-set-bone-scale}
Changes the proportions of one bone on a skinned geo actor, on top of whatever clip it plays. `size` scales the bone's own part and pushes the joints of its children out by the same amount, while the children keep their size. `length` stretches the part and its children's joints along the bone only. A size and length of 1 remove the bone's scale. When a leg changes, the whole body is raised or lowered so the lowest foot stays on the ground. Returns \#f when the actor has no such bone.
<pre><code class="language-scheme">(actor-set-bone-scale!
  id : string?
  bone : string?
  size : real?
  [length : real? = 1])
  ↵ boolean?
</code></pre>

### actor-bone-scale {#actor-bone-scale}
Reads a bone's scale as `(size length)`, which is `(1 1)` for a bone without one. Returns \#f when the actor has no such bone.
<pre><code class="language-scheme">(actor-bone-scale
  id : string?
  bone : string?)
  ↵ (or list? boolean?)
</code></pre>

### actor-clear-bone-scales! {#actor-clear-bone-scales}
Removes every bone scale from an actor, including those its thing-def gave it.
<pre><code class="language-scheme">(actor-clear-bone-scales!
  id : string?)
  ↵ boolean?
</code></pre>

### actor-set-shader-param! {#actor-set-shader-param}
Sets a `vec4` uniform for one geo actor's draws, read by the model deform hook described on the @ref assets page. `name` is the uniform as the shader declares it, and an array element such as `"jiggle[2]"` works too. Missing components are 0. The value is uploaded just before the actor draws, in the world and shadow passes, and set back to zero afterwards, so other models never see it. Returns \#f when the actor has no geo.
<pre><code class="language-scheme">(actor-set-shader-param!
  id : string?
  name : string?
  [x y z w : real? = 0])
  ↵ boolean?
</code></pre>

### actor-clear-shader-params! {#actor-clear-shader-params}
Removes every shader param from an actor.
<pre><code class="language-scheme">(actor-clear-shader-params!
  id : string?)
  ↵ boolean?
</code></pre>

### actor-sever-bone! {#actor-sever-bone}
Cuts a bone and everything below it off a rigged actor. The skin is split where the cut bones and the rest carry equal weight, and both open ends are closed with a stump using the `fx/stump` material, which a game can override. A new entity carrying just that part takes over from the same pose and flies off as a small ragdoll, so a cut arm still bends at the elbow. It collides only with world geometry, not with characters, other severed parts, linescans or sight. A part whose bones have no vertices falls back to a single tumbling box. Geo hung on the cut bones, such as a held weapon, goes with the part. `dx dy dz` is the push direction, `speed` its speed in meters a second plus a little lift, `spin` how fast in radians a second it tumbles end over end away from the push, with some random wobble on top and `lifetime` how many seconds the part stays. The oldest part is removed once 48 exist. Returns the part's id, or \#f when the bone is unknown or already cut.
<pre><code class="language-scheme">(actor-sever-bone!
  id, bone : string?
  [dx dy dz] : real? = 0
  [speed] : real? = 3
  [spin] : real? = 6
  [lifetime] : real? = 20)
  ↵ (or string? \#f)
</code></pre>

### actor-hide-bone! {#actor-hide-bone}
Hides a bone and everything below it on a rigged actor, the way `actor-sever-bone!` does, but drops nothing. The hidden bones collapse, take no hits and get no parts in a later `actor-ragdoll!`, and `actor-restore-bones!` brings them back. Useful for props built into a rig that should vanish when the actor dies. Returns \#f when the bone is unknown or already hidden.
<pre><code class="language-scheme">(actor-hide-bone!
  id, bone : string?) ↵ boolean?
</code></pre>

### actor-ragdoll! {#actor-ragdoll}
Hands a rigged actor over to physics. Its animation stops, its collider is removed, it becomes a corpse, and each bone with vertices becomes a box from its hitbox, jointed to its parent with swing and twist limits picked from the bone's name, such as elbows, knees, shoulders and the neck. Bones without vertices follow their nearest jointed parent, and cut bones stay hidden. The whole body moves along `dx dy dz` at a third of `speed`, and `hit-bone`'s part, or its nearest jointed parent's, takes the full `speed`, so a shot knee kicks the leg out. The parts collide only with world geometry. At most 16 ragdolls simulate at once, the oldest freezing in place first, and each freezes after `lifetime` seconds. Returns \#f when the actor has no rig or is already a ragdoll.
<pre><code class="language-scheme">(actor-ragdoll!
  id : string?
  [dx dy dz] : real? = 0
  [speed] : real? = 3
  [hit-bone] : (or string? \#f) = \#f
  [lifetime] : real? = 30)
  ↵ boolean?
</code></pre>

### physics-blast! {#physics-blast}
Pushes every simulating ragdoll part and cut-off piece within `radius` of a point that a straight line from the point reaches past world geometry. Each body gains `speed` away from the point and `lift` upward, both fading linearly to zero at `radius`, and cut pieces also tumble. Frozen ragdolls and live actors are not moved, so kill or ragdoll actors first. Returns how many bodies moved.
<pre><code class="language-scheme">(physics-blast!
  x, y, z, radius, speed : real?
  [lift] : real? = 0)
  ↵ integer?
</code></pre>

### actor-bone-severed? {#actor-bone-severed}
Checks whether a bone of an actor has been cut off by `actor-sever-bone!`.
<pre><code class="language-scheme">(actor-bone-severed?
  id, bone : string?) ↵ boolean?
</code></pre>

### actor-restore-bones! {#actor-restore-bones}
Shows every bone `actor-sever-bone!` hid on an actor again and puts back its uncut skin. Parts already cut off stay where they are.
<pre><code class="language-scheme">(actor-restore-bones!
  id : string?) ↵ boolean?
</code></pre>

### actor-bone-pos {#actor-bone-pos}
Returns the world position of a bone's joint on a rigged actor as `(x y z)`, or \#f.
<pre><code class="language-scheme">(actor-bone-pos
  id, bone : string?)
  ↵ (or (list real? real? real?) \#f)
</code></pre>

### actor-bone-springs! {#actor-bone-springs}
Gives a rigged actor a set of bone springs the engine steps every frame while they are active. Each spec names a bone, the `vec4` shader param to drive (an array element such as `"jiggle[2]"` works, as with `actor-set-shader-param!`), and the spring's stiffness, damping, gain and limit. Each frame the engine samples the bone's world position, smooths its acceleration, drives a damped spring with it and clamps the offset to `limit` meters. The param receives the offset rotated into the actor's yaw frame as `xyz` and the spring's speed as `w`. The optional tuning list sets the integration substep, the longest frame time stepped, and the acceleration smoothing rate, defaulting to `1/120`, `0.1` and `20`. The springs start inactive. Calling this again replaces the set. Returns \#f when the actor has no geo.
<pre><code class="language-scheme">(actor-bone-springs!
  id : string?
  specs : (list (list bone param : string? stiffness damping gain limit : real?) ...)
  [tuning : (list step max-dt accel-smoothing : real?)])
  ↵ boolean?
</code></pre>

### actor-bone-springs-active! {#actor-bone-springs-active}
Starts or stops an actor's bone springs. Stopping writes zero to every spring's param once and resets the springs, so a restarted set settles in from rest. Returns \#f when the actor has no bone springs.
<pre><code class="language-scheme">(actor-bone-springs-active!
  id : string?
  active? : boolean?)
  ↵ boolean?
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

### actor-jump! {#actor-jump}
Launches a grounded actor upward at `speed` meters per second, keeping its horizontal velocity. Gravity and the actor's wish carry it from there. Returns \#f when the actor does not exist or is not standing on anything.
<pre><code class="language-scheme">(actor-jump!
  id : string?
  speed : real?)
  ↵ boolean?
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
Plays an animation clip on an actor or on geo attached to it. `path` is an actor id, optionally followed by attachment names, such as `guard/held` or `guard/held/sight`. Actor ids that themselves contain a slash, such as prefab ids, are matched first, and the longest id that exists wins. A sprite actor plays a `.spanim` clip, and a skinned geo actor or attachment plays a clip from its animation bank. Returns \#f when the path or the clip does not exist.
<pre><code class="language-scheme">(actor-play-anim
  path : string?
  clip : string?
  [loop] : boolean? = \#t)
  ↵ boolean?
</code></pre>

### actor-anim-busy? {#actor-anim-busy}
Returns \#t while a non-looping clip started by `actor-play-anim` is still playing on the actor or attachment `path` names.
<pre><code class="language-scheme">(actor-anim-busy?
  path : string?) ↵ boolean?
</code></pre>

### actor-attach-geo {#actor-attach-geo}
Pins a geo to a bone of a skinned geo actor, Half-Life attachment style. `path` is the actor id, or an attachment path such as `guard/held` to hang the geo on the held geo instead, which then needs bones of its own. `point` is a bone name or the name of an attachment declared in the host geo's `(attachments ...)` block (see @ref filegeo). The geo follows the bone's pose each frame with the offset applied relative to that point, pitch yaw roll in radians, and a uniform scale. A skinned geo gets an animation player over its skeleton's clip bank, driven with `actor-play-anim` on its path. A second call with the same `name` on the same host replaces the earlier attachment, along with anything attached to it.
<pre><code class="language-scheme">(actor-attach-geo
  path : string?
  name : string?
  point : string?
  geo : string?
  [x y z] : real? = 0
  [pitch yaw roll] : real? = 0
  [scale] : real? = 1)
  ↵ boolean?
</code></pre>

### actor-detach {#actor-detach}
Removes an attachment made by `actor-attach-geo`, along with anything attached to it. `path` names the host the attachment hangs on.
<pre><code class="language-scheme">(actor-detach
  path : string?
  name : string?) ↵ boolean?
</code></pre>

### avatars {#avatars}
Returns the ids of the stand-ins a network client spawned from `on-player-appear` for other players. Empty anywhere else.
<pre><code class="language-scheme">(avatars) ↵ (listof string?)
</code></pre>

### avatar-velocity {#avatar-velocity}
Returns a stand-in's velocity from its last two snapshots as `(x y z)`, or `#f` when `id` isn't a stand-in.
<pre><code class="language-scheme">(avatar-velocity
  id : string?) ↵ (or (list real? real? real?) #f)
</code></pre>

### avatar-play-anim {#avatar-play-anim}
`actor-play-anim` for a stand-in, allowed in `present` scope. Returns `#f` for anything that isn't a stand-in or attached to one.
<pre><code class="language-scheme">(avatar-play-anim
  path : string?
  clip : string?
  [loop : boolean? = #t]) ↵ boolean?
</code></pre>

### avatar-attach-geo {#avatar-attach-geo}
`actor-attach-geo` for a stand-in, allowed in `present` scope.
<pre><code class="language-scheme">(avatar-attach-geo
  path : string?
  name : string?
  point : string?
  geo : string?
  [x, y, z : real?]
  [pitch, yaw, roll : real?]
  [scale : real?]) ↵ boolean?
</code></pre>

### avatar-detach {#avatar-detach}
`actor-detach` for a stand-in, allowed in `present` scope.
<pre><code class="language-scheme">(avatar-detach
  path : string?
  name : string?) ↵ boolean?
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
Checks whether two points' BSP leaves are mutually visible in the map's precomputed PVS. Also false when a closed door separates the first point's area from the second's.
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
Linescans for the nearest actor hit along a ray; returns `(id part distance x y z)` or \#f. Sprite actors are hit on their billboard and `part` names the sprite part. Rigged geo actors are hit on per-bone boxes built from the vertices each bone carries most weight for, posed with the actor, and `part` names the bone. Bones cut off with `actor-sever-bone!` and corpses are skipped.
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

### mover-ids {#mover-ids}
Returns the ids of every mover in the loaded map, doors and lifts alike.
<pre><code class="language-scheme">(mover-ids) ↵ list?
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
Sets an actor's pathfinding goal to a fixed ground position. Pass the target's feet height as `y` when it can be on another floor; without it the goal height is the actor's own.
<pre><code class="language-scheme">(nav-set-goal-pos
  id : string?
  x, z : real?
  [y] : real?)
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
Returns the normalized horizontal direction from an actor toward its current waypoint (or, once past the last waypoint, straight at the goal) as `(dx dz)`, or \#f if the actor has no path or its goal is known-unreachable.
<pre><code class="language-scheme">(nav-path-direction
  id : string?) ↵ (or (list real? real?) \#f)
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

## Camera paths {#camera-paths}

Camera paths are authored in slopmap or by hand in a map's `graphs.s7` file. See @ref camera_paths for the file format and the editor workflow. While a path plays it replaces the presentation camera and the audio listener. The player entity does not move, so a cutscene usually pairs `camera-path-play` with `player-set-control`.

### camera-path-play {#camera-path-play}
Starts playing a camera path from the current map. `loop?` defaults to `#f`, `speed` to `1.0` and `blend-out` (seconds to ease back to the player view when the path ends or is stopped) to `0`. Returns `#f` when the map has no path with that id.
<pre><code class="language-scheme">(camera-path-play
  id : string?
  [loop?] : boolean?
  [speed] : real?
  [blend-out] : real?)
  ↵ boolean?
</code></pre>

### camera-path-stop {#camera-path-stop}
Stops the playing path. With a positive `blend-out` the view eases back to the player over that many seconds. Returns whether a path was playing.
<pre><code class="language-scheme">(camera-path-stop
  [blend-out] : real?) ↵ boolean?
</code></pre>

### camera-path-playing? {#camera-path-playing}
Checks whether a camera path is currently driving the view.
<pre><code class="language-scheme">(camera-path-playing?) ↵ boolean?
</code></pre>

### camera-path-progress {#camera-path-progress}
Returns the playing path's progress from `0` to `1`, or `#f` when nothing is playing.
<pre><code class="language-scheme">(camera-path-progress) ↵ (or real? \#f)
</code></pre>

### camera-path-duration {#camera-path-duration}
Returns the total duration of a path in seconds, or `#f` for an unknown id.
<pre><code class="language-scheme">(camera-path-duration
  id : string?) ↵ (or real? \#f)
</code></pre>

### camera-path-sample {#camera-path-sample}
Samples a path at `t` seconds without playing it. Returns `(x y z yaw pitch fov)`, with `fov` as `0` when the path does not set one. Useful for driving a thing or an actor along a path from `tick`.
<pre><code class="language-scheme">(camera-path-sample
  id : string?
  t : real?)
  ↵ (or (list real? real? real? real? real? real?) \#f)
</code></pre>

### camera-paths {#camera-paths-list}
Lists the camera path ids defined by the current map.
<pre><code class="language-scheme">(camera-paths) ↵ (listof string?)
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

### present {#present}
Called every drawn frame in `present` scope, with the frame delta time in seconds. It runs on a network client, which runs no `tick`.
<pre><code class="language-scheme">(present
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

### on-anim-event {#on-anim-event}
Called when a skinned geo's clip playback crosses an `event` frame declared in its `.anim` bank. `source` is the entity id, or the socket name for first-person geo, followed by attachment names for attached geo, such as `guard/held` or `weapon/held`. It is the same path that addresses the geo in `actor-play-anim` and `fp-play-anim`. `clip` is the clip name. A looping clip fires its events every pass.
<pre><code class="language-scheme">(on-anim-event
  source, event, clip : string?)
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

### on-camera-path-end {#on-camera-path-end}
Called when a non-looping camera path reaches its end. Starting another path from this hook is allowed.
<pre><code class="language-scheme">(on-camera-path-end
  path-id : string?)
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
