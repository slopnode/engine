@page dataapi Data API

The Data API is a core component that provides a flexible and extensible system for defining game content and behavior. It serves as the primary mechanism through which game definitions are specified allowing developers define what property everything can have. This layer also seperates the presentation from what you see on screen, items, guns, actors, from the data that actually define them.

Package data is defined using Scheme symbolic expressions and stored in `.s7` files within the `data/` directory of each package.

# Folder Structure {#folder-structure}

All package data files should be placed in the `data/` directory of your package and named according to their purpose:

- `actions.s7` - Package actions
- `map-handlers.s7` - Map event handlers  
- `things.s7` - Thing templates
- `items.s7` - Item catalog
- `view.s7` - View canvas configuration
- `cli.s7` - CLI flags
- `title.s7` - Title screen layers

# Actions {#actions}

Symbol `*package-actions*`

Actions define gameplay actions that can be bound to input devices. Each entry accepts a `label` and a `default` bind token (a key or mouse button name); the default bind is used until the player rebinds the action.

<pre><code class="language-scheme">(define *package-actions*
  (list
    (cons "attack"
      '((label . "Attack")
        (default . "mouse1")))
    (cons "weapon-1"
      '((label . "Weapon 1")
        (default . "1")))))
</code></pre>

# Handlers {#handlers}

Symbol `*package-map-handlers*`

Map handlers define event handlers for map interactions. These are used to create custom behavior for map elements like triggers, use events, or other interactive components. Valid kinds are `enter`, `exit`, `touch`, `use`, and `can-use`. Param types are `int`, `float`, `bool`, `string`, `color`, `vec3`, `thing`, `brush`, and `face`; a param may include a default value, in which case it becomes optional.

<pre><code class="language-scheme">(define *package-map-handlers*
  (list
    (cons "toggle-light"
      '((label . "Toggle light")
        (kinds . (enter exit))
        (params .
          ((color color)
           (intensity float 1.0)
           (target thing)))))))
</code></pre>

# Things {#things}

Symbols `*package-things*` and `*package-thing-folders*`

Thing templates are the props, pickups, and actors that map authors can place from `slopmap` or `slopsprite`. Each entry needs an id and a `kind` of `prop`, `pickup`, or `actor`; which other keys apply depends on the kind.

- `label`, `icon`, `path` - browser presentation; `path` groups the thing under a folder declared in `*package-thing-folders*`
- `sprite` or `geo`, `frame`, `anim` - the graphic to show; a pickup requires exactly one of `sprite` or `geo`
- `motor (radius ...) (height ...) (speed ...) (gravity ...) (step-height ...) (hover-height ...) (max-fall ...) (water-aversion ...) (hull box|capsule|sphere) (move try-move|slide|fly) (nav-profile ...)` - a physics-driven movement capsule; implied for `actor`, optional for the others. `nav-profile` names a baked nav profile (see the nav-profile catalog) for this thing's capsule size to path against instead of the auto-matched one
- `tags` - a list of string tags, matched against `see-tags`/`ignore-tags` on other things and used by handlers
- `on-enter`, `on-use` - handler bindings from the map handler catalog; a pickup requires at least one
- `trigger-size` - the enter/use detection box as `(x y z)`, defaults to `1 1.5 1`
- `health`, `pain-chance`, `pain-threshold`, `idle-anim`, `behavior`, `behaviors` - actor combat and AI state; each entry under `behaviors` is a named clause of its own params
- `sight (range ...) (fov ...) (eye-lift ...) (see-tags ...) (ignore-tags ...) (filter ...) (enabled ...)` - actor perception config

<pre><code class="language-scheme">(define *package-thing-folders*
  '(("ammo" . "bullet_black")
    ("monsters" (icon . "group") (label . "Monsters"))))

(define *package-things*
  (list
    (cons "red-square"
      '((label . "Red Square")
        (path . "props")
        (kind . prop)
        (sprite . "red-square")
        (frame . "still")))
    (cons "clip"
      '((label . "Clip")
        (path . "ammo")
        (icon . "bullet_black")
        (kind . pickup)
        (sprite . "items/clip")
        (frame . "A")
        (on-enter "on-enter-ammo" (ammo "clip"))
        (trigger-size 1 1.5 1)))
    (cons "poss"
      '((label . "Zombieman")
        (path . "monsters")
        (kind . actor)
        (sprite . "monsters/poss")
        (anim . "walk")
        (anim-loop . #t)
        (motor (radius 0.3) (height 1.0) (speed 3.5) (gravity 9.81) (hull capsule) (move slide))
        (tags "actor" "team:hell")
        (health . 20)
        (pain-chance 0.78)
        (idle-anim . "walk")
        (behavior . "idle")
        (behaviors
          (ranged (cooldown 2.2) (range 24) (anim "attack") (recipe "hitscan")))
        (sight (range 28) (fov 160) (see-tags "player") (ignore-tags "team:hell") (enabled #t))))))
</code></pre>

# Nav Profiles {#nav-profiles}

Symbol `*package-nav-profiles*`, loaded from `data/nav-profiles.s7`.

Declares the set of Recast bake parameters (`tools/slopnav`) a map's navmesh is baked
against -- one file per profile under `maps/<name>/compiled/nav/` (see @ref nav). A
thing-def's `motor` selects one by name via `nav-profile`; a thing-def that doesn't
name one is auto-matched at spawn to the smallest registered profile whose radius and
height both cover its own motor capsule (see `resolveAutoNavProfile`). A package that
never defines `*package-nav-profiles*` keeps baking/loading exactly one implicit
`"default"` profile, unchanged from before this catalog existed.

<pre><code class="language-scheme">(define *package-nav-profiles*
  (list
    (cons "default"
      '((radius . 0.4) (height . 1.8) (max-climb . 0.6) (max-slope . 45)))
    (cons "small"
      '((radius . 0.25) (height . 1.0) (max-climb . 0.4) (max-slope . 45)))))
</code></pre>

- `radius`, `height` - agent capsule dimensions Recast erodes the walkable surface by
- `max-climb` - max ledge height the agent can step up, in world units
- `max-slope` - max walkable slope, in degrees
- `cell-size`, `cell-height` - Recast voxelization resolution; omit to use the tool's defaults

# Items {#items}

Symbol `*item-catalog*`

The engine loads `data/items.s7` into the script environment but does not interpret its contents; the shape of `*item-catalog*` and any accessors like `item-def` are entirely up to the package. A common convention is a small alist of item properties such as whether the item is unique or how many can be stacked, with helper functions to read them.

<pre><code class="language-scheme">(define *item-catalog*
  (list
    (cons "key-blue" '((unique . #t)))
    (cons "shells" '((stack . 50)))))

(define (item-def id)
  (assoc id *item-catalog*))
</code></pre>

# View {#view}

Symbol `*view-canvas*`

View canvas defines the resolution and presentation properties for the main game view.

<pre><code class="language-scheme">(define *view-canvas* '(1920 1080))
</code></pre>

# Canvas {#canvas}

Symbol `*hud-canvas*`

HUD canvas defines the resolution and presentation properties for the heads-up display.

<pre><code class="language-scheme">(define *hud-canvas* '(1920 1080))
</code></pre>

# CLI {#package-cli}

Symbol `*package-cli*`

CLI flags define additional command-line arguments that can be used when launching the package. Each flag has a name, a value kind of either `string` or `flag`, and a help string.

<pre><code class="language-scheme">(define *package-cli*
  '((flags
      ((name "map") (value "string") (help "Initial map folder under maps/"))
      ((name "god-mode") (value "flag") (help "Player takes no damage")))))
</code></pre>

# Title {#title}

Symbol `*package-title*`

Title screen configuration defines the background shown behind the title screen. Use `image` for a static picture, with a fit mode of `fit`, `cover`, or `stretch`, or `map` to render a live map as the background. `canvas` sets the virtual resolution the title screen is drawn at.

<pre><code class="language-scheme">(define *package-title*
  '((image "freedom/TITLEPIC" fit)
    (canvas 320 200)))
</code></pre>

<pre><code class="language-scheme">(define *package-title*
  '((map "test")
    (canvas 320 240)))
</code></pre>