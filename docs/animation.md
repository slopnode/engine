# Skeletal animation

This page covers skinned-mesh clips from Blender (`.skel` / `.anim` / `.tracks`). Doom-style sprite clip banks (`.spanim`) are documented under [Sprites](sprites.md).

Blender animation export is for skinned meshes: an armature’s bone poses become clips that deform a weighted mesh at runtime. The export path deliberately records skeleton transforms only. That keeps the mapping from Blender unambiguous (one armature, one skeleton id, one set of bone tracks) rather than trying to interpret arbitrary object motion, constraints, or empty hierarchies as game animation.

Moving rigid props, doors, pickups, or other whole entities is not what this export is for. Those motions belong on gameplay entities through component animator systems that drive entity transforms (and related components) in the running game. Do not keyframe a rigid mesh in Blender and expect an `.anim` file to scrub its object transform.

Mesh and skin weight export is covered in [Geometry](geometry.md). Package folders are summarized in [Package structure](package-structure.md).

## What gets exported

From Blender you typically need three related assets that share a skeleton id:

- Skeleton: bone hierarchy and bind data (`.skel`, optional `.bind`)
- Skinned geometry: mesh with weights referencing that skeleton (`.geo`, `.vert`, `.weights`)
- Animation: clips sampled from the armature’s actions (`.anim`, `.tracks`)

The addon lives under `tools/blender/slopengine_exporter` (File → Export → Slopengine). Animation exports clips from the selected armature. Skeleton exports the bind hierarchy. Geometry exports selected meshes (skinned when they use an Armature modifier). Multiple writes all three into a package’s `skeletons/`, `geometry/`, and `animations/` folders for one asset name.

## Skeleton-only targeting

Every exported `.anim` names a skeleton:

```text
(anim
  (skeleton "character")
  (clips
   (
    (name "walk"
     fps 30
     duration 1.0
     tracks "character_walk.tracks")
   )))
```

Tracks are per-bone pose samples for that skeleton. Bone count must match the skeleton. The exporter:

- Requires an armature (not a loose mesh or empty)
- Collects actions on that armature (current action and NLA clip actions)
- Samples each action in pose mode at the chosen FPS
- Writes bone matrices (converted from Blender’s Y-up space into the engine’s Z-up space)
- Does not sample mesh object keyframes, parented empties, or non-armature object animation

Object transforms on meshes are baked into the geometry at geo export time. They are not animated by `.anim` / `.tracks`. If a character should walk, that motion is bones deforming a skinned mesh (and/or moving the entity with gameplay code), not Blender object-level location keys on the mesh.

That split is intentional. Blender scenes mix many kinds of animation; limiting the file format to skeleton pose removes guesswork about which objects “count” as the character.

## Exporting clips from Blender

Select the armature. Use File → Export → Slopengine → Animation (or Multiple). Provide the same skeleton id the skinned `.geo` and `.skel` use. Choose an FPS (default 30); duration and sample count are derived from each action’s frame range and the scene frame rate.

Each action becomes one clip. Track files are written beside the `.anim` (names like `<anim>_<clip>.tracks`). Point the export at your package so paths land under `animations/` as you intend, or use Multiple with a package root and asset name:

```text
<package>/skeletons/<asset>/<asset>.skel
<package>/geometry/<asset>/<asset>.geo
<package>/animations/<asset>/<asset>.anim
```

Skin the mesh to that armature in Blender, export geometry with weights and the same skeleton id, then play clips at runtime with an `AnimationPlayer` on the skinned model entity (`animBankPath`, `clipName`, playback fields). The player advances skeletal clips and updates CPU skinning; it does not animate arbitrary entity transforms from those tracks.

## Rigid motion: component animators

For rigid objects (crates that slide, doors that swing as a whole, platforms, bobbing pickups), author motion in the game as component animator systems: components on flecs entities that update `LocalTransformation` (and whatever else you need) over time.

That keeps Blender export focused on characters and skinned props, and keeps object motion explicit in gameplay code where timing, triggers, and interaction live. A spinning decoration might use a simple spin component; a scripted door would use whatever animator components and systems you build for that behavior. None of that goes through `.anim` bone tracks unless the object is genuinely skinned to a skeleton.

In short: Blender actions map to skeleton bones on a skinned mesh; entity transform animation belongs in component systems.
