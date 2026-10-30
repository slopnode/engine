@page slopmodel slopmodel

slopmodel is the 3D model studio, the counterpart of slopsprite for skinned and static geometry. It opens a `.geo`, plays the clips of the skeleton's animation bank, and edits the two things the Blender exporter does not author: where a geo sits when it is a first-person view model, and the named attachment points held items hang from. It also edits the frame events of each clip. The model is previewed both as a world model, orbiting it on a grid, and as a view model, drawn through the same mirrored eye camera the engine uses for the first-person pass.

# Running

```
slopmodel --game <path> [--mod <path>]... [--target <path>] [--geo <path>] [--mode world|fp] [--verbose]
```

`--game` mounts the base package, `--mod` adds mods in order, and `--target` names the mounted package that receives saves. `--target` defaults to `--game` and is required once a `--mod` is given, same as the other editors (see @ref cli). `--geo` opens a geometry asset by virtual path at start and `--mode` picks the preview to start in. sloplauncher's Development tab launches it against the selected target.

# Layout

The left panel lists every `.geo` under the mounted packages' `geometry/` folders, with a filter on path or package id. Clicking one opens it. The center panel is the preview, switched between World and FP by the tabs above it. The right panel is the inspector. The bar along the bottom drives animation.

The world preview orbits with the right mouse button and zooms with the wheel. Spin rotates the model by itself, Frame fits the camera to it. Clicking near an attachment marker selects that attachment.

The FP preview puts the eye at the origin looking down the socket's axis, with the engine's 75 degree field of view and the horizontal mirror the first-person pass applies, so what it shows is what `fp-attach-geo` produces. A faint floor and far wall at eye height give the placement something to read against. The fov field in the corner only changes the preview.

The View menu toggles the skeleton overlay, the attachment markers, and shading. Shaded draws through the engine's view-model shader with a fixed key light so the form reads; off, the preview draws the materials flat.

# Inspector

Model shows the geo's package, skeleton, bone and clip counts, and its primitives with their materials. World scale only affects the world preview.

View placement edits the `(view ...)` block of the `.geo`: position, rotation as pitch yaw roll in degrees, and scale. Store view defaults controls whether the block is written at all. With it stored, a script attaches the geo with no placement arguments and gets this one:

```scheme
(fp-attach-geo "weapon" "hands/hands")
```

Attachments edits the `(attachments ...)` block: each has a name, a bone chosen from the skeleton, a position in bone space, a rotation and a uniform scale. The markers in both previews follow the bone through the playing clip, so an attachment can be placed against the pose that matters. Preview geo picks another geo from the package to show hanging on the selected attachment; it is only for looking, nothing about it is saved. A script refers to the attachment by name where it would otherwise name a bone:

```scheme
(actor-attach-geo "guard" "weapon" "hand" "weapons/pistol")
(fp-attach-held "weapon" "item" "hand" "weapons/pistol")
```

Clip events lists the events of the clip picked in the animation bar, each a frame index and a name, and adds one at the current frame. During playback the bar flashes each event as it fires and marks them along the track, which is the way to check that a "fire" or "footstep" lands on the right frame. Events reach scripts through `on-anim-event`.

# Animation bar

The clip combo picks a clip from the bank at the skeleton's path. Play and Pause run it at the given speed, Stop returns to frame zero, Loop wraps. The slider scrubs by frame and Space toggles playback when no text field has focus.

# Saving

Ctrl+S writes the `.geo` into the target package's `geometry/` folder and, when events changed, the `.anim` into `animations/`. The binary `.vert`, `.weights` and `.tracks` files are copied alongside when the target package does not already have them, so a mod saving an edited model stays self-contained. The Blender exporter keeps the view and attachment blocks of an existing `.geo` when it re-exports the mesh, and keeps clip events on re-export of the bank, so re-exporting from Blender does not undo this tool's edits. See @ref filegeo for the file layout and @ref blender for the exporter.
