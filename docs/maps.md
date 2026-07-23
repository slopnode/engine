# Maps

Maps are first-person spaces built from brush solids, compiled for structure and visible faces, then optionally lightmapped. Authoring is plain Scheme on disk; the shipped tools compile that source into BSP, VIS, and radiosity data the game can load. Because the source is readable S-expression / s7 text, custom editors and generators that write the same files are welcome alongside the built-in tools. The interactive editor is [slopmap](slopmap.md).

Props and characters are separate .geo assets. This page covers world geometry under maps/. Prop export is described in [Geometry](geometry.md); surface appearance in [Materials, textures, and shaders](materials.md).

## Folder layout

Each map is a directory under maps/{name}/:

```text
maps/{name}/
  map.meta
  static.csg
  things.s7
  graphs.s7
  static.bsp
  static.vis
  rad/
    static.rad
    atlas0.png
    ...
```

map.meta and static.csg are authored. things.s7 places props, usables, actors, and lights (optional). graphs.s7 is optional nav-graph Scheme loaded with the map (see [Scripting](scripting.md)). static.bsp comes from slopbsp. static.vis comes from slopvis (visible face fragments for draw, lightmaps, and audio). The rad/ folder comes from sloprad and may be omitted. --map {name} selects that folder name, not a file path.

Virtual paths used by the loader strip the maps/ prefix and the file extension: {name}/map for meta, {name}/static for CSG, BSP, and VIS, {name}/things for things, {name}/rad/static for the bake file, {name}/rad/atlasN for atlases.

## Authoring

### map.meta

Describes the map and which *other* packages it needs mounted. Owning package is implied by the directory that contains the map (under that package's maps/).

```text
(map
  (id "my-map")
  (name "My Map")
  (depends)
  (ambient 0.03 0.03 0.04))
```

id is required. (depends ...) lists other package ids that must be mounted when this map uses their assets; omit or leave empty when the map only uses its own package. A legacy (package ...) field is ignored. name is display-only. ambient is a soft fill color used when baking radiosity; if omitted, the tools use a small default gray-blue.

### static.csg

The authoritative level source. It is Scheme loaded through s7 with a small CSG API bound for the duration of the load. Top-level forms create brushes; an empty result is a failed map.

The canonical solid is a convex polyhedron of polygonal faces. Each face is an ordered loop of vertices (outward winding). Solids are the intersection of those face halfspaces.

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

brush-convex requires id and at least four planar faces that form a closed convex. Optional (role ...) (default hull). Optional brush-level (material ...) fills in faces that omit their own. Per-face clauses may set id, material, (uv-shift x y), (uv-scale sx sy), (uv-lock), (uv-axes ux uy uz vx vy vz), (nodraw), and (verts (v x y z)...).

| Role | Splits | Seals | VIS faces | Default physics | Notes |
|------|--------|-------|-----------|-----------------|-------|
| hull | yes | yes (Solid) | yes | collide | Structural shell |
| detail | no | no | yes | collide | Must sit in sealed interior open space |
| hint | yes | no | no | no collide | Split-only; schema stub |
| trigger | no | no | no | no collide | Marks open leaves Trigger; brush callbacks later |
| water | yes | no | yes | no collide | Marks open leaves Water; gameplay later |
| window | yes | yes (Glass) | yes | collide | Fills openings; later fake-glass rad + breakable prop |

Open leaves may also carry Water / Trigger bits. Flood / sealing treat Solid and Glass as blocked. Why the hull/non-hull split exists (versus modern mesh editors): [BSP](bsp.md#why-hull-and-non-hull).

(uv-lock) pins planar texture coordinates to the face. Locked faces store UV axes (defaulting to the usual world-axial basis for the face normal). Prefab thing and editor transforms rotate those axes with the geometry and adjust uv-shift, so the texture stays glued under move and rotate. Optional (uv-axes ...) overrides the basis when it differs from axial. Omit (uv-lock) to keep world-aligned tiling (default). Optional (uv-scale sx sy) multiplies material texel-size per UV axis (default 1 1).

For convenience, brush-box expands an axis-aligned box into a six-face convex (same runtime representation):

```text
(brush-box
  (id "floor")
  (mins -4.0 -0.25 -4.0)
  (maxs 4.0 0.0 4.0)
  (material "surfaces/stone"))
```

id, mins, and maxs are required on brush-box. Optional (faces ...) overrides individual sides (top, bottom, north, south, east, west) with their own id, material, (uv-shift x y), (uv-scale sx sy), (uv-lock), (uv-axes ...), or (nodraw). Future sugar such as brush-circle may expand other primitives the same way; the compiler always sees convexes.

(prefab ...) instances a brush assembly from prefabs/{path}.csg. Required: path string argument and (id ...). Optional (at x y z) (default origin) and (angles pitch yaw roll) in radians (default zero). No scale. At load/compile the prefab expands into ordinary brushes: local brush/face ids become {instance-id}/{local-id}, vertices are rotated then translated, and rotated boxes are no longer treated as axis-aligned. Faces marked (uv-lock) in the prefab keep their local texture thing under that transform. Brush role / nocollide come from the prefab author (hull modular rooms and detail furniture both work). Prefabs may nest; cycles error. Map files keep (prefab ...) references (they are not baked on save).

```text
(prefab "furniture/desk"
  (id "desk-a")
  (at -1.5 0.0 -1.5)
  (angles 0.0 0.0 0.0))
```

Prefabs are authored and placed in [slopmap](slopmap.md) (separate Prefab scene, Place mode in Level). Explode-to-brushes is not implemented yet.

### things.s7

Optional Scheme file of things loaded after map geometry. Engine bindings spawn flecs entities for the level. Missing file keeps geometry and uses the default player spawn (0, 0.1, 0) facing yaw pi.

[slopmap](slopmap.md) loads and saves this file with the level (and optional prefabs/{path}.s7 sidecars in Prefab scene).

```text
(player-start
  (id "start")
  (at 0.0 0.1 0.0)
  (yaw 3.14159))

(prop
  (id "guard-a")
  (at -2.0 0.0 -2.0)
  (yaw 0.0)
  (sprite "characters/guard")
  (anim "walk" #t))

(usable
  (id "use-test")
  (at 1.5 0.0 0.0)
  (yaw 0.0)
  (sprite "characters/guard")
  (frame "A")
  (prompt "Test use")
  (on-use "on-use-test"))

(point-light
  (id "lamp-a")
  (at 0.0 2.0 0.0)
  (color 1.0 0.95 0.9)
  (intensity 1.0)
  (range 8.0))
```

| Form | Required | Notes |
|------|----------|-------|
| player-start | id, at | First wins; sets player spawn pose. Optional yaw (radians). See [Player](player.md). |
| prop | id, at, exactly one of sprite / geo | Optional yaw, frame, (anim clip [loop]). See [Things](things.md). |
| usable | same as prop | Adds interact prompt; (on-use "handler") names a Scheme procedure called with the entity id on Interact. See [Things](things.md). |
| actor | same as prop | Adds character motor + tags; optional (motor ...) / (tags ...). See [Things](things.md#actors). |
| point-light | id, at | Optional color, intensity, range. Baked by sloprad; see [Lights](lights.md). |
| spot-light | id, at | Optional yaw/angles, color, intensity, range, cone. Baked by sloprad. |
| area-light | id, at | Optional angles, color, intensity, size. Authoring / gizmo; not a bake emitter. |
| sun | id | Optional at (gizmo), angles/yaw, color, intensity. Authoring / gizmo. |
| prefab | path, id | Loads optional prefabs/{path}.s7 with the same at / angles as CSG instances; missing sidecar is a no-op. Entity ids are prefixed with the instance id. |

on-use handlers live in package scripts (for example scripts/things.s7). If the handler is missing, Interact falls back to the inspect UI. Props, usables, lights, actors, and scripting are covered in [Things](things.md). Baked vs dynamic lighting is covered in [Lights](lights.md).

(nodraw) marks a face as out of bounds for rendering: it is omitted from the compiled mesh and from radiosity charts, so it does not consume lightmap atlas space. The brush stays solid for physics and BSP occlusion. You can still set it explicitly on any face when you want nodraw true:

```text
(brush-box
  (id "floor")
  (mins -4.0 -0.25 -4.0)
  (maxs 4.0 0.0 4.0)
  (material "surfaces/stone")
  (faces
    (bottom (nodraw))))
```

Authored (nodraw) is never cleared by the tools. When the hull is sealed, slopvis clips hull and detail faces to sealed interior empty space and treats faces with no remaining visible area as inferred nodraw (outer skins, buried sides, buried detail). Map load ORs that onto brush face flags. Large faces that are only partly playable keep only the interior-visible fragment(s) in static.vis rather than a whole-face keep or drop.

Default authored face ids look like floor/top for boxes, or brushId/N for convex faces without an explicit id. After VIS, drawable hull fragments use ids such as floor/top#0; coplanar merges across sources use merge/... ids. Radiosity charts key off those VIS face ids, so renaming a face id or rebuilding VIS without re-baking changes how atlases line up.

Hull and window brushes form the structural shell and seal the map (Solid / Glass leaf contents). Detail, hint, trigger, and water must sit in sealed interior open space (their centers are checked). Detail and water still draw/lightmap; hint and trigger do not. Hint planes reshape the tree without sealing. Window is reserved for later fake-glass radiosity transmission and breakable props; for now it seals and draws like a thin hull.

You can edit .csg by hand, generate it from another program, or build a dedicated editor. The contract is the file on disk and the brush API, not a particular authoring UI.

### Materials on brushes

Face materials drive diffuse appearance and bake sampling (albedo + emission only; no normal maps). texel-size on the material controls how densely the texture tiles in world space. Emission fields on the material feed radiosity (see the materials guide). After changing materials or emitters that affect a map, re-run sloprad if you rely on baked lighting.

## Compile order

Author map.meta and static.csg first. Run slopbsp -> slopvis -> sloprad. Order is strict: VIS refuses an unsealed hull / missing BSP; radiosity refuses a missing VIS. The game requires meta, CSG, and BSP to load. VIS is preferred for the draw mesh (if missing, the loader builds visible faces in memory and warns). Radiosity is optional: without rad/ the map still loads, but without baked lightmaps.

The same sequence is available in [slopmap](slopmap.md) under Compile -> Run BSP / Run VIS / Run RAD / **Run All** (plus Clean and RAD Options).

| Stage | Input | Output | Required to load map? |
|-------|--------|--------|------------------------|
| Author | - | map.meta, static.csg (+ optional things.s7) | meta + CSG yes |
| slopbsp | meta + CSG (hull brushes) | static.bsp | yes |
| slopvis | CSG + static.bsp | static.vis | preferred (in-memory fallback) |
| sloprad | meta + CSG + static.bsp + static.vis + materials/textures | rad/static.rad, rad/atlasN.png | no |

Typical sequence:

```bash
./build/slopbsp --base-game {package-path} --map {name}

./build/slopvis --base-game {package-path} --map {name}

./build/sloprad --base-game {package-path} --map {name} \
  --luxels-per-meter 16 --bounces 2 --samples 16

./build/slopengine --base-game {package-path} --map {name}   # --map is a package CLI flag when declared in data/cli.s7
```

--mod may be repeated on any of these, the same as the game. After editing brushes, rebuild BSP, then VIS, then radiosity if you use it. After editing only materials or emission for lighting, BSP and VIS can stay; re-bake radiosity.

### Rebuild cheatsheet

| You changed... | Run |
|--------------|-----|
| Hull brushes, sealing, or hull face layout | slopbsp, then slopvis, then sloprad if you use lightmaps |
| Detail brushes only (no hull / face-id churn) | slopvis, then sloprad; run slopbsp if you care about detail-outside warnings or stale analysis |
| Face ids | slopvis then sloprad (charts key off VIS ids); slopbsp if hull planes changed |
| Materials, albedo, emission, ambient | sloprad |
| things.s7 light thing change | Re-bake sloprad if point/spot should change static light; runtime DynamicLight is separate ([Lights](lights.md)) |
| Authored (nodraw) | slopvis, then sloprad |

## BSP, VIS, and radiosity tools

```bash
./build/slopbsp --base-game {package-path} [--mod {path}]... --map {name}

./build/slopvis --base-game {package-path} [--mod {path}]... --map {name}

./build/sloprad --base-game {package-path} [--mod {path}]... --map {name} \
  [--luxels-per-meter N] [--bounces N] [--samples N] [--gpu|--cpu]
```

slopbsp mounts packages, loads brushes, builds a hull-only tree, and writes static.bsp next to static.csg. On a leak it still writes the file for debug but exits with an error and a leaf-center path. On a seal it reports exterior/interior empty counts and a preview of visible-face / inferred-nodraw counts (run slopvis to write static.vis). Detail brushes do not split the tree and cannot seal. Details: [BSP](bsp.md).

slopvis requires a sealed static.bsp. It clips hull and detail faces to sealed interior empty leaves, welds T-junctions, snap-welds coincident verts, culls slivers, merges compatible coplanar fragments, sorts by material, and writes static.vis. This is not classic leaf<->leaf PVS; it is the visible face set for draw, bake, and audio. Details: [VIS](vis.md).

sloprad requires BSP and VIS. It collects lightmap faces from static.vis, clears maps/{name}/rad/, and writes static.rad plus atlasN.png. Defaults are 16 luxels per meter, 2 bounces, 16 samples; atlas size is 1024^2 (not a CLI flag). --gpu (default) prefers GPU compute for direct and bounce; --cpu forces the CPU paths. --bounces 0 keeps ambient, emission, and direct light only. Emission textures matter at bake time; at runtime the lightmap shader uses flat material emission color/power. Details: [Radiosity](rad.md).

The BSP is structural (sealing, runtime leaf debug). Draw meshes and lightmap charts come from VIS faces; collision from per-brush convex hulls. Bake-time occlusion uses a BVH of lightmap faces.

## Loading in the game

With --map {name}, the game validates meta and package dependencies, loads CSG brushes, reads static.bsp, prefers static.vis for the draw mesh, and optionally reads rad/. Missing VIS warns and builds visible faces in memory when the hull is sealed; otherwise it falls back to authored non-nodraw brush faces. Diffuse UVs come from materials; lightmap UVs from charts when a bake is present. If radiosity data and atlases load successfully, materials on the map use the lightmap shader and bind the atlases. Otherwise the map draws without baked lighting.

All brushes from the CSG are registered as static convex physics hulls. Missing BSP stops the load; missing rad only skips lightmaps. After geometry is up, the game evaluates things.s7 when present, then spawns the player at player-start (or the default pose).

## Custom tooling

static.csg and map.meta are ordinary text. Any workflow that emits valid brush forms and meta fields is fine: hand editing, scripts, or a full map editor. The compile tools and the game care about the files and the brush API, not how those files were produced. Re-run slopbsp, slopvis, and sloprad after your tools rewrite source the same way you would after a manual edit.
