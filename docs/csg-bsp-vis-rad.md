@page tut_csg CSG, BSP, VIS, NAV, and RAD

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

# The compilers: CSG → BSP → VIS → NAV → RAD {#the-compilers-csg-bsp-vis-rad}

Once a map is authored as brushes (`.map`) in `slopmap`, getting it into something the engine can render and query at runtime is an offline, five-stage compile, run in order:

1. **`slopcsg`** loads the authored brushes and carves them (below), writing the carved set to `compiled/csg`.
2. **`slopbsp`** triangulates the brushes and partitions them into a `.bsp` tree: internal split-plane nodes, convex leaves tagged with contents flags (`Solid`, `Glass`, `Water`, `Trigger`), and the portals connecting adjacent open leaves. It also emits the final triangulated surface faces (material, UVs, id) directly into the `.bsp` file.
3. **`slopvis`** walks the leaf/portal graph produced by `slopbsp` and computes potential visibility (below).
4. **`slopnav`** bakes one or more navigation graphs against the BSP's walkable geometry (below).
5. **`sloprad`** bakes static lighting into per-face lightmap atlases (below).

## Carving: resolving overlapping brushes {#carving-resolving-overlapping-brushes}

"CSG" names two different things in this pipeline, and it's worth keeping them apart: authoring a map as a union of brushes is CSG in the classic sense (see above), and `slopcsg` is the specific tool that resolves overlap between those brushes before anything gets triangulated.

A brush-based map is rarely a set of brushes that only touch at their boundaries -- a mapper routinely overlaps a wall brush into the floor brush at a corner, or overlaps a detail brush into a hull brush, because getting every brush's bounds to align exactly to the millimeter is more friction than it's worth. Left alone, two overlapping opaque brushes both contribute coincident faces at the overlap, which is exactly the setup for z-fighting at render time and duplicated geometry in the BSP.

`slopcsg` fixes this by carving: for every carve-eligible brush (`Hull`, `Window`, `Transparent`), each face is clipped against every later, spatially-overlapping carve-eligible brush in brush order, keeping only the portion of the face that isn't embedded in one of those later brushes (`clipPolygonOutsideBrush`, classic brush-CSG subtract -- clip sequentially against each carver face plane, keep the outside fragment, discard whatever's still inside after all of them). `Door`, `Detail`, `Trigger`, `Hint`, and `Water` brushes pass through unmodified and never carve or get carved -- `Door` is deliberately excluded even though it seals/splits like `Hull`, since a door's volume routinely overlaps the floor and walls at a doorway and nothing else fills that gap while it's open. Brush order and ids are preserved; only face geometry and per-brush bounds change on carved brushes.

`slopbsp` needs both versions of the brush list, for different reasons: BSP tree structure and leaf solidity classification test whether points fall inside a brush's full convex volume, which requires every one of its original faces to still bound it -- a carved (partially clipped) brush's remaining faces describe an unbounded region and would corrupt leaf classification map-wide. Only the *emitted surface geometry* -- what actually gets triangulated into the `.bsp` file's surface-faces section and rendered -- wants the carved, non-overlapping faces. So `slopbsp` loads the raw, uncarved brushes for tree-building and loads `slopcsg`'s carved output (from `compiled/csg`, asset kind `MapCarved`) for surface emission, and fails outright with "run slopcsg first" if that carved file doesn't exist yet. `slopmap`'s "Run All" and its dirty-tracking both know about this ordering: any brush geometry edit invalidates the carve stage, and invalidating carve cascades to invalidate BSP (and everything downstream of it) too.

# VIS: the Potentially Visible Set {#vis-the-potentially-visible-set}

Quake's `vis.exe` earned its reputation as the slowest tool in the toolchain, sometimes running for hours on a large map, because what it computes is heavy. For every leaf in the BSP tree, which other leaves could possibly be seen from it, looking through however many portals connect them? Once that's answered offline, the runtime question becomes trivial: look up the current leaf, check one bit per candidate leaf, and skip rendering (and later, skip a lot of gameplay bookkeeping too) for anything not marked visible. On mid-90s hardware, that was the only way a game with indoor, occluding geometry could keep a stable frame rate.

The engine's `.vis` file is exactly that precomputed bitset: a `leafCount × leafCount` matrix, packed one bit per leaf pair into `wordsPerRow` 32-bit words, row-major so that testing "can leaf A see leaf B" is a single word fetch and mask (`slopengine::pvsCanSee`). It's surfaced to package scripts as `(pvs-can-see x0 y0 z0 x1 y1 z1)`, which actors' sight and audio-occlusion checks lean on instead of doing a full linescan every time.

# NAV: baked navigation {#nav-baked-navigation}

Everything above this stage exists to answer "what can be seen" and "what can be drawn." `slopnav` answers a different question: "where can an actor walk." Rather than routing actors over the BSP's leaf/portal graph directly (accurate, but as fine-grained and irregular as the brushwork itself, since leaves are a rendering/visibility partition, not a walkability one), `slopnav` voxelizes the map's walkable static geometry through Recast's heightfield → compact heightfield → region → contour → polymesh pipeline, producing a much coarser, cleaner graph of walkable polygons sized to an actual agent capsule.

A map can bake more than one such graph, one per entry in its `*package-nav-profiles*` catalog (see @ref nav-profiles), each with its own agent radius, height, max step, and max slope -- a crouching enemy and a wide-bodied boss don't need to share a navmesh eroded for the same body size. Every profile is written to its own file under `compiled/nav/` (see @ref nav for the on-disk format); a thing-def opts into a specific profile via its `motor`'s `nav-profile` clause, or is auto-matched at spawn to the smallest profile that fits its own capsule.

Because it only voxelizes static geometry already resolved by `slopbsp`, `slopnav` depends on BSP but not on VIS or RAD, and produces no visual output of its own -- it's purely the data `nav-*` script calls (pathfinding, flow fields, funnel-pulled waypoints) query at runtime.

# RAD: baking light {#rad-baking-light}

Radiosity is older than computer graphics; it's a heat-transfer technique for modeling how energy radiates between surfaces. Cindy Goral, Kenneth Torrance, Donald Greenberg, and Bennett Battaile brought it into computer graphics at Cornell in 1984 ("Modeling the Interaction of Light Between Diffuse Surfaces"), treating light the same way: every diffuse surface both receives and re-emits light to every other surface it can see.

That's expensive, so Quake's `light.exe` took a shortcut: bake direct light (plus a fudge factor for bounce) into a lightmap texture offline, and just sample that texture at runtime. No per-frame lighting computation, no per-frame bounce simulation, just a texture fetch. It's a huge reason Quake's diffuse lighting still looks as good as it does on hardware from that era.

This engine's `sloprad` is closer to the original Cornell method than Quake's approximation: `RadiositySettings` has a real `bounces` count and per-bounce `samples`, direct lighting from a stratified NxN grid per emissive face, and an optional GPU compute path for both the direct and bounce passes on top of the CPU reference implementation. The output, `.rad`, doesn't embed the actual pixels; it stores atlas metadata and UV chart placement per baked face, with the pixels themselves living in separate PNGs (`compiled/rad/atlas0.png` and so on) that the renderer composites with dynamic lights at draw time.
