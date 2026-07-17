# Maps

Maps are first-person spaces built from brush solids, compiled for structure, then optionally lightmapped. Authoring is plain Scheme on disk; the shipped tools compile that source into BSP and radiosity data the game can load. Because the source is readable S-expression / s7 text, custom editors and generators that write the same files are welcome alongside the built-in tools.

Props and characters are separate mesh assets. This page covers world geometry under `maps/`. Mesh export is described in [Geometry](geometry.md); surface appearance in [Materials, textures, and shaders](materials.md).

## Folder layout

Each map is a directory under `maps/<name>/`:

```text
maps/<name>/
  map.meta
  static.csg
  static.bsp
  rad/
    static.rad
    atlas0.png
    ...
```

`map.meta` and `static.csg` are authored. `static.bsp` comes from `slopbsp`. The `rad/` folder comes from `sloprad` and may be omitted. `--map <name>` selects that folder name, not a file path.

Virtual paths used by the loader strip the `maps/` prefix and the file extension: `<name>/map` for meta, `<name>/static` for CSG and BSP, `<name>/rad/static` for the bake file, `<name>/rad/atlasN` for atlases.

## Authoring

### map.meta

Describes the map and which packages it needs:

```text
(map
  (id "my-map")
  (name "My Map")
  (package "com.example.game")
  (depends "com.example.game")
  (ambient 0.03 0.03 0.04))
```

`id` and `package` are required. `package` and each entry in `depends` must match mounted package ids. `name` is display-only. `ambient` is a soft fill color used when baking radiosity; if omitted, the tools use a small default gray-blue.

### static.csg

The authoritative level source. It is Scheme loaded through s7 with a small CSG API bound for the duration of the load. Top-level forms create brushes; an empty result is a failed map.

The canonical solid is a **convex polyhedron** of polygonal faces. Each face is an ordered loop of vertices (outward winding). Solids are the intersection of those face halfspaces.

```text
(brush-convex
  (id "wedge")
  (role "detail")
  (material "surfaces/stone")
  (faces
    (face
      (id "wedge/bottom")
      (verts (v 1.0 0.0 0.5) (v 2.0 0.0 0.5) (v 1.5 0.0 1.2)))
    (face
      (verts (v 1.0 0.0 0.5) (v 1.5 0.7 0.7) (v 2.0 0.0 0.5)))
    (face
      (verts (v 2.0 0.0 0.5) (v 1.5 0.7 0.7) (v 1.5 0.0 1.2)))
    (face
      (verts (v 1.5 0.0 1.2) (v 1.5 0.7 0.7) (v 1.0 0.0 0.5)))))
```

`brush-convex` requires `id` and at least four planar faces that form a closed convex. Optional `(role "hull")` or `(role "detail")` (default hull). Optional brush-level `(material ...)` fills in faces that omit their own. Per-face clauses may set `id`, `material`, `(uv-shift x y)`, `(nodraw)`, and `(verts (v x y z)...)`.

For convenience, `brush-box` expands an axis-aligned box into a six-face convex (same runtime representation):

```text
(brush-box
  (id "floor")
  (mins -4.0 -0.25 -4.0)
  (maxs 4.0 0.0 4.0)
  (material "surfaces/stone"))
```

`id`, `mins`, and `maxs` are required on `brush-box`. Optional `(faces ...)` overrides individual sides (`top`, `bottom`, `north`, `south`, `east`, `west`) with their own `id`, `material`, `(uv-shift x y)`, or `(nodraw)`. Future sugar such as `brush-circle` may expand other primitives the same way; the compiler always sees convexes.

`(nodraw)` marks a face as out of bounds for rendering: it is omitted from the compiled mesh and from radiosity charts, so it does not consume lightmap atlas space. The brush stays solid for physics and BSP occlusion. You can still set it explicitly on any face when you want nodraw true:

```text
(brush-box
  (id "floor")
  (mins -4.0 -0.25 -4.0)
  (maxs 4.0 0.0 4.0)
  (material "surfaces/stone")
  (faces
    (bottom (nodraw))))
```

Authored `(nodraw)` is never cleared by the tools. When the hull is sealed, `slopbsp` / `sloprad` / map load also infer nodraw on hull faces that never face sealed interior empty space (outer skins, buried sides) and OR that onto the face flags.

Default face ids look like `floor/top` for boxes, or `brushId/N` for convex faces without an explicit id. Radiosity charts key off those ids, so renaming a face id between bakes changes how atlases line up unless you re-bake.

Hull brushes form the structural shell of the space and are what seal the map. Detail brushes are still drawn and lightmapped, and become solid convex collision hulls, but they do not contribute splits to the BSP tree and **cannot seal** a leak. Place detail entirely inside the sealed hull (their center must sit in interior empty space). Use detail for props that should not reshape the leaf structure of the level.

You can edit `.csg` by hand, generate it from another program, or build a dedicated editor. The contract is the file on disk and the brush API, not a particular authoring UI.

### Materials on brushes

Face materials drive diffuse appearance and bake sampling. `texel-size` on the material controls how densely the texture tiles in world space. Emission fields on the material feed radiosity (see the materials guide). After changing materials or emitters that affect a map, re-run `sloprad` if you rely on baked lighting.

## Compile order

Author `map.meta` and `static.csg` first. Run `slopbsp` to produce `static.bsp`. Run `sloprad` only after a current BSP exists; it refuses to bake without one. The game requires meta, CSG, and BSP to load a map. Radiosity is optional: without `rad/` the map still loads, but without baked lightmaps.

Typical sequence:

```bash
./build/slopbsp --base-game <package-path> --map <name>

./build/sloprad --base-game <package-path> --map <name> \
  --luxels-per-meter 16 --bounces 2 --samples 32

./build/slopengine --base-game <package-path> --map <name>
```

`--mod` may be repeated on any of these, the same as the game. After editing brushes, rebuild BSP (and radiosity if you use it). After editing only materials or emission for lighting, BSP can stay; re-bake radiosity.

## BSP compilation (`slopbsp`)

```bash
./build/slopbsp --base-game <package-path> [--mod <path>]... --map <name>
```

The tool mounts packages, loads map meta and brushes from Scheme, keeps hull brushes for structure, and writes `static.bsp` next to `static.csg`.

Build steps, in outline: gather hull convexes; pad the world bounds slightly; collect unique face planes from hull faces; recursively partition space with general planes into polyhedral solid or empty leaves; record adjacency between empty leaves; emit occlusion surfaces from hull faces that face interior empty space; write a binary `BSP2` file; flood exterior empty leaves from the padded bounds to detect leaks and infer hull auto-nodraw.

If exterior empty reaches the playable volume, `slopbsp` still writes the BSP (for debug) but exits with an error and prints a leaf-center leak path. When the hull is sealed, it logs exterior/interior empty leaf counts and how many hull faces were inferred nodraw. Detail brushes whose centers lie outside sealed interior empty space produce warnings only.

The BSP is structural and occlusion data. It is what radiosity uses when testing whether light is blocked, and what runtime uses for leaf-related debug. The visible level mesh is still compiled from brush faces at load time (triangulated polygons). Player collision uses convex hulls built from each brush’s face vertices through the physics library, not by walking the BSP tree as a mesh.

Detail brushes are skipped when building that tree. If you only change detail solids, you may still want a fresh BSP when face ids or neighboring hulls change in ways that affect surfaces, but detail-only decoration does not add new BSP splits.

## Radiosity compilation (`sloprad`)

```bash
./build/sloprad --base-game <package-path> [--mod <path>]... --map <name> \
  [--luxels-per-meter N] [--bounces N] [--samples N]
```

Defaults are 16 luxels per meter, 2 bounces, and 32 samples per luxel per bounce. Atlas resolution is fixed at 512×512 in the current settings (not a CLI flag). Lower luxel density, zero bounces, and few samples are useful for quick previews; higher values cost more time and memory.

The tool requires `static.bsp`. It reloads brushes from `static.csg`, clears and recreates `maps/<name>/rad/`, then writes `static.rad` plus `atlas0.png`, `atlas1.png`, and so on as needed.

Bake outline as implemented today:

1. Analyze the hull (same exterior flood as `slopbsp`): if sealed, OR inferred nodraw onto hull faces; if leaky, warn and keep authored `(nodraw)` only.
2. Collect lightmap faces from every brush face that is not nodraw (hull and detail).
3. Pack faces into charts on one or more atlases.
4. Resolve materials; sample albedo and emission using the same planar UV rules as the game (`texel-size`, texture size, `uv-shift`).
5. Build acceleration structures: BSP surfaces for occlusion, lightmap faces for gathering light.
6. Seed receivers with map ambient plus emission; build emitter patches from bright emission luxels.
7. Compute direct lighting with form factors and BSP occlusion tests.
8. Run bounce passes with cosine-weighted hemisphere samples (skipped when `--bounces 0`).
9. Tonemap into RGB atlas images and write the `.rad` sidecar that points at those atlases.

`--bounces 0` keeps ambient, emission, and direct light only—no indirect loop. Emission textures matter during this bake; at runtime the lightmap shader adds a flat emit from the material’s emission color when emission power is set, and does not re-sample the emission map.

## Loading in the game

With `--map <name>`, the game validates meta and package dependencies, loads CSG brushes, reads `static.bsp`, and optionally reads `rad/`. If the BSP hull is sealed, the same auto-nodraw pass as `sloprad` runs before mesh compile. Brushes are compiled into render meshes (skipping nodraw faces): diffuse UVs from materials, lightmap UVs from charts when a bake is present. If radiosity data and atlases load successfully, materials on the map use the lightmap shader and bind the atlases. Otherwise the map draws without baked lighting.

All brushes from the CSG are registered as static convex physics hulls. Missing BSP stops the load; missing rad only skips lightmaps.

## Custom tooling

`static.csg` and `map.meta` are ordinary text. Any workflow that emits valid brush forms and meta fields is fine: hand editing, scripts, or a full map editor. The compile tools and the game care about the files and the brush API, not how those files were produced. Re-run `slopbsp` and `sloprad` after your tools rewrite source the same way you would after a manual edit.
