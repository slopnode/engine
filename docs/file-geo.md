@page filegeo Mesh/Animation formats

# .vert {#vert}

| Field | Type | Notes |
|-------|------|-------|
| magic | char[4] | D L K V |
| version | u16 | 1 |
| vertexCount | u32 |  |
| indexCount | u32 |  |
| flags | u16 | 1 = normals present, 2 = UVs present (bits may combine) |
| positions | f32[vertexCount×3] | Interleaved x y z |
| normals | f32[vertexCount×3] | Only if flags & 1 |
| uvs | f32[vertexCount×2] | Only if flags & 2 |
| indices | u32[indexCount] | Triangle indices into the shared vertex buffer |

Primitives in .geo select ranges via vertex/index offsets and counts.

## .weights {#weights}

| Field | Type | Notes |
|-------|------|-------|
| magic | char[4] | D L K W |
| version | u16 | 1 |
| vertexCount | u32 | Must match .vert |
| verts[vertexCount] | … |  |

Each vertex:

| Field | Type | Notes |
|-------|------|-------|
| jointIndices | u8[4] | Bone indices |
| jointWeights | f32[4] | Corresponding weights |

## .bind {#bind}

| Field | Type | Notes |
|-------|------|-------|
| magic | char[4] | D L K B |
| version | u16 | 1 |
| boneCount | u32 | Must match .skel bone order |
| matrices[boneCount] | f32[16] | Column-major 4×4, engine Z-up / raylib float order |

## .anim {#anim}

The text bank that names the clips over one skeleton. Each clip lists one field per line.

```
(anim
  (skeleton "mannequin")
  (clips
   (
    (name "Sword_Regular_A"
     fps 24
     duration 0.416667
     event 5 "swing"
     event 10 "done"
     tracks "mannequin_Sword_Regular_A.tracks")
   )))
```

| Field | Notes |
|-------|-------|
| fps | Sample rate of the tracks file |
| duration | Seconds, defaults to (frames - 1) / fps when 0 |
| event | Frame index and name; playback crossing the frame calls `on-anim-event` |
| tracks | Tracks file next to the bank, or `tracks implicit` for `<bank>.tracks` |

## .tracks {#tracks}

| Field | Type | Notes |
|-------|------|-------|
| magic | char[4] | D L K T |
| version | u16 | 1 = TRS, 2 = matrix |
| boneCount | u32 | Must match skeleton |
| frameCount | u32 |  |
| poses | … | frameCount × boneCount entries |

Pose entry by version:

| Version | Per-bone payload | Notes |
|---------|------------------|-------|
| 1 | 10×f32 | Translation xyz, quaternion xyzw, scale xyz |
| 2 | 16×f32 | Column-major 4×4 matrix (same float order as .bind) |

The Blender exporter writes version 2. Clip metadata (fps, duration, track path) lives in the text .anim file.