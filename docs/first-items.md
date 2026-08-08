@page tut_first_items First items

Go back and look at the list of base things from the [first things tutorial](@ref tut_first_things) and you'll notice `pickup` is described only as "allows a user to collide and trigger a pickup hook." That's not a summary that's leaving detail out; it's the whole feature. The engine has no idea what "ammo," "health," or "a key" are, doesn't have an inventory anywhere in its C++, and doesn't track what a pickup gives you. All a `pickup` (or a `usable`, if you'd rather require a keypress instead of a walk-over) does is fire a script hook when the player touches or activates it. Everything past that point, deciding what the thing is worth, how much of it you can carry, what happens when you already have the max, is a package-side definition, same as `on-map-ready` or any other hook from @ref tut_scheme "the Scheme introduction". The engine hands you a small, generic primitive; you build the actual item system out of it.

This tutorial builds the smallest version of that: a pickup that adds to a single running ammo count, capped at a maximum, shown on the HUD.

# The pickup thing

A `pickup` needs a presentation (`sprite` or `geo`) and either an `on-enter` or `on-use` handler, or the map compiler will reject it outright, since a pickup that can't be triggered isn't doing anything. We'll use `on-enter` here so walking over the clip is enough to collect it, the way ammo works in Doom or Quake, rather than requiring a use key press.

We can reuse the tinting trick from the things tutorial for the sprite. A yellow square reads reasonably well as "ammo" at a glance:

<pre><code class="language-scheme">; sprites/ammo-sprite.spr
(sprite
    (texel-size 64)
    (tint 1 0.85 0.1 1)
    (billboard screen)
    (frame "still"
        (rot 0 "engine/dev/sprite-square" offset 16 16)))
</code></pre>

# Defining the placeable thing

Same as before, `data/things.s7` exports a template so map authors can drop this into `slopmap` without hand-writing the clauses every time:

<pre><code class="language-scheme">; data/things.s7
(define (create-ammo-pickup id label)
    (cons id
        (list (cons 'label label)
              (cons 'kind 'pickup)
              (cons 'frame "still")
              (cons 'sprite "ammo-sprite"))))

(define *package-things*
  (list
    (create-ammo-pickup "ammo-clip" "Ammo Clip")))
</code></pre>

Placed in a map's `things.s7`, it looks like any other thing, just with `on-enter` instead of `on-use`:

<pre><code class="language-scheme">; maps/&lt;name&gt;/things.s7
(pickup
  (id "ammo-1")
  (at 2 0 -3)
  (sprite "ammo-sprite")
  (frame "still")
  (on-enter "on-enter-ammo"))
</code></pre>

Under the hood, giving a thing an `on-enter` clause (pickup or not) attaches a trigger volume to it; entering that volume is what calls the handler. If you wanted to be stricter about who can trigger it, `collide-tags` on the same thing filters entry by tag, but we'll keep this example open to anything that walks through it.

# Counting ammo, package-side

The state itself is nothing more than a Scheme variable, capped by a constant the package chooses:

<pre><code class="language-scheme">; scripts/things.s7
(define *ammo* 0)
(define *ammo-max* 50)
(define *ammo-per-pickup* 10)

(define (ammo-add amount)
  (set! *ammo* (min *ammo-max* (+ *ammo* amount))))
</code></pre>

And the handler the pickup calls on entry. Trigger handlers receive the id of the thing that was entered and the id of whatever entered it, `(thing-id other-id)`; a real game would probably check that `other-id` is the player before handing out ammo, but we'll skip that here to keep the example short:

<pre><code class="language-scheme">; scripts/things.s7
(define (on-enter-ammo thing-id other-id)
  (ammo-add *ammo-per-pickup*)
  (thing-despawn thing-id))
</code></pre>

That `thing-despawn` matters: nothing removes a collected pickup for you. As far as the engine is concerned, the trigger fired and that's the end of its job; if the package doesn't despawn the thing, it just sits there and can be walked into again.
