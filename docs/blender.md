@page blender Blender

An exporter add-on that writes package geometry, skeleton, and animation assets straight out of Blender, in the engine's own formats rather than FBX or glTF (see @ref filegeo). It lives at `tools/blender/slopengine_exporter/` in the repository and requires Blender 4.2 or newer.

# Install {#blender-install}

Blender extensions install from a zip. On Windows, `tools/blender/package_extension.ps1` builds one:

```powershell
tools/blender/package_extension.ps1
```

On Linux or macOS, zip the contents of the add-on folder (not the folder itself — `blender_manifest.toml` needs to sit at the root of the archive):

```
cd tools/blender/slopengine_exporter
zip -r ../slopengine_exporter.zip . -x '__pycache__/*'
```

Either way this produces `slopengine_exporter.zip` next to the add-on folder. In Blender, open Edit > Preferences > Get Extensions, use the dropdown menu in the top corner, choose Install from Disk, and select that zip. Repeat after any change to the add-on to pick up the update.

# Exporting {#exporting}

Exports live under File > Export > Slopengine, with four entries.

## Multiple {#export-multiple}

Writes a skeleton, geometry, and animation together into a package folder, laid out the way the engine expects to find them. Point it at a package directory (e.g. `packages/base`), give it an asset name, and it creates `skeletons/<asset>/`, `geometry/<asset>/`, and `animations/<asset>/` beneath that root, each holding the matching file.

The skeleton ID written into the geometry and animation files can either be typed in directly or taken from the selected armature's object name, which is the default and keeps one less thing to keep in sync by hand. This is the export to reach for day to day; the three below exist for exporting one piece on its own.

## Geometry {#export-geo}

Exports the selected mesh objects into one `.geo` file with a shared vertex buffer, one primitive per material used across the selection. If any of the meshes are skinned, they all need to share the same armature, and a skeleton ID is required so the geometry knows which skeleton its weights refer to; unskinned geometry leaves the skeleton ID blank. When a `.geo` already exists at the path, its `(view ...)` and `(attachments ...)` blocks are carried over unchanged, so the placement and attachment points authored in slopmodel survive a re-export of the mesh.

Any object in the scene parented to a bone of the exported armature, such as an empty or a linked weapon collection instance, is written as an attachment named after the object, on that bone, with its offset, rotation and uniform scale relative to the bone. An attachment of the same name in the existing `.geo` is replaced, with a warning when its values differ, and the others are kept. An attachment that comes from a bone-parented object is best adjusted in Blender, since an edit made to it in slopmodel is replaced on the next export. Posing a weapon in a hand and exporting is enough for `fp-attach-held` and `actor-attach-geo` to hang it there by name.

Faces are triangulated but keep the winding the mesh was authored with. Recomputing normals on export was dropped because it flipped faces on models built from nested shells, such as the knee pads and chest piece of a segmented mannequin, which then culled away in the engine. Normals are read per face corner rather than per vertex, so sharp edges and custom normals imported from glTF survive, and a vertex on a hard edge is written once per distinct normal.

## Skeleton {#export-skel}

Exports the active (or first selected) armature as a `.skel` file, under the skeleton ID typed into the operator. Geometry and animation exports reference this same ID to link back to it.

Only bones with Deform enabled are exported, so control bones such as IK targets and pole targets stay in Blender while their effect on the deform bones is baked into every exported pose. A deform bone under a non-deform parent is parented to its nearest deform ancestor. An armature with no deform bones at all exports every bone.

## Animation {#export-anim}

Exports every action on the selected armature into one `.anim` file, one clip per action — this covers both a directly assigned action and any actions reachable through NLA strips. Frames are resampled at the chosen rate (30 fps by default) rather than exported at Blender's native keyframe times, so raising or lowering it trades animation file size against smoothness. The Clips field takes a comma separated list of action names and limits the export to those, which keeps a bank small when the source file carries a whole animation library.

# Headless export {#blender-headless}

`tools/blender/export_asset.py` runs the Multiple export without the UI. It imports a `.blend`, `.glb` or `.fbx` into an empty scene, so the engine side never sees those formats, and writes the package folders the same way the menu entry does.

```
blender -b --python tools/blender/export_asset.py -- \
  --input source.glb --root path/to/package --asset mannequin \
  --skeleton mannequin --fps 24 --clips "Idle_Loop,Walk_Loop" \
  --meshes Mannequin --rename-material M_Main=mannequin/main
```

Pose markers on an action (Pose Markers in the Action Editor) export as clip events, named after the marker and placed on the nearest sampled frame. Hand written `event` lines in an existing `.anim` survive a re-export, and so do clips that were exported earlier but left out of the current `--clips` list, as long as their `.tracks` files are still there. A bank therefore accumulates the way a Half-Life model's sequence list does.

`--skeleton` defaults to the asset name. `--armature` and `--meshes` pick the objects when the file holds more than one rig, otherwise the first armature and every mesh skinned to it are used. `--rename-material OLD=NEW` renames a Blender material before export so its slot resolves to a package material path, and can be given more than once.

`--bones` keeps only the geometry weighted mostly to the named bones and their children, which cuts a first-person arms rig out of a full body without touching the source file. `--only` limits the run to some of `skeleton`, `geometry` and `animation`, and `--geo-name` names the `.geo` file when it should not be the asset name, so a second geo can sit beside the body in the same folder and share its skeleton and bank:

```
blender -b --python tools/blender/export_asset.py -- \
  --input source.glb --root path/to/package --asset mannequin \
  --bones DEF-shoulder.L,DEF-shoulder.R --only geometry --geo-name arms
```

# Rigid mannequins {#blender-mannequin}

Add > Slopengine Mannequin builds a starting character in the style of Half-Life 1 models: a body made of separate closed shells (head, neck, chest, abdomen, pelvis, upper arms, forearms, hands, thighs, calves, feet), each weighted fully to one bone, so limbs turn as rigid pieces instead of bending. The result is a collection holding the armature, the body mesh, and a hitbox collection.

The Build dropdown picks a preset (Average, Heavy, Slim, Short, Tall) and fills in the proportion fields below it, which can then be adjusted one by one: height in meters, head size, neck and torso length, shoulder, chest, belly and hip width, torso depth, arm and leg length, limb thickness, and hand and foot size. Sides sets how many facets go around each round piece. Every build uses the same 20 bone names, so poses and clips keyed on one build play on another, though each build exports its own skeleton because the bone lengths differ.

The rest pose is a T-pose facing -Y, the front in Blender, which exports facing +Z. Bones are `root`, `pelvis`, `spine`, `chest`, `neck`, `head`, then `clavicle`, `upperarm`, `forearm` and `hand`, and `thigh`, `calf` and `foot`, with `.L` and `.R` suffixes so Blender's X-mirror posing works. These 20 bones deform. The clavicles carry no geometry but are kept for shrugging the arms.

Bone rolls are set so a positive rotation about a bone's local X axis is the natural bend on both sides: the spine and head lean forward, arms and thighs swing forward, elbows fold the forearm forward, knees fold the shin back, hands curl toward the palm, feet raise the toes and clavicles shrug. Elbows and knees are hinges, with Y and Z rotation locked and a Limit Rotation constraint holding X between 0 and 150 degrees, so they cannot bend backwards. The constraint is evaluated before export, so clips keep the clamped pose.

Arms and legs are posed with IK. `ik_hand` and `ik_foot` are the hand and foot targets, and the chain reaches for the target while the hand or foot copies its rotation. `pole_elbow` behind each elbow and `pole_knee` in front of each knee set which way the joint points. The IK solver uses the same hinge limits. The elbows and knees are bent very slightly in the rest pose so the solver always knows which way to fold. The control bones are in the IK bone collection, parented to `root`, and do not deform, so they are left out of the exported skeleton while the pose they produce is baked into every clip. Setting a chain's IK constraint influence to 0 returns it to plain rotation keys.

The body is UV unwrapped on creation with Auto UV Unwrap, described below, so it is ready for painting in Blender, Substance Painter, or any other 3D paint tool. The mesh has a single material named `<name>/skin`, which resolves to that package material on export.

## Hitboxes {#blender-hitboxes}

Every bone with geometry gets a box named `hbox.<bone>`, parented to the bone and sized to the vertices weighted to it, so it follows the pose the way a Half-Life `$hbox` does. The boxes are cube empties, scaled to their half extents, and carry custom properties for the game side:

| Property | Meaning |
| --- | --- |
| `slop_hitbox` | Marks the object as a hitbox |
| `slop_hitgroup` | `head`, `chest`, `stomach`, `left_arm`, `right_arm`, `left_leg` or `right_leg` |
| `slop_damage_scale` | Damage multiplier, 3 for the head and 1 elsewhere |
| `slop_severable` | Whether a hit here can sever the bone along with every bone below it |

The head, arms, hands, legs and feet are severable. The torso pieces and the neck are not. Because each piece is a closed shell, hiding a severed bone's pieces leaves the parent piece capped rather than open. Hitboxes are not written to the `.geo` yet, and the exporter skips them when it collects bone-parented attachments.

## Auto UV unwrap {#blender-auto-uv}

Object > Slopengine > Auto UV Unwrap lays out UVs for painting on any selected mesh, in the same spirit as the automatic unwrap in Substance Painter. It replaces the existing UVs and, unless Keep Existing Seams is on, the existing seams.

1. Every edge sharper than Hard Angle (60 degrees by default) becomes a seam, which splits off end caps and the faces of boxy parts.
2. Any piece that is still a closed shell or a tube is cut open along its least visible side: the back of the body, the underside of the arms, the inner side of the legs. The cut is the cheapest path across the piece, with edges facing the camera costing more.
3. Each chart is flattened with Blender's minimum stretch solver. A chart whose texel density still varies by more than 25% over a fifth of its area is split in half, front from back and then top from bottom, and flattened again.
4. Charts are scaled to the same texel density and packed into the 0 to 1 square with rotation.

When Face Group names a vertex group (`head` by default), the front of that group gets a chart of its own, projected straight from the front (-Y) rather than flattened, kept upright and given Face Detail times the texel density (2 by default). A front facing photograph therefore lines up with it directly. It can be dropped onto that chart's area of the texture in an image editor, or painted on in Blender's Texture Paint mode with the photo as a stencil brush texture while looking at the head in front orthographic view.

Single Skin builds one closed quad mesh instead, for characters that bend at the joints and can be cut apart with `actor-sever-bone!` like any other smooth skinned body. The torso, neck and head are one tube. The arms grow out of holes in the sides of the chest, the legs out of the bottom of the pelvis, split at the crotch, and the hands, feet and crown close with quad caps, so the mesh has no triangles before export. Both halves mirror exactly across X. Each edge loop is weighted as a whole, fully to one bone or blended between the two bones of the joint it sits on, so a cut follows the loops and leaves a clean round stump. The hips blend over three loops, with the pelvis no wider than the thighs below it, so the legs swing without a fold at the top of the thigh. When Limb Thickness would push the thighs into each other, they move apart instead, so the joints stay where they are. Sides sets the facets around each limb and is rounded up to an even number, and the torso and head get twice that minus four. The single skin keeps the same bones and hitboxes as the segmented one. Its UVs come from the edge loops rather than Auto UV Unwrap: each tube is unrolled into one strip, seamed down the back of the torso and head, under the arms and along the inside of the legs, with the caps folded in at the ends, and the strips are packed at an even texel density. The UVs are laid out from the body at standard girth, so builds that differ only in chest, belly, torso depth and limb thickness share one UV layout and can share textures, which stretch with the body.

Tight Packing packs by the exact outline of each chart, which uses about a tenth more of the texture but takes from a few seconds to tens of seconds. The mannequin generator packs by bounding box so its redo panel stays responsive, so run Auto UV Unwrap once more on the finished body, or pass `--tight-pack` to the headless script, before painting.

## Using your own mesh {#blender-rigidify}

Object > Slopengine also holds two operators that work on any armature and the meshes skinned to it, so a character modeled by hand can get the same treatment. Rigidify Weights reweights each loose part to the single bone it is mostly weighted to, or to the nearest bone when it has no weights at all, and can instead work per vertex for a mesh that is one continuous skin. Fit Hitboxes creates any missing hitboxes and refits all of them to the current weights, keeping the properties already set on existing ones.

## Headless generation {#blender-mannequin-headless}

`tools/blender/make_mannequin.py` builds the same mannequin without the UI, saves it as a `.blend`, and can export the skeleton and geometry straight into a package:

```
blender -b --python tools/blender/make_mannequin.py -- \
  --name grunt --preset HEAVY --set limb_thickness=1.6 \
  --output grunt.blend --root path/to/package
```

`--set FIELD=VALUE` overrides one proportion field and can be given more than once. `--sides`, `--no-hitboxes` and `--no-uv` match the operator options, and `--tight-pack` packs the UV charts by exact shape. `--fused` builds the single skin. `--material` sets the body's material path in place of `<name>/skin`. `--face-material` gives the single skin's neck and head their own material, with their strip stretched to fill its own texture, the way the photo-textured heads of the slopdemo mannequins work.

`--skeleton` exports only the geometry, into the folder of an existing skeleton and against it, so a new body can play that skeleton's clips. The bone names always match, and the bone lengths match when the build values do. `--geo-name` names the `.geo` file and defaults to `--name`:

```
blender -b --python tools/blender/make_mannequin.py -- \
  --name smooth_mannequin --fused --skeleton rigid_mannequin \
  --material mannequin/skin --root path/to/package
```

# Conventions {#conventions}

Blender's Z-up axis convention is converted automatically, so models and animations don't need to be reoriented or pre-rotated before export.

Material slot names decide where the engine looks for the material. A slot named `human01/skin` or `human01.skin` both resolve to the material at that path in the package; an empty slot falls back to a placeholder material rather than failing the export. Object, material, and action names get sanitized on the way out — anything other than letters, numbers, underscores, hyphens, dots, and slashes becomes an underscore.

Skeletons and skinned geometry always export from the armature's rest pose, with the NLA stack and the assigned action switched off for the duration, so it's safe to leave the armature posed or mid-animation in the viewport when exporting. A mesh object can also be moved, rotated, or scaled relative to its armature (or, for static geometry, relative to the scene origin) and that offset gets baked into the exported vertices rather than lost.

UVs are flipped vertically on export, from Blender's bottom-left texture origin to the engine's top-left one, the same flip glTF makes.

Each vertex carries at most four bone influences, sorted by weight and renormalized to sum to one; a vertex group name that doesn't match a bone name is skipped and reported as a warning rather than silently dropped. Non-triangular faces are triangulated automatically, and a mesh with no UV layer gets an empty one created so the export doesn't fail, though it won't have meaningful texture coordinates until real UVs are added.
