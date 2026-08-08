@page assets Assets

Every asset the engine loads goes through one virtual filesystem (`VirtualFileSystem`, `src/core/vfs.hpp`) layered over the mounted packages -- the engine package, the base game, then mods in the order given on the command line (see @ref cli). Game code and data never touch a package's folder directly; they ask the VFS for a *kind* (texture, sprite, script, ...) and a *virtual path*, and it finds the first mounted package that has it.

# Virtual paths {#virtual-paths}

A virtual path is package-relative and extensionless, e.g. `"weapons/glock/GLKGA0"`. The VFS turns that into a real file by prefixing the asset kind's fixed subdirectory and appending the kind's fixed extension:

```
kind Texture, path "weapons/glock/GLKGA0"  ->  textures/weapons/glock/GLKGA0.png
kind Sprite,  path "weapons/glock/glock"    ->  sprites/weapons/glock/glock.spr
```

Each kind is fixed to exactly one directory and one extension -- there's no way to point a `.spr` reference at a `.jpg`, or ask for a texture under `sprites/`. The full table:

| Kind | Directory | Extension | Notes |
|---|---|---|---|
| Texture | `textures/` | `.png` | |
| TextureAnim | `textures/` | `.texanim` | Parsed kind exists in the VFS, but nothing in the engine loads it yet -- effectively reserved. |
| Material | `materials/` | `.mat` | |
| Shader | `shaders/` | `.glsl` | |
| Script | `scripts/` | `.s7` | Behavior: `init.s7`, `things.s7`, per-weapon/actor logic. |
| Skeleton | `skeletons/` | `.skel` | |
| SkeletonBind | `skeletons/` | `.bind` | |
| Geo | `geometry/` | `.geo` | Engine's own animated/static mesh format; see @ref filegeo. |
| GeoVert | `geometry/` | `.vert` | |
| GeoWeights | `geometry/` | `.weights` | |
| Anim | `animations/` | `.anim` | |
| AnimTracks | `animations/` | `.tracks` | |
| MapCsg | `maps/<name>/` | `static.csg` | Authored map geometry; see @ref tut_csg. |
| MapMeta | `maps/<name>/` | `map.meta` | |
| MapBsp | `maps/<name>/` | `static.bsp` | Compiled; see @ref bsp. |
| MapFac | `maps/<name>/` | `static.fac` | Compiled; see @ref fac. |
| MapVis | `maps/<name>/` | `static.vis` | Compiled PVS; see @ref vis. |
| MapRad | `maps/<name>/rad/` | `static.rad` | Compiled lightmap data; see @ref rad. |
| MapLightmap | `maps/<name>/rad/` | `.png` | Baked lightmap atlas images. |
| MapThings | `maps/<name>/` | `things.s7` | Placed thing instances. |
| MapGraphs | `maps/<name>/` | `graphs.s7` | Nav-mesh/pathing graph data. |
| PrefabCsg | `prefabs/` | `.csg` | Reusable brush groups, same authoring format as a map's `static.csg`. |
| PrefabThings | `prefabs/` | `.s7` | Things sidecar for a prefab. |
| Sprite | `sprites/` | `.spr` | Rotation frames and hit masks; texture refs resolve under `textures/`. |
| SpriteAnim | `sprites/` | `.spanim` | Animation clip banks over a `.spr`. |
| Icon | `icons/` | `.png` | The packed atlas image referenced by an `.iconmap`. |
| IconMap | `icons/` | `.iconmap` | Frame rects into an `Icon` atlas. |
| Data | `data/` | `.s7` | Declarative catalogs (actions, items, CLI flags, ...); see @ref dataapi. |
| Font | `fonts/` | `.ttf` | |
| Sound | `sound/` | `.ogg` | |
| Audio | `audio/` | `.s7` | Procedural audio defs. Currently broken by a name collision in the engine -- use plain `sound/` clips with `play-sound` instead. |
| AudioSaudio | `audio/` | `.saudio` | Sample playback defs; this path works. |
| Particle | `particles/` | `.prt` | |

# Override order {#override-order}

When more than one mounted package has the same kind and virtual path, the last-mounted package wins -- mods override the base game, and a later `--mod` overrides an earlier one, checked in reverse mount order until a match is found. This is per asset, not per package: a mod can override a single texture without touching anything else the base game defines at that path's neighbors.

Virtual paths aren't sandboxed to the owning package's folder the way save-relative paths are (see @ref profiles). A path is only backslash-normalized and collapsed, not checked for `..` segments, so a virtual path engineered with enough `../` can resolve to a file outside the package's asset directory (still with the kind's extension forced onto the end). In practice virtual paths come from package authors and their own scripts -- the same trust boundary as script execution -- rather than from an untrusted external source, so this is a fact about current behavior worth knowing when debugging an unexpected resolution, not a boundary the engine tries to enforce.

#### Text formats not covered elsewhere {#text-formats-not-covered-elsewhere}

Materials, particle systems, and icon atlases don't have their own tutorial yet; a real example from the engine's own package for each:

<pre><code class="language-scheme">; materials/engine/miss.mat
(material
  (shader "default")
  (texture "engine/missing")
  (texel-size 64)
  (base-color 1 1 1 1))
</code></pre>

<pre><code class="language-scheme">; particles/fx/generic-smoke.prt
(particle-system
  (duration 0)
  (loop #t)
  (emitter "smoke"
    (sim gpu)
    (sprite "fx/smoke")
    (billboard fixed)
    (blend alpha)
    (max-particles 2048)
    (rate 120)
    (burst 80)
    (lifetime 0.6 2.0)
    (speed 0.2 0.6)
    (size 0.6 1.2)
    (color 1 1 1 0.65)
    (gravity 14)
    (space world)
    (shape sphere 4)
    (size-over-life 1.0 0.45)
    (alpha-over-life 1.0 0.0)))
</code></pre>

An `.iconmap` describes one atlas image: its size, then a rect per icon id (`x y w h` in atlas pixels), used to pull a single icon out of the shared `Icon` PNG for a UI draw call:

```
(iconmap
  (atlas "silk")
  (size 512 512)
  (icon "accept" 0 0 16 16)
  (icon "add" 16 0 16 16))
```

Sprites (`.spr`/`.spanim`), geometry/skeletons/animations, and maps each have their own tutorials and format pages -- see @ref tutorials and @ref file_formats.
