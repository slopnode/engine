# slopsprite

Authoring tool for `.spr` / `.spanim` with live previews. File formats stay on [Sprites](sprites.md); frame `(sound ...)` paths on [Audio](audio.md). This page covers how to build and run the editor.

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

| Mode | Purpose |
|------|---------|
| World | 3D billboard preview (optional auto-orbit) |
| FirstPerson | Screen-space weapon / view canvas; uses authored `(view ...)` defaults when present |
| Align | Pivot, offset, rotation, scale, translate editing; onion-skin compare against another frame |

## Authoring

Frames with rotation modes None / Five / Eight / Custom (five-angle mode keeps mirror pairs in sync). Base pose channels plus tweenable `anim-*` channels. Clip timeline: play, scrub, speed, loop. Frame sounds pick `.ogg` paths from mounted `sound/` via the sound browser.

Browsers: sprite/anim, texture, sound. Debug -> Masks overlays hit-mask silhouettes in World preview.
