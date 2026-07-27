# Title screen

Related: [Scripting](scripting.md), [Package structure](package-structure.md), [Player](player.md), [Maps](maps.md).

The title screen is a package-driven stack shown while the game is in the main menu. The engine loads config, optionally brings up a map through the normal render pipeline, blits an optional background image, then calls a Scheme draw hook on a virtual canvas. 

## Layers

Each frame while in the menu:

1. Clear to black
2. Optional title **map** — full world draw (same pipeline as gameplay, without first-person view or HUD)
3. Optional background **image** — fit to the screen (`stretch`, `fit`, or `cover`)
4. Package **`(draw-title)`** — immediate 2D on the title canvas (same `hud-*` primitives as the HUD)
5. ImGui package menus

Map, image, and canvas are independent. Any subset may be omitted.

## Config (`data/title.s7`)

Define `*package-title*` as a list of entries:

```text
(define *package-title*
  '((map "doors")                          ; optional
    (image "freedom/TITLEPIC" fit)         ; optional: stretch | fit | cover
    (canvas 320 200)))                     ; virtual pixels for (draw-title)
```

| Entry | Meaning |
|-------|---------|
| `(map "name")` | Load `maps/{name}` with reason `"title"`. Stays in MainMenu; camera freezes at player-start; no CharacterMotor / FP / HUD. |
| `(image "path" [fit])` | Texture virtual path (under textures/). Fit defaults to `stretch` if omitted. |
| `(canvas w h)` | Virtual pixel size for `(draw-title)`. Defaults to 640×480 if omitted. Letterboxed to the window like `*hud-canvas*`. |

Image fit modes:

| Mode | Behavior |
|------|----------|
| `stretch` | Fill the window; ignore aspect |
| `fit` | Contain; letterbox / pillarbox |
| `cover` | Fill; crop overflow |

Legacy `(background picture\|map "path")` still parses (picture → image stretch) with a warning. `(subtitle ...)` is ignored; draw text in `(draw-title)` instead.

## Draw hook

Define `(draw-title)` in package Scheme (typically `scripts/menus.s7`). It runs every menu frame under the same capability as `(draw-hud)`:

| Binding | Notes |
|---------|--------|
| `(hud-anchor symbol)` | `top-left`, `top-right`, `bottom-left`, `bottom-right`, `center`, `bottom-center` |
| `(hud-font path)` | Font for following text |
| `(hud-rect x y w h r g b [a])` | Solid rect in canvas pixels |
| `(hud-image path x y [w h] [r g b a])` | Texture blit |
| `(hud-text str x y size [r g b a])` | Text; color channels 0–255 |

Coordinates are canvas pixels relative to the current anchor. Animate by changing what you draw each frame (for example advance a phase and offset with `sin`). `(tick dt)` does not run in the menu; advance state inside `(draw-title)` if needed.

## Example (slopdoom)

Image and canvas share TITLEPIC’s 320×200 aspect with `fit`, so the picture and canvas letterbox together. Chrome is drawn in Scheme:

```text
(define *package-title*
  '((image "freedom/TITLEPIC" fit)
    (canvas 320 200)))

(define (draw-title)
  (hud-anchor 'bottom-center)
  (hud-text "Slopdoom Edition!" -51 -22 12 240 220 160 255))
```

## Boot and map load

- `data/title.s7` is loaded at script boot; `applyMenuBackground` runs on enter menu.
- Title maps use `(request-map-load name "title")` (or the `(map …)` catalog entry). A normal map load clears the title image and enters Playing.
- `(on-startup)` may still queue a gameplay map (for example from `--map`); that bypasses staying on the title stack.
