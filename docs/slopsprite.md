# slopsprite

Authoring tool for `.spr` / `.spanim` with live previews. File formats stay on [Sprites](sprites.md); frame `(sound ...)` paths on [Audio](audio.md); logic `(hint ...)` markers on [Sprites: Logic hints](sprites.md#logic-hints). This page covers how to build and run the editor.

Related: [Sprites](sprites.md), [Audio](audio.md), [Icons](icons.md), [slopmap](slopmap.md).

## Build and CLI

CMake target `slopsprite` (root `CMakeLists.txt`), linked against `sloplib` plus rlImGui.

```bash
cmake --build build --target slopsprite

./build/slopsprite --base-game <package-path> [--mod <path>]... --target <package-path>
```

| Flag | Meaning |
|------|---------|
| `--base-game` | Base package to mount (required) |
| `--mod` | Additional package; repeatable |
| `--target` | Package that receives saves (required; must be one of the mounted packages) |

`--target` is always required. Saves write `sprites/<path>.spr` and sibling `.spanim` under that package. File -> New Sprite... picks a virtual path under `sprites/`. Rescan refreshes the sprite browser from mounted packages. UI chrome uses the silk icon atlas; see [Icons](icons.md).

## Preview modes

World is the 3D billboard preview: how the sprite reads as a prop in space, with optional auto-orbit so you can check rotation frames without scrubbing by hand. FirstPerson is the screen-space weapon / view canvas; when the asset has authored `(view ...)` defaults, this mode uses them so FP pose matches what the game will start from. Align is the editing surface for pivot, offset, rotation, scale, and translate -- including onion-skin compare against another frame when you need to line up a sequence.

## Authoring

Each frame can use rotation mode None, Five, Eight, or Custom. Five-angle mode keeps the usual Doom-style mirror pairs in sync so you only author one side of a mirrored yaw. Pose has base channels plus tweenable `anim-*` channels for clips that interpolate between holds. The clip timeline plays, scrubs, changes speed, and loops; frame `(sound ...)` entries pick `.ogg` paths from mounted `sound/` folders through the sound browser; frame hints are a space-separated list of `(hint ...)` names for gameplay callbacks.

Sprite/anim, texture, and sound browsers keep mounted package content reachable without leaving the tool. Debug -> Masks overlays hit-mask silhouettes in World preview so part boundaries are visible while you tune offsets and rotations.
