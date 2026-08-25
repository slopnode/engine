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

Exports the selected mesh objects into one `.geo` file with a shared vertex buffer, one primitive per material used across the selection. If any of the meshes are skinned, they all need to share the same armature, and a skeleton ID is required so the geometry knows which skeleton its weights refer to; unskinned geometry leaves the skeleton ID blank.

## Skeleton {#export-skel}

Exports the active (or first selected) armature as a `.skel` file, under the skeleton ID typed into the operator. Geometry and animation exports reference this same ID to link back to it.

## Animation {#export-anim}

Exports every action on the selected armature into one `.anim` file, one clip per action — this covers both a directly assigned action and any actions reachable through NLA strips. Frames are resampled at the chosen rate (30 fps by default) rather than exported at Blender's native keyframe times, so raising or lowering it trades animation file size against smoothness.

# Conventions {#conventions}

Blender's Z-up axis convention is converted automatically, so models and animations don't need to be reoriented or pre-rotated before export.

Material slot names decide where the engine looks for the material. A slot named `human01/skin` or `human01.skin` both resolve to the material at that path in the package; an empty slot falls back to a placeholder material rather than failing the export. Object, material, and action names get sanitized on the way out — anything other than letters, numbers, underscores, hyphens, dots, and slashes becomes an underscore.

Skeletons and skinned geometry always export from the armature's rest pose, not whatever frame the timeline happens to be sitting on, so it's safe to leave the armature posed or mid-animation in the viewport when exporting. A mesh object can also be moved, rotated, or scaled relative to its armature (or, for static geometry, relative to the scene origin) and that offset gets baked into the exported vertices rather than lost.

Each vertex carries at most four bone influences, sorted by weight and renormalized to sum to one; a vertex group name that doesn't match a bone name is skipped and reported as a warning rather than silently dropped. Non-triangular faces are triangulated automatically, and a mesh with no UV layer gets an empty one created so the export doesn't fail, though it won't have meaningful texture coordinates until real UVs are added.
