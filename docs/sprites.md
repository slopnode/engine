# Sprites

Sprites are Doom-style billboards: named frames of PNG art, optional view rotations, and optional hit masks. Animation is a separate clip bank that advances which frame a `SpriteInstance` shows. This is not skeletal animation; see [Animation](animation.md) for skinned meshes.

Package layout is summarized in [Package structure](package-structure.md).

## Assets

| Kind | Extension | Directory | Virtual path example |
|------|-----------|-----------|----------------------|
| Sprite definition | `.spr` | `sprites/` | `usmc/umca` → `sprites/usmc/umca.spr` |
| Sprite animation bank | `.spanim` | `sprites/` | `usmc/umca` → `sprites/usmc/umca.spanim` |
| Frame / hit textures | `.png` | `textures/` | `sprites/usmc/UMCAA1` → `textures/sprites/usmc/UMCAA1.png` |

The `.spr` and `.spanim` for one character usually share the same virtual path (`usmc/umca`). Texture paths inside those files omit the `textures/` prefix and the `.png` extension.

## Sprite definition (`.spr`)

```text
(sprite
  (texel-size 32)
  (hit-part "legs" 0 0 255)
  (hit-part "body" 0 255 0)
  (hit-part "head" 255 0 0)
  (frame "A"
    (rot 1 "sprites/usmc/UMCAA1" offset 32 64 hit "sprites/usmc/hit/UMCAA1")
    (rot 2 "sprites/usmc/UMCAA2A8" offset 30 64 mirror hit "sprites/usmc/hit/UMCAA2A8")
    (rot 8 "sprites/usmc/UMCAA2A8" offset 34 64 mirror hit "sprites/usmc/hit/UMCAA2A8")
    (rot 5 "sprites/usmc/UMCAA5" offset 32 64 hit "sprites/usmc/hit/UMCAA5"))
  (frame "H"
    (rot 0 "sprites/usmc/UMCAH0" offset 40 70 hit "sprites/usmc/hit/UMCAH0")))
```

| Field | Meaning |
|-------|---------|
| `(texel-size N)` | Pixels per world meter (default `64`). World size is `pixelSize / texel-size` times entity scale. |
| `(hit-part "name" R G B)` | Named hit region keyed by exact RGB in a hit-mask texture. Optional. |
| `(frame "id" …)` | Named pose. Clip lists and `SpriteInstance.frame` refer to these ids. |
| `(rot R "texture" …)` | View rotation `R` for that frame. Texture is a virtual path under `textures/`. |
| `offset X Y` | Optional SLADE-style pixel origin from the top-left of that texture. Omit → bottom-center (`width/2`, `height`). |
| `mirror` | Flip UVs / hit samples horizontally for this rotation. |
| `hit "…"` | Optional hit-mask texture path (same size as the frame texture). |

### Rotations

Rotation index is Doom-style:

| Index | Role |
|-------|------|
| `1`–`8` | Eight yaw sectors around the sprite (camera relative to `facingYaw`) |
| `0` | Non-directional (death / special poses); used when no angle set is needed |

Opposite angles often share one texture with `mirror` on the line (for example rot `2` and rot `8`). Mirrored UVs and hit samples flip at runtime; do not duplicate flipped PNGs unless you want to.

If a requested rotation is missing, the loader falls back to rot `0`, then nearby angles, then any available rotation.

### Atlasing

On load, unique frame textures are packed into GPU atlases. Source images are not kept in CPU memory after pack. Point filtering and clamp wrap are used.

## Hit masks

Hit testing uses per-texture occupancy data baked at load time.

**Default (no hit data):** every opaque texel of the frame PNG (`alpha >= 128`) is one region named `default`.

**Configured:** declare `(hit-part …)` colors and point each `(rot …)` at a hit texture with `hit "…"`. The hit PNG must match the frame texture size. Opaque pixels whose RGB exactly match a `hit-part` belong to that part; other pixels are non-solid.

```text
(hit-part "legs" 0 0 255)
(hit-part "body" 0 255 0)
(hit-part "head" 255 0 0)

(rot 1 "sprites/usmc/UMCAA1" hit "sprites/usmc/hit/UMCAA1")
```

Hit masks are authoring data. The engine does not invent body proportions; paint or generate the colored PNGs yourself. Mirrored rotations reuse the same hit texture and flip X when sampling.

Ray hits against sprites use the current billboard (frame + view rotation + mirror), then the mask pixel under the hit. This is separate from player capsule / world physics.

## Runtime components

### `SpriteInstance`

| Field | Meaning |
|-------|---------|
| `sprite` | Virtual sprite path (e.g. `usmc/umca`) |
| `frame` | Current frame id (e.g. `"A"`) |
| `facingYaw` | Sprite facing in world radians; view rotation is derived from camera vs this yaw |

Place the entity with `LocalTransformation` / `GlobalTransformation` and `WorldSpace`. The billboard sits on a camera-facing quad. The origin (feet / pivot) comes from each rotation's optional `offset X Y` (SLADE-style pixels from the texture top-left). When omitted, the origin is bottom-center. Feet sample map light when lightmaps are available.

### `SpriteAnimator`

Drives `SpriteInstance.frame` from a `.spanim` bank.

| Field | Meaning |
|-------|---------|
| `animPath` | Virtual path of the `.spanim` (often same as `sprite`) |
| `clipName` | Active clip name |
| `time` | Playback time in seconds |
| `speed` | Playback rate multiplier |
| `loop` | Whether this play should loop (set by `play`) |
| `playing` | Clip is advancing |
| `justStarted` | True for one frame after `play` |
| `justFinished` | True for one frame when a non-looping clip ends |

```text
animator.animPath = "usmc/umca";
animator.play("walk", true);
```

`play(clip, shouldLoop = true, playbackSpeed = 1)` resets time and starts the clip. `stop()` clears `playing`. The `AdvanceSpriteAnimator` system loads the bank, picks the clip, advances `time` by `delta * speed`, and writes the frame id into `SpriteInstance.frame`. Non-looping clips clamp on the last frame, set `playing = false`, and pulse `justFinished`.

## Animation bank (`.spanim`)

Sibling of the `.spr` at the same virtual path:

```text
(sprite-anim
  (clip "walk"
    (loop 1)
    (frame "A" 0.125)
    (frame "B" 0.125)
    (frame "C" 0.125)
    (frame "D" 0.125)
    (frame "E" 0.125)
    (frame "F" 0.125)
    (frame "G" 0.125)
  )
  (clip "fall"
    (loop 0)
    (frame "H" 0.1)
    (frame "I" 0.1)
    (frame "J" 0.1)
    (frame "K" 0.1)
    (frame "L" 0.1)
    (frame "M" 0.1)
    (frame "N" 0.1)
    (frame "O" 0.1)
    (frame "P" 0.1)
    (frame "Q" 0.1)
    (frame "R" 0.1)
    (frame "S" 0.1)
    (frame "T" 0.1)
    (frame "U" 0.1)
    (frame "V" 0.1)
    (frame "W" 0.1)
  )
)
```

| Field | Meaning |
|-------|---------|
| `(clip "name")` | Clip id used by `SpriteAnimator::play` |
| `(loop 0\|1)` | Default loop flag in the file (runtime `play` can override) |
| `(frame "id" seconds)` | Ordered hold: `.spr` frame id and duration in seconds |

Frame ids must exist in the paired `.spr`. Missing banks or clips leave the instance on whatever `frame` was last set.

## Debug

With the main menu open, **Debug** includes:

- **Sprite Masks** — billboard bounds plus silhouette outlines per hit part (distinct colors per part)
- **Sprite Aim** — center-view ray against sprite masks; HUD shows sprite, frame, part name, pixel, and distance

## Example entity

```text
SpriteInstance { sprite = "usmc/umca", frame = "A", facingYaw = 0 }
SpriteAnimator { animPath = "usmc/umca" }  →  play("walk")
LocalTransformation { position, scale, … }
WorldSpace
```
