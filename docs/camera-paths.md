@page camera_paths Camera paths

A camera path is an ordered list of knots that the engine flies the view along. Paths are used for fly-bys, cutscenes and animated title screens. They are stored per map in `maps/<name>/graphs.s7`, next to the AI data graphs that file already holds. slopmap edits them in place, and the same file can be written by hand.

# File format {#camera-paths-format}

Each path is one `(path ...)` form. The id is what scripts and the title screen refer to.

<pre><code class="language-scheme">(path "intro"
  (interp "catmull-rom")
  (orient "knots")
  (duration 3)
  (fov 70)
  (ease "smooth")
  (knot (at 2 1.6 -4) (look 0.4 -0.1))
  (knot (at 6 2.2 -1) (look 1.2 -0.2) (duration 5) (fov 60))
  (knot (at 4 1.8 5) (look 2.4 0) (ease "out")))
</code></pre>

Path options, all optional:

| Clause | Values | Meaning |
|--------|--------|---------|
| interp | "linear", "catmull-rom" | Curve type. Catmull-Rom is the default and passes smoothly through every knot. |
| orient | "knots", "tangent", "look-at" | "knots" interpolates each knot's own yaw and pitch. "tangent" looks along the direction of travel. "look-at" keeps a fixed point in view. |
| look-at | x y z | The point used by the look-at orientation. |
| closed | \#t or \#f | Joins the last knot back to the first. |
| constant-speed | \#t or \#f | Spreads the total duration evenly over the curve's length instead of per segment. |
| duration | seconds | Time from one knot to the next when the knot does not set its own. Default 2. |
| fov | degrees | Vertical field of view. Zero or absent keeps the player's current field of view. |
| ease | "linear", "smooth", "in", "out" | Easing for every segment unless a knot overrides it. |

Knot clauses:

| Clause | Values | Meaning |
|--------|--------|---------|
| at | x y z | World position of the camera. Required. |
| look | yaw pitch | Radians, the same convention as a player start. |
| fov | degrees | Overrides the path field of view from this knot, blended toward the next knot's value. |
| duration | seconds | Time from this knot to the next one. |
| ease | "linear", "smooth", "in", "out" | Easing for the segment that starts at this knot. |

Yaw, pitch and field of view are interpolated with the same curve type as the position, so a Catmull-Rom path turns as smoothly as it moves.

# Playing a path {#camera-paths-playing}

Scripts start a path with `camera-path-play` and stop it with `camera-path-stop`. The view, the frustum used for culling and the audio listener all follow the path camera. The first-person weapon is hidden while a path plays. The player entity itself is not moved, so a cutscene normally disables movement and look with `player-set-control` first and restores them from the `on-camera-path-end` hook. A blend-out time on play or stop eases the camera back to the player's view instead of cutting.

<pre><code class="language-scheme">(define (start-intro)
  (player-set-control \#f \#f)
  (camera-path-play "intro" \#f 1.0 0.5))

(define (on-camera-path-end id)
  (when (string=? id "intro")
    (player-set-control \#t \#t)))
</code></pre>

`camera-path-sample` returns a pose for any time along a path without playing it, which lets a script move an actor or a prop along the same curve from `tick`. The full list of functions is in the @ref camera-paths section of the scripting API.

With the engine started with `--debug`, the Debug menu has a Camera Paths entry that lists the current map's paths and plays any of them looping, which is a quick way to check a path in the real renderer.

# Title screen fly-by {#camera-paths-title}

The title screen's map mode accepts a path id. The map loads behind the menu as before and the camera loops along the path instead of standing at the player start.

<pre><code class="language-scheme">(define *package-title*
  '((map "E1M1" (path "intro"))
    (canvas 640 480)))
</code></pre>

# Editing in slopmap {#camera-paths-editor}

The Paths tab in the properties panel lists the map's paths. New path creates one, the Path combo switches the active path, and the Id field renames it. The options below the id mirror the file format.

Knots are added in three ways. Place knots turns the viewport into a placement tool: each click on the grid or a brush face adds a knot at that point, lifted to eye height on floors and facing the direction of travel, until Escape ends placement. Add from camera inserts a knot at the viewport camera's own position and orientation, after the active knot or at the end. Duplicating a selected knot with Shift+D inserts a copy after it.

Knots are entities in the viewport. They are picked, moved with G and rotated with R like things, and Delete removes them. Multiple knots can be selected with Shift. The knot list in the Paths tab reorders knots with the arrow buttons and removes them with the red button. Double-clicking a row moves the viewport camera to that knot. Selecting a knot shows its position, look angles in degrees, field of view, duration and easing. Set from camera copies the viewport camera into the knot and View from knot does the reverse.

The Preview section flies the viewport camera along the active path. The Time slider scrubs, Play runs it in real time, and turning the preview off restores the camera. The preview uses the same sampler as the engine, so what the viewport shows is what the game plays.

The curve, knots, look directions and a small frustum wedge per knot are drawn in the viewport. The camera button in the view toolbar hides and shows them, and hidden paths cannot be picked.
