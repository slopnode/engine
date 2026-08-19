@page dataapi Data API

The Data API is a core component that provides a flexible and extensible system for defining game content and behavior. It serves as the primary mechanism through which game definitions are specified allowing developers define what property everything can have. This layer also seperates the presentation from what you see on screen, items, guns, actors, from the data that actually define them.

Package data is defined using Scheme symbolic expressions and stored in `.s7` files within the `data/` directory of each package.

# Folder Structure {#folder-structure}

All package data files should be placed in the `data/` directory of your package and named according to their purpose:

- `actions.s7` - Package actions
- `map-handlers.s7` - Map event handlers  
- `items.s7` - Item catalog
- `view.s7` - View canvas configuration
- `cli.s7` - CLI flags
- `title.s7` - Title screen layers

# Actions {#actions}

Symbol `*package-actions*`

Actions define gameplay actions that can be bound to input devices. These are typically used for key bindings, controller buttons, or other user input events.

<pre><code class="language-scheme">(define *package-actions*
  (list
    (cons "fire"
      '((label . "Fire weapon")
        (type . "button")))
    (cons "jump"
      '((label . "Jump")
        (type . "button")))
    (cons "run"
      '((label . "Run")
        (type . "toggle")))))
</code></pre>

# Handlers {#handlers}

Symbol `*package-map-handlers*`

Map handlers define event handlers for map interactions. These are used to create custom behavior for map elements like triggers, use events, or other interactive components.

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

# Items {#items}

Symbol `*item-catalog*`

Item catalog defines items that can be used by the package, including their properties and behaviors.

<pre><code class="language-scheme">(define *item-catalog*
  (list
    (cons "health-potion"
      '((label . "Health Potion")
        (type . "consumable")
        (effects .
          ((health . 50)))))
    (cons "sword"
      '((label . "Sword")
        (type . "weapon")
        (stats .
          ((damage . 10)
           (speed . 1.2)))))))
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

CLI flags define additional command-line arguments that can be used when launching the package.

<pre><code class="language-scheme">(define *package-cli*
  '((flags
      (("--debug" . "Enable debug mode")
       ("--verbose" . "Enable verbose logging")))
    (args
      ((level . "Set difficulty level")))))
</code></pre>

# Title {#title}

Symbol `*package-title*`

Title screen layers define the elements that make up the title screen.

<pre><code class="language-scheme">(define *package-title*
  '((image "freedom/TITLEPIC" fit)
    (text "SlopEngine Demo" center)
    (logo "freedom/LOGO")))
</code></pre>