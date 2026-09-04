@page tut_csg CSG, BSP, VIS, and RAD

# Why BSP? {#why-bsp}

If you fire up Unreal or Unity today, you won't find a brush editor, a BSP compiler, or a bake step called "VIS." Modern engines build levels out of meshes sculpted in Blender or Maya, not axis-aligned boxes glued together and carved out of each other. Visibility is handled by GPU-driven culling instead: hierarchical Z-buffer occlusion queries, frustum culling, and increasingly virtualized geometry systems like Unreal's Nanite, which sidestep the "how many triangles can I afford" question entirely by only rasterizing the detail that's actually visible at the pixel level, recomputed every frame. Lighting has followed the same trajectory: Lumen and other real-time voxel/SDF/ray-traced global illumination let light bounce and update live as the world changes, instead of being baked once and frozen until the next compile.

There are good reasons for the shift. A precomputed potentially-visible-set assumes the world is static: walls don't move, nothing streams in mid-level, and there's a finite, known-in-advance set of leaves to cross-reference against each other. That assumption breaks the moment you want an open world, destructible geometry, or procedurally generated levels. Baked radiosity has a similar problem: it looks great right up until someone turns off a light or moves the sun, at which point you either re-bake offline or fall back to a cheaper real-time light that won't quite match. And brush-based CSG, while great for boxy corridors and rooms, fights you the moment an artist wants an organic rock formation or a curved surface, which is exactly where sculpted meshes and normal maps do a better job.

So why does this engine still lean on brushes, BSP, VIS, and RAD? Because those tradeoffs run the other way for the kind of game this engine targets. A Doom/Quake-style shooter has levels that are mostly static, is often built by a small team without access to Nanite- or Lumen-grade rendering infrastructure, and benefits a lot from tooling simple enough to implement and reason about end to end. Baking VIS and RAD offline pushes the runtime cost of visibility and lighting close to zero, on hardware far more modest than real-time GI needs. And brush-based CSG, whatever its limits for organic shapes, is still a fast, tactile way to block out and iterate on exactly the kind of corridor-and-room layout this style of game is made of. It's less a limitation than the right tool for the job actually being done here.

# History of BSP {#history-of-bsp}

Binary space partitioning didn't start out as a game development trick. It came out of a 1980 SIGGRAPH paper by Henry Fuchs, Zvi Kedem, and Bruce Naylor, "On Visible Surface Generation by A Priori Tree Structures." The problem they were solving had nothing to do with levels or brushes: it was hidden surface removal for flight simulators, where the scene geometry was static (a fixed terrain and airport model) but had to be sorted and drawn correctly from a viewpoint that moved every frame, on hardware with no depth buffer to speak of.

Their insight was to precompute a binary tree of splitting planes once, offline. Every plane in the tree divides space into a front half and a back half. At runtime, you don't need to sort a single polygon: you walk the tree relative to the camera's position, and the tree itself tells you whether to draw the front child or the back child first at every branch. What used to be an expensive per-frame sort becomes an O(n) tree walk. For 1980s flight simulator hardware, this was the difference between real time and not.

## Doom's and Quake's 3D {#doom-s-and-quake-s-3d}

id Software picked this idea up for Doom in 1993, but in a scaled-down form. Doom's levels are conceptually 2D: a floor plan of linedefs and sectors, each sector carrying a flat floor height and ceiling height. Doom's BSP tree partitions that 2D linedef layout, which is what let it run hidden-surface removal and collision on a 33MHz 486. There was no real solid modeling involved in building a Doom map; you placed line segments and sectors directly in a 2D editor.

Quake, in 1996, is where the modern brush-and-BSP toolchain most people mean when they say "BSP level" actually appeared. Quake's levels are true 3D volumes, and id's `qbsp` tool compiled them from convex solid primitives called brushes, combined into rooms and hallways. That compiled brush soup produced a real 3D BSP tree, and it's this brushes-in / BSP-tree-out toolchain that got copied (and refined) by Half-Life's Hammer editor, Unreal's UnrealEd, and basically everyone else who wanted to ship a 3D shooter for the next decade.

# CSG: building solids out of brushes {#csg-building-solids-out-of-brushes}

Constructive solid geometry itself predates any of this by a good while. It comes out of 1970s-80s CAD research where the idea was to describe a complex solid as a combination of simple primitives (boxes, cylinders, wedges) joined with boolean operators: union, intersection, subtraction. A machined part could be described as "a block, minus a cylinder bored through it, minus a couple of counter-sunk holes," and the CAD system would deal with the resulting solid geometry.

id borrowed exactly that idea for level design, minus the intersection/subtraction operators in the simplest case. A Quake brush is a closed convex solid; a map is a union of brushes. Editors added "carve" and "clip" tools later so subtraction-like workflows were possible, but the core representation stayed the same: a small list of convex solids in a text file, rather than a raw triangle soup. It's a good fit for BSP compilation for the same reason it's a good fit for CAD: convex, watertight primitives compile cleanly into splitting planes, and the resulting tree is guaranteed to have sane inside/outside semantics.

## Brushes in this engine {#brushes-in-this-engine}

This engine keeps that same "brushes in a text file" idea, just written as Scheme s-expressions instead of Quake's `.map` format, because everything else in a package is already an s-expr. Here's a trimmed piece of `packages/demo/maps/empty-room/brushes.map`:

<pre><code class="language-scheme">(brush-box
  (id "a-floor")
  (mins -4 -0.25 -4)
  (maxs 4 0 4)
  (material "surfaces/floor")
)

(brush-box
  (id "detail-crate")
  (mins 2.2 0 2.2)
  (maxs 2.8 0.8 2.8)
  (material "texture-test")
  (role "detail")
)

(brush-convex
  (id "detail-wedge")
  (role "detail")
  (material "texture-test")
  (faces
    (face
      (id "detail-wedge/bottom")
      (verts (v -1.75 0 1) (v -0.75 0 1) (v -1.25 0 1.7)))
    ...))
</code></pre>

`brush-box` is the axis-aligned convenience case; `brush-convex` lets you list arbitrary convex faces directly, which is how the wedge above gets its slope. Every brush also carries a `role` (`slopengine::BrushRole`): `Hull` brushes are structural, participate in the BSP split, and block visibility and physics like a Quake 1 solid brush; `Detail` brushes are drawn and can still collide, but don't cut the BSP tree or affect VIS, exactly like the `func_detail` brushes Quake 2 and Half-Life added once mappers wanted decorative geometry (crates, trim, light fixtures) without blowing up compile times or visibility graphs on every window ledge.

# The compilers: CSG → BSP → VIS → RAD {#the-compilers-csg-bsp-vis-rad}

Once a map is authored as brushes in `slopmap`, getting it into something the engine can render and query at runtime is an offline, three-stage compile, run in order:

1. **`slopbsp`** triangulates the brushes and partitions them into a `.bsp` tree: internal split-plane nodes, convex leaves tagged with contents flags (`Solid`, `Glass`, `Water`, `Trigger`), and the portals connecting adjacent open leaves. It also emits the final triangulated surface faces (material, UVs, id) directly into the `.bsp` file.
2. **`slopvis`** walks the leaf/portal graph produced by `slopbsp` and computes potential visibility (below).
3. **`sloprad`** bakes static lighting into per-face lightmap atlases (below).

You'll notice there's no FAC stage. There used to be: a `.fac` file sat between BSP and RAD, holding the compiled face list that lightmapping keyed off of. That data has since folded directly into the `.bsp` file's own surface-faces section, so the format is deprecated and the live pipeline is just CSG → BSP → VIS → RAD.

# VIS: the Potentially Visible Set {#vis-the-potentially-visible-set}

Quake's `vis.exe` earned its reputation as the slowest tool in the toolchain, sometimes running for hours on a large map, because what it computes is heavy. For every leaf in the BSP tree, which other leaves could possibly be seen from it, looking through however many portals connect them? Once that's answered offline, the runtime question becomes trivial: look up the current leaf, check one bit per candidate leaf, and skip rendering (and later, skip a lot of gameplay bookkeeping too) for anything not marked visible. On mid-90s hardware, that was the only way a game with indoor, occluding geometry could keep a stable frame rate.

The engine's `.vis` file is exactly that precomputed bitset: a `leafCount × leafCount` matrix, packed one bit per leaf pair into `wordsPerRow` 32-bit words, row-major so that testing "can leaf A see leaf B" is a single word fetch and mask (`slopengine::pvsCanSee`). It's surfaced to package scripts as `(pvs-can-see x0 y0 z0 x1 y1 z1)`, which actors' sight and audio-occlusion checks lean on instead of doing a full linescan every time.

# RAD: baking light {#rad-baking-light}

Radiosity is older than computer graphics; it's a heat-transfer technique for modeling how energy radiates between surfaces. Cindy Goral, Kenneth Torrance, Donald Greenberg, and Bennett Battaile brought it into computer graphics at Cornell in 1984 ("Modeling the Interaction of Light Between Diffuse Surfaces"), treating light the same way: every diffuse surface both receives and re-emits light to every other surface it can see.

That's expensive, so Quake's `light.exe` took a shortcut: bake direct light (plus a fudge factor for bounce) into a lightmap texture offline, and just sample that texture at runtime. No per-frame lighting computation, no per-frame bounce simulation, just a texture fetch. It's a huge reason Quake's diffuse lighting still looks as good as it does on hardware from that era.

This engine's `sloprad` is closer to the original Cornell method than Quake's approximation: `RadiositySettings` has a real `bounces` count and per-bounce `samples`, direct lighting from a stratified NxN grid per emissive face, and an optional GPU compute path for both the direct and bounce passes on top of the CPU reference implementation. The output, `.rad`, doesn't embed the actual pixels; it stores atlas metadata and UV chart placement per baked face, with the pixels themselves living in separate PNGs (`compiled/rad/atlas0.png` and so on) that the renderer composites with dynamic lights at draw time.
