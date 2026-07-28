# slopmap

CSG level / prefab / things editor (raylib + rlImGui). Writes map and prefab source under --target. File formats and compile algorithms stay on their own guides; this page covers how to build and run the editor.

Related: [Maps](maps.md), [Things](things.md), [Lights](lights.md), [Icons](icons.md), [BSP](bsp.md), [FAC](fac.md), [VIS](vis.md), [Radiosity](rad.md), [slopsprite](slopsprite.md).

## Build and CLI

CMake target slopmap (root CMakeLists.txt), linked against sloplib plus rlImGui.

```bash
cmake --build build --target slopmap

./build/slopmap --base-game {package-path} [--mod {path}]... --target {package-path} [--map {name}]
```

| Flag | Meaning |
|------|---------|
| --base-game | Base package to mount (required) |
| --mod | Additional package; repeatable |
| --target | Package that receives saves (required; must be one of the mounted packages) |
| --map | Map folder name under a mounted package (optional; omit for untitled) |

--target is always required. New / Save As pickers choose a write package among the mounted set; they do not invent a target from --base-game alone. UI chrome uses the silk icon atlas; see [Icons](icons.md).

## Scenes and modes

The editor keeps two documents in play. Level scene is the open map: maps/{name}/static.csg and things.s7. Prefab scene is for authoring a reusable brush assembly under prefabs/{path}.csg (and an optional .s7 sidecar) without mutating the level -- the level stays loaded in memory while you work on the prefab. Switch with the Prefab / Level menus. Prefab -> New / Open / Save As writes under --target or whichever mounted package the picker selects; the path you choose is the virtual path used when placing instances later. Level save round-trips (prefab ...) references rather than exploding them into brushes (explode-to-brushes is not implemented yet).

Work happens in one of three modes from the toolbar or Tools menu (there are no digit 1 / 2 / 3 shortcuts). Select picks and transforms brushes, faces, edges, verts, or entities -- switch selection scope from the Select toolbar or Edit menu. Create draws new brush solids into the active scene. Place drops content into the Level: prefab instances from the Prefabs panel, or thing kinds from Library -> Things. Place is Level-only for CSG prefabs; Prefab scene is for editing the assembly itself.

## Authoring

Create mode offers box, cylinder, and stairs primitives, optionally hollow. New brushes in Prefab scene default to detail so furniture-style pieces do not accidentally join the structural shell. H / Edit -> Toggle Brush Role cycles hull -> detail -> hint -> trigger -> water -> window. L / Edit -> Toggle UV Lock pins textures on the selected brush (or face in face scope).

Brush role is the important authoring choice: hull and window participate in the sealed BSP shell; detail and the other non-hull roles must sit inside that sealed interior. Use hull for walls, floors, and ceilings that keep the void out; use detail for clutter that should still draw and collide but must not punch holes in the shell. See [BSP: hull vs non-hull](bsp.md#why-hull-and-non-hull).

Clip (Shift+X / Edit -> Clip) picks two points on a selected brush face; the cut plane is perpendicular to that face through those points and splits the selected brush(es). F cycles keep Front / Back / Both, Shift+F flips the plane, Enter commits, Esc cancels. Clip does not reject non-convex halves; fix brush shape in the editor before compile/load. Punch-out subtracts from brushes using selected faces when you need openings without rebuilding the solid by hand.

In Select mode, Edge and Vert scopes pick brush edges or vertices (screen-space, with click-cycle through overlaps). G translates the selection with free/XYZ drag and grid snap like Brush move: co-located face vertices on the same brush move together (shared-vertex weld). Intermediate non-convex shapes are allowed while editing; brushes must still be convex to load via brush-convex. R does not rotate edges or verts. Delete removes the parent brush(es), same as Face scope.

In Level scene, pick a prefab in Library → Prefabs and use Place to drop instances; Select moves them with G and rotates yaw by 90 deg with R. Things work the same way through Library → Things and the Scene outliner. Scene tabs Brushes / Prefabs / Things (Properties below for the selection). Library tabs Materials / Texture / Things / Prefabs. Prefab scene can load and save optional prefabs/{path}.s7 sidecars for attached entities. Material browser, texture panel, and brush panel edit face appearance and UV fields on the open document. In face selection scope, Properties shows face `on-use` / `on-touch` from the package map-handler catalog (combo + typed params, or Custom for legacy names). Thing Properties edits trigger `on-enter` / `on-exit`, usable/mover `on-use`, and pickup touch/use (presentation, `on-enter` + size, `on-use`) the same way. Thing/brush/face param pickers write resolved runtime ids from the open Level document. Face handlers do not change brush role (a hull face can still seal).

For volume enter/exit triggers, draw a brush then Edit -> Convert to Trigger: the AABB becomes a `(trigger …)` thing in things.s7 and the brush is removed from CSG. Or Place a trigger from Library -> Things and set on-enter / on-exit / size in Properties.

For CSG doors, author a brush in the doorway and set role **door** (Edit -> Set as Door, role combo, or `H`). Door properties (raise / slide / swing, hinge, `can-use`) appear only for that role. Compile/load omit the leaf from static FAC/collision. Elevators and other platforms still use Edit -> Convert to Mover on detail brushes. See [Things: Doors](things.md#doors-brush-door) and [Movers](things.md#movers-mover).

## View

Cameras are Perspective, Top, Front, and Side. Grid size steps with [ ] \. While translating, O toggles snap between Offset and Absolute. View -> Transform Space chooses Global (world axes) or Relative (face move along the face normal). Ignore backfaces is a view toggle for picking through one-sided faces.

Z cycles viewport fill so you can judge source CSG versus compiled results. Wireframe and Solid are quick CSG readouts (edges only, or faux-shaded solids). Textures shows CSG albedo. Unlit and Lit show the compiled FAC mesh without and with lightmaps. SolidLit shades the CSG solids with the bake when you want structure and lighting together without switching fully to the FAC mesh.

X-Ray Overlay (Shift+Z, toolbar **XRay**, or View -> X-Ray Overlay) draws brush edges on top of the fill: Off, Visible (depth-tested), or All (every edge through the fill with depth off). Use it when nested or overlapping brushes hide each other in solid fills.

## Compile and play

Compile menu spawns the same CLI tools as a hand-run bake: Run BSP, Run FAC, Run VIS, Run RAD, or **Run All** (slopbsp -> slopfac -> slopvis -> sloprad). Dirty markers show when source is ahead of compile data. Clean BSP / FAC / VIS / RAD / All removes generated files for the open map. RAD Options sets luxels, bounces, samples, and GPU/CPU. Play Map (F5) launches slopengine with the current package mount and map.

Compile order and rebuild rules: [Maps](maps.md). Tool details: [BSP](bsp.md), [FAC](fac.md), [VIS](vis.md), [Radiosity](rad.md).

## Shortcuts

| Key | Action |
|-----|--------|
| Ctrl+N / Ctrl+O / Ctrl+S | New / Open / Save |
| Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y | Undo / Redo |
| F5 | Play Map |
| G / R | Move / rotate yaw (Select; R is Brush/Entity only) |
| H / L | Toggle brush role / UV lock |
| Shift+X | Clip tool |
| Tab | Toggle ortho top view |
| Z / Shift+Z | Cycle fill / X-Ray |
| [ ] \ | Grid |
| Home | Frame selection / origin |
| Delete | Delete selection |
