# Sprites

Sprites are Doom-style billboards: named frames of PNG art, optional view rotations, and optional hit masks. A sibling .spanim clip bank advances which frame a SpriteInstance shows and can tween pose channels or fire sounds. This is not skeletal animation; see [Skeletal animation](animation.md) for skinned meshes.

Package layout is summarized in [Package structure](package-structure.md). Authoring UI: [slopsprite](slopsprite.md). First-person screen-space sprites use the same assets with different components; see [World vs first-person](#world-vs-first-person) and [Player](player.md). World sprites are frustum-culled each frame before draw; see [View frustum culling](frustum.md).

## Assets

| Kind | Extension | Directory | Virtual path example |
|------|-----------|-----------|----------------------|
| Sprite definition | .spr | sprites/ | characters/guard -> sprites/characters/guard.spr |
| Sprite animation bank | .spanim | sprites/ | characters/guard -> sprites/characters/guard.spanim |
| Frame / hit textures | .png | textures/ | sprites/characters/guard_a1 -> textures/sprites/characters/guard_a1.png |

The .spr and .spanim for one sprite usually share the same virtual path. Texture paths inside those files omit the textures/ prefix and the .png extension.

## Sprite definition (.spr)

```text
(sprite
  (texel-size 32)
  (hit-part "legs" 0 0 255)
  (hit-part "body" 0 255 0)
  (hit-part "head" 255 0 0)
  (frame "A"
    (rot 1 "sprites/characters/guard_a1" offset 32 64 hit "sprites/characters/hit/guard_a1")
    (rot 2 "sprites/characters/guard_a2a8" offset 30 64 mirror hit "sprites/characters/hit/guard_a2a8")
    (rot 8 "sprites/characters/guard_a2a8" offset 34 64 mirror hit "sprites/characters/hit/guard_a2a8")
    (rot 5 "sprites/characters/guard_a5" offset 32 64 hit "sprites/characters/hit/guard_a5"))
  (frame "H"
    (rot 0 "sprites/characters/guard_h0" offset 40 70 hit "sprites/characters/hit/guard_h0")))
```

| Field | Meaning |
|-------|---------|
| (texel-size N) | Pixels per world meter (default 64). World size is pixelSize / texel-size times entity scale. |
| (fullbright) | Whole sprite ignores scene tint (map / dynamic / FX lights) and draws at full brightness. Does not emit light into the world. |
| (hit-part "name" R G B) | Named hit region keyed by exact RGB in a hit-mask texture. Optional. |
| (frame "id" ...) | Named pose. Clip lists and SpriteInstance.frame refer to these ids. |
| (rot R "texture" ...) | View rotation R for that frame. Texture is a virtual path under textures/. |
| offset X Y | Optional SLADE-style pixel origin from the top-left of that texture. Omit -> bottom-center (width/2, height). |
| mirror | Flip UVs / hit samples horizontally for this rotation. |
| hit "..." | Optional hit-mask texture path (same size as the frame texture). |
| bright "..." | Optional grayscale brightmap (same size as the frame). White texels stay fullbright; black texels use scene lighting. |
| rotation DEG | Base in-plane rotation in degrees (default 0). |
| scale SX SY | Base scale multipliers (default 1 1). |
| translate TX TY | Base canvas-space shift; not rotated with the sprite (default 0 0). |
| anim-rotation DEG | Tweenable rotation offset (default 0). |
| anim-scale SX SY | Tweenable scale multipliers (default 1 1). |
| anim-translate TX TY | Tweenable canvas-space shift (default 0 0). |

Effective pose per channel:

- rotation = rotation + anim-rotation (degrees)
- scale = scale x anim-scale
- translate = translate + anim-translate

.spanim tween interpolates the **effective** pose for the listed channels toward the next hold (base and anim-* combined). Without tween, base and anim-* stay on the current hold for the whole hold.

### Fullbright and brightmaps

Sprites can look bright without emitting light into the map (use FxLocalLight / DynamicLight separately if the sprite should light others).

- `(fullbright)` on the sprite: the whole billboard skips baked and overlay tint and draws with white vertex lighting.
- `bright "..."` on a `(rot ...)`: a same-size grayscale mask. White texels force fullbright; black texels keep scene lighting (`albedo * mix(lit, 1, bright)`).

Typical projectiles and torch props use `(fullbright)`. Use a brightmap when only part of a lit sprite should glow.

For first-person view sprites, draw stacks three layers:

- Rest: `ViewSprite` canvas / scale / rotation (from `(view ...)` or `fp-set-sprite-pos` / scale / rotation)
- Presentation offset: `ViewSprite` offsetX/Y (`fp-set-sprite-offset`) for package policy such as raise/lower/bob
- Art: frame base + `anim-*` from the current `.spr` hold / `.spanim` tween

Screen position uses `canvas + offset + (translate + anim-translate)`.

### View defaults ((view ...))

Optional block for first-person defaults (used by slopsprite and by runtime attach). Parsed into the asset; `(fp-attach-sprite socket sprite)` copies canvas, scale, rotation, and origin into `ViewSprite` when present and leaves presentation offset at zero. Optional canvas-x/y args override only the canvas pin. Scheme may drive raise/lower/bob with `fp-set-sprite-offset` without overwriting the rest pin.

```text
(sprite
  (view
    (canvas 160 200)
    (scale 1 1)
    (rotation 0)
    (origin 0.5 1)
    (eye-offset 0 0 0))
  (frame "Idle"
    (rot 0 "sprites/fp/weapons/gun_idle"
         offset 64 128
         scale 1 1
         translate 0 0
         anim-scale 1 1
         anim-translate 0 0)))
```

| Field | Meaning |
|-------|---------|
| (canvas X Y) | Default view-canvas position for the sprite origin. |
| (scale SX SY) | Default view-sprite scale. |
| (rotation DEG) | Default view-sprite rotation. |
| (origin OX OY) | Normalized pivot (default 0.5 1 = bottom-center). |
| (eye-offset X Y Z) | Suggested view-space eye offset (meters); presentation only. |

### Rotations

Rotation index is Doom-style. Indices 1 through 8 are yaw sectors around the sprite (camera relative to facingYaw). Index 0 is non-directional -- death, special, or first-person poses when no angle set is needed.

Opposite angles often share one texture with mirror on the line (for example rot 2 and rot 8). Mirrored UVs and hit samples flip at runtime; do not duplicate flipped PNGs unless you want to.

If a requested rotation is missing, the loader falls back to rot 0, then nearby angles, then any available rotation.

### Billboard mode ((billboard ...))

Optional world-draw orientation (default `face`):

| Mode | Texture rotation | Quad |
|------|------------------|------|
| face | Doom yaw vs `facingYaw` (1..8), with rot-0 fallback | Upright (yaw-only) toward the camera |
| view | Always rot 0 (viewer-oriented art) | Upright, yaw-aligned to the camera |
| screen | Always rot 0 (viewer-oriented art) | Screen-aligned: full camera facing, including pitch |
| fixed | Doom yaw vs `facingYaw` | Locked to `facingYaw` (wall / decal style) |

Use `(billboard screen)` for projectiles and explosions that should stay flat to the screen when looking up or down. Pickups typically use `(billboard view)` so they stay upright. Actors usually use the default `face` mode.

### Atlasing

On load, unique frame textures are packed into GPU atlases. Source images are not kept in CPU memory after pack. Point filtering and clamp wrap are used.

## Hit masks

Hit testing uses per-texture occupancy data baked at load time.

Default (no hit data): every opaque texel of the frame PNG (alpha >= 128) is one region named default.

Configured: declare (hit-part ...) colors and point each (rot ...) at a hit texture with hit "...". The hit PNG must match the frame texture size. Opaque pixels whose RGB exactly match a hit-part belong to that part; other pixels are non-solid.

```text
(hit-part "legs" 0 0 255)
(hit-part "body" 0 255 0)
(hit-part "head" 255 0 0)

(rot 1 "sprites/characters/guard_a1" hit "sprites/characters/hit/guard_a1")
```

Hit masks are authoring data. The engine does not invent body proportions; paint or generate the colored PNGs yourself. Mirrored rotations reuse the same hit texture and flip X when sampling.

Ray hits against sprites use the current billboard (frame + view rotation + mirror), then the mask pixel under the hit. This is separate from player capsule / world physics.

## Animation bank (.spanim)

Sibling of the .spr at the same virtual path:

```text
(sprite-anim
  (clip "walk"
    (loop 1)
    (frame "A" 0.125)
    (frame "B" 0.125)
    (frame "C" 0.125)
    (frame "D" 0.125))
  (clip "fire"
    (loop 0)
    (frame "FireStart" 0.05 (tween all) (sound "weapons/fire"))
    (frame "Fire" 0.08 (tween rot translate) (sound "weapons/fire" 0.8) (hint "fire"))
    (frame "Idle" 0.1)))
```

| Field | Meaning |
|-------|---------|
| (clip "name") | Clip id used by SpriteAnimator::play / (fp-play-sprite-anim ...) |
| (loop 0\|1) | Default loop flag in the file (runtime play can override) |
| (frame "id" seconds ...) | Ordered hold: .spr frame id and duration in seconds |
| (tween all\|rot\|scale\|translate ...) | Per-hold: interpolate effective pose (base + anim-*) toward the next hold for the listed channels (all = rot + scale + translate) |
| (sound "path" [volume]) | On hold enter, play a raw clip from sound/ (see [Audio](audio.md)). Optional volume defaults to 1.0. |
| (hint "name") | On hold enter, call Scheme (on-sprite-hint source name) if defined. Repeatable on one hold. |
| (overlay LAYER "sprite" "clip" X Y) | On hold enter, spawn or replace a layered sprite on the host (see [Overlays](#overlays)). Repeatable on one hold. |

Legacy clip-level (tween 0|1) expands to tween-all on every frame in that clip. A bare offset token inside (tween ...) is ignored (offset is not tweenable).

Frame ids must exist in the paired .spr. Missing banks or clips leave the instance on whatever frame was last set.

### Frame sounds

(sound ...) paths are virtual sound paths (weapons/fire -> sound/weapons/fire.ogg), not audio defs under audio/. Playback uses playSound / playSound3d: 3D when the entity has a GlobalTransformation, otherwise 2D on the sfx bus. See [Audio](audio.md).

### Logic hints

(hint "name") is a gameplay marker, not audio. On the same hold-enter path as frame sounds, AdvanceSpriteAnimator invokes (on-sprite-hint source name) when that procedure exists.

| Arg | Meaning |
|-----|---------|
| source | Flecs entity name when set; otherwise the FP socket parent name (weapon or emission). Anonymous entities skip the callback (one warn log). |
| name | The string from (hint "..."). |

Multiple (hint ...) forms on one hold each fire once, in order. Holds skipped in a single dt still fire (same as sounds). Packages own what a name means (hitscan, spawn, muzzle flash, etc.).

### Overlays

`(overlay LAYER "sprite" "clip" X Y)` is a presentation cue on the same hold-enter path as sounds and hints. It spawns a child sprite entity on the animating host:

| Arg | Meaning |
|-----|---------|
| LAYER | Signed draw order. Host/base is `0` (reserved; must not be used). Negative draws behind the host; positive in front. Re-entering the same layer replaces the previous overlay on that host. |
| sprite | Virtual sprite path (own `.spr` / `.spanim`). |
| clip | Clip to play on spawn. Uses that clip's authored `(loop …)` flag. |
| X Y | Canvas-space offset relative to the host (same units as `.spr` translate). |

First-person hosts (`ViewSprite`) get a child view sprite that inherits the host rest pose; screen pin is host canvas + host presentation offset + `(X, Y)`. World hosts get a child billboard with a local offset derived from `X Y` and texel size. Non-looping overlays despawn when their clip finishes. Sticky overlays (for example a scope) stay script-driven via `(fp-attach-sprite …)`.

Example:

```text
(frame "B" 0.05
  (sound "pistol/fire")
  (hint "fire")
  (overlay 1 "fx/muzzle" "flash" 48 -36))
```

## World vs first-person

The same .spr / .spanim assets can draw in two places. A world billboard is a SpriteInstance with WorldSpace (optional SpriteAnimator): a camera-facing quad whose rotation index comes from the camera versus facingYaw. A view / first-person sprite keeps SpriteInstance but adds ViewSprite (usually under ViewSpace) and draws as a screen-space overlay on the view canvas, typically with rot 0 because yaw sectors are for world facing.

World props and usables use map clauses (sprite ...) / (anim ...); see [Things](things.md). First-person sockets use Scheme (fp-attach-sprite ...) and related mutators; see [Player](player.md).

## Runtime components

### SpriteInstance

| Field | Meaning |
|-------|---------|
| sprite | Virtual sprite path (e.g. characters/guard) |
| frame | Current frame id (e.g. "A") |
| facingYaw | Sprite facing in world radians; view rotation is derived from camera vs this yaw |

Place the entity with LocalTransformation / GlobalTransformation and WorldSpace. The billboard sits on a camera-facing quad. The origin (feet / pivot) comes from each rotation's optional offset X Y (SLADE-style pixels from the texture top-left). When omitted, the origin is bottom-center. Feet sample map light when lightmaps are available.

### SpriteAnimator

Drives SpriteInstance.frame from a .spanim bank, advances tween state, and fires frame sounds and logic hints.

| Field | Meaning |
|-------|---------|
| animPath | Virtual path of the .spanim (often same as sprite) |
| clipName | Active clip name |
| time | Playback time in seconds |
| speed | Playback rate multiplier |
| loop | Whether this play should loop (set by play) |
| playing | Clip is advancing |
| justStarted | True for one frame after play |
| justFinished | True for one frame when a non-looping clip ends |

```text
animator.animPath = "characters/guard";
animator.play("walk", true);
```

play(clip, shouldLoop = true, playbackSpeed = 1) resets time and starts the clip. playSpriteAnim also writes the clip's first hold into SpriteInstance.frame immediately when the bank is available. stop() clears playing. The AdvanceSpriteAnimator system loads the bank, picks the clip, advances time by delta * speed, writes the frame id into SpriteInstance.frame, updates tween blend, and on hold enter plays frame sounds and calls (on-sprite-hint ...) for any (hint ...) markers. Non-looping clips clamp on the last frame, set playing = false, and pulse justFinished.

### ViewSprite

Screen-space presentation for first-person / HUD-like weapon sprites. On `(fp-attach-sprite)` without canvas args, authored `(view ...)` defaults fill canvas, scale, rotation, and origin; offset starts at zero. Rest canvas stays on canvasX/Y; package presentation (raise/lower/bob) uses offsetX/Y via `fp-set-sprite-offset`. Drawn after the world in the FP pass.

## slopsprite

Interactive authoring for .spr / .spanim (World / FirstPerson / Align previews, browsers, save under --target) is covered in [slopsprite](slopsprite.md).

## Debug

With the main menu open, Debug includes:

- Sprite Masks: billboard bounds plus silhouette outlines per hit part (distinct colors per part)
- Sprite Aim: center-view ray against sprite masks; HUD shows sprite, frame, part name, pixel, and distance

## Example entities

World:

```text
SpriteInstance { sprite = "characters/guard", frame = "A", facingYaw = 0 }
SpriteAnimator { animPath = "characters/guard" }  ->  play("walk")
LocalTransformation { position, scale, ... }
WorldSpace
```

First-person (via Scheme):

```text
(fp-attach-sprite "weapon" "fp/weapons/gun" 160 200)
(fp-play-sprite-anim "weapon" "fire" #f)
```
