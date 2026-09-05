@page ui_hud UI and HUD

Presentation code splits into two systems that never overlap. Adding to a menu or dialog, the File/Pause/Debug menus or a modal, is UI: real ImGui, built with the `ui-screen-*` wrapper. Drawing gameplay overlay, health/ammo, title screens, death or intro screens, is HUD: direct `hud-*` calls onto a 2D canvas, no wrapper involved. See @ref scriptingapi for exact signatures (@ref ui-imgui, @ref ui-screens, @ref hud). This page covers how the pieces fit together.

# Scopes {#ui-hud-scopes}

A script runs under one `ScriptScope` at a time (@ref scopes-roles-and-caps). `Ui` scope grants `ui-draw`, not `hud-draw`. `Hud` scope grants `hud-draw`, not `ui-draw`. A `draw-hud` hook can't call `ui-button`, and a `draw-pause-menu` hook can't call `hud-text`. Either call raises a capability error immediately. `Ui`-scope hooks are `draw-file-menu`, `draw-pause-menu`, `draw-debug-menu`, and `draw-modals`. `Hud`-scope hooks are `draw-hud` and `draw-title`.

# Hooks {#ui-hud-hooks}

Both systems are wired up the same way, through the fixed hook whitelist in `src/script/hook_registry.hpp`. A script defines `(define (draw-pause-menu) ...)` directly to become that hook's owner, or calls `(hook-add 'draw-pause-menu my-proc)` to run alongside the existing owner without replacing it, the mechanism a mod uses to add its own menu entries on top of the base game's. Owners are captured once at boot, right after the base package's scripts load. Contributors added later all run after the owner, in the order they were added.

The engine calls each hook from a fixed point in native code: `src/ui/ui_module.cpp`'s `drawMainMenuBar()` calls `draw-file-menu` inside the native File menu and `draw-debug-menu` inside the native Debug menu, `drawPauseMenu()` calls `draw-pause-menu`, and `drawUi()` calls `draw-modals` every frame regardless of menu state. On the HUD side, `src/render/render_pass_fp.cpp`'s `drawHud()` calls `draw-hud` once per frame after clearing the frame's draw list, and `src/game/menu_background.cpp` calls `draw-title` for the main menu's title screen.

# The UI wrapper {#ui-hud-wrapper}

The `ui-*` bindings in @ref ui-imgui are a thin, literal mapping onto ImGui calls: `ui-begin`/`ui-end`, `ui-menu-item`, `ui-button`, and so on. Calling them directly from a hook works, and for a hook that's a couple of lines it's the simplest option:

<pre><code class="language-scheme">(define (draw-pause-menu)
  (if (ui-button "Select Map") (open-level-select-menu) #f))
</code></pre>

`packages/engine/scripts/ui.s7`, loaded automatically as part of `lang.s7`, wraps those same primitives in a small declarative node tree (@ref ui-screens) so a menu with conditional rows, shared state, or more than a couple of widgets doesn't turn into a wall of imperative ImGui calls. A screen is declared once with `define-screen`, given a builder thunk that returns a tree of `window`/`vbox`/`button`/`menu-item`/... nodes, and rendered each frame with `ui-screen-render`:

<pre><code class="language-scheme">(define-screen 'pause-menu
  (lambda ()
    (apply vbox
      (append
        (if (campaign-policy-allow? 'allow-new?)
            (list (button "pause-new" "New Game" open-new-game-menu))
            '())
        (if (campaign-policy-allow? 'allow-save?)
            (list (when-visible? playing? (button "pause-save" "Save Game" open-save-game-menu)))
            '())))))

(define (draw-pause-menu) (ui-screen-render 'pause-menu))
</code></pre>

The tree is built once, the first time it's rendered, and cached from then on. Per-frame cost is just walking the cached nodes and calling the underlying `ui-*` primitives. Only `dynamic` and `dynamic-text` nodes re-evaluate anything each frame, for the rare row whose content or child list has to change on its own. `when-visible?` wraps a node so it's skipped while a getter returns `#f`, which is how a static tree still hides rows depending on game state (`playing?`, a settings toggle, whatever) without rebuilding anything. `ui-screen-node`, `ui-set!`, and `ui-get` let code outside the builder read or patch a specific row by its `id` after the fact, and `ui-screen-append!` adds a new child to the root later, both without forcing a rebuild of the rest of the tree.

Modal dialogs follow the same pattern with `modal` instead of `window`, and typically several such screens get aggregated behind the single `draw-modals` hook:

<pre><code class="language-scheme">(require "screens/modals/new-game")
(require "screens/modals/save-game")
(require "screens/modals/load-game")

(define (draw-modals)
  (draw-new-game-modal)
  (draw-save-game-modal)
  (draw-load-game-modal))
</code></pre>

# The HUD canvas {#ui-hud-canvas}

The HUD has no equivalent node-tree wrapper, and doesn't need one. `hud-*` calls append draw commands to a flat per-frame list, so writing them directly is already cheap and there's no ImGui window/focus state to manage. All coordinates are virtual pixels on a canvas sized from `*hud-canvas*` in `view.s7` (see @ref canvas), letterboxed and scaled to the real screen at draw time. `hud-anchor` picks the origin subsequent calls are relative to (`'top-left`, `'top-right`, `'bottom-left`, `'bottom-right`, `'center`, `'bottom-center`). `hud-font` picks the font subsequent `hud-text` calls use. `hud-rect`, `hud-image`, and `hud-text` draw the obvious primitives, and `hud-draw-eye` composites a player's secondary render eye (a scope or periscope view) onto the canvas, optionally through a mask texture.

A typical `draw-hud` hook just dispatches by game state:

<pre><code class="language-scheme">(define (draw-hud)
  (cond (*intro-active?* (draw-intro-hud))
        (*report-active?* (draw-report-hud))
        (else (draw-gameplay-hud) (if *player-dead?* (draw-death-hud) #f))))

(define (draw-gameplay-hud)
  (hud-anchor 'top-left)
  (hud-text (weapon-label *weapon-slot*) 10 8 18 255 255 255 255)
  (hud-text (string-append "Health: " (number->string *health*)) 10 30 16 220 220 220 255))
</code></pre>

# Title screens {#ui-hud-title}

`draw-title` runs under `Hud` scope, so a title screen animates with `hud-*` calls, not the UI wrapper. It composes with the static-image and live-map background modes configured in `*package-title*` (@ref title). The image or map renders first, and `draw-title` draws on top of it each frame. See @ref tut_first_title_screen for a full worked example, including the wiggle-text animation used there.
