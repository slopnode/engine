# Geometry

slopengine has two geometry pipelines that meet only at runtime.

Props and characters are Blender meshes under geometry/ (plus skeletons/ when skinned): arbitrary triangulated .geo / .vert with optional .weights, optional skinning, no lightmaps. Level geometry is Scheme CSG under maps/{name}/: convex brushes (brush-convex, with brush-box sugar), never skinned, compiled to .bsp / .vis and optionally lightmapped under rad/.

Both paths end as the same runtime types (GeoAsset + VertBuffer) and are drawn through buildModelFromGeo. They are authored and packaged differently on purpose.

Skeleton and clip export for skinned meshes are covered in [Skeletal animation](animation.md). This page covers .geo / .vert data and how the Blender exporter writes them.

## Prop and character geometry

Props and characters live under geometry/ as a text descriptor plus binary buffers that share a virtual path stem:

```text
geometry/{asset}/{asset}.geo
geometry/{asset}/{asset}.vert
geometry/{asset}/{asset}.weights   # skinned only
```

Virtual path {asset}/{asset} resolves those files (weights only when present and declared).

### .geo

S-expression descriptor. Typical skinned export:

```text
(geo
  (vertices implicit)
  (weights implicit)
  (skeleton "character")
  (primitives
   (
    (name "0"
     material "characters/skin"
     vertex-offset 0
     vertex-count 1000
     index-offset 0
     index-count 3000)
    ...
   )))
```

| Clause | Meaning |
|--------|---------|
| (vertices implicit) | Vertex data is the sibling .vert (always how the loader resolves it) |
| (weights implicit) | Skin data is the sibling .weights |
| (skeleton "...") | Skeleton id for skinning |
| primitives | Named slices into the shared buffers, each with a material virtual path |

Static meshes omit weights and skeleton. One .geo can contain several primitives (usually one per material).

### .vert / .weights

Binary companions of .geo: .vert (DLKV) holds positions, optional normals/UVs, and indices; .weights (DLKW) holds four joints/weights per vertex for skinned exports. Layouts: [Binary formats — DLKV](binary-formats.md#dlkv-vert), [DLKW](binary-formats.md#dlkw-weights). Bind pose matrices: [Skeletal animation](animation.md#binary-bind-and-tracks).

### Runtime load

AssetStore::getGeoModel loads .geo + .vert, optionally .weights, resolves each primitive's material, and binds a skeleton when skeleton is set. Blender-exported props do not receive lightmap UVs; that channel is used by level compile only.

## Level geometry (CSG)

Levels are not Blender meshes. They are authored as Scheme brushes under maps/{name}/, then compiled with slopbsp, slopvis, and optionally sloprad. See [Maps](maps.md) for the full authoring and compile pipeline.

There is no package .geo / .vert for the level itself. At load time, VIS faces (or brushes as a fallback) compile into the same in-memory geometry types used by props.

## Two pipelines, no automatic bridge

There is no converter either direction. The Blender exporter never writes .csg, and CSG never emits package .geo / .vert files -- level faces become the same in-memory geometry types at load time only.

Use CSG for rooms and structural solids so they participate in BSP, VIS, and radiosity. Use Blender .geo for characters, props, and clutter meshes -- especially anything movable or skinned. World shells, floors, and fixed detail boxes belong in CSG; placeable models belong as prop assets.

## Blender exporter

Addon: tools/blender/slopengine_exporter (File -> Export -> Slopengine).

| Menu item | Writes | Input |
|-----------|--------|-------|
| Geometry | .geo + .vert [+ .weights] | Selected mesh objects |
| Skeleton | .skel + .bind | Armature |
| Animation | .anim + .tracks | Armature |
| Multiple | All of the above into package folders | Armature + selected meshes |

The exporter never chooses between CSG and .geo. Everything it emits is prop/character content under geometry/, skeletons/, and animations/.

### How Geometry export decides formats

Only MESH objects are exported for geo. Armatures, empties, lights, and other types are ignored (armatures are discovered only via Armature modifiers for skinning).

A mesh with an Armature modifier and target is treated as skinned; otherwise it is static. All selected meshes must share the same armature, or all be static. Skinned output requires a skeleton id, writes .weights, and adds (weights implicit) plus (skeleton "...") to the .geo. Static output omits .weights; a skeleton id field is ignored with a warning.

Materials become primitives: one primitive per material slot. The Blender material name becomes the material virtual path (leading materials/ and known extensions stripped; . without / becomes /; empty -> default/unassigned).

Mesh processing bakes object transforms (into armature space when skinned, world when static), converts Blender Y-up to engine Z-up, triangulates, requires UVs (creates ExportUV if missing), and keeps the top four vertex-group influences per vertex. Skinned export temporarily evaluates the armature at bind pose before writing buffers.

### Multiple export layout

With project root = a package directory and asset name {asset}:

```text
{project_root}/skeletons/{asset}/{asset}.skel
{project_root}/geometry/{asset}/{asset}.geo
{project_root}/animations/{asset}/{asset}.anim
```

Skeleton id comes from the armature object name or a custom field. Geometry rules above still apply to the mesh half of the export.
