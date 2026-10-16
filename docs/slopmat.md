@page slopmat slopmat

slopmat is the material studio. It browses the textures and materials of a mounted game, edits `.mat` files, and authors texture graphs: `.texgraph` files that composite several textures and generated patterns into the albedo and emission images a material draws with. The idea comes from Doom's texture lumps, where a wall texture is a stack of patches placed at offsets. Here the stack stays as data the engine evaluates when the material loads, so a graph can be re-tuned without re-exporting a PNG, and the same mechanism carries procedural generators.

# Running

```
slopmat --game <path> [--mod <path>]... [--target <path>] [--verbose]
```

`--game` mounts the base package, `--mod` adds mods in order, and `--target` names the mounted package that receives saves. `--target` defaults to `--game` and is required once a `--mod` is given, same as the other editors (see @ref cli). sloplauncher's Development tab launches it against the selected target.

# Layout

The left panel lists assets from every mounted package under four tabs: Materials (`materials/*.mat`), Graphs (`textures/*.texgraph`), Animations (`textures/*.texanim`) and Textures (`textures/*.png`). Each row carries a small thumbnail, built lazily a few per frame. The filter box matches on virtual path or package id. Clicking an entry opens it. Rows can be dragged onto any path field in the inspector, so a texture drops into an image node or a material's albedo slot, and a graph or animation drops into the matching material field.

The center panel previews the open document. The combo picks which image to show: a material offers its albedo and, when it has one, its emission mask, a graph offers each of its outputs, an animation offers its frames and plays them at their durations, a texture shows itself. The zoom slider and mouse wheel scale the view, Tile repeats the image across the panel to check seams, and Rebuild re-evaluates the preview from the current edits. 3D swaps the flat view for a floor and wall drawn at the material's texel size with a one metre grid, so density reads at world scale. Drag to orbit, wheel to zoom. Graphs and textures take their density from the slider next to the toggle. Evaluation errors appear above the canvas.

The right panel is the inspector. For a material it edits the surface, emission, sky and acoustic fields. For a graph it edits outputs and nodes. For an animation it edits clips and frames. Textures are view-only.

Ctrl+S saves the open document into the target package. Ctrl+N creates a material, Ctrl+Shift+N a texture graph, and the File menu also creates texture animations. Ctrl+Z and Ctrl+Y undo and redo; a slider drag collapses into one step. Export Preview as PNG writes the image shown in the preview to `textures/<path>-<output>.png` in the target package, for packages that want a flattened file. A new graph starts with a checker node feeding a stack node wired to an `albedo` output, so the preview shows something immediately.

# Texture graphs

A texture graph lives under `textures/` with the `.texgraph` extension and is the `TextureGraph` asset kind (see @ref assets). It is one `(texture-graph ...)` form holding nodes and outputs:

```
(texture-graph
  (node "wall" image (texture "freedom/WALL25_1"))
  (node "grime" image (texture "overlays/grime-a"))
  (node "comp" stack
    (layer "wall" (repeat 1))
    (layer "grime" (blend multiply) (opacity 0.6) (offset 0 16)))
  (node "glow" color (size 64 64) (color 0 0 0 1))
  (output "albedo" "comp")
  (output "emission" "glow"))
```

Every node has an id and a kind. Ids must be unique within the file and are how layers and outputs refer to nodes. Nodes can appear in any order.

Generators make an image from parameters:

| Kind | Fields | Result |
|---|---|---|
| `image` | `(texture "path")` | The PNG at that virtual texture path, at its own size. |
| `color` | `(size w h)` `(color r g b a)` | A solid fill. |
| `checker` | `(size w h)` `(cells nx ny)` `(color ...)` `(color-b ...)` | A checkerboard with `nx` by `ny` cells. |
| `gradient` | `(size w h)` `(angle deg)` `(color ...)` `(color-b ...)` | A linear gradient from the first color to the second, 0 degrees running top to bottom. |
| `noise` | `(size w h)` `(type value|perlin|worley)` `(scale n)` `(octaves 1..8)` `(persistence n)` `(lacunarity n)` `(seed n)` `(color ...)` `(color-b ...)` | Seeded noise between `color-b` (low) and `color` (high). `scale` is the number of lattice cells across the image and whole numbers tile seamlessly. More than one octave gives fractal detail. |
| `bricks` | `(size w h)` `(brick w h)` `(mortar px)` `(row-offset fraction)` `(color ...)` `(color-b ...)` | Running bond bricks in `color` with mortar lines in `color-b`. |
| `stripes` | `(size w h)` `(stripe px)` `(angle deg)` `(color ...)` `(color-b ...)` | Alternating bands. |

Filters read one node through `(input "node-id")` and keep its size unless noted:

| Kind | Fields | Result |
|---|---|---|
| `gradient-map` | one or more `(stop position r g b a)` | Looks the input's luminance up through the stops, which is how grayscale noise becomes a colored material. |
| `transform` | `(size w h)` `(scale sx sy)` `(angle deg)` `(flip h v)` | Magnifies, rotates and mirrors the input, sampling it tiled, with an optional output size. |
| `levels` | `(in-black n)` `(in-white n)` `(gamma n)` `(out-black n)` `(out-white n)` | Remaps the channel range. |
| `hsv` | `(hue deg)` `(saturation n)` `(value n)` | Shifts hue and scales saturation and value. |
| `invert` | | Inverts color, keeps alpha. |
| `blur` | `(radius px)` | Box blur that wraps at the edges, so a tiling input stays tiling. |
| `sharpen` | `(amount n)` | Unsharp mask. |
| `edges` | `(amount n)` | Sobel edge magnitude as grayscale. |

A `stack` takes `(size w h)` and one or more `(layer ...)` and composites the layers bottom to top. `(size 0 0)`, or no size, uses the first layer's size. A layer is `(layer "node-id" ...)` with optional fields. `(blend normal|multiply|add|screen)` picks how the layer's color combines with what is already on the canvas, `(opacity n)` scales its alpha, `(offset x y)` places it in canvas pixels, `(repeat 1)` tiles it across the canvas instead of placing it once, `(tint r g b a)` multiplies its color and alpha, and `(mask "node-id")` multiplies its alpha by another node's luminance sampled in canvas space, tiled. Compositing is source-over with straight alpha. A layer only covers the pixels it lands on, so a small patch on a large canvas leaves the rest untouched, and the canvas starts fully transparent.

A worn wall built this way reads as: an image node for the base texture, a noise node with a few octaves, a gradient-map turning that noise into brown grime, and a stack that tiles the base and multiplies the grime over it at partial opacity, masked by a second noise so the grime pools rather than covers. All of it stays editable in the inspector and re-evaluates as the sliders move.

Outputs are `(output "name" "node-id")`. Materials read `albedo` and `emission`. Other names are allowed and show up in slopmat's preview, which is where future map kinds (height, normal and so on) will attach without a format change.

The file is validated on parse: unknown kinds or fields, missing references, a filter without an input, duplicate ids, a missing output or a cycle through stacks, inputs and masks all reject the file with a message naming the line.

## Materials and graphs

A material points at a graph with `(texture-graph "path")` in place of `(texture ...)` or `(texture-anim ...)`. The three are mutually exclusive. The graph's `albedo` output becomes the albedo map. If the material has no `(emission "path")` of its own and the graph has an `emission` output, that output becomes the emission mask. An explicit `(emission ...)` wins over the graph.

```
(material
  (shader "default")
  (texture-graph "walls/brick-grime")
  (texel-size 64)
  (base-color 1 1 1 1))
```

Evaluation happens on the CPU. The engine evaluates a graph the first time a material needs it, uploads the result once and caches it, so the runtime cost is a one-time hit at load. sloprad evaluates the same graph for the bake so lightmaps and emission match what is drawn, and slopmap's material browser thumbnails evaluate it too. Changing a graph on disk therefore needs a rebake for maps that use it, as changing a PNG would.

# Materials in slopmat

The inspector edits the shader name, which of texture, texture animation or graph supplies the albedo, texel size, base color, index of refraction, the emission mask, color, power and range, the fullbright and emission blend settings, bake, precise and exact emission, the sky flag and the acoustic absorption, scattering and transmission values. Sky appearance (solid color, gradient, cube or cylinder) is shown but not yet editable here. Saving writes the whole material back, so those fields survive a round trip untouched.

Saving omits fields that still have their default value, and colors are stored as 8-bit channels, so a hand-written `0.5` reads back as `0.501961`. Ids and paths are written as strings. The loader rounds color channels to the nearest 8-bit value when parsing.

# Plan

Phase 0, done. The `.texgraph` format with named outputs, validation and a CPU evaluator. The `TextureGraph` asset kind and `AssetStore` loading, caching and invalidation. `(texture-graph ...)` in materials with sloprad and slopmap following. A `.mat` serializer. The slopmat tool with browser, preview, material inspector and graph inspector, saving into the target package. Tests for parse, serialize, validate, evaluate and the material round trip.

Phase 1, done except for one item. Thumbnails in the browser. Texture animation banks listed, played and edited. Export of the previewed output as a flattened PNG. Undo and redo. Drag and drop from the browser onto path fields. A 3D floor and wall preview at the material's texel size. Still open: moving evaluation to a background thread so slider drags on large graphs stay smooth. Today the preview waits until no widget is active before re-evaluating, which keeps drags smooth at the cost of showing the result only on release.

Phase 2, done. Noise with value, Perlin and Worley types, octaves, persistence and lacunarity, all seeded and tiling. Gradient map, bricks, stripes, transform, levels, hsv, invert, blur, sharpen and edges. Layer masks. Filters take a named input.

Phase 3, more outputs. Height and normal outputs once the renderer has a use for them, and roughness or specular if the lighting model grows those. Time-varying parameters so a graph can emit the frames of a `.texanim`, which replaces hand-sliced animation strips for things like scrolling or flickering surfaces.

Phase 4, performance and workflow. Evaluated outputs cached on disk keyed by a content hash of the graph and its source images, mirroring the thumb atlas cache, so cold starts do not re-evaluate every graph. Optional GPU evaluation for large canvases. Hot reload of graphs in the running game and in slopmap.

Phase 5, material management. Sky appearance editing. Acoustic presets (concrete, wood, metal, glass) applied in one click. Bulk edits such as setting texel size across a folder. Material variants that share a graph and differ in tint or emission.
